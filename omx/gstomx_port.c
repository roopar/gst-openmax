/*
 * Copyright (C) 2006-2009 Texas Instruments, Incorporated
 * Copyright (C) 2007-2009 Nokia Corporation.
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>

#include "gstomx_util.h"
#include "gstomx.h"

GST_DEBUG_CATEGORY_EXTERN (gstomx_util_debug);

#define CODEC_DATA_FLAG 0x00000080 /* special nFlags field to use to indicated codec-data */

static OMX_BUFFERHEADERTYPE * request_buffer (GOmxPort *port);
static void release_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer);


/*
 * Port
 */

GOmxPort *
g_omx_port_new (GOmxCore *core, guint index)
{
    GOmxPort *port;
    port = g_new0 (GOmxPort, 1);

    port->core = core;
    port->port_index = index;
    port->num_buffers = 0;
    port->buffer_size = 0;
    port->buffers = NULL;

    port->enabled = TRUE;
    port->queue = async_queue_new ();
    port->mutex = g_mutex_new ();

    return port;
}

void
g_omx_port_free (GOmxPort *port)
{
    g_mutex_free (port->mutex);
    async_queue_free (port->queue);

    g_free (port->buffers);
    g_free (port);
}

void
g_omx_port_setup (GOmxPort *port,
                  OMX_PARAM_PORTDEFINITIONTYPE *omx_port)
{
    GOmxPortType type = -1;

    switch (omx_port->eDir)
    {
        case OMX_DirInput:
            type = GOMX_PORT_INPUT;
            break;
        case OMX_DirOutput:
            type = GOMX_PORT_OUTPUT;
            break;
        default:
            break;
    }

    port->type = type;
    /** @todo should it be nBufferCountMin? */
    port->num_buffers = omx_port->nBufferCountActual;
    port->buffer_size = omx_port->nBufferSize;
    port->port_index = omx_port->nPortIndex;

    GST_DEBUG_OBJECT (port->core->object,
        "type=%d, num_buffers=%d, buffer_size=%d, port_index=%d",
        port->type, port->num_buffers, port->buffer_size, port->port_index);

    g_free (port->buffers);
    port->buffers = g_new0 (OMX_BUFFERHEADERTYPE *, port->num_buffers);
}


static GstBuffer *
buffer_alloc (GOmxPort *port, gint len)
{
    GstBuffer *buf = NULL;
    if (port->buffer_alloc)
        buf = port->buffer_alloc (port, len);
    if (!buf)
        buf = gst_buffer_new_and_alloc (len);
    return buf;
}


void
g_omx_port_allocate_buffers (GOmxPort *port)
{
    guint i;
    guint size;

    size = port->buffer_size;

    for (i = 0; i < port->num_buffers; i++)
    {

        if (port->omx_allocate)
        {
            GST_DEBUG_OBJECT (port->core->object, "%d: OMX_AllocateBuffer(), size=%d", i, size);
            OMX_AllocateBuffer (port->core->omx_handle,
                                &port->buffers[i],
                                port->port_index,
                                NULL,
                                size);
        }
        else
        {
            GstBuffer *buf = NULL;
            gpointer buffer_data;
            if (port->share_buffer)
            {
                buf = buffer_alloc (port, size);
                buffer_data = GST_BUFFER_DATA (buf);
            }
            else
            {
                buffer_data = g_malloc (size);
            }

            GST_DEBUG_OBJECT (port->core->object, "%d: OMX_UseBuffer(), size=%d, share_buffer=%d", i, size, port->share_buffer);
            OMX_UseBuffer (port->core->omx_handle,
                           &port->buffers[i],
                           port->port_index,
                           NULL,
                           size,
                           buffer_data);

            if (port->share_buffer)
            {
                port->buffers[i]->pAppPrivate = buf;
                port->buffers[i]->pBuffer     = GST_BUFFER_DATA (buf);
                port->buffers[i]->nAllocLen   = GST_BUFFER_SIZE (buf);
                port->buffers[i]->nOffset     = 0;
            }
        }
    }
}

void
g_omx_port_free_buffers (GOmxPort *port)
{
    guint i;

    for (i = 0; i < port->num_buffers; i++)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer;

        omx_buffer = port->buffers[i];

        if (omx_buffer)
        {
#if 0
            /** @todo how shall we free that buffer? */
            if (!port->omx_allocate)
            {
                g_free (omx_buffer->pBuffer);
                omx_buffer->pBuffer = NULL;
            }
#endif

            OMX_FreeBuffer (port->core->omx_handle, port->port_index, omx_buffer);
            port->buffers[i] = NULL;
        }
    }
}

void
g_omx_port_start_buffers (GOmxPort *port)
{
    guint i;

    for (i = 0; i < port->num_buffers; i++)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer;

        omx_buffer = port->buffers[i];

        /* If it's an input port we will need to fill the buffer, so put it in
         * the queue, otherwise send to omx for processing (fill it up). */
        if (port->type == GOMX_PORT_INPUT)
            g_omx_core_got_buffer (port->core, port, omx_buffer);
        else
            release_buffer (port, omx_buffer);
    }
}

void
g_omx_port_push_buffer (GOmxPort *port,
                        OMX_BUFFERHEADERTYPE *omx_buffer)
{
    async_queue_push (port->queue, omx_buffer);
}

static OMX_BUFFERHEADERTYPE *
request_buffer (GOmxPort *port)
{
    GST_LOG_OBJECT (port->core->object, "request buffer");
    return async_queue_pop (port->queue);
}

static void
release_buffer (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer)
{
    switch (port->type)
    {
        case GOMX_PORT_INPUT:
            GST_DEBUG_OBJECT (port->core->object, "ETB: omx_buffer=%p, pAppPrivate=%p", omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0);
            OMX_EmptyThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        case GOMX_PORT_OUTPUT:
            GST_DEBUG_OBJECT (port->core->object, "FTB: omx_buffer=%p, pAppPrivate=%p", omx_buffer, omx_buffer ? omx_buffer->pAppPrivate : 0);
            OMX_FillThisBuffer (port->core->omx_handle, omx_buffer);
            break;
        default:
            break;
    }
}

/* NOTE ABOUT BUFFER SHARING:
 *
 * Buffer sharing is a sort of "extension" to OMX to allow zero copy buffer
 * passing between GST and OMX.
 *
 * There are only two cases:
 *
 * 1) shared_buffer is enabled, in which case we control nOffset, and use
 *    pAppPrivate to store the reference to the original GstBuffer that
 *    pBuffer ptr is copied from.  Note that in case of input buffers,
 *    the DSP/coprocessor should treat the buffer as read-only so cache-
 *    line alignment is not an issue.  For output buffers which are not
 *    pad_alloc()d, some care may need to be taken to ensure proper buffer
 *    alignment.
 * 2) shared_buffer is not enabled, in which case we respect the nOffset
 *    set by the component and pAppPrivate is NULL
 *
 */

typedef void (*SendPrep) (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, gpointer obj);

static void
send_prep_codec_data (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, GstBuffer *buf)
{
    omx_buffer->nFlags |= CODEC_DATA_FLAG;
    omx_buffer->nFilledLen = GST_BUFFER_SIZE (buf);

    memcpy (omx_buffer->pBuffer + omx_buffer->nOffset,
            GST_BUFFER_DATA (buf), omx_buffer->nFilledLen);
}

static void
send_prep_buffer_data (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, GstBuffer *buf)
{
    if (port->share_buffer)
    {
        omx_buffer->nOffset     = 0;
        omx_buffer->pBuffer     = GST_BUFFER_DATA (buf);
        omx_buffer->nFilledLen  = GST_BUFFER_SIZE (buf);
        omx_buffer->nAllocLen   = GST_BUFFER_SIZE (buf);
        omx_buffer->pAppPrivate = gst_buffer_ref (buf);
    }
    else
    {
        omx_buffer->nFilledLen = MIN (GST_BUFFER_SIZE (buf),
                omx_buffer->nAllocLen - omx_buffer->nOffset);
        memcpy (omx_buffer->pBuffer + omx_buffer->nOffset,
                GST_BUFFER_DATA (buf), omx_buffer->nFilledLen);
    }

    if (port->core->use_timestamps)
    {
        omx_buffer->nTimeStamp = gst_util_uint64_scale_int (
                GST_BUFFER_TIMESTAMP (buf),
                OMX_TICKS_PER_SECOND, GST_SECOND);
    }

    GST_DEBUG_OBJECT (port->core->object,
            "omx_buffer: size=%lu, len=%lu, flags=%lu, offset=%lu, timestamp=%lld",
            omx_buffer->nAllocLen, omx_buffer->nFilledLen, omx_buffer->nFlags,
            omx_buffer->nOffset, omx_buffer->nTimeStamp);
}

static void
send_prep_eos_event (GOmxPort *port, OMX_BUFFERHEADERTYPE *omx_buffer, GstEvent *evt)
{
    omx_buffer->nFlags |= OMX_BUFFERFLAG_EOS;
    omx_buffer->nFilledLen = 0;
}

/**
 * Send a buffer/event to the OMX component.  This handles conversion of
 * GST buffer, codec-data, and EOS events to the equivalent OMX buffer.
 *
 * This method does not take ownership of the ref to @obj
 *
 * Returns number of bytes sent, or negative if error
 */
gint
g_omx_port_send (GOmxPort *port, gpointer obj)
{
    SendPrep send_prep = NULL;

    g_return_val_if_fail (port->type == GOMX_PORT_INPUT, -1);

    if (GST_IS_BUFFER (obj))
    {
        if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (obj, GST_BUFFER_FLAG_IN_CAPS)))
            send_prep = (SendPrep)send_prep_codec_data;
        else
            send_prep = (SendPrep)send_prep_buffer_data;
    }
    else if (GST_IS_EVENT (obj))
    {
        if (G_LIKELY (GST_EVENT_TYPE (obj) == GST_EVENT_EOS))
            send_prep = (SendPrep)send_prep_eos_event;
    }

    if (G_LIKELY (send_prep))
    {
        gint ret;
        OMX_BUFFERHEADERTYPE *omx_buffer = request_buffer (port);

        if (!omx_buffer)
        {
            GST_DEBUG_OBJECT (port->core->object, "null buffer");
            return -1;
        }

        /* if buffer sharing is enabled, pAppPrivate might hold the ref to
         * a buffer that is no longer required and should be unref'd.  We
         * do this check here, rather than in send_prep_buffer_data() so
         * we don't keep the reference live in case, for example, this time
         * the buffer is used for an EOS event.
         */
        if (omx_buffer->pAppPrivate)
        {
            GstBuffer *old_buf = omx_buffer->pAppPrivate;
            gst_buffer_unref (old_buf);
            omx_buffer->pAppPrivate = NULL;
        }

        send_prep (port, omx_buffer, obj);

        ret = omx_buffer->nFilledLen;

        release_buffer (port, omx_buffer);

        return ret;
    }

    GST_WARNING_OBJECT (port->core->object, "unknown obj type");
    return -1;
}

/**
 * Receive a buffer/event from OMX component.  This handles the conversion
 * of OMX buffer to GST buffer, codec-data, or EOS event.
 *
 * Returns <code>NULL</code> if buffer could not be received.
 */
gpointer
g_omx_port_recv (GOmxPort *port)
{
    gpointer ret = NULL;

    g_return_val_if_fail (port->type == GOMX_PORT_OUTPUT, NULL);

    while (!ret && port->enabled)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer = request_buffer (port);

        if (G_UNLIKELY (!omx_buffer))
        {
            return NULL;
        }

        GST_DEBUG_OBJECT (port->core->object,
                "omx_buffer: size=%lu, len=%lu, flags=%lu, offset=%lu, timestamp=%lld",
                omx_buffer->nAllocLen, omx_buffer->nFilledLen, omx_buffer->nFlags,
                omx_buffer->nOffset, omx_buffer->nTimeStamp);

        if (G_UNLIKELY (omx_buffer->nFlags & OMX_BUFFERFLAG_EOS))
        {
            GST_DEBUG_OBJECT (port->core->object, "got eos");
            ret = gst_event_new_eos ();
        }
        else if (G_LIKELY (omx_buffer->nFilledLen > 0))
        {
            GstBuffer *buf = omx_buffer->pAppPrivate;

            /* I'm not really sure if it was intentional to block zero-copy of
             * the codec-data buffer.. this is how the original code worked,
             * so I kept the behavior
             */
            if (!buf || (omx_buffer->nFlags & CODEC_DATA_FLAG))
            {
                if (buf)
                    gst_buffer_unref (buf);

                buf = buffer_alloc (port, omx_buffer->nFilledLen);
                memcpy (GST_BUFFER_DATA (buf),
                        omx_buffer->pBuffer + omx_buffer->nOffset,
                        omx_buffer->nFilledLen);
            }

            if (port->core->use_timestamps)
            {
                GST_BUFFER_TIMESTAMP (buf) = gst_util_uint64_scale_int (
                        omx_buffer->nTimeStamp,
                        GST_SECOND, OMX_TICKS_PER_SECOND);
            }

            if (G_UNLIKELY (omx_buffer->nFlags & CODEC_DATA_FLAG))
            {
                GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
            }

            ret = buf;
        }
        else
        {
            GstBuffer *buf = omx_buffer->pAppPrivate;

            if (buf)
                gst_buffer_unref (buf);

            GST_DEBUG_OBJECT (port->core->object, "empty buffer"); /* keep looping */
        }

        if (port->share_buffer)
        {
            GstBuffer *new_buf = buffer_alloc (port, omx_buffer->nAllocLen);
            omx_buffer->pAppPrivate = new_buf;
            omx_buffer->pBuffer     = GST_BUFFER_DATA (new_buf);
            omx_buffer->nAllocLen   = GST_BUFFER_SIZE (new_buf);
            omx_buffer->nOffset     = 0;
        }
        else
        {
            g_assert (omx_buffer->pBuffer && !omx_buffer->pAppPrivate);
        }

        release_buffer (port, omx_buffer);
    }


    return ret;
}

void
g_omx_port_resume (GOmxPort *port)
{
    async_queue_enable (port->queue);
}

void
g_omx_port_pause (GOmxPort *port)
{
    async_queue_disable (port->queue);
}

void
g_omx_port_flush (GOmxPort *port)
{
    if (port->type == GOMX_PORT_OUTPUT)
    {
        /* This will get rid of any buffers that we have received, but not
         * yet processed in the output_loop.
         */
        OMX_BUFFERHEADERTYPE *omx_buffer;
        while ((omx_buffer = async_queue_pop_forced (port->queue)))
        {
            omx_buffer->nFilledLen = 0;
            release_buffer (port, omx_buffer);
        }
    }

    OMX_SendCommand (port->core->omx_handle, OMX_CommandFlush, port->port_index, NULL);
    g_sem_down (port->core->flush_sem);
}

void
g_omx_port_enable (GOmxPort *port)
{
    GOmxCore *core;

    core = port->core;

    OMX_SendCommand (core->omx_handle, OMX_CommandPortEnable, port->port_index, NULL);
    g_omx_port_allocate_buffers (port);
    if (core->omx_state != OMX_StateLoaded)
        g_omx_port_start_buffers (port);
    g_omx_port_resume (port);

    g_sem_down (core->port_sem);
}

void
g_omx_port_disable (GOmxPort *port)
{
    GOmxCore *core;

    core = port->core;

    OMX_SendCommand (core->omx_handle, OMX_CommandPortDisable, port->port_index, NULL);
    g_omx_port_pause (port);
    g_omx_port_flush (port);
    g_omx_port_free_buffers (port);

    g_sem_down (core->port_sem);
}

void
g_omx_port_finish (GOmxPort *port)
{
    port->enabled = FALSE;
    async_queue_disable (port->queue);
}
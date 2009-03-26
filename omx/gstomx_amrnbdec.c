/*
 * Copyright (C) 2007-2008 Nokia Corporation.
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

#include "gstomx_amrnbdec.h"
#include "gstomx_base_filter.h"
#include "gstomx.h"

#ifdef BUILD_WITH_ANDROID
#define OMX_COMPONENT_NAME "OMX.PV.amrdec"
#else
#define OMX_COMPONENT_NAME "OMX.st.audio_decoder.amrnb"
#endif

static GstOmxBaseFilterClass *parent_class = NULL;

static GstStateChangeReturn gst_nb_change_state (GstElement *element, GstStateChange transition);

static GstCaps *
generate_src_template (void)
{
    GstCaps *caps;

    caps = gst_caps_new_simple ("audio/x-raw-int",
                                "endianness", G_TYPE_INT, G_BYTE_ORDER,
                                "width", GST_TYPE_INT_RANGE, 8, 32,
                                "depth", GST_TYPE_INT_RANGE, 8, 32,
                                "rate", GST_TYPE_INT_RANGE, 8000, 48000,
                                "signed", G_TYPE_BOOLEAN, TRUE,
                                "channels", GST_TYPE_INT_RANGE, 1, 8,
                                NULL);

    return caps;
}

static GstCaps *
generate_sink_template (void)
{
    GstCaps *caps;

    caps = gst_caps_new_simple ("audio/AMR",
                                "rate", GST_TYPE_INT_RANGE, 8000, 48000,
                                "channels", GST_TYPE_INT_RANGE, 1, 8,
                                NULL);

    return caps;
}

static void
type_base_init (gpointer g_class)
{
    GstElementClass *element_class;

    element_class = GST_ELEMENT_CLASS (g_class);

    {
        GstElementDetails details;

        details.longname = "OpenMAX IL AMR-NB audio decoder";
        details.klass = "Codec/Decoder/Audio";
        details.description = "Decodes audio in AMR-NB format with OpenMAX IL";
        details.author = "Felipe Contreras";

        gst_element_class_set_details (element_class, &details);
    }

    {
        GstPadTemplate *template;

        template = gst_pad_template_new ("src", GST_PAD_SRC,
                                         GST_PAD_ALWAYS,
                                         generate_src_template ());

        gst_element_class_add_pad_template (element_class, template);
    }

    {
        GstPadTemplate *template;

        template = gst_pad_template_new ("sink", GST_PAD_SINK,
                                         GST_PAD_ALWAYS,
                                         generate_sink_template ());

        gst_element_class_add_pad_template (element_class, template);
    }
}

static void
type_class_init (gpointer g_class,
                 gpointer class_data)
{  
    GstElementClass *element_class;
    element_class = GST_ELEMENT_CLASS (g_class);

    parent_class = g_type_class_ref (GST_OMX_BASE_FILTER_TYPE);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_nb_change_state);
}

static void
settings_changed_cb (GOmxCore *core)
{
    GstOmxBaseFilter *omx_base;
    guint rate;
    guint channels;

    omx_base = core->client_data;

    GST_DEBUG_OBJECT (omx_base, "settings changed");

    {
        OMX_AUDIO_PARAM_PCMMODETYPE *param;

        param = calloc (1, sizeof (OMX_AUDIO_PARAM_PCMMODETYPE));
        param->nSize = sizeof (OMX_AUDIO_PARAM_PCMMODETYPE);
        param->nVersion.s.nVersionMajor = 1;
        param->nVersion.s.nVersionMinor = 1;

        param->nPortIndex = 1;
        OMX_GetParameter (omx_base->gomx->omx_handle, OMX_IndexParamAudioPcm, param);

        rate = param->nSamplingRate;
        channels = param->nChannels;

        free (param);
    }

    {
        GstCaps *new_caps;

        new_caps = gst_caps_new_simple ("audio/x-raw-int",
                                        "width", G_TYPE_INT, 16,
                                        "depth", G_TYPE_INT, 16,
                                        "rate", G_TYPE_INT, rate,
                                        "signed", G_TYPE_BOOLEAN, TRUE,
                                        "endianness", G_TYPE_INT, G_BYTE_ORDER ? 1234 : 4321,
                                        "channels", G_TYPE_INT, channels,
                                        NULL);

        GST_INFO_OBJECT (omx_base, "caps are: %" GST_PTR_FORMAT, new_caps);
        gst_pad_set_caps (omx_base->srcpad, new_caps);
    }
}

static void
type_instance_init (GTypeInstance *instance,
                    gpointer g_class)
{
    GstOmxBaseFilter *omx_base;

    omx_base = GST_OMX_BASE_FILTER (instance);

    omx_base->omx_component = g_strdup (OMX_COMPONENT_NAME);

    omx_base->gomx->settings_changed_cb = settings_changed_cb;
}

static GstStateChangeReturn
gst_nb_change_state (GstElement *element,
                GstStateChange transition)
{
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

    OMX_AUDIO_PARAM_PCMMODETYPE *param;
    GstOmxAmrNbDec *self;
    GstOmxBaseFilter *omx_base;
    OMX_HANDLETYPE nb_handle;

    self = GST_OMX_AMRNBDEC(element);
    omx_base = GST_OMX_BASE_FILTER (element);
    nb_handle = omx_base->gomx->omx_handle;

    param = calloc (1, sizeof (OMX_AUDIO_PARAM_PCMMODETYPE));
    param->nSize = sizeof (OMX_AUDIO_PARAM_PCMMODETYPE);
    param->nVersion.s.nVersionMajor = 1;
    param->nVersion.s.nVersionMinor = 1;
    param->nPortIndex = 1;

    switch (transition)
    {
        case GST_STATE_CHANGE_NULL_TO_READY:
            GST_DEBUG_OBJECT(self,"GST_STATE_CHANGE_NULL_TO_READY\n");
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            GST_DEBUG_OBJECT(self,"GST_STATE_CHANGE_READY_TO_PAUSED\n");
#ifdef BUILD_WITH_ANDROID            
            /* 
             * set OMX_IndexConfigAudioAmrNB and OMX_IndexConfigAudioAmrFrameFormatFSF
             * to work with PV OpenMax
             */
            OMX_SetParameter(nb_handle, OMX_IndexConfigAudioAmrNB, param);
            OMX_SetParameter(nb_handle, OMX_IndexConfigAudioAmrFrameFormatFSF, param);
#endif /* BUILD_WITH_ANDROID */
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            GST_DEBUG_OBJECT(self,"GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
            break;
        default:
            break;
    }
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;
    
    switch (transition)
    {
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            GST_DEBUG_OBJECT(self,"GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            GST_DEBUG_OBJECT(self,"GST_STATE_CHANGE_PAUSED_TO_READY\n");
            break;
        case GST_STATE_CHANGE_READY_TO_NULL:
            GST_DEBUG_OBJECT(self,"GST_STATE_CHANGE_READY_TO_NULL\n");
            break;
        default:
            break;
    }

    free (param);
    return ret;
}


GType
gst_omx_amrnbdec_get_type (void)
{
    static GType type = 0;

    if (type == 0)
    {
        GTypeInfo *type_info;

        type_info = g_new0 (GTypeInfo, 1);
        type_info->class_size = sizeof (GstOmxAmrNbDecClass);
        type_info->base_init = type_base_init;
        type_info->class_init = type_class_init;
        type_info->instance_size = sizeof (GstOmxAmrNbDec);
        type_info->instance_init = type_instance_init;

        type = g_type_register_static (GST_OMX_BASE_FILTER_TYPE, "GstOmxAmrNbDec", type_info, 0);

        g_free (type_info);
    }

    return type;
}

noinst_LIBRARIES = libomxil-foo.so

libomxil_foo_so_SOURCES = core.c
libomxil_foo_so_CFLAGS = -I$(top_srcdir)/omx/headers $(GTHREAD_CFLAGS) -I$(top_srcdir)/util
libomxil_foo_so_LIBADD = $(GTHREAD_LIBS) $(top_srcdir)/util/.libs/libutil.a

# Manual stuff

CFLAGS = -ggdb
top_srcdir = ../..
srcdir = .
CC = gcc
LIBRARIES = $(noinst_LIBRARIES)
GTHREAD_CFLAGS=`pkg-config --cflags gthread-2.0`
GTHREAD_LIBS=`pkg-config --libs gthread-2.0`

all: 
check: $(LIBRARIES)

libomxil-foo.so: $(patsubst %.c,%.o,$(libomxil_foo_so_SOURCES))
libomxil-foo.so: CFLAGS := $(CFLAGS) -fPIC $(libomxil_foo_so_CFLAGS)
libomxil-foo.so: LIBS := $(libomxil_foo_so_LIBADD)

%.so::
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

.PHONY: clean distclean install

clean:
	rm -rf *.o $(LIBRARIES)

install:
distdir:
	cp -pR $(srcdir)/core.c $(distdir)
	cp -pR $(srcdir)/Makefile $(distdir)
distclean: clean

# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstsunxisrc.la

# sources used to compile this plug-in
libgstsunxisrc_la_SOURCES = gstsunxisrc.c gstsunxisrc.h \
	h264enc.c h264enc.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstsunxisrc_la_CFLAGS = $(GST_CFLAGS)
libgstsunxisrc_la_LIBADD = $(GST_LIBS)
libgstsunxisrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lpthread -lm -lcdc_base -lVE  -lvencoder -lMemAdapter
libgstsunxisrc_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstsunxisrc.h h264enc.h

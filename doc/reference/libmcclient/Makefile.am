## Process this file with automake to produce Makefile.in

abs_top_builddir = @abs_top_builddir@

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

# This is a blank Makefile.am for using gtk-doc.
# Copy this to your project's API docs directory and modify the variables to
# suit your project. See the GTK+ Makefiles in gtk+/docs/reference for examples
# of using the various options.

# The name of the module, e.g. 'glib'.
DOC_MODULE=libmcclient

# The top-level SGML file. You can change this if you want to.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
# e.g. DOC_SOURCE_DIR=../../../gtk
DOC_SOURCE_DIR=../../../libmcclient

# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=--type-init-func='g_type_init();dbus_g_bus_get(0,NULL)'
export DBUS_FATAL_WARNINGS:=0

# Extra options to supply to gtkdoc-scan.
# e.g. SCAN_OPTIONS=--deprecated-guards="GTK_DISABLE_DEPRECATED" 
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
# e.g. MKDB_OPTIONS=--sgml-mode --output-format=xml
MKDB_OPTIONS=--sgml-mode --output-format=xml

# If _gen is a subdirectory of $(top_srcdir)/libmcclient then we
# already get it scanned by virtue of setting DOC_SOURCE_DIR. If
# it's not a subdirectory, we'll have to add a second --source-dir
# so both get scanned (we want the declarations and doc-comments in
# the generated source to take effect).
if OUT_OF_TREE_BUILD
SCAN_OPTIONS += --source-dir=$(abs_top_builddir)/libmcclient/_gen
MKDB_OPTIONS += --source-dir=$(abs_top_builddir)/libmcclient/_gen
endif

# Extra options to supply to gtkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
# e.g. HFILE_GLOB=$(top_srcdir)/gtk/*.h
# e.g. CFILE_GLOB=$(top_srcdir)/gtk/*.c
HFILE_GLOB=$(top_srcdir)/libmcclient/*.h $(top_builddir)/libmcclient/_gen/*.h
CFILE_GLOB=$(top_srcdir)/libmcclient/*.c $(top_builddir)/libmcclient/_gen/*.c

# Header files to ignore when scanning.
# e.g. IGNORE_HFILES=gtkdebug.h gtkintl.h
IGNORE_HFILES= mc-account-priv.h \
	       dbus-api.h \
	       gtypes-body.h \
	       interfaces-body.h \
	       mission-control-signals-marshal.h

# Images to copy into HTML directory.
# e.g. HTML_IMAGES=$(top_srcdir)/gtk/stock-icons/stock_about_24.png
HTML_IMAGES=

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files=

# Undefine this so the scanner doesn't inherit the ultra-pedantic warnings used
# for real code.
AM_CFLAGS =

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
# e.g. INCLUDES=-I$(top_srcdir) -I$(top_builddir) $(GTK_DEBUG_FLAGS)
# e.g. GTKDOC_LIBS=$(top_builddir)/gtk/$(gtktargetlib)
INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(DBUS_CFLAGS) \
	$(TELEPATHY_CFLAGS)
GTKDOC_LIBS = \
	$(top_builddir)/libmcclient/libmcclient.la \
	$(GCONF_LIBS) \
	$(GLIB_LIBS) \
	$(DBUS_LIBS) \
	$(TELEPATHY_LIBS)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
# e.g. EXTRA_DIST += version.xml.in
EXTRA_DIST += 

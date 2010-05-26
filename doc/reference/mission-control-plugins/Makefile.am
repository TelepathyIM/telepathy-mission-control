## Process this file with automake to produce Makefile.in

abs_top_builddir = @abs_top_builddir@

AUTOMAKE_OPTIONS = 1.9

# This is a blank Makefile.am for using gtk-doc.
# Copy this to your project's API docs directory and modify the variables to
# suit your project. See the GTK+ Makefiles in gtk+/docs/reference for examples
# of using the various options.

# The name of the module, e.g. 'glib'.
DOC_MODULE=mission-control-plugins

# The top-level SGML file. You can change this if you want to.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
# e.g. DOC_SOURCE_DIR=../../../gtk
DOC_SOURCE_DIR=../../../mission-control-plugins

# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=

# Extra options to supply to gtkdoc-scan.
# e.g. SCAN_OPTIONS=--deprecated-guards="GTK_DISABLE_DEPRECATED"
SCAN_OPTIONS=--rebuild-types --rebuild-sections

# Extra options to supply to gtkdoc-mkdb.
# e.g. MKDB_OPTIONS=--sgml-mode --output-format=xml
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-mktmpl
# e.g. MKTMPL_OPTIONS=--only-section-tmpl
MKTMPL_OPTIONS=

# Extra options to supply to gtkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
# e.g. HFILE_GLOB=$(top_srcdir)/gtk/*.h
# e.g. CFILE_GLOB=$(top_srcdir)/gtk/*.c
HFILE_GLOB=$(top_srcdir)/mission-control-plugins/*.h
CFILE_GLOB=$(top_srcdir)/mission-control-plugins/*.c

# Header files to ignore when scanning.
# e.g. IGNORE_HFILES=gtkdebug.h gtkintl.h
IGNORE_HFILES=implementation.h debug-internal.h

# Images to copy into HTML directory.
# e.g. HTML_IMAGES=$(top_srcdir)/gtk/stock-icons/stock_about_24.png
HTML_IMAGES=

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files=

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
# e.g. INCLUDES=-I$(top_srcdir) -I$(top_builddir) $(GTK_DEBUG_FLAGS)
# e.g. GTKDOC_LIBS=$(top_builddir)/gtk/$(gtktargetlib)
INCLUDES = \
    -I$(top_srcdir) \
    $(TELEPATHY_CFLAGS) \
    $(DBUS_CFLAGS) \
    $(GLIB_CFLAGS)
GTKDOC_LIBS = \
    $(top_builddir)/mission-control-plugins/libmission-control-plugins.la \
    $(TELEPATHY_LIBS) \
    $(DBUS_LIBS) \
    $(GLIB_LIBS)


# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

clean-local: clean-mcp

# we actually treat all of these as disposable, in this "book"
clean-mcp:
	rm -fr *.txt *.types

# this will be enabled in a future release
ENABLE_DOC_CHECKS = no

if ENABLE_GTK_DOC
check-local:
	@err=0; \
	echo "Checking for undocumented symbols..."; \
	if grep '^0 symbols incomplete' \
		$(srcdir)/$(DOC_MODULE)-undocumented.txt && \
		grep '^0 not documented' \
		$(srcdir)/$(DOC_MODULE)-undocumented.txt; then\
		:; \
	else \
		cat $(srcdir)/$(DOC_MODULE)-undocumented.txt; \
		err=1; \
	fi; \
	echo "Checking for unused symbols..."; \
	if grep . $(srcdir)/$(DOC_MODULE)-unused.txt; then\
		echo "^^^ Unused symbols" >&2; \
		err=1; \
	fi; \
	echo "Checking for undeclared symbols..."; \
	if test -e $(srcdir)/$(DOC_MODULE)-undeclared.txt &&\
		grep . $(srcdir)/$(DOC_MODULE)-undeclared.txt; then\
		echo "^^^ Undeclared symbols" >&2; \
		err=1; \
	fi; \
	sed -n -e 's,<FILE>\(.*\)</FILE>,\1,p' $(srcdir)/$(DOC_MODULE)-sections.txt |\
	while read file; do \
		if grep -F "xml/$$file.xml" $(srcdir)/$(DOC_MODULE)-docs.sgml \
			>/dev/null;\
		then \
		:; \
		else \
			echo "$$file missing from $(DOC_MODULE)-docs.sgml"; \
			err=1; \
		fi; \
	done; \
	case $(ENABLE_DOC_CHECKS)/$$err in \
		(yes/1) \
			echo "Documentation check failed" >&2; \
			exit $$err; \
			;; \
		(*) \
			exit 0; \
			;; \
	esac
endif

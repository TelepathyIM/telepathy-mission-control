EXTRA_DIST = \
    c-constants-generator.xsl \
    c-interfaces-generator.xsl \
    doc-generator.xsl \
    glib-client-gen.py \
    glib-blocking-client-gen.py \
    glib-client-marshaller-gen.py \
    glib-interfaces-generator.xsl \
    glib-interfaces-body-generator.xsl \
    glib-ginterface-gen.py \
    glib-gtypes-generator.py \
    glib-signals-marshal-gen.py \
    gquark-gen.py \
    identity.xsl \
    lcov.am \
    libtpcodegen.py \
    libglibcodegen.py \
    spec-to-introspect.xsl \
    telepathy.am

CLEANFILES = libglibcodegen.pyc libglibcodegen.pyo

all: $(EXTRA_DIST)

libglibcodegen.py: libtpcodegen.py
	touch $@
glib-client-marshaller-gen.py: libglibcodegen.py
	touch $@
glib-ginterface-gen.py: libglibcodegen.py
	touch $@
glib-gtypes-generator.py: libglibcodegen.py
	touch $@
glib-signals-marshal-gen.py: libglibcodegen.py
	touch $@

TELEPATHY_GLIB_SRCDIR = $(top_srcdir)/../telepathy-glib
maintainer-update-from-telepathy-glib:
	set -e && cd $(srcdir) && \
	for x in $(EXTRA_DIST); do \
		if test -f $(TELEPATHY_GLIB_SRCDIR)/tools/$$x; then \
			cp $(TELEPATHY_GLIB_SRCDIR)/tools/$$x $$x; \
		fi; \
	done

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
    identity.xsl \
    lcov.am \
    libtpcodegen.py \
    libglibcodegen.py \
    run_and_bt.gdb \
    spec-to-introspect.xsl \
    telepathy.am

CLEANFILES = libglibcodegen.pyc libglibcodegen.pyo

all: $(EXTRA_DIST)

libglibcodegen.py: libtpcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@
glib-client-marshaller-gen.py: libglibcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@
glib-ginterface-gen.py: libglibcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@
glib-gtypes-generator.py: libglibcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@

TELEPATHY_GLIB_SRCDIR = $(top_srcdir)/../telepathy-glib
maintainer-update-from-telepathy-glib:
	set -e && cd $(srcdir) && \
	for x in $(EXTRA_DIST); do \
		if test -f $(TELEPATHY_GLIB_SRCDIR)/tools/$$x; then \
			cp $(TELEPATHY_GLIB_SRCDIR)/tools/$$x $$x; \
		fi; \
	done

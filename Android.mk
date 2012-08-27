LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

TELEPATHY_MISSION_CONTROL_BUILT_SOURCES := \
	mission-control-plugins.pc \
	src/Android.mk \
	server/Android.mk \
	mission-control-plugins/Android.mk \
	util/Android.mk

telepathy-mission-control-configure-real:
	cd $(TELEPATHY_MISSION_CONTROL_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG="pkg-config --static" \
	PKG_CONFIG_LIBDIR="$(CONFIGURE_PKG_CONFIG_LIBDIR)" \
	PKG_CONFIG_TOP_BUILD_DIR=$(PKG_CONFIG_TOP_BUILD_DIR) \
	$(TELEPATHY_MISSION_CONTROL_TOP)/$(CONFIGURE) --host=arm-linux-androideabi \
		--disable-gtk-doc --disable-conn-setting && \
	for file in $(TELEPATHY_MISSION_CONTROL_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

telepathy-mission-control-configure: telepathy-mission-control-configure-real

.PHONY: telepathy-mission-control-configure

CONFIGURE_TARGETS += telepathy-mission-control-configure

#include all the subdirs...
-include $(TELEPATHY_MISSION_CONTROL_TOP)/src/Android.mk
-include $(TELEPATHY_MISSION_CONTROL_TOP)/util/Android.mk
-include $(TELEPATHY_MISSION_CONTROL_TOP)/server/Android.mk
-include $(TELEPATHY_MISSION_CONTROL_TOP)/mission-control-plugins/Android.mk
-include $(TELEPATHY_MISSION_CONTROL_TOP)/utils/Android.mk

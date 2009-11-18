include $(top_srcdir)/tools/shave.mk
include $(top_srcdir)/tools/flymake.mk

AM_CPPFLAGS =						\
	$(ERROR_CFLAGS)					\
	-I$(top_srcdir)                          	\
	-DG_LOG_DOMAIN=\"empathy\"			\
	$(EMPATHY_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)				\
	$(LIBCHAMPLAIN_CFLAGS)				\
	$(DISABLE_DEPRECATED)				\
	$(WEBKIT_CFLAGS)				\
	$(WARN_CFLAGS)

LDADD =								\
	$(top_builddir)/libempathy-gtk/libempathy-gtk.la	\
	$(top_builddir)/libempathy/libempathy.la		\
	$(top_builddir)/extensions/libemp-extensions.la		\
	$(LIBNOTIFY_LIBS)					\
	$(EMPATHY_LIBS)						\
	$(LIBCHAMPLAIN_LIBS)					\
	$(WEBKIT_LIBS)

bin_PROGRAMS =			\
	empathy

BUILT_SOURCES= \
	empathy-tube-dispatch-enumtypes.h \
	empathy-tube-dispatch-enumtypes.c

empathy_handwritten_source = \
	empathy-about-dialog.c empathy-about-dialog.h			\
	empathy-account-assistant.c empathy-account-assistant.h		\
	empathy-accounts-dialog.c empathy-accounts-dialog.h		\
	empathy-call-window-fullscreen.c empathy-call-window-fullscreen.h \
	empathy-call-window.c empathy-call-window.h			\
	empathy-chat-window.c empathy-chat-window.h			\
	empathy-chatrooms-window.c empathy-chatrooms-window.h		\
	empathy-debug-window.c empathy-debug-window.h			\
	empathy-event-manager.c empathy-event-manager.h			\
	empathy-ft-manager.c empathy-ft-manager.h			\
	empathy-import-dialog.c empathy-import-dialog.h			\
	empathy-import-mc4-accounts.c empathy-import-mc4-accounts.h	\
	empathy-import-pidgin.c empathy-import-pidgin.h			\
	empathy-import-utils.c empathy-import-utils.h			\
	empathy-import-widget.c empathy-import-widget.h			\
	empathy-main-window.c empathy-main-window.h			\
	empathy-new-chatroom-dialog.c empathy-new-chatroom-dialog.h	\
	empathy-preferences.c empathy-preferences.h			\
	empathy-sidebar.c empathy-sidebar.h				\
	empathy-status-icon.c empathy-status-icon.h			\
	empathy-tube-dispatch.c empathy-tube-dispatch.h			\
	empathy.c

empathy_SOURCES =							\
	$(empathy_handwritten_source)					\
	ephy-spinner.c ephy-spinner.h

nodist_empathy_SOURCES = $(BUILT_SOURCES)

check_c_sources = \
    $(empathy_handwritten_source) \
    $(empathy_logs_SOURCES)
include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

uidir = $(datadir)/empathy
ui_DATA = 					\
	empathy-accounts-dialog.ui		\
	empathy-call-window-fullscreen.ui	\
	empathy-call-window.ui			\
	empathy-chat-window.ui			\
	empathy-chatrooms-window.ui		\
	empathy-ft-manager.ui			\
	empathy-import-dialog.ui		\
	empathy-main-window.ui			\
	empathy-new-chatroom-dialog.ui		\
	empathy-preferences.ui			\
	empathy-status-icon.ui

EXTRA_DIST =			\
	$(autostart_DATA)	\
	$(ui_DATA)

if HAVE_LIBCHAMPLAIN
empathy_handwritten_source += 				\
	empathy-map-view.c			\
	empathy-map-view.h

ui_DATA += 					\
	empathy-map-view.ui
else
EXTRA_DIST +=					\
	empathy-map-view.c			\
	empathy-map-view.h			\
	empathy-map-view.ui
endif

dist_man_MANS =			\
	empathy.1

# rules for making the glib enum objects
%-enumtypes.h: %.h Makefile.in
	$(QUIET_GEN)glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h Makefile.in
	$(QUIET_GEN)glib-mkenums \
	--fhead "#include <$*.h>\n#include <$*-enumtypes.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@

CLEANFILES = $(BUILT_SOURCES)


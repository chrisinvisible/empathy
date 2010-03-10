include $(top_srcdir)/tools/flymake.mk

AM_CPPFLAGS =                                           \
	$(ERROR_CFLAGS)					\
	-I.                                             \
	-I$(top_srcdir)                                 \
	-DDATADIR=\""$(datadir)"\"              	\
	-DPKGDATADIR=\""$(pkgdatadir)"\"		\
	$(LIBEMPATHYGTK_CFLAGS)				\
	$(TPL_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)				\
	$(ENCHANT_CFLAGS)				\
	$(LIBCHAMPLAIN_CFLAGS)				\
	$(GEOCLUE_CFLAGS)				\
	$(MEEGO_CFLAGS)				\
	$(WEBKIT_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(DISABLE_DEPRECATED)

BUILT_SOURCES = 					\
	empathy-gtk-marshal.h				\
	empathy-gtk-marshal.c				\
	empathy-gtk-marshal.list			\
	empathy-gtk-enum-types.h			\
	empathy-gtk-enum-types.c

noinst_LTLIBRARIES = libempathy-gtk.la

libempathy_gtk_handwritten_source =            	\
	empathy-account-chooser.c		\
	empathy-account-widget-irc.c		\
	empathy-account-widget-private.h	\
	empathy-account-widget-sip.c		\
	empathy-account-widget.c		\
	empathy-audio-sink.c			\
	empathy-audio-src.c			\
	empathy-avatar-chooser.c		\
	empathy-avatar-image.c			\
	empathy-cell-renderer-activatable.c	\
	empathy-cell-renderer-expander.c	\
	empathy-cell-renderer-text.c		\
	empathy-chat-text-view.c		\
	empathy-chat-view.c			\
	empathy-chat.c				\
	empathy-conf.c				\
	empathy-contact-dialogs.c		\
	empathy-contact-list-store.c		\
	empathy-contact-list-view.c		\
	empathy-contact-menu.c			\
	empathy-contact-selector.c		\
	empathy-contact-selector-dialog.c \
	empathy-contact-widget.c		\
	empathy-geometry.c			\
	empathy-irc-network-dialog.c		\
	empathy-kludge-label.c			\
	empathy-log-window.c			\
	empathy-new-message-dialog.c		\
	empathy-new-call-dialog.c		\
	empathy-notify-manager.c		\
	empathy-presence-chooser.c		\
	empathy-protocol-chooser.c		\
	empathy-search-bar.c			\
	empathy-share-my-desktop.c		\
	empathy-smiley-manager.c		\
	empathy-sound.c				\
	empathy-spell.c				\
	empathy-status-preset-dialog.c		\
	empathy-string-parser.c			\
	empathy-theme-boxes.c			\
	empathy-theme-irc.c			\
	empathy-theme-manager.c			\
	empathy-ui-utils.c			\
	empathy-video-src.c			\
	empathy-video-widget.c

libempathy_gtk_headers =			\
	empathy-account-chooser.h		\
	empathy-account-widget-irc.h		\
	empathy-account-widget-sip.h		\
	empathy-account-widget.h		\
	empathy-audio-sink.h			\
	empathy-audio-src.h			\
	empathy-avatar-chooser.h		\
	empathy-avatar-image.h			\
	empathy-cell-renderer-activatable.h	\
	empathy-cell-renderer-expander.h	\
	empathy-cell-renderer-text.h		\
	empathy-chat-text-view.h		\
	empathy-chat-view.h			\
	empathy-chat.h				\
	empathy-conf.h				\
	empathy-contact-dialogs.h		\
	empathy-contact-list-store.h		\
	empathy-contact-list-view.h		\
	empathy-contact-menu.h			\
	empathy-contact-selector.h		\
	empathy-contact-selector-dialog.h \
	empathy-contact-widget.h		\
	empathy-geometry.h			\
	empathy-images.h			\
	empathy-irc-network-dialog.h		\
	empathy-kludge-label.h			\
	empathy-log-window.h			\
	empathy-new-message-dialog.h		\
	empathy-new-call-dialog.h		\
	empathy-notify-manager.h		\
	empathy-presence-chooser.h		\
	empathy-protocol-chooser.h		\
	empathy-search-bar.h			\
	empathy-share-my-desktop.h		\
	empathy-smiley-manager.h		\
	empathy-sound.h				\
	empathy-spell.h				\
	empathy-status-preset-dialog.h		\
	empathy-string-parser.h			\
	empathy-theme-boxes.h			\
	empathy-theme-irc.h			\
	empathy-theme-manager.h			\
	empathy-ui-utils.h			\
	empathy-video-src.h			\
	empathy-video-widget.h

libempathy_gtk_la_SOURCES =			\
	$(libempathy_gtk_handwritten_source)	\
	$(libempathy_gtk_headers)		\
	totem-subtitle-encoding.c totem-subtitle-encoding.h

# do not distribute generated files
nodist_libempathy_gtk_la_SOURCES =\
	$(BUILT_SOURCES)

libempathy_gtk_la_LIBADD =			\
	$(LIBEMPATHYGTK_LIBS)			\
	$(LIBNOTIFY_LIBS)			\
	$(TPL_LIBS)				\
	$(ENCHANT_LIBS)				\
	$(LIBCHAMPLAIN_LIBS)			\
	$(GEOCLUE_LIBS)				\
	$(MEEGO_LIBS)				\
	$(WEBKIT_LIBS)				\
	$(top_builddir)/libempathy/libempathy.la

check_c_sources = \
    $(libempathy_gtk_handwritten_source) \
    $(libempathy_gtk_headers)
include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

uidir = $(datadir)/empathy
ui_DATA = 					\
	empathy-contact-widget.ui		\
	empathy-contact-dialogs.ui		\
	empathy-account-widget-generic.ui	\
	empathy-account-widget-jabber.ui	\
	empathy-account-widget-msn.ui		\
	empathy-account-widget-sip.ui		\
	empathy-account-widget-local-xmpp.ui	\
	empathy-account-widget-irc.ui		\
	empathy-account-widget-icq.ui		\
	empathy-account-widget-yahoo.ui		\
	empathy-account-widget-groupwise.ui	\
	empathy-account-widget-aim.ui		\
	empathy-status-preset-dialog.ui		\
	empathy-log-window.ui			\
	empathy-chat.ui				\
	empathy-contact-selector-dialog.ui      \
	empathy-search-bar.ui

empathy-gtk-marshal.list: $(libempathy_gtk_la_SOURCES) Makefile.am
	$(AM_V_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*empathy_gtk_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(libempathy_gtk_la_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	@if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
	else \
		mv $@.tmp $@; \
	fi

%-marshal.h: %-marshal.list Makefile
	$(AM_V_GEN)$(GLIB_GENMARSHAL) --header --prefix=_$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list Makefile
	$(AM_V_GEN)echo "#include \"empathy-gtk-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) --body --prefix=_$(subst -,_,$*)_marshal $< >> $*-marshal.c

empathy-gtk-enum-types.h: stamp-empathy-gtk-enum-types.h
	$(AM_V_GEN)true
stamp-empathy-gtk-enum-types.h: Makefile $(libempathy_gtk_headers)
	$(AM_V_GEN)(cd $(srcdir) \
	&& glib-mkenums \
			--fhead "#ifndef __LIBEMPATHY_GTK_ENUM_TYPES_H__\n" \
			--fhead "#define __LIBEMPATHY_GTK_ENUM_TYPES_H__ 1\n\n" \
			--fhead "#include <glib-object.h>\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* __LIBEMPATHY_GTK_ENUM_TYPES_H__ */\n" \
			--fprod "#include <libempathy-gtk/@filename@>\n" \
			--eprod "#define EMPATHY_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(libempathy_gtk_headers) ) > xgen-gth \
	&& (cmp -s xgen-gth empathy-gtk-enum-type.h || cp xgen-gth empathy-gtk-enum-types.h) \
	&& rm -f xgen-gth \
	&& echo timestamp > $(@F)

empathy-gtk-enum-types.c: Makefile $(libempathy_gtk_headers)
	$(AM_V_GEN)(cd $(srcdir) \
	&& glib-mkenums \
			--fhead "#include <config.h>\n" \
			--fhead "#include <glib-object.h>\n" \
			--fhead "#include \"empathy-gtk-enum-types.h\"\n\n" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$(libempathy_gtk_headers) ) > xgen-gtc \
	&& cp xgen-gtc $(@F) \
	&& rm -f xgen-gtc

EXTRA_DIST =			\
	$(ui_DATA)

if HAVE_GEOCLUE
libempathy_gtk_handwritten_source +=		\
	empathy-location-manager.c

libempathy_gtk_headers +=			\
	empathy-location-manager.h
else
EXTRA_DIST +=					\
	empathy-location-manager.c		\
	empathy-location-manager.h
endif

if HAVE_WEBKIT
libempathy_gtk_handwritten_source +=		\
	empathy-plist.c				\
	empathy-theme-adium.c
libempathy_gtk_headers +=			\
	empathy-plist.h				\
	empathy-theme-adium.h
else
EXTRA_DIST +=					\
	empathy-plist.c				\
	empathy-plist.h				\
	empathy-theme-adium.c			\
	empathy-theme-adium.h
endif

CLEANFILES =				\
	$(BUILT_SOURCES)		\
	stamp-empathy-gtk-enum-types.h

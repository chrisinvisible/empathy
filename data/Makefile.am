SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files = \
	empathy.desktop.in \
	empathy-accounts.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

gsettings_files = \
	org.gnome.Empathy.gschema.xml.in \
	$(NULL)
gsettings_SCHEMAS = $(gsettings_files:.xml.in=.xml)
@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

convertdir = $(datarootdir)/GConf/gsettings
convert_DATA = \
	empathy.convert

streamingprefsdir =  $(datadir)/empathy
streamingprefs_DATA =		\
	codec-preferences	\
	element-properties

servicefiledir = $(datadir)/dbus-1/services
servicefile_in_files = \
	org.freedesktop.Telepathy.Client.Empathy.Chat.service.in \
	org.freedesktop.Telepathy.Client.Empathy.AudioVideo.service.in \
	org.freedesktop.Telepathy.Client.Empathy.Auth.service.in

servicefile_DATA = $(servicefile_in_files:.service.in=.service)

org.freedesktop.Telepathy.Client.Empathy.Chat.service: org.freedesktop.Telepathy.Client.Empathy.Chat.service.in
	$(AM_V_GEN)sed -e "s|[@]libexecdir[@]|$(libexecdir)|" $< > $@

org.freedesktop.Telepathy.Client.Empathy.AudioVideo.service: org.freedesktop.Telepathy.Client.Empathy.AudioVideo.service.in
	$(AM_V_GEN)sed -e "s|[@]libexecdir[@]|$(libexecdir)|" $< > $@

org.freedesktop.Telepathy.Client.Empathy.Auth.service: org.freedesktop.Telepathy.Client.Empathy.Auth.service.in
	$(AM_V_GEN)sed -e "s|[@]libexecdir[@]|$(libexecdir)|" $< > $@

clientfiledir = $(datarootdir)/telepathy/clients
clientfile_DATA = \
	Empathy.Chat.client \
	Empathy.AudioVideo.client \
	Empathy.Auth.client

htmldir = $(datadir)/empathy
html_DATA = Template.html

EXTRA_DIST = 				\
	$(convert_DATA)			\
	$(desktop_in_files)		\
	$(desktop_DATA)			\
	$(schemas_DATA)			\
	$(streamingprefs_DATA)		\
	$(html_DATA)			\
	$(clientfile_DATA)		\
	$(servicefile_in_files)		\
	$(gsettings_files)		\
	$(NULL)

DISTCLEANFILES = 		\
	$(desktop_DATA)		\
	$(schemas_DATA)		\
	$(servicefile_DATA)	\
	$(gsettings_SCHEMAS)	\
	$(NULL)

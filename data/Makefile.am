SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files = \
	empathy.desktop.in \
	empathy-accounts.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

gsettings_SCHEMAS = \
	org.gnome.Empathy.gschema.xml \
	$(NULL)

@GSETTINGS_RULES@

convertdir = $(datarootdir)/GConf/gsettings
convert_DATA = \
	empathy.convert

streamingprefsdir =  $(datadir)/empathy
streamingprefs_DATA =		\
	codec-preferences	\
	element-properties

servicefiledir = $(datadir)/dbus-1/services
servicefile_in_files = org.freedesktop.Telepathy.Client.Empathy.service.in
servicefile_DATA = $(servicefile_in_files:.service.in=.service)

org.freedesktop.Telepathy.Client.Empathy.service: org.freedesktop.Telepathy.Client.Empathy.service.in
	$(AM_V_GEN)sed -e "s|[@]bindir[@]|$(bindir)|" $< > $@

clientfiledir = $(datarootdir)/telepathy/clients
clientfile_DATA = Empathy.client

htmldir = $(datadir)/empathy
html_DATA = Template.html

EXTRA_DIST = 				\
	$(convert_DATA)			\
	$(desktop_in_files)		\
	$(desktop_DATA)			\
	$(gsettings_SCHEMAS)		\
	$(schemas_DATA)			\
	$(streamingprefs_DATA)		\
	$(html_DATA)			\
	$(clientfile_DATA)		\
	$(servicefile_in_files)

DISTCLEANFILES = 		\
	$(desktop_DATA)		\
	$(schemas_DATA)		\
	$(servicefile_DATA)


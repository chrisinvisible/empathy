SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files = \
	empathy.desktop.in \
	empathy-accounts.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

schemasdir	 = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files = empathy.schemas.in
schemas_DATA 	 = $(schemas_in_files:.schemas.in=.schemas)
@INTLTOOL_SCHEMAS_RULE@

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

if GCONF_SCHEMAS_INSTALL
install-data-local:
	if test -z "$(DESTDIR)" ; then \
	  for p in $(schemas_DATA) ; do \
	    GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $$p ; \
	  done \
	fi
else
install-data-local:
endif

htmldir = $(datadir)/empathy
html_DATA = Template.html

EXTRA_DIST = 				\
	$(desktop_in_files)		\
	$(desktop_DATA)			\
	$(schemas_in_files)		\
	$(schemas_DATA)			\
	$(streamingprefs_DATA)		\
	$(html_DATA)			\
	$(clientfile_DATA)		\
	$(servicefile_in_files)

DISTCLEANFILES = 		\
	$(desktop_DATA)		\
	$(schemas_DATA)		\
	$(servicefile_DATA)


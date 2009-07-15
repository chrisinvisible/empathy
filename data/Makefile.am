SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_files = empathy.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

schemasdir	 = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files = empathy.schemas.in
schemas_DATA 	 = $(schemas_in_files:.schemas.in=.schemas)
@INTLTOOL_SCHEMAS_RULE@

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
	$(html_DATA)

DISTCLEANFILES = 			\
	$(desktop_DATA)		\
	$(schemas_DATA)


SUBDIRS = interactive xml

CLEANFILES=

include $(top_srcdir)/rules/check.mak

SUPPRESSIONS=valgrind.supp dlopen.supp

EXTRA_DIST = 		\
	test.manager	\
	test.profile

AM_CPPFLAGS =						\
	$(ERROR_CFLAGS)					\
	-I$(top_srcdir)                          	\
	-DPKGDATADIR=\""$(pkgdatadir)"\"		\
	$(EMPATHY_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(DISABLE_DEPRECATED)

LDADD =								\
	$(top_builddir)/libempathy-gtk/libempathy-gtk.la	\
	$(top_builddir)/libempathy/libempathy.la		\
	$(EMPATHY_LIBS)

TEST_PROGS =                                     \
     empathy-utils-test                          \
     empathy-irc-server-test                     \
     empathy-irc-network-test

empathy_utils_test_SOURCES = empathy-utils-test.c \
     test-helper.c test-helper.h

empathy_irc_server_test_SOURCES = empathy-irc-server-test.c \
     test-helper.c test-helper.h                       \
     check-irc-helper.h check-irc-helper.c

empathy_irc_network_test_SOURCES = empathy-irc-network-test.c \
     test-helper.c test-helper.h                       \
     check-irc-helper.h check-irc-helper.c

check_PROGRAMS = check-main $(TEST_PROGS)

TESTS = check-main
check_main_SOURCES =                             \
    check-main.c                                 \
    check-helpers.c                              \
    check-helpers.h                              \
    check-libempathy.h                           \
    check-empathy-helpers.h                      \
    check-empathy-helpers.c                      \
    check-irc-helper.h                           \
    check-irc-helper.c                           \
    check-empathy-irc-network-manager.c          \
    check-empathy-chatroom.c                     \
    check-empathy-chatroom-manager.c

check_c_sources = \
    $(check_main_SOURCES)
include $(top_srcdir)/tools/check-coding-style.mk
check-local: test check-coding-style

check_main_LDADD = \
    @CHECK_LIBS@ \
    $(top_builddir)/libempathy-gtk/libempathy-gtk.la	\
    $(top_builddir)/libempathy/libempathy.la		\
    $(AM_LDFLAGS)

check_main_CFLAGS = \
    @CHECK_CFLAGS@ \
    $(AM_CFLAGS)

TESTS_ENVIRONMENT = EMPATHY_SRCDIR=@abs_top_srcdir@ \
		    MC_PROFILE_DIR=@abs_top_srcdir@/tests \
		    MC_MANAGER_DIR=@abs_top_srcdir@/tests

test-report: test-report.xml
	gtester-report $(top_builddir)/tests/$@.xml > \
	  $(top_builddir)/tests/$@.html

test-report.xml: ${TEST_PROGS} test

test: ${TEST_PROGS}
	gtester -o test-report.xml -k --verbose ${TEST_PROGS}

test-%: empathy-%-test
	gtester -o $@-report.xml -k --verbose $<

.PHONY: test test-report

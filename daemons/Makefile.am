
## Process this file with automake to produce Makefile.in
ACLOCAL_AMFLAGS         = -I m4

AM_CPPFLAGS             = -I$(top_srcdir) -I$(top_srcdir)/lib -Wall
AM_LDFLAGS              = -lpthread

CLEANFILES              = *~

varrundir               = $(localstatedir)/run

sbin_PROGRAMS           = lircd lircmd

lircd_SOURCES           = lircd.c
lircd_LDADD             = ../lib/liblirc.la

lircmd_SOURCES          = lircmd.c
lircmd_LDADD            = ../lib/liblirc.la

noinst_PROGRAMS         = lircd.simsend lircd.simrec
lircd_simsend_SOURCES   = lircd.c
lircd_simsend_CFLAGS    = -DSIM_SEND
lircd_simsend_LDADD     = ../lib/liblirc.la
lircd_simrec_SOURCES    = lircd.c
lircd_simrec_CFLAGS     = -DSIM_REC
lircd_simrec_LDADD      = ../lib/liblirc.la


install-exec-local:
	test -d  $(DESTDIR)$(varrundir)/$(PACKAGE) \
		 || mkdir -p $(DESTDIR)$(varrundir)/$(PACKAGE)
uninstall-local:
	-$(RM) $(DESTDIR)$(varrundir)/$(PACKAGE)/lircd

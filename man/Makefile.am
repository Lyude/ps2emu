man_MANS = \
	ps2emu-record.1 \
	ps2emu-replay.1

MAN_SUBSTS = -e 's|__version__|$(PACKAGE_VERSION)|g'

SUFFIXES = .1 .man

%.1 : %.man
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@

EXTRA_DIST = \
	ps2emu-record.man \
	ps2emu-replay.man

CLEANFILES = $(man_MANS)

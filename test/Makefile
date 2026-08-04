# Host tests for Odograph's pure logic. Nothing here is compiled into the FAP;
# application.fam lists its sources explicitly so this directory stays out of
# the firmware build.
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2 -g

BINS = host_tpms_test host_expose_test

.PHONY: all run clean
all: run

host_tpms_test: host_tpms_test.c ../helpers/od_tpms.c ../helpers/od_tpms.h \
                ../helpers/od_demod.c ../helpers/od_demod.h
	$(CC) $(CFLAGS) -o $@ host_tpms_test.c ../helpers/od_tpms.c ../helpers/od_demod.c

host_expose_test: host_expose_test.c ../helpers/od_expose.c ../helpers/od_expose.h
	$(CC) $(CFLAGS) -o $@ host_expose_test.c ../helpers/od_expose.c

run: $(BINS)
	@./host_tpms_test
	@echo
	@./host_expose_test

clean:
	rm -rf $(BINS) *.dSYM

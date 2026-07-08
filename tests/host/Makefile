# Host build of the portable core (manifest parser, version compare,
# hex utilities) + unit tests. Compiles the REAL production source
# from ../../src -- see docs/VERIFICATION.md for exactly what this
# does and does not cover.
CC      ?= cc
CSTD    ?= -std=gnu99
CFLAGS  ?= $(CSTD) -O0 -g -Wall -Wextra -Wpedantic
INCLUDES := -iquote ../../include

CORE := ../../src/manifest.c ../../src/version.c ../../src/hexutil.c

BINS := test_manifest test_version test_hexutil

.PHONY: all test clean
all: $(BINS)

test_manifest: test_manifest.c $(CORE)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

test_version: test_version.c $(CORE)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

test_hexutil: test_hexutil.c $(CORE)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

test: $(BINS)
	./test_manifest
	./test_version
	./test_hexutil

clean:
	rm -f $(BINS)

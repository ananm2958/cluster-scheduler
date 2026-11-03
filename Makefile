CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11

.PHONY: all clean

all: ns-launcher

ns-launcher: ns_launcher.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f ns-launcher

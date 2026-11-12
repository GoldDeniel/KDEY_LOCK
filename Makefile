CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

all: keyclean

keyclean: src/keyclean.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $<

test/fakekbd: test/fakekbd.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $<

test/evmon: test/evmon.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $<

test: keyclean test/fakekbd test/evmon

clean:
	rm -f keyclean test/fakekbd test/evmon

install: keyclean
	install -Dm755 keyclean $(DESTDIR)$(BINDIR)/keyclean
	install -Dm644 data/70-keyclean.rules $(DESTDIR)/etc/udev/rules.d/70-keyclean.rules

.PHONY: all clean install test

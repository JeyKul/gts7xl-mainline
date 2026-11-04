# SPDX-License-Identifier: MIT
CC ?= cc
CFLAGS ?= -O2
override CFLAGS += -std=gnu11 -Wall -Wextra -Wshadow -Wformat=2 -D_FORTIFY_SOURCE=2
PREFIX ?= /usr/local

OBJS := src/main.o src/listener.o src/client.o src/fsrv.o src/wire.o src/pb.o src/ssc.o
TEST_OBJS := tests/test.o src/fsrv.o src/wire.o src/pb.o src/ssc.o

gts7xl-sensord: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJS) tests/test.o: $(wildcard src/*.h)

tests/test.o: override CFLAGS += -Isrc
tests/test: $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# The real program with its kernel interfaces simulated at link time.
E2E_WRAP := -Wl,--wrap=socket,--wrap=getsockname,--wrap=sendto,--wrap=recvfrom,--wrap=poll,--wrap=ioctl
tests/e2e: tests/e2e.c $(wildcard src/*.c src/*.h)
	$(CC) -std=gnu11 -O1 -g -Isrc -Dmain=sensord_main -c -o tests/e2e_main.o src/main.c
	$(CC) -std=gnu11 -O1 -g -Isrc $(LDFLAGS) -o $@ tests/e2e.c tests/e2e_main.o src/listener.c src/client.c src/fsrv.c src/wire.c src/pb.c src/ssc.c $(E2E_WRAP)

check: tests/test tests/e2e
	$(RUN) tests/test
	$(RUN) tests/e2e

install: gts7xl-sensord
	install -Dm755 gts7xl-sensord $(DESTDIR)$(PREFIX)/bin/gts7xl-sensord
	install -Dm755 tools/gts7xl-sensord-extract $(DESTDIR)$(PREFIX)/sbin/gts7xl-sensord-extract
	install -Dm644 dist/gts7xl-sensord-fs.service $(DESTDIR)/etc/systemd/system/gts7xl-sensord-fs.service
	install -Dm644 dist/gts7xl-sensord-accel.service $(DESTDIR)/etc/systemd/system/gts7xl-sensord-accel.service
	install -Dm644 dist/70-gts7xl-sensord.rules $(DESTDIR)/etc/udev/rules.d/70-gts7xl-sensord.rules

# Separate: with this in place, every SLPI restart starts the services.
install-autostart:
	install -Dm644 dist/71-gts7xl-sensord-start.rules $(DESTDIR)/etc/udev/rules.d/71-gts7xl-sensord-start.rules
	install -Dm644 dist/81-gts7xl-sensord-no-libssc.rules $(DESTDIR)/etc/udev/rules.d/81-gts7xl-sensord-no-libssc.rules

clean:
	rm -f gts7xl-sensord tests/test tests/e2e src/*.o tests/*.o

.PHONY: check install install-autostart clean

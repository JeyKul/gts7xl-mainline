# Bionic (Android) binaries for the GNSS stack, without the NDK: clang + lld
CC = clang --target=aarch64-linux-android30 -fuse-ld=lld
CFLAGS = -O2 -Wall -nostdlibinc -ffreestanding
O = out

all: $(O)/gnss-hal $(O)/libhardware_legacy.so

$(O)/stub/libc.so $(O)/stub/libdl.so: stubs.sh
	sh stubs.sh $(O)/stub
	$(CC) -shared -nostdlib -fPIC -fno-builtin -w -Wl,-soname,libc.so -o $(O)/stub/libc.so $(O)/stub/libc.c
	$(CC) -shared -nostdlib -fPIC -Wl,-soname,libdl.so -o $(O)/stub/libdl.so $(O)/stub/libdl.c

$(O)/gnss-hal: start.S hal.c $(O)/stub/libc.so $(O)/stub/libdl.so
	$(CC) $(CFLAGS) -fPIE -pie -nostdlib -Wl,--dynamic-linker=/system/bin/linker64 \
		-o $@ start.S hal.c -L$(O)/stub -lc -ldl

$(O)/libhardware_legacy.so: hwlegacy.c $(O)/stub/libc.so
	$(CC) $(CFLAGS) -fPIC -shared -nostdlib -Wl,-soname,libhardware_legacy.so \
		-o $@ hwlegacy.c -L$(O)/stub -lc

install: all
	install -Dm755 $(O)/gnss-hal $(DESTDIR)/usr/local/lib/gts7xl-gnss/gnss-hal
	install -Dm644 $(O)/libhardware_legacy.so $(DESTDIR)/usr/local/lib/gts7xl-gnss/libhardware_legacy.so

clean:
	rm -rf $(O)

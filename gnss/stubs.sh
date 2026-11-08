#!/bin/sh
# Link-time stand-ins for bionic's libc.so and libdl.so: just the symbols the
# GNSS helpers import. At run time the tablet's own bionic is used instead.
set -e
O=$1
mkdir -p "$O"
{
	for f in printf fflush putchar puts sleep pthread_create __libc_init __stack_chk_fail socket bind recv close; do
		echo "void $f(void) {}"
	done
	echo "void *stdout;"
} > "$O/libc.c"
printf 'void dlopen(void) {}\nvoid dlsym(void) {}\nvoid dlerror(void) {}\n' > "$O/libdl.c"

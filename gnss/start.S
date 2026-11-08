// Minimal bionic entry (crtbegin_dynamic): __libc_init(raw_args, onexit, main, structors)
	.text
	.globl _start
_start:
	mov	x0, sp
	mov	x1, #0
	adrp	x2, main
	add	x2, x2, :lo12:main
	adrp	x3, structors
	add	x3, x3, :lo12:structors
	bl	__libc_init
	.data
	.balign 8
structors:			// preinit, init, fini: the linker runs init arrays
	.quad 0, 0, 0

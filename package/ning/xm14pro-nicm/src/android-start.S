/* SPDX-License-Identifier: GPL-2.0-only */

	.text
	.align 2
	.global _start
	.type _start, %function
_start:
	mov x29, xzr
	mov x30, xzr
	ldr x0, [sp]
	add x1, sp, #8
	bl main
	mov x8, #94
	svc #0
	b .
	.size _start, .-_start

	.section .note.GNU-stack,"",%progbits

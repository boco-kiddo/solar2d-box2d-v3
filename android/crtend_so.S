	.section .note.GNU-stack, "", %progbits
#if !defined(__arm__)
	.section .eh_frame, "a", @progbits
	.balign 4
	.type	__FRAME_END__, @object
	.size	__FRAME_END__, 4
__FRAME_END__:
	.zero	4
#endif

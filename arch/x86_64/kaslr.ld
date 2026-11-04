OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS {
	text PT_LOAD FLAGS(5); /* Execute + Read */
	rodata PT_LOAD FLAGS(4); /* Read only */
	extable PT_LOAD FLAGS(4); /* Read only */
	fixup PT_LOAD FLAGS(5); /* Execute + Read */
	data PT_LOAD FLAGS(6); /* Write + Read */
	dynamic PT_DYNAMIC FLAGS(6); /* Write + Read */
}

SECTIONS {
	. = 0;
	INCLUDE "kernel/generic.ld"
	INCLUDE "arch/x86_64/generic.ld"
}

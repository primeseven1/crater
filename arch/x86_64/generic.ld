. = ALIGN(CONSTANT(MAXPAGESIZE));
.extable : {
	. = ALIGN(8);
	_ld_arch_x86_64_kernel_extable_start = .;
	KEEP(*(.extable .extable.*))
	_ld_arch_x86_64_kernel_extable_end = .;
} :extable

. = ALIGN(CONSTANT(MAXPAGESIZE));
.fixup : {
	. = ALIGN(8);
	_ld_arch_x86_64_kernel_fixup_start = .;
	KEEP(*(.fixup .fixup.*))
	_ld_arch_x86_64_kernel_fixup_end = .;
} :fixup

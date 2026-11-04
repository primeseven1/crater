_ld_kernel_start = .;

. = ALIGN(CONSTANT(MAXPAGESIZE));

.text : {
	_ld_kernel_text_start = .;
	*(.text .text.*)
	_ld_kernel_text_end = .;
} :text

. = ALIGN(CONSTANT(MAXPAGESIZE));

.rodata : {
	_ld_kernel_rodata_start = .;
	*(.rodata .rodata.*)

	. = ALIGN(8);
	_ld_kernel_schedpolicies_start = .;
	KEEP(*(.schedpolicies .schedpolicies.*))
	_ld_kernel_schedpolicies_end = .;

	_ld_kernel_rodata_end = .;
} :rodata

. = ALIGN(CONSTANT(MAXPAGESIZE));

.data : {
	_ld_kernel_data_start = .;
	*(.data .data.*)

	. = ALIGN(8);
	_ld_kernel_modules_start = .;
	KEEP(*(.modules .modules.*))
	_ld_kernel_modules_end = .;
	. = ALIGN(8);
	_ld_kernel_initt_start = .;
	KEEP(*(.initt .initt.*))
	_ld_kernel_initt_end = .;
	. = ALIGN(8);
	_ld_kernel_timekeepers_start = .;
	KEEP(*(.timekeepers .timekeepers.*))
	_ld_kernel_timekeepers_end = .;
	. = ALIGN(8);
	_ld_kernel_irqctl_start = .;
	KEEP(*(.irqctl .irqctl.*))
	_ld_kernel_irqctl_end = .;
	. = ALIGN(8);
	_ld_kernel_timers_start = .;
	KEEP(*(.timers .timers.*))
	_ld_kernel_timers_end = .;
	. = ALIGN(8);

	KEEP(*(.limine_requests_start_marker))
	KEEP(*(.limine_requests))
	KEEP(*(.limine_requests_end_marker))

	_ld_kernel_data_end = .;
} :data

. = ALIGN(CONSTANT(MAXPAGESIZE));

.dynamic : {
	_ld_kernel_dynamic_start = .;
	*(.dynamic)
	_ld_kernel_dynamic_end = .;
} :data :dynamic

. = ALIGN(CONSTANT(MAXPAGESIZE));

.bss : {
	_ld_kernel_bss_start = .;
	*(.bss .bss.*)
	*(COMMON)
	_ld_kernel_bss_end = .;
} :data

/DISCARD/ : {
	*(.eh_frame)
	*(.note .note.*)
}

_ld_kernel_end = .;

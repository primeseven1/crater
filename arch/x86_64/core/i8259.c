#include <crater/irq.h>
#include <x86_64/interrupt.h>
#include <x86_64/pmio.h>

#include "internal.h"

enum pic_io_address {
	PIC_IO_ADDRESS_MASTER = 0x20,
	PIC_IO_ADDRESS_SLAVE = 0xA0,
	PIC_IO_ADDRESS_MASTER_DATA = PIC_IO_ADDRESS_MASTER + 1,
	PIC_IO_ADDRESS_SLAVE_DATA = PIC_IO_ADDRESS_SLAVE + 1
};

#define PIC_CASCADE_IRQ 0x02
#define PIC_EOI 0x20

static inline void i8259_write(enum pic_io_address address, u8 data) {
	arch_x86_64_outb(address, data);
	arch_x86_64_pmio_delay();
}

/*
 * Spurious IRQ's can only happen on IRQ 7 and IRQ 15. Since all IRQ's are masked on the i8259,
 * the IRQ has to be spurious, so no need to check the PIC for a spurious IRQ.
 */
static void spurious_masked(struct isr* isr) {
	int irqnum = isr->arch_specific.id - I8259_VECTOR_OFFSET;

	/* A spurious IRQ15 comes from the slave PIC, but the master doesn't know it was spurious, so send the EOI to the master */
	if (irqnum == 15)
		arch_x86_64_outb(PIC_IO_ADDRESS_MASTER, PIC_EOI);
	else
		bug(irqnum != 7);
}

#define PIC_ICW1_ICW4 0x01
#define PIC_ICW1_SINGLE 0x02
#define PIC_ICW1_INTERVAL4 0x04
#define PIC_ICW1_LEVEL 0x08
#define PIC_ICW1_INIT 0x10
#define PIC_ICW4_8086 0x01
#define PIC_ICW4_AUTO 0x02
#define PIC_ICW4_BUF_SLAVE 0x08
#define PIC_ICW4_BUF_MASTER 0x0C
#define PIC_ICW4_SFNM 0x10

void i8259_initialize_and_mask(void) {
	static struct isr irq7, irq15;
	int err = arch_x86_64_register_isr_vector(&irq7, I8259_VECTOR_OFFSET + 7, spurious_masked, NULL, ISR_FLAG_TYPE_IRQ, false);
	if (unlikely(err))
		panic("Failed to register i8259 spurious IRQ7: %d", err);
	err = arch_x86_64_register_isr_vector(&irq15, I8259_VECTOR_OFFSET + 15, spurious_masked, NULL, ISR_FLAG_TYPE_IRQ, false);
	if (unlikely(err))
		panic("Failed to register i8259 spurious IRQ15: %d", err);

	i8259_write(PIC_IO_ADDRESS_MASTER, PIC_ICW1_INIT | PIC_ICW1_ICW4); /* Start initialization on PIC1, 4 commands */
	i8259_write(PIC_IO_ADDRESS_SLAVE, PIC_ICW1_INIT | PIC_ICW1_ICW4); /* Start initialization on PIC2, also 4 commands */
	i8259_write(PIC_IO_ADDRESS_MASTER_DATA, I8259_VECTOR_OFFSET); /* Set IDT vector offset for IRQ's 0-7 */
	i8259_write(PIC_IO_ADDRESS_SLAVE_DATA, I8259_VECTOR_OFFSET + 8); /* Set IDT vector offset for IRQ's 8-15  */
	i8259_write(PIC_IO_ADDRESS_MASTER_DATA, 1 << PIC_CASCADE_IRQ); /* For some reason, the master PIC has this as a bitmask */
	i8259_write(PIC_IO_ADDRESS_SLAVE_DATA, PIC_CASCADE_IRQ); /* Tell the slave PIC what cascade IRQ the master PIC expects */
	i8259_write(PIC_IO_ADDRESS_MASTER_DATA, PIC_ICW4_8086); /* Put the master PIC into 8086 mode */
	i8259_write(PIC_IO_ADDRESS_SLAVE_DATA, PIC_ICW4_8086); /* Put the slave PIC into 8086 mode */
	i8259_write(PIC_IO_ADDRESS_MASTER_DATA, 0xFF); /* Mask IRQ 0-7 */
	i8259_write(PIC_IO_ADDRESS_SLAVE_DATA, 0xFF); /* Mask IRQ 8-15 */
}

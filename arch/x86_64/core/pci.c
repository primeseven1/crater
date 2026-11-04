#include <crater/spinlock.h>
#include <crater/pci.h>

#include <arch/pci.h>
#include <arch/io.h>
#include <x86_64/pmio.h>

#define PCI_LEGACY_CONFIG_PORT 0xCF8
#define PCI_LEGACY_DATA_PORT 0xCFC

static SPINLOCK_DEFINE(legacy_lock);

static inline u32 pci_legacy_address(struct pci_device* device, u32 off) {
	return 0x80000000u | (device->bus << 16) | (device->dev << 11) | (device->fn << 8) | (off & 0xFC);
}

int arch_pci_legacy_read(struct pci_device* device, u16 off, u32* out) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&legacy_lock, &irq_flags);

	arch_x86_64_outl(PCI_LEGACY_CONFIG_PORT, pci_legacy_address(device, off));
	*out = arch_x86_64_inl(PCI_LEGACY_DATA_PORT);

	spinlock_release_irq_restore(&legacy_lock, &irq_flags);
	return 0;
}

int arch_pci_legacy_write(struct pci_device* device, u16 off, u32 value) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&legacy_lock, &irq_flags);

	arch_x86_64_outl(PCI_LEGACY_CONFIG_PORT, pci_legacy_address(device, off));
	arch_x86_64_outl(PCI_LEGACY_DATA_PORT, value);

	spinlock_release_irq_restore(&legacy_lock, &irq_flags);
	return 0;
}

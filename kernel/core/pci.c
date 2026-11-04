#include <crater/pci.h>
#include <crater/slab.h>
#include <crater/vmm.h>
#include <crater/printk.h>
#include <crater/init.h>

#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include <arch/io.h>
#include <arch/pci.h>

struct pci_ecam {
	physaddr_t physical;
	void __iomem* virtual;
	size_t len;
	u16 domain;
	u8 start_bus, end_bus;
};

static struct pci_ecam* ecam_arr = NULL;
static size_t ecam_count = 0;

static const struct pci_ecam* pci_ecam_find(u16 domain, u8 bus) {
	for (size_t i = 0; i < ecam_count; i++) {
		const struct pci_ecam* e = &ecam_arr[i];
		if (e->domain == domain && bus >= e->start_bus && bus <= e->end_bus)
			return e;
	}
	return NULL;
}

static inline size_t pci_ecam_offset(const struct pci_ecam* ecam, const struct pci_device* device, u16 off) {
	size_t b = device->bus - ecam->start_bus;
	return (b << 20) | (device->dev << 15) | (device->fn << 12) | (off & 0xFC);
}

static int pci_ecam_read(struct pci_device* device, u16 off, u32* out) {
	const struct pci_ecam* e = pci_ecam_find(device->domain, device->bus);
	if (!e)
		return -ENODEV;

	size_t offset = pci_ecam_offset(e, device, off);
	*out = readl((u32 __iomem*)((uintptr_t)e->virtual + offset));
	return 0;
}

static int pci_ecam_write(struct pci_device* device, u16 off, u32 value) {
	const struct pci_ecam* e = pci_ecam_find(device->domain, device->bus);
	if (!e)
		return -ENODEV;

	size_t offset = pci_ecam_offset(e, device, off);
	writel((u32 __iomem*)((uintptr_t)e->virtual + offset), value);
	return 0;
}

static bool use_legacy = false;

#define PCI_LEGACY_CONFIG_SIZE 256u
#define PCI_ECAM_CONFIG_SIZE 4096u

static int read(struct pci_device* device, u16 off, u32* out) {
	if (use_legacy) {
		if (off >= PCI_LEGACY_CONFIG_SIZE)
			return -EINVAL;
		return arch_pci_legacy_read(device, off, out);
	}

	if (off >= PCI_ECAM_CONFIG_SIZE)
		return -EINVAL;

	return pci_ecam_read(device, off, out);
}

static int write(struct pci_device* device, u16 off, u32 value) {
	if (use_legacy) {
		if (off >= PCI_LEGACY_CONFIG_SIZE)
			return -EINVAL;
		return arch_pci_legacy_write(device, off, value);
	}

	if (off >= PCI_ECAM_CONFIG_SIZE)
		return -EINVAL;

	return pci_ecam_write(device, off, value);
}

int pci_read_config_byte(struct pci_device* device, u16 off, u8* out) {
	u32 _out;
	int ret = read(device, off, &_out);
	if (ret)
		return ret;
	*out = (u8)((_out >> ((off & 3u) * 8u)) & 0xFFu);
	return 0;
}

int pci_read_config_word(struct pci_device* device, u16 off, u16* out) {
	if (off & (sizeof(u16) - 1))
		return -EINVAL;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;

	u32 _out;
	int err = read(device, base, &_out);
	if (err)
		return err;
	*out = (u16)((_out >> (byte * 8u)) & 0xFFFFu);
	return 0;
}

int pci_read_config_dword(struct pci_device* device, u16 off, u32* out) {
	if (off & (sizeof(u32) - 1))
		return -EINVAL;
	return read(device, off, out);
}

static inline u32 mask32(unsigned int bits, unsigned int shift) {
	return ((bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u)) << shift);
}

int pci_write_config_byte(struct pci_device* device, u16 off, u8 val) {
	unsigned int shift = (off & 3u) * 8u;
	u32 base = off & ~3u;

	u32 _val;
	int err = read(device, base, &_val);
	if (err)
		return err;
	_val = (_val & ~mask32(8, shift)) | ((u32)val << shift);
	return write(device, base, _val);
}

int pci_write_config_word(struct pci_device* device, u16 off, u16 val) {
	if (off & (sizeof(u16) - 1))
		return -EINVAL;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;
	unsigned int shift = byte * 8u;

	u32 _val;
	int err = read(device, base, &_val);
	if (err)
		return err;
	_val = (_val & ~mask32(16, shift)) | ((u32)val << shift);
	return write(device, base, _val);
}

int pci_write_config_dword(struct pci_device* dev, u16 off, u32 val) {
	if (off & (sizeof(u32) - 1))
		return -EINVAL;
	return write(dev, off, val);
}

int pci_handle_open(u16 domain, u8 bus, u8 dev, u8 fn, struct pci_device** out) {
	*out = NULL;
	if (dev >= PCI_MAX_DEVICES || fn >= PCI_MAX_FUNCTIONS)
		return -EINVAL;

	if (!use_legacy) {
		const struct pci_ecam* e = pci_ecam_find(domain, bus);
		if (!e)
			return -ENODEV;
	} else if (domain != 0) {
		return -ENODEV;
	}

	struct pci_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->domain = domain;
	device->bus = bus;
	device->dev = dev;
	device->fn = fn;

	*out = device;
	return 0;
}

void pci_handle_close(struct pci_device* device) {
	kfree(device);
}

static int pci_enumerate_function(u16 domain, u8 bus, u8 dev, u8 func) {
	struct pci_device* device;

	int err = pci_handle_open(domain, bus, dev, func, &device);
	if (err)
		return 0;
	u16 vendor;
	err = pci_read_config_word(device, PCI_CONFIG_VENDOR, &vendor);
	pci_handle_close(device);

	if (err || vendor == U16_MAX)
		return 0;

	/* assuming 1 PCI controller (for now) */
	printk("pci: %04x:%02x:%02x.%x found\n", domain, bus, dev, func); /* TODO, do something... */
	return 1;
}

static int pci_enumerate_device(u16 domain, u8 bus, u8 dev) {
	int visited = 0;

	struct pci_device* device;
	int err = pci_handle_open(domain, bus, dev, 0, &device);
	if (err)
		return err == -ENODEV ? -ENODEV : 0;

	u16 vendor;
	err = pci_read_config_word(device, PCI_CONFIG_VENDOR, &vendor);
	if (err || vendor == U16_MAX)
		goto out;
	u8 header;
	err = pci_read_config_byte(device, PCI_CONFIG_HEADER_TYPE, &header);
	if (err)
		goto out;
	bool multi = header & 0x80;
	u32 max_func = multi ? 7 : 0;

	for (u32 func = 0; func <= max_func; func++)
		visited += pci_enumerate_function(domain, bus, dev, func);
out:
	pci_handle_close(device);
	return visited;
}

static int pci_enumerate_bus(u16 domain, u8 bus) {
	int visited = 0;
	for (u32 device = 0; device < PCI_MAX_DEVICES; device++) {
		int rc = pci_enumerate_device(domain, bus, device);
		if (rc < 0)
			return rc;
		visited += rc;
	}

	return visited;
}

static int pci_enumerate(void) {
	int visited = 0;
	for (u16 domain = 0; domain < U16_MAX; domain++) {
		for (u32 bus = 0; bus < PCI_MAX_BUSES; bus++) {
			int rc = pci_enumerate_bus(domain, bus);
			if (rc < 0)
				break;
			visited += rc;
		}
	}

	return visited;
}

static void pci_init(void) {
	uacpi_table table;
	if (uacpi_table_find_by_signature("MCFG", &table) != UACPI_STATUS_OK) {
		use_legacy = true;
		return;
	}

	bool fail = true;

	struct acpi_mcfg* mcfg = table.ptr;
	size_t count = (mcfg->hdr.length - sizeof(*mcfg)) / sizeof(struct acpi_mcfg_allocation);
	if (unlikely(count == 0))
		goto cleanup;

	ecam_arr = kzalloc(count * sizeof(*ecam_arr), MM_ZONE_NORMAL);
	if (!ecam_arr)
		out_of_memory();

	for (size_t i = 0; i < count; i++) {
		const struct acpi_mcfg_allocation* a = &mcfg->entries[i];

		struct pci_ecam* e = &ecam_arr[i];
		e->domain = a->segment;
		e->start_bus = a->start_bus;
		e->end_bus = a->end_bus;

		size_t len = (a->end_bus - a->start_bus + 1) * (1 << 20);
		e->len = len;

		e->physical = a->address;
		e->virtual = iomap(e->physical, len, PGPROT_UC);
		if (IS_PTR_ERR(e->virtual))
			goto cleanup;
	}

	fail = false;
	ecam_count = count;
cleanup:
	if (fail) {
		use_legacy = true;
		if (ecam_arr) {
			for (size_t i = 0; i < count; i++) {
				struct pci_ecam* e = &ecam_arr[i];
				if (e->virtual)
					iounmap(e->virtual, e->len);
			}
			kfree(ecam_arr);
		}
	}
	uacpi_table_unref(&table);

	if (use_legacy)
		printk("pci: Using legacy\n");
	else
		printk("pci: Using MCFG\n");

	printk("pci: Found %d devices\n", pci_enumerate());
}

INIT_TASK_DECLARE(vmm_init_task, acpi_tables_init_task);
INIT_TASK_DEFINE(pci_init_task, INIT_TASK_SCOPE_BSP, pci_init, &vmm_init_task, &acpi_tables_init_task);

#pragma once

#include <crater/errno.h>

struct pci_device;

int arch_pci_legacy_read(struct pci_device* device, u16 off, u32* out);
int arch_pci_legacy_write(struct pci_device* device, u16 off, u32 value);

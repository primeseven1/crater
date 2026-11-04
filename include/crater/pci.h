#pragma once

#include <crater/errno.h>

#define PCI_MAX_BUSES 256u
#define PCI_MAX_FUNCTIONS 8u
#define PCI_MAX_DEVICES 32u

#define PCI_CONFIG_VENDOR 0x00
#define PCI_CONFIG_DEVICE_ID 0x02
#define PCI_CONFIG_COMMAND 0x04
#define PCI_CONFIG_STATUS 0x06
#define PCI_CONFIG_REVISION 0x08
#define PCI_CONFIG_PROGIF 0x09
#define PCI_CONFIG_SUBCLASS 0x0a
#define PCI_CONFIG_CLASS 0x0b
#define PCI_CONFIG_HEADER_TYPE 0x0e
#define PCI_CONFIG_CAP 0x34

#define PCI_COMMAND_IO (1 << 0)
#define PCI_COMMAND_MMIO (1 << 1)
#define PCI_COMMAND_BUSMASTER (1 << 2)
#define PCI_COMMAND_IRQ_DISABLE (1 << 10)

#define PCI_STATUS_INT 0x08
#define PCI_STATUS_HAS_CAP 0x10
#define PCI_STATUS_66MHZ 0x20
#define PCI_STATUS_FAST_BACK 0x80
#define PCI_STATUS_MASTER_PARITY 0x100
#define PCI_STATUS_DEVSEL_FAST 0x00
#define PCI_STATUS_DEVSEL_MEDIUM 0x200
#define PCI_STATUS_DEVSEL_SLOW 0x400
#define PCI_STATUS_DEVSEL_MASK 0x600
#define PCI_STATUS_SIG_TARGET_ABORT 0x800
#define PCI_STATUS_REC_TARGET_ABORT 0x1000
#define PCI_STATUS_REC_MASTER_ABORT 0x2000
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define PCI_STATUS_PARITY_ERROR 0x8000

#define PCI_CLASS_STORAGE 0x01

#define PCI_SUBCLASS_STORAGE_SCSI 0x00
#define PCI_SUBCLASS_STORAGE_IDE 0x01
#define PCI_SUBCLASS_STORAGE_FLOPPY 0x02
#define PCI_SUBCLASS_STORAGE_IPI 0x03
#define PCI_SUBCLASS_STORAGE_RAID 0x04
#define PCI_SUBCLASS_STORAGE_ATA 0x05
#define PCI_SUBCLASS_STORAGE_SATA 0x06
#define PCI_SUBCLASS_STORAGE_SAS 0x07
#define PCI_SUBCLASS_STORAGE_NVM 0x08

#define PCI_HEADER_TYPE_MULTIFUNCTION_MASK 0x80
#define PCI_HEADER_TYPE_MASK 0x7f
#define PCI_HEADER_TYPE_STANDARD 0

#define PCI_CAP_MSI 0x05
#define PCI_CAP_MSIX 0x11

struct pci_device {
	u16 domain;
	u8 bus, dev, fn;
};

/**
 * @brief Create a PCI device handle
 *
 * This function does NOT check the vendor ID of the device.
 *
 * @param[in] domain The PCI segment
 * @param[in] bus The PCI bus number
 * @param[in] dev The device number
 * @param[in] fn The function number
 * @param[out] out The pointer to the handle
 *
 * @retval 0 Successful
 * @retval -ENOMEM Out of memory
 * @retval -ENODEV Device doesn't exist
 */
int pci_handle_open(u16 domain, u8 bus, u8 dev, u8 fn, struct pci_device** out);

/**
 * @brief Close a PCI device handle
 * @param device The device the close
 */
void pci_handle_close(struct pci_device* device);

/**
 * @brief Read a byte from a PCI device config
 *
 * @param device[in] The device to read from
 * @param off[in] Offset into the config
 * @param out[out] Where the value is stored
 *
 * @retval 0 Successful
 * @retval -EINVAL Bad offset
 */
int pci_read_config_byte(struct pci_device* device, u16 off, u8* out);

/**
 * @brief Read a word from a PCI device config
 *
 * @param device[in] The device to read from
 * @param off[in] Offset into the config
 * @param out[out] Where the value is stored
 *
 * @retval 0 Successful
 * @retval -EINVAL Bad offset
 */
int pci_read_config_word(struct pci_device* device, u16 off, u16* out);

/**
 * @brief Read a dword from a PCI device config
 *
 * @param device[in] The device to read from
 * @param off[in] Offset into the config
 * @param out[out] Where the value is stored
 *
 * @retval 0 Successful
 * @retval -EINVAL Bad offset
 */
int pci_read_config_dword(struct pci_device* device, u16 off, u32* out);

/**
 * @brief Write a byte to a PCI device config
 *
 * @param device The device to write to
 * @param off Offset into the config
 * @param val The value to write
 *
 * @retval 0 Successful
 * @retval -EINVAL Bad offset
 */
int pci_write_config_byte(struct pci_device* device, u16 off, u8 val);

/**
 * @brief Write a word to a PCI device config
 *
 * @param device The device to write to
 * @param off Offset into the config
 * @param val The value to write
 *
 * @retval 0 Successful
 * @retval -EINVAL Bad offset
 */
int pci_write_config_word(struct pci_device* device, u16 off, u16 val);

/**
 * @brief Write a dword to a PCI device config
 *
 * @param device The device to write to
 * @param off Offset into the config
 * @param val The value to write
 *
 * @retval 0 Successful
 * @retval -EINVAL Bad offset
 */
int pci_write_config_dword(struct pci_device* device, u16 off, u32 val);

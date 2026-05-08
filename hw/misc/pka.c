#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define TYPE_PCI_PKA_DEVICE "pka"
typedef struct PkaState PkaState;
DECLARE_INSTANCE_CHECKER(PkaState, PKA, TYPE_PCI_PKA_DEVICE)

#define PCI_DEVICE_ID_PKA 0x1234
#define PCI_BASE_CLASS_PROCESSING_ACCEL 0x12

struct PkaState {
    PCIDevice pdev;
    MemoryRegion mmio;
    uint32_t id;
    uint32_t irq_status;
};

static void pci_pka_realize(PCIDevice *pdev, Error **erp)
{
    uint8_t *pci_conf = pdev->config;

    pci_config_set_interrupt_pin(pci_conf, 1);
}

static void pci_pka_uninit(PCIDevice *dev)
{
    return;
}

static void pka_instance_init(Object *obj)
{
    return;
}

static void pka_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_pka_realize;
    k->exit = pci_pka_uninit;
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = PCI_DEVICE_ID_PKA;
    k->revision = 0x00;
    k->class_id = PCI_BASE_CLASS_PROCESSING_ACCEL;
    dc->desc = "PCI Public Key Accelerator";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pka_types[] = {
    {
        .name          = TYPE_PCI_PKA_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PkaState),
        .instance_init = pka_instance_init,
        .class_init    = pka_class_init,
        .interfaces    = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    }
};

DEFINE_TYPES(pka_types)

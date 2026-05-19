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
#include "system/memory.h"
#include "qemu/bitops.h"
#include <string.h>
#include "pka.h"

#define TYPE_PCI_PKA_DEVICE "pka"
typedef struct PkaState PkaState;
DECLARE_INSTANCE_CHECKER(PkaState, PKA, TYPE_PCI_PKA_DEVICE)

#define PCI_DEVICE_ID_PKA 0x1234
#define PCI_BASE_CLASS_PROCESSING_ACCEL 0x12
#define PKA_DEVICE_ID 0xba101a10

struct PkaState {
    PCIDevice pdev;
    MemoryRegion mmio;
    MemoryRegion membar;
    const uint32_t *membar_ptr;
    uint32_t regs[REGS_NUM];
    uint32_t irq_status;
};

static bool is_start(PkaState *pka)
{
    return test_bit32(1, &pka->regs[CR]) == 1;
}

static uint32_t montgomery_param(PkaState *pka)
{
    uint32_t *ram = (uint32_t *)pka->membar_ptr;
    uint32_t mod_len = ram[PKA_MONTGOMERY_PARAM_IN_MOD_NB_BITS];
    uint32_t mod = ram[PKA_MONTGOMERY_PARAM_IN_MODULUS];
    uint64_t R = 1 << mod_len;

    /*
       some bignum arithmetic magic:
       R = 1 << mod_len
       result = R^2 % modulus

       bn_t R = 1
       bn_lshift_n_bits(R, mod_len)
       bn_t result = bn_mul(R, R)
       result = result % modulus
       ram[PKA_MONTGOMERY_PARAM_OUT_PARAMETER] = result;
    */

    printf("%" PRIu64 "^2 mod %d = idk\n", R, mod);

    return 0;
}

static uint64_t pka_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PkaState *pka = opaque;
    uint64_t val = ~0ULL;
    uint32_t *ram_addr;

    // Convert memory addr to device's register offset
    addr = (addr / 4) & 0xff;

    switch (addr) {
    case ID:
        val = PKA_DEVICE_ID;
        break;
    case CR:
        val = pka->regs[CR];
        break;
    case SR:
        val = pka->regs[SR];
        break;
    case RAM_ADDR_OFFSET:
        val = pka->regs[RAM_ADDR_OFFSET];
        break;
    case RAM_DATA:
        ram_addr = (uint32_t *)pka->membar_ptr + (pka->regs[RAM_ADDR_OFFSET])/4;
        val = *ram_addr;
        printf("Reading RAM at %p: %" PRIx64 "\n", ram_addr, val);
        break;
    }

    return val;
}

static void pka_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PkaState *pka = opaque;

    // Convert memory addr to device's register offset
    addr = (addr / 4) & 0xff;

    switch(addr) {
    case CR:
        pka->regs[CR] = val;
        if(is_start(pka)) {
            clear_bit32(1, &pka->regs[CR]);
            montgomery_param(pka);
            set_bit32(17, &pka->regs[SR]);
        }
        break;
    case CLRFR:
        pka->regs[CLRFR] = val;
        break;
    case RAM_ADDR_OFFSET:
        pka->regs[RAM_ADDR_OFFSET] = val;
        break;
    }
}

static const MemoryRegionOps pka_mmio_ops = {
    .read = pka_mmio_read,
    .write = pka_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pci_pka_realize(PCIDevice *pdev, Error **erp)
{
    PkaState *pka = PKA(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&pka->mmio, OBJECT(pka), &pka_mmio_ops, pka,
            "pka-mmio", 1 * MiB);
    memory_region_init_ram(&pka->membar, OBJECT(pka),
            "pka-membar", 1 * MiB, NULL);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &pka->mmio);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &pka->membar);
    pka->membar_ptr = (uint32_t *) memory_region_get_ram_ptr(&pka->membar);
}

static void pci_pka_uninit(PCIDevice *pdev)
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

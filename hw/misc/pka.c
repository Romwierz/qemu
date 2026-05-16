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
    uint32_t regs[PKA_REGS_NUM];
    uint32_t irq_status;
};

static bool is_start(PkaState *pka)
{
    return test_bit32(1, &pka->regs[PKA_CR_ADDR]) == 1;
}

static uint32_t montgomery_param(PkaState *pka)
{
    uint32_t *ram_base_addr = (uint32_t *)pka->membar_ptr;
    uint32_t mod_len = *(ram_base_addr + 0x4/4);
    uint32_t mod = *(ram_base_addr + 0x95c/4);
    if (mod == 0) return 0;
    uint64_t R = 1 << mod_len;
    uint32_t param = R*R % mod;
    *(ram_base_addr + 0x194/4) = param;
    printf("%" PRIu64 "^2 mod %d = %d\n", R, mod, param);
    return param;
}

static uint64_t pka_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PkaState *pka = opaque;
    uint64_t val = ~0ULL;
    uint32_t *ram_addr;

    // Convert memory addr to device's register offset
    addr = (addr / 4) & 0xff;

    switch (addr) {
    case PKA_ID_ADDR:
        val = PKA_DEVICE_ID;
        break;
    case PKA_ADDR4:
        val = pka->regs[PKA_ADDR4];
        break;
    case PKA_CR_ADDR:
        val = pka->regs[PKA_CR_ADDR];
        break;
    case PKA_SR_ADDR:
        val = pka->regs[PKA_SR_ADDR];
        break;
    case PKA_CLRFR_ADDR:
        val = pka->regs[PKA_CLRFR_ADDR];
        break;
    case PKA_RAM_ADDR_OFFSET:
        val = pka->regs[PKA_RAM_ADDR_OFFSET];
        break;
    case PKA_RAM_DATA:
        ram_addr = (uint32_t *)pka->membar_ptr + (pka->regs[PKA_RAM_ADDR_OFFSET])/4;
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
    case PKA_ADDR4:
        pka->regs[PKA_ADDR4] = val;
        break;
    case PKA_CR_ADDR:
        pka->regs[PKA_CR_ADDR] = val;
        if(is_start(pka)) {
            montgomery_param(pka);
            clear_bit32(1, &pka->regs[PKA_CR_ADDR]);
        }
        break;
    case PKA_SR_ADDR:
        pka->regs[PKA_SR_ADDR] = val;
        break;
    case PKA_CLRFR_ADDR:
        pka->regs[PKA_CLRFR_ADDR] = val;
        break;
    case PKA_RAM_ADDR_OFFSET:
        pka->regs[PKA_RAM_ADDR_OFFSET] = val;
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

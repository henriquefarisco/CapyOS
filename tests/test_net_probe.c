#include <stdio.h>
#include <string.h>

#include "drivers/hyperv/hyperv.h"
#include "drivers/net/net_probe.h"
#include "drivers/net/netvsc.h"
#include "drivers/pcie.h"

struct fake_pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t header_type;
    uint16_t command;
    uint64_t bar0;
    uint8_t bar0_is_io;
};

static struct fake_pci_device g_fake_devices[8];
static size_t g_fake_device_count = 0;
static int g_fake_hyperv = 0;
static int g_fake_hyperv_offer = 0;

static void fake_pci_reset(void) {
    memset(g_fake_devices, 0, sizeof(g_fake_devices));
    g_fake_device_count = 0;
    g_fake_hyperv = 0;
    g_fake_hyperv_offer = 0;
}

static void fake_pci_add(uint8_t bus, uint8_t device, uint8_t function,
                         uint16_t vendor_id, uint16_t device_id,
                         uint64_t bar0, uint8_t bar0_is_io) {
    struct fake_pci_device *dev = NULL;
    if (g_fake_device_count >= (sizeof(g_fake_devices) / sizeof(g_fake_devices[0]))) {
        return;
    }
    dev = &g_fake_devices[g_fake_device_count++];
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->vendor_id = vendor_id;
    dev->device_id = device_id;
    dev->class_code = 0x02u;
    dev->header_type = 0x00u;
    dev->command = 0x0000u;
    dev->bar0 = bar0;
    dev->bar0_is_io = bar0_is_io;
}

static struct fake_pci_device *fake_pci_find(uint8_t bus, uint8_t device,
                                             uint8_t function) {
    size_t i = 0;
    for (i = 0; i < g_fake_device_count; ++i) {
        if (g_fake_devices[i].bus == bus && g_fake_devices[i].device == device &&
            g_fake_devices[i].function == function) {
            return &g_fake_devices[i];
        }
    }
    return NULL;
}

void pci_init(void) {}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    struct fake_pci_device *entry = fake_pci_find(bus, dev, func);
    if (!entry) {
        return 0xFFFFFFFFu;
    }
    if (offset == PCI_CLASS_REVISION) {
        return ((uint32_t)entry->class_code << 24);
    }
    if (offset == PCI_BAR0) {
        return entry->bar0_is_io ? ((uint32_t)entry->bar0 | 0x1u)
                                 : (uint32_t)(entry->bar0 & 0xFFFFFFF0u);
    }
    return 0u;
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    struct fake_pci_device *entry = fake_pci_find(bus, dev, func);
    if (!entry) {
        return 0xFFFFu;
    }
    if (offset == PCI_VENDOR_ID) {
        return entry->vendor_id;
    }
    if (offset == PCI_DEVICE_ID) {
        return entry->device_id;
    }
    if (offset == PCI_COMMAND) {
        return entry->command;
    }
    return 0u;
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    struct fake_pci_device *entry = fake_pci_find(bus, dev, func);
    if (!entry) {
        return 0xFFu;
    }
    if (offset == PCI_HEADER_TYPE) {
        return entry->header_type;
    }
    return 0u;
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
                        uint32_t value) {
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    (void)value;
}

void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
                        uint16_t value) {
    struct fake_pci_device *entry = fake_pci_find(bus, dev, func);
    if (!entry) {
        return;
    }
    if (offset == PCI_COMMAND) {
        entry->command = value;
    }
}

int pci_find_device(uint8_t class_code, uint8_t subclass, struct pci_device *out) {
    (void)class_code;
    (void)subclass;
    (void)out;
    return -1;
}

int pci_find_nvme(struct pci_device *out) {
    (void)out;
    return -1;
}

uint64_t pci_read_bar64(uint8_t bus, uint8_t dev, uint8_t func, int bar_index) {
    struct fake_pci_device *entry = fake_pci_find(bus, dev, func);
    (void)bar_index;
    if (!entry) {
        return 0;
    }
    return entry->bar0;
}

int hyperv_detect(void) { return g_fake_hyperv; }

int vmbus_query_offer(const struct hv_guid *guid, struct vmbus_offer_info *out) {
    (void)guid;
    if (!g_fake_hyperv || !g_fake_hyperv_offer || !out) {
        return -1;
    }
    out->child_relid = 77u;
    out->connection_id = 4096u;
    out->is_dedicated_interrupt = 1u;
    return 0;
}

void vmbus_channel_runtime_reset(struct vmbus_channel_runtime *channel) {
    if (!channel) {
        return;
    }
    memset(channel, 0, sizeof(*channel));
}

int vmbus_channel_runtime_open(struct vmbus_channel_runtime *channel) {
    if (!channel || !g_fake_hyperv_offer) {
        return -1;
    }
    channel->initialized = 1u;
    channel->opened = 1u;
    return 0;
}

int run_net_probe_tests(void) {
    int fails = 0;
    struct net_nic_probe probe;
    struct fake_pci_device *selected = NULL;

    if (!net_probe_kind_runtime_supported(NET_NIC_KIND_E1000) ||
        !net_probe_kind_runtime_supported(NET_NIC_KIND_TULIP) ||
        !net_probe_kind_runtime_supported(NET_NIC_KIND_RTL8139) ||
        !net_probe_kind_runtime_supported(NET_NIC_KIND_VIRTIO_NET) ||
        !net_probe_kind_runtime_supported(NET_NIC_KIND_VMXNET3) ||
        !net_probe_kind_runtime_supported(NET_NIC_KIND_HYPERV_NETVSC)) {
        printf("[net] runtime capability map unexpected\n");
        fails++;
    }

    fake_pci_reset();
    fake_pci_add(0, 1, 0, 0x1AF4u, 0x1000u, 0x1000u, 0u);
    fake_pci_add(0, 2, 0, 0x8086u, 0x100Eu, 0x2000u, 0u);
    memset(&probe, 0, sizeof(probe));
    if (net_probe_first_supported(&probe) != 0 || !probe.found ||
        probe.kind != NET_NIC_KIND_VIRTIO_NET || !probe.runtime_supported) {
        printf("[net] failed to select the first runtime-capable PCI backend\n");
        fails++;
    }
    selected = fake_pci_find(probe.bus, probe.device, probe.function);
    if (!selected || (selected->command &
         (PCI_CMD_IO_SPACE | PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER)) == 0u) {
        printf("[net] selected runtime backend did not enable PCI command bits\n");
        fails++;
    }

    fake_pci_reset();
    fake_pci_add(0, 3, 0, 0x1AF4u, 0x1000u, 0x3000u, 0u);
    memset(&probe, 0, sizeof(probe));
    if (net_probe_first_supported(&probe) != 0 || !probe.found ||
        probe.kind != NET_NIC_KIND_VIRTIO_NET || !probe.runtime_supported) {
        printf("[net] failed to surface VirtIO-Net as runtime-capable backend\n");
        fails++;
    }

    fake_pci_reset();
    fake_pci_add(0, 4, 0, 0x15ADu, 0x07B0u, 0x4000u, 0u);
    memset(&probe, 0, sizeof(probe));
    if (net_probe_first_supported(&probe) != 0 || !probe.found ||
        probe.kind != NET_NIC_KIND_VMXNET3 || !probe.runtime_supported) {
        printf("[net] failed to surface VMXNET3 as runtime-capable backend\n");
        fails++;
    }

    fake_pci_reset();
    memset(&probe, 0, sizeof(probe));
    if (net_probe_first_supported(&probe) == 0 || probe.found) {
        printf("[net] probe should fail cleanly when no NIC exists\n");
        fails++;
    }

    fake_pci_reset();
    g_fake_hyperv = 1;
    g_fake_hyperv_offer = 1;
    memset(&probe, 0, sizeof(probe));
    if (net_probe_first_supported(&probe) != 0 || !probe.found ||
        probe.kind != NET_NIC_KIND_HYPERV_NETVSC || !probe.runtime_supported) {
        printf("[net] failed to surface Hyper-V synthetic NIC as runtime-capable backend\n");
        fails++;
    }
    if (probe.vmbus_relid != 0u || probe.vmbus_connection_id != 0u ||
        probe.vmbus_dedicated_interrupt != 0u) {
        printf("[net] early boot probe should not negotiate VMBus offers\n");
        fails++;
    }

    {
        struct vmbus_offer_info offer;
        memset(&offer, 0, sizeof(offer));
        if (netvsc_query_offer(&offer) != 0 || offer.child_relid != 77u ||
            offer.connection_id != 4096u || offer.is_dedicated_interrupt != 1u) {
            printf("[net] netvsc offer query did not surface Hyper-V channel metadata\n");
            fails++;
        }
    }

    {
        struct netvsc_runtime_status status;
        memset(&status, 0, sizeof(status));
        if (netvsc_refresh_runtime(&status) <= 0 || !status.offer_ready ||
            status.channel_ready || status.offer.child_relid != 77u) {
            printf("[net] netvsc runtime refresh did not surface Hyper-V offer state\n");
            fails++;
        }
    }

    fake_pci_reset();
    memset(&probe, 0, sizeof(probe));
    {
        struct vmbus_offer_info offer;
        memset(&offer, 0, sizeof(offer));
        if (netvsc_query_offer(&offer) == 0) {
            printf("[net] netvsc offer query should fail outside Hyper-V\n");
            fails++;
        }
    }

    if (fails == 0) {
        printf("[tests] net_probe OK\n");
    }
    return fails;
}

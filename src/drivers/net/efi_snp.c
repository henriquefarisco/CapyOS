#include "drivers/net/efi_snp.h"

#include <stddef.h>
#include <stdint.h>

#include "core/system_init.h"
#include "drivers/acpi/acpi.h"
#include "drivers/efi/efi_console.h"
#include "kernel/log/klog.h"

#define EFI_SNP_STATE_STOPPED 0u
#define EFI_SNP_STATE_STARTED 1u
#define EFI_SNP_STATE_INITIALIZED 2u

#define EFI_SNP_RECEIVE_UNICAST 0x01u
#define EFI_SNP_RECEIVE_MULTICAST 0x02u
#define EFI_SNP_RECEIVE_BROADCAST 0x04u

typedef struct {
  uint8_t Addr[32];
} EFI_MAC_ADDRESS_K;

typedef struct {
  uint32_t State;
  uint32_t HwAddressSize;
  uint32_t MediaHeaderSize;
  uint32_t MaxPacketSize;
  uint32_t NvRamSize;
  uint32_t NvRamAccessSize;
  uint32_t ReceiveFilterMask;
  uint32_t ReceiveFilterSetting;
  uint32_t MaxMCastFilterCount;
  uint32_t MCastFilterCount;
  EFI_MAC_ADDRESS_K MCastFilter[16];
  EFI_MAC_ADDRESS_K CurrentAddress;
  EFI_MAC_ADDRESS_K BroadcastAddress;
  EFI_MAC_ADDRESS_K PermanentAddress;
  uint8_t IfType;
  uint8_t MacAddressChangeable;
  uint8_t MultipleTxSupported;
  uint8_t MediaPresentSupported;
  uint8_t MediaPresent;
} EFI_SIMPLE_NETWORK_MODE_K;

struct efi_simple_network_protocol_k;

typedef EFI_STATUS_K(EFIAPI *efi_snp_start_fn)(
    struct efi_simple_network_protocol_k *This);
typedef EFI_STATUS_K(EFIAPI *efi_snp_initialize_fn)(
    struct efi_simple_network_protocol_k *This, uint64_t ExtraRxBufferSize,
    uint64_t ExtraTxBufferSize);
typedef EFI_STATUS_K(EFIAPI *efi_snp_receive_filters_fn)(
    struct efi_simple_network_protocol_k *This, uint32_t Enable,
    uint32_t Disable, uint8_t ResetMCastFilter, uint64_t MCastFilterCnt,
    EFI_MAC_ADDRESS_K *MCastFilter);
typedef EFI_STATUS_K(EFIAPI *efi_snp_get_status_fn)(
    struct efi_simple_network_protocol_k *This, uint32_t *InterruptStatus,
    void **TxBuf);
typedef EFI_STATUS_K(EFIAPI *efi_snp_transmit_fn)(
    struct efi_simple_network_protocol_k *This, uint64_t HeaderSize,
    uint64_t BufferSize, void *Buffer, EFI_MAC_ADDRESS_K *SrcAddr,
    EFI_MAC_ADDRESS_K *DestAddr, uint16_t *Protocol);
typedef EFI_STATUS_K(EFIAPI *efi_snp_receive_fn)(
    struct efi_simple_network_protocol_k *This, uint64_t *HeaderSize,
    uint64_t *BufferSize, void *Buffer, EFI_MAC_ADDRESS_K *SrcAddr,
    EFI_MAC_ADDRESS_K *DestAddr, uint16_t *Protocol);

typedef struct efi_simple_network_protocol_k {
  uint64_t Revision;
  efi_snp_start_fn Start;
  void *Stop;
  efi_snp_initialize_fn Initialize;
  void *Reset;
  void *Shutdown;
  efi_snp_receive_filters_fn ReceiveFilters;
  void *StationAddress;
  void *Statistics;
  void *MCastIpToMac;
  void *NvData;
  efi_snp_get_status_fn GetStatus;
  efi_snp_transmit_fn Transmit;
  efi_snp_receive_fn Receive;
  void *WaitForPacket;
  EFI_SIMPLE_NETWORK_MODE_K *Mode;
} EFI_SIMPLE_NETWORK_PROTOCOL_K;

static EFI_SIMPLE_NETWORK_PROTOCOL_K *g_snp;
static int g_snp_ready;

static const EFI_GUID_K k_efi_simple_network_guid = {
    0xA19832B9u,
    0xAC25u,
    0x11D3u,
    {0x9Au, 0x2Du, 0x00u, 0x90u, 0x27u, 0x3Fu, 0xC1u, 0x4Du}};

static int efi_snp_boot_services_active(void) {
  struct system_runtime_platform platform;
  system_runtime_platform_get(&platform);
  return platform.boot_services_active ? 1 : 0;
}

static void efi_snp_copy_mac(uint8_t mac[6],
                             const EFI_SIMPLE_NETWORK_MODE_K *mode) {
  if (!mac || !mode) {
    return;
  }
  for (uint32_t i = 0; i < 6u; ++i) {
    mac[i] = mode->CurrentAddress.Addr[i];
  }
}

static int efi_snp_locate(EFI_SIMPLE_NETWORK_PROTOCOL_K **out) {
  EFI_SYSTEM_TABLE_K *st = NULL;
  EFI_BOOT_SERVICES_K *bs = NULL;
  void *iface = NULL;
  EFI_STATUS_K status = 0;

  if (out) {
    *out = NULL;
  }
  if (!out || !efi_snp_boot_services_active() || !g_efi_system_table) {
    return -1;
  }

  st = (EFI_SYSTEM_TABLE_K *)(uintptr_t)g_efi_system_table;
  bs = st ? st->BootServices : NULL;
  if (!bs || !bs->LocateProtocol) {
    return -2;
  }

  status = bs->LocateProtocol((EFI_GUID_K *)&k_efi_simple_network_guid, NULL,
                              &iface);
  if ((status & EFI_ERROR_BIT_K) || !iface) {
    return -3;
  }

  *out = (EFI_SIMPLE_NETWORK_PROTOCOL_K *)iface;
  return 0;
}

int efi_snp_probe(uint8_t mac[6], uint16_t *mtu) {
  EFI_SIMPLE_NETWORK_PROTOCOL_K *snp = NULL;

  if (efi_snp_locate(&snp) != 0 || !snp || !snp->Mode) {
    return -1;
  }

  g_snp = snp;
  if (mtu) {
    uint32_t max_packet = snp->Mode->MaxPacketSize;
    *mtu = (uint16_t)(max_packet >= 1514u ? 1500u : max_packet);
  }
  efi_snp_copy_mac(mac, snp->Mode);
  return 0;
}

int efi_snp_init(uint8_t mac[6], uint16_t *mtu) {
  EFI_SIMPLE_NETWORK_PROTOCOL_K *snp = g_snp;

  if (!snp && efi_snp_locate(&snp) != 0) {
    return -1;
  }
  if (!snp || !snp->Mode || !snp->Start || !snp->Initialize ||
      !snp->Transmit || !snp->Receive) {
    return -2;
  }

  if (snp->Mode->State == EFI_SNP_STATE_STOPPED) {
    EFI_STATUS_K status = snp->Start(snp);
    if (status & EFI_ERROR_BIT_K) {
      klog_hex(KLOG_WARN, "[efi-snp] Start failed status=", status);
      return -3;
    }
  }

  if (snp->Mode->State == EFI_SNP_STATE_STARTED) {
    EFI_STATUS_K status = snp->Initialize(snp, 0, 0);
    if (status & EFI_ERROR_BIT_K) {
      klog_hex(KLOG_WARN, "[efi-snp] Initialize failed status=", status);
      return -4;
    }
  }

  if (snp->ReceiveFilters) {
    (void)snp->ReceiveFilters(
        snp,
        EFI_SNP_RECEIVE_UNICAST | EFI_SNP_RECEIVE_MULTICAST |
            EFI_SNP_RECEIVE_BROADCAST,
        0, 0, 0, NULL);
  }

  g_snp = snp;
  g_snp_ready = 1;
  if (mtu) {
    uint32_t max_packet = snp->Mode->MaxPacketSize;
    *mtu = (uint16_t)(max_packet >= 1514u ? 1500u : max_packet);
  }
  efi_snp_copy_mac(mac, snp->Mode);
  klog(KLOG_INFO, "[efi-snp] UEFI Simple Network Protocol ready.");
  return 0;
}

int efi_snp_ready(void) { return g_snp_ready && g_snp && g_snp->Mode; }

int efi_snp_send_frame(const uint8_t *frame, uint16_t len) {
  EFI_STATUS_K status = 0;
  void *tx_buf = NULL;

  if (!efi_snp_ready() || !frame || len == 0u || !g_snp->Transmit) {
    return -1;
  }
  if (g_snp->GetStatus) {
    (void)g_snp->GetStatus(g_snp, NULL, &tx_buf);
  }
  status = g_snp->Transmit(g_snp, 0, len, (void *)(uintptr_t)frame, NULL, NULL,
                           NULL);
  if (status & EFI_ERROR_BIT_K) {
    return -2;
  }
  return 0;
}

int efi_snp_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len) {
  uint64_t header_size = 0;
  uint64_t buffer_size = cap;
  uint16_t protocol = 0;
  EFI_STATUS_K status = 0;

  if (len) {
    *len = 0u;
  }
  if (!efi_snp_ready() || !out || !len || cap == 0u || !g_snp->Receive) {
    return -1;
  }

  status = g_snp->Receive(g_snp, &header_size, &buffer_size, out, NULL, NULL,
                          &protocol);
  if (status & EFI_ERROR_BIT_K) {
    return 0;
  }
  if (buffer_size > cap) {
    return -2;
  }
  *len = (uint16_t)buffer_size;
  return buffer_size ? 1 : 0;
}

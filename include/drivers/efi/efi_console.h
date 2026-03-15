/* Minimal EFI definitions for Hybrid Boot Input */
#ifndef EFI_CONSOLE_H
#define EFI_CONSOLE_H

#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

typedef uint64_t EFI_STATUS_K;
typedef void *EFI_HANDLE_K;

typedef struct {
  uint16_t ScanCode;
  uint16_t UnicodeChar;
} EFI_INPUT_KEY;

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
  EFI_STATUS_K(EFIAPI *Reset)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
                              uint8_t ExtendedVerification);
  EFI_STATUS_K(EFIAPI *ReadKeyStroke)(
      struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);
  void *WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS_K(EFIAPI *efi_get_memory_map_fn)(
    uint64_t *MemoryMapSize, void *MemoryMap, uint64_t *MapKey,
    uint64_t *DescriptorSize, uint32_t *DescriptorVersion);
typedef EFI_STATUS_K(EFIAPI *efi_exit_boot_services_fn)(EFI_HANDLE_K ImageHandle,
                                                        uint64_t MapKey);

typedef struct {
  uint8_t Hdr[24];
  void *RaiseTPL;
  void *RestoreTPL;
  void *AllocatePages;
  void *FreePages;
  efi_get_memory_map_fn GetMemoryMap;
  void *AllocatePool;
  void *FreePool;
  void *CreateEvent;
  void *SetTimer;
  void *WaitForEvent;
  void *SignalEvent;
  void *CloseEvent;
  void *CheckEvent;
  void *InstallProtocolInterface;
  void *ReinstallProtocolInterface;
  void *UninstallProtocolInterface;
  void *HandleProtocol;
  void *Reserved;
  void *RegisterProtocolNotify;
  void *LocateHandle;
  void *LocateDevicePath;
  void *InstallConfigurationTable;
  void *LoadImage;
  void *StartImage;
  void *Exit;
  void *UnloadImage;
  efi_exit_boot_services_fn ExitBootServices;
  void *GetNextMonotonicCount;
  void *Stall;
} EFI_BOOT_SERVICES_K;

/* EFI_SYSTEM_TABLE layout per UEFI 2.x spec:
 * Offset 0:  EFI_TABLE_HEADER Hdr (24 bytes)
 * Offset 24: CHAR16 *FirmwareVendor (8 bytes)
 * Offset 32: UINT32 FirmwareRevision (4 bytes)
 * Offset 36: (4 bytes padding for alignment)
 * Offset 40: EFI_HANDLE ConsoleInHandle (8 bytes)
 * Offset 48: EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn (8 bytes)
 */
typedef struct {
  uint8_t Hdr[24];
  uint64_t FirmwareVendor;
  uint32_t FirmwareRevision;
  uint32_t _pad1;
  uint64_t ConsoleInHandle;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
  uint64_t ConsoleOutHandle;
  uint64_t ConOut;
  uint64_t StandardErrorHandle;
  uint64_t StdErr;
  uint64_t RuntimeServices;
  EFI_BOOT_SERVICES_K *BootServices;
} EFI_SYSTEM_TABLE_K;

/* EFI_NOT_READY = EFIERR(6) = 0x8000000000000006 on 64-bit */
#define EFI_ERROR_BIT_K (1ULL << 63)
#define EFI_SUCCESS_K 0ULL
#define EFI_INVALID_PARAMETER_K (EFI_ERROR_BIT_K | 2ULL)
#define EFI_BUFFER_TOO_SMALL_K (EFI_ERROR_BIT_K | 5ULL)

/* Kernel wrapper for EFI input - returns 1 if key available, 0 otherwise */
static inline int efi_poll_char(uint64_t system_table_addr, char *out_char) {
  if (!system_table_addr || !out_char)
    return 0;

  EFI_SYSTEM_TABLE_K *st = (EFI_SYSTEM_TABLE_K *)(uintptr_t)system_table_addr;
  if (!st->ConIn || !st->ConIn->ReadKeyStroke)
    return 0;

  EFI_INPUT_KEY key;
  key.ScanCode = 0;
  key.UnicodeChar = 0;

  EFI_STATUS_K status = st->ConIn->ReadKeyStroke(st->ConIn, &key);

  /* Check if call failed or no key ready (high bit set = error) */
  if (status & EFI_ERROR_BIT_K) {
    return 0; /* No key available or error */
  }

  /* We have a key! Check UnicodeChar first */
  uint16_t uc = key.UnicodeChar;

  /* Printable ASCII */
  if (uc >= 0x20 && uc <= 0x7E) {
    *out_char = (char)uc;
    return 1;
  }

  /* Carriage Return -> Newline */
  if (uc == 0x0D) {
    *out_char = '\n';
    return 1;
  }

  /* Backspace */
  if (uc == 0x08) {
    *out_char = '\b';
    return 1;
  }

  /* Tab */
  if (uc == 0x09) {
    *out_char = '\t';
    return 1;
  }

  /* Escape or other non-printable - ignore */
  return 0;
}

#endif

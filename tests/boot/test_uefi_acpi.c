#include "boot/uefi_loader/internal/uefi_loader_internal.h"
#include <stdio.h>
#include <string.h>

static uint8_t copied[4096];
static int allocation_ok;
static EFI_STATUS __attribute__((ms_abi)) allocate_page(
    EFI_ALLOCATE_TYPE how, EFI_MEMORY_TYPE type, UINTN pages,
    EFI_PHYSICAL_ADDRESS *address) {
  allocation_ok = how == AllocateMaxAddress && type == EfiLoaderData &&
                  pages == 1 && *address == 0x3fffffff;
  *address = (EFI_PHYSICAL_ADDRESS)(UINTN)copied;
  return EFI_SUCCESS;
}
static void sum(uint8_t *p, size_t n, size_t offset) {
  p[offset] = 0; uint8_t v = 0;
  for (size_t i = 0; i < n; ++i) v = (uint8_t)(v + p[i]);
  p[offset] = (uint8_t)(0 - v);
}
static void root(uint8_t *p, size_t n) {
  memset(p, 0, n);
  memcpy(p, "RSD PTR ", 8);
  if (n >= 36) {
    p[15] = 2;
    ((acpi_rsdp_t *)(void *)p)->length = (UINT32)n;
  }
  sum(p, 20, 8);
  if (n >= 36) sum(p, n, 32);
}
#define CHECK(x) do { ++checks; if (!(x)) { \
  printf("[uefi-acpi] FAIL line %d: %s\n", __LINE__, #x); ++failures; \
} } while (0)
int main(void) {
  int failures = 0, checks = 0;
  uint8_t v1[20], v2[4096];
  root(v1, sizeof(v1)); root(v2, sizeof(v2));
  EFI_CONFIGURATION_TABLE tables[3] = {0};
  EFI_GUID id1 = ACPI_TABLE_GUID, id2 = ACPI_20_TABLE_GUID;
  tables[0].VendorTable = (void *)(UINTN)1; /* unrelated GUID: never dereference */
  tables[1].VendorGuid = id1; tables[1].VendorTable = v1;
  tables[2].VendorGuid = id2; tables[2].VendorTable = v2;
  EFI_SYSTEM_TABLE st = {0}; st.ConfigurationTable = tables;
  st.NumberOfTableEntries = 3;
  UINT64 address = 99;
  CHECK(find_rsdp(&st, &address) == EFI_SUCCESS && address == (UINT64)(UINTN)v2);
  st.NumberOfTableEntries = 2;
  CHECK(find_rsdp(&st, &address) == EFI_SUCCESS && address == (UINT64)(UINTN)v1);
  st.NumberOfTableEntries = 1;
  CHECK(find_rsdp(&st, &address) == EFI_NOT_FOUND && !address);
  st.NumberOfTableEntries = 3;
  v2[32] ^= 1;
  CHECK(find_rsdp(&st, &address) == EFI_COMPROMISED_DATA && !address);
  root(v2, sizeof(v2)); ((acpi_rsdp_t *)(void *)v2)->length = 4097;
  CHECK(!rsdp_is_valid_ptr(v2));
  ((acpi_rsdp_t *)(void *)v2)->length = 35;
  CHECK(!rsdp_is_valid_ptr(v2));
  CHECK(!rsdp_is_valid_ptr(NULL));
  CHECK(find_rsdp(NULL, &address) == EFI_INVALID_PARAMETER && !address);
  CHECK(find_rsdp(&st, NULL) == EFI_INVALID_PARAMETER);
  EFI_BOOT_SERVICES bs = {0};
  bs.AllocatePages = (EFI_ALLOCATE_PAGES)allocate_page;
  st.BootServices = &bs;
  root(v2, sizeof(v2)); v2[4095] = 0x5a; sum(v2, sizeof(v2), 32);
  CHECK(copy_rsdp_low(&st, (UINT64)(UINTN)v2, &address) == EFI_SUCCESS);
  CHECK(allocation_ok && address == (UINT64)(UINTN)copied);
  CHECK(!memcmp(v2, copied, sizeof(v2)) && rsdp_is_valid_ptr(copied));
  memset(copied, 0xa5, sizeof(copied));
  CHECK(copy_rsdp_low(&st, (UINT64)(UINTN)v1, &address) == EFI_SUCCESS);
  CHECK(!memcmp(v1, copied, 20) && copied[20] == 0 && copied[4095] == 0);
  v1[8] ^= 1;
  CHECK(copy_rsdp_low(&st, (UINT64)(UINTN)v1, &address) == EFI_COMPROMISED_DATA && !address);
  printf("[uefi-acpi] %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}

/*
 * UEFI loader is split into implementation fragments while preserving a single
 * translation unit for boot-time static helpers and firmware-facing state.
 */

#include "uefi_loader/prelude_boot_files.inc"
#include "uefi_loader/kernel_loader.inc"
#include "uefi_loader/kernel_discovery_streaming.inc"
#include "uefi_loader/installer_disk_selection.inc"
#include "uefi_loader/recovery_gpt_layout.inc"
#include "uefi_loader/fat32_writer.inc"
#include "uefi_loader/installer_run.inc"
#include "uefi_loader/acpi_log_gop.inc"
#include "uefi_loader/efi_main.inc"

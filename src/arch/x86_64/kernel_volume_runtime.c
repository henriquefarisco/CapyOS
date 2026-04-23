/*
 * x86_64 volume runtime is grouped as implementation fragments while private
 * key, mount and filesystem helpers remain in one translation unit.
 */

#include "kernel_volume_runtime/io_key_helpers.inc"
#include "kernel_volume_runtime/key_storage_probe.inc"
#include "kernel_volume_runtime/mount_initialize.inc"
#include "kernel_volume_runtime/filesystem_helpers.inc"
#include "kernel_volume_runtime/public_mount_api.inc"

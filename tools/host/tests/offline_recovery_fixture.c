/* Fixture generator, never linked into the operator entry point. Uses the
 * production encoder/control initializer instead of a second AB serializer. */
#define main recovery_backend_main
#include "../src/offline_recovery_slot.c"
#undef main

int main(int argc, char **argv) {
  struct disk_io disk = {.fd = -1, .writable = 1};
  struct stat info;
  struct capyos_gpt_identity identity;
  struct boot_slot_layout layout;
  struct boot_slot_store store;
  struct boot_slot_image image = {.version = "0.10.0+20260924",
                                 .payload_size = 512};
  uint8_t payload[512] = "\177ELFfixture", header[512];
  if (argc != 2) return 1;
  disk.fd = open(argv[1], O_RDWR | O_NOFOLLOW);
  if (disk.fd < 0 || fstat(disk.fd, &info)) return 1;
  disk.sectors = (uint32_t)(info.st_size / 512);
  if (capyos_gpt_identity_read(raw_read, &disk, 512, disk.sectors, &identity)) return 1;
  disk.boot_lba = identity.boot.lba;
  disk.boot_sectors = identity.boot.sectors;
  sha256_hash(payload, sizeof(payload), image.payload_sha256);
  if (boot_slot_layout_plan(disk.boot_sectors, &layout) || boot_slot_init() ||
      boot_slot_store_init(&store, &layout, slot_read, slot_write, slot_flush, &disk) ||
      boot_slot_store_bind_control(&store, 0) != BOOT_SLOT_PERSIST_EMPTY ||
      slot_write(&disk, layout.slots[0].payload_lba, payload) ||
      boot_slot_store_encode_header(&layout, 0, &image, header) ||
      slot_write(&disk, layout.slots[0].header_lba, header) ||
      slot_flush(&disk) || boot_slot_store_initialize_persistent(&store, 0, &image))
    return 1;
  close(disk.fd);
  return 0;
}

/* Private host backend. The public Python entry point authenticates the bundle
 * and supplies a disposable copy, never the operator's original disk. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "boot/boot_slot.h"
#include "boot/boot_slot_store.h"
#include "boot/gpt_identity.h"
#include "security/sha256.h"

struct disk_io {
  int fd;
  uint32_t sectors;
  uint32_t boot_lba;
  uint32_t boot_sectors;
  int writable;
};

static int raw_read(void *ctx, uint32_t lba, uint8_t sector[512]) {
  struct disk_io *disk = ctx;
  return lba < disk->sectors &&
                 pread(disk->fd, sector, 512, (off_t)lba * 512) == 512
             ? 0 : -1;
}

static int slot_read(void *ctx, uint32_t lba, uint8_t sector[512]) {
  struct disk_io *disk = ctx;
  return lba < disk->boot_sectors
             ? raw_read(ctx, disk->boot_lba + lba, sector) : -1;
}

static int slot_write(void *ctx, uint32_t lba, const uint8_t sector[512]) {
  struct disk_io *disk = ctx;
  return disk->writable && lba < disk->boot_sectors &&
                 pwrite(disk->fd, sector, 512,
                        (off_t)(disk->boot_lba + lba) * 512) == 512
             ? 0 : -1;
}

static int slot_flush(void *ctx) {
  struct disk_io *disk = ctx;
  return disk->writable ? fsync(disk->fd) : -1;
}

static int confirmed_payload(struct disk_io *disk,
                             const struct boot_slot *slot,
                             const struct boot_slot_image *header) {
  uint8_t sector[512], digest[32];
  struct sha256_ctx hash;
  if (strcmp(slot->version, header->version) ||
      slot->payload_size != header->payload_size ||
      memcmp(slot->payload_sha256, header->payload_sha256, 32))
    return -1;
  sha256_init(&hash);
  for (uint32_t consumed = 0; consumed < header->payload_size;) {
    uint32_t count = header->payload_size - consumed;
    if (count > 512) count = 512;
    if (slot_read(disk, slot->payload_lba + consumed / 512, sector))
      return -1;
    sha256_update(&hash, sector, count);
    consumed += count;
  }
  sha256_final(&hash, digest);
  return memcmp(digest, header->payload_sha256, 32) ? -1 : 0;
}

static void describe(const struct capyos_gpt_identity *identity,
                     const struct boot_slot_snapshot *snapshot,
                     const struct boot_slot_layout *layout) {
  uint32_t confirmed = snapshot->manager.confirmed_slot;
  uint32_t inactive = confirmed ^ 1u;
  char guid[33];
  char candidate_sha[65];
  sha256_hex(snapshot->manager.slots[inactive].payload_sha256, candidate_sha);
  for (size_t i = 0; i < 16; ++i)
    snprintf(guid + 2 * i, 3, "%02x", identity->disk_guid[i]);
  printf("{\"disk_guid\":\"%s\",\"boot_lba\":%u,\"boot_sectors\":%u,"
         "\"data_lba\":%u,\"data_sectors\":%u,\"confirmed_slot\":%u,"
         "\"inactive_start\":%u,\"inactive_end\":%u,\"control_start\":%u,"
         "\"generation\":%llu,\"pending_slot\":%u,\"tries_remaining\":%u,"
         "\"candidate_size\":%u,\"candidate_sha256\":\"%s\"}\n",
         guid, identity->boot.lba, identity->boot.sectors,
         identity->data.lba, identity->data.sectors, confirmed,
         identity->boot.lba + layout->slots[inactive].header_lba,
         identity->boot.lba + layout->slots[inactive].payload_lba +
             layout->slots[inactive].payload_capacity_sectors,
         identity->boot.lba + layout->control_lba[0],
         (unsigned long long)snapshot->generation,
         snapshot->manager.pending_slot, snapshot->manager.tries_remaining,
         snapshot->manager.slots[inactive].payload_size, candidate_sha);
}

int main(int argc, char **argv) {
  struct disk_io disk = {.fd = -1};
  struct stat info;
  struct capyos_gpt_identity identity;
  struct boot_slot_layout layout;
  struct boot_slot_store store;
  struct boot_slot_snapshot before, after;
  struct boot_slot_image header, bridge = {0};
  uint8_t *payload = NULL;
  FILE *stream = NULL;
  uint64_t valid_generation = 0;
  int result = 1;
  if (argc != 4 && argc != 6) {
    fprintf(stderr, "backend: inspect IMAGE EXPECTED_VERSION or stage COPY EXPECTED_VERSION PAYLOAD BRIDGE_VERSION\n");
    return 1;
  }
  disk.writable = argc == 6 && !strcmp(argv[1], "stage");
  if ((!disk.writable && (argc != 4 || strcmp(argv[1], "inspect"))) ||
      strlen(argv[3]) >= BOOT_SLOT_VERSION_MAX)
    goto done;
  disk.fd = open(argv[2], (disk.writable ? O_RDWR : O_RDONLY) | O_NOFOLLOW);
  if (disk.fd < 0 || fstat(disk.fd, &info) || !S_ISREG(info.st_mode) ||
      info.st_nlink != 1 || info.st_size <= 0 || info.st_size % 512 ||
      (uint64_t)info.st_size / 512 > UINT32_MAX ||
      flock(disk.fd, LOCK_EX | LOCK_NB))
    goto done;
  disk.sectors = (uint32_t)((uint64_t)info.st_size / 512);
  if (capyos_gpt_identity_read(raw_read, &disk, 512, disk.sectors, &identity))
    goto done;
  disk.boot_lba = identity.boot.lba;
  disk.boot_sectors = identity.boot.sectors;
  if (boot_slot_layout_plan(disk.boot_sectors, &layout) || boot_slot_init() ||
      boot_slot_store_init(&store, &layout, slot_read, slot_write, slot_flush, &disk) ||
      boot_slot_store_bind_control(&store, 0) || boot_slot_snapshot_get(&before))
    goto done;
  uint32_t confirmed = before.manager.confirmed_slot;
  if (confirmed >= BOOT_SLOT_COUNT || before.manager.active_slot != confirmed ||
      before.manager.pending_slot != BOOT_SLOT_NONE || before.manager.rollback_pending ||
      !before.manager.slots[confirmed].health_confirmed ||
      strcmp(before.manager.slots[confirmed].version, argv[3]) ||
      boot_slot_store_read_header(&store, 0, confirmed, &header) ||
      confirmed_payload(&disk, &before.manager.slots[confirmed], &header))
    goto done;
  if (!disk.writable) {
    describe(&identity, &before, &layout);
    result = 0;
    goto done;
  }
  if (!argv[5][0] || strlen(argv[5]) >= sizeof(bridge.version)) goto done;
  stream = fopen(argv[4], "rb");
  if (!stream || fstat(fileno(stream), &info) || !S_ISREG(info.st_mode) ||
      info.st_size <= 0 || info.st_size > 4243456) goto done;
  bridge.payload_size = (uint32_t)info.st_size;
  payload = malloc(bridge.payload_size);
  if (!payload || fread(payload, 1, bridge.payload_size, stream) != bridge.payload_size ||
      bridge.payload_size < 4 || memcmp(payload, "\177ELF", 4)) goto done;
  strcpy(bridge.version, argv[5]);
  sha256_hash(payload, bridge.payload_size, bridge.payload_sha256);
  uint32_t target = confirmed ^ 1u;
  if (boot_slot_store_stage_inactive_authorized(&store, 0, &before, target,
                                               &bridge, payload, bridge.payload_size,
                                               &valid_generation) ||
      boot_slot_activate(target) || boot_slot_snapshot_get(&after) ||
      after.manager.confirmed_slot != confirmed || after.manager.pending_slot != target ||
      after.manager.tries_remaining != 1 || after.manager.slots[target].health_confirmed ||
      after.generation <= valid_generation)
    goto done;
  describe(&identity, &after, &layout);
  result = 0;
done:
  if (result) fprintf(stderr, "offline recovery backend: refused unsafe or invalid image/state\n");
  if (stream) fclose(stream);
  free(payload);
  if (disk.fd >= 0) close(disk.fd);
  return result;
}

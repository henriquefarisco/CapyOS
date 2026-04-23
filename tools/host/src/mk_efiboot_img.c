/* mk_efiboot_img.c: Create a FAT16 EFI boot image for ISO El Torito.
 *
 * Usage:
 *   mk_efiboot_img --out <path> --bootx64 <path> --kernel <path>
 *                  [--manifest <path>] [--bootcfg <path>]
 *                  [--size 8M] [--spc 2] [--label EFIBOOT]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t ceil_div(uint32_t a, uint32_t b) {
  return (a + b - 1) / b;
}

static void dirent16(uint8_t *entry, const char *name8, const char *ext3,
                     uint8_t attr, uint16_t cluster, uint32_t size) {
  memset(entry, 0, 32);
  memset(entry, ' ', 11);
  for (int i = 0; i < 8 && name8[i]; i++) entry[i] = (uint8_t)name8[i];
  for (int i = 0; i < 3 && ext3[i]; i++) entry[8 + i] = (uint8_t)ext3[i];
  entry[11] = attr;
  put_le16(&entry[26], cluster);
  put_le32(&entry[28], size);
}

static uint8_t *read_file_data(const char *path, size_t *out_size) {
  FILE *fp = fopen(path, "rb");
  uint8_t *buf = NULL;
  long size = 0;

  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  size = ftell(fp);
  if (size <= 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  buf = (uint8_t *)malloc((size_t)size);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
    free(buf);
    fclose(fp);
    return NULL;
  }

  fclose(fp);
  *out_size = (size_t)size;
  return buf;
}

static size_t parse_size(const char *raw) {
  size_t value = 0;
  size_t mul = 1;
  size_t len = strlen(raw);
  char suffix = 0;

  if (len == 0) return 0;

  suffix = raw[len - 1];
  if (suffix == 'K' || suffix == 'k') {
    mul = 1024;
  } else if (suffix == 'M' || suffix == 'm') {
    mul = 1024 * 1024;
  } else if (suffix == 'G' || suffix == 'g') {
    mul = 1024UL * 1024UL * 1024UL;
  }

  value = (size_t)atol(raw);
  return value * mul;
}

static uint16_t fat_entries[65536];
static uint16_t next_free_cluster;

static uint16_t fat_alloc(uint32_t bytes_needed, uint32_t cluster_bytes) {
  uint32_t need = ceil_div(bytes_needed, cluster_bytes);
  uint16_t start = next_free_cluster;

  if (need == 0) need = 1;

  for (uint32_t i = 0; i < need; i++) {
    uint16_t cluster = (uint16_t)(start + i);
    fat_entries[cluster] = (i + 1 < need) ? (uint16_t)(cluster + 1) : 0xFFFF;
  }

  next_free_cluster = (uint16_t)(start + need);
  return start;
}

static void write_at(FILE *fp, size_t offset, const void *data, size_t len) {
  fseek(fp, (long)offset, SEEK_SET);
  fwrite(data, 1, len, fp);
}

static void write_cluster_data(FILE *fp, uint32_t data_lba, uint32_t spc,
                               uint32_t cluster_bytes, uint16_t start_cluster,
                               const uint8_t *data, size_t data_len) {
  uint16_t cluster = start_cluster;
  size_t written = 0;

  while (cluster >= 2 && cluster < 0xFFF8 && written < data_len) {
    size_t offset = (size_t)(data_lba + (uint32_t)(cluster - 2) * spc) * SECTOR;
    size_t chunk = data_len - written;
    uint8_t *buf = NULL;

    if (chunk > cluster_bytes) chunk = cluster_bytes;
    buf = (uint8_t *)calloc(1, cluster_bytes);
    if (!buf) return;

    memcpy(buf, data + written, chunk);
    write_at(fp, offset, buf, cluster_bytes);
    free(buf);

    written += chunk;
    cluster = fat_entries[cluster];
  }
}

int main(int argc, char **argv) {
  const char *out_path = NULL;
  const char *bootx64_path = NULL;
  const char *kernel_path = NULL;
  const char *manifest_path = NULL;
  const char *bootcfg_path = NULL;
  const char *size_str = "8M";
  const char *label = "EFIBOOT";
  const char *marker_str = "INSTALLER=1\n";
  uint8_t *bootx64_data = NULL;
  uint8_t *kernel_data = NULL;
  uint8_t *manifest_data = NULL;
  uint8_t *bootcfg_data = NULL;
  uint8_t *root = NULL;
  uint8_t *efi_dir = NULL;
  uint8_t *efi_boot_dir = NULL;
  uint8_t *boot_dir = NULL;
  uint8_t *fat_buf = NULL;
  size_t bootx64_sz = 0;
  size_t kernel_sz = 0;
  size_t manifest_sz = 0;
  size_t bootcfg_sz = 0;
  size_t marker_sz = strlen(marker_str);
  size_t size_bytes = 0;
  FILE *fp = NULL;
  int spc = 2;
  uint32_t total_sectors = 0;
  uint32_t reserved = 1;
  uint32_t num_fats = 2;
  uint32_t root_entries = 512;
  uint32_t root_dir_sectors = 0;
  uint32_t fat_size = 1;
  uint32_t cluster_count = 0;
  uint32_t cluster_bytes = 0;
  uint32_t fat1_lba = 0;
  uint32_t root_lba = 0;
  uint32_t data_lba = 0;
  uint16_t efi_cl = 0;
  uint16_t efi_boot_cl = 0;
  uint16_t bootdir_cl = 0;
  uint16_t bootx64_cl = 0;
  uint16_t kernel_cl = 0;
  uint16_t manifest_cl = 0;
  uint16_t bootcfg_cl = 0;
  uint16_t marker_cl = 0;
  int exit_code = 1;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      out_path = argv[++i];
    } else if (strcmp(argv[i], "--bootx64") == 0 && i + 1 < argc) {
      bootx64_path = argv[++i];
    } else if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) {
      kernel_path = argv[++i];
    } else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
      manifest_path = argv[++i];
    } else if (strcmp(argv[i], "--bootcfg") == 0 && i + 1 < argc) {
      bootcfg_path = argv[++i];
    } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      size_str = argv[++i];
    } else if (strcmp(argv[i], "--spc") == 0 && i + 1 < argc) {
      spc = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc) {
      label = argv[++i];
    }
  }

  if (!out_path || !bootx64_path || !kernel_path) {
    fprintf(stderr,
            "Usage: mk_efiboot_img --out <path> --bootx64 <path> --kernel <path> "
            "[--manifest <path>] [--bootcfg <path>] [--size 8M] [--spc N] "
            "[--label EFIBOOT]\n");
    goto cleanup;
  }

  size_bytes = parse_size(size_str);
  if (size_bytes < SECTOR || size_bytes % SECTOR != 0) {
    fprintf(stderr, "[err] Invalid size: %s\n", size_str);
    goto cleanup;
  }
  total_sectors = (uint32_t)(size_bytes / SECTOR);

  bootx64_data = read_file_data(bootx64_path, &bootx64_sz);
  kernel_data = read_file_data(kernel_path, &kernel_sz);
  if (!bootx64_data || !kernel_data) {
    fprintf(stderr, "[err] Cannot read BOOTX64.EFI or kernel input\n");
    goto cleanup;
  }
  if (manifest_path) {
    manifest_data = read_file_data(manifest_path, &manifest_sz);
    if (!manifest_data) {
      fprintf(stderr, "[err] Cannot read manifest: %s\n", manifest_path);
      goto cleanup;
    }
  }
  if (bootcfg_path) {
    bootcfg_data = read_file_data(bootcfg_path, &bootcfg_sz);
    if (!bootcfg_data) {
      fprintf(stderr, "[err] Cannot read boot config: %s\n", bootcfg_path);
      goto cleanup;
    }
  }

  root_dir_sectors = ceil_div(root_entries * 32, SECTOR);
  cluster_bytes = (uint32_t)spc * SECTOR;

  for (;;) {
    uint32_t data_sectors = total_sectors - reserved - num_fats * fat_size - root_dir_sectors;
    uint32_t need = 0;

    cluster_count = data_sectors / (uint32_t)spc;
    need = (cluster_count + 2) * 2;
    if (ceil_div(need, SECTOR) == fat_size) break;
    fat_size = ceil_div(need, SECTOR);
  }

  if (cluster_count < 4085 || cluster_count >= 65525) {
    fprintf(stderr, "[err] FAT16 invalid cluster count: %u\n", cluster_count);
    goto cleanup;
  }

  fat1_lba = reserved;
  root_lba = fat1_lba + num_fats * fat_size;
  data_lba = root_lba + root_dir_sectors;

  memset(fat_entries, 0, sizeof(fat_entries));
  fat_entries[0] = 0xFFF8;
  fat_entries[1] = 0xFFFF;
  next_free_cluster = 2;

  efi_cl = fat_alloc(cluster_bytes, cluster_bytes);
  efi_boot_cl = fat_alloc(cluster_bytes, cluster_bytes);
  bootdir_cl = fat_alloc(cluster_bytes, cluster_bytes);
  bootx64_cl = fat_alloc((uint32_t)bootx64_sz, cluster_bytes);
  kernel_cl = fat_alloc((uint32_t)kernel_sz, cluster_bytes);
  if (manifest_data) manifest_cl = fat_alloc((uint32_t)manifest_sz, cluster_bytes);
  if (bootcfg_data) bootcfg_cl = fat_alloc((uint32_t)bootcfg_sz, cluster_bytes);
  marker_cl = fat_alloc((uint32_t)marker_sz, cluster_bytes);

  root = (uint8_t *)calloc(1, root_dir_sectors * SECTOR);
  efi_dir = (uint8_t *)calloc(1, cluster_bytes);
  efi_boot_dir = (uint8_t *)calloc(1, cluster_bytes);
  boot_dir = (uint8_t *)calloc(1, cluster_bytes);
  fat_buf = (uint8_t *)calloc(1, (size_t)fat_size * SECTOR);
  if (!root || !efi_dir || !efi_boot_dir || !boot_dir || !fat_buf) {
    fprintf(stderr, "[err] Out of host memory while building EFI image\n");
    goto cleanup;
  }

  dirent16(&root[0], "EFI     ", "   ", 0x10, efi_cl, 0);
  dirent16(&root[32], "BOOT    ", "   ", 0x10, bootdir_cl, 0);
  dirent16(&root[64], "CAPYOS  ", "INI", 0x20, marker_cl, (uint32_t)marker_sz);

  dirent16(&efi_dir[0], ".       ", "   ", 0x10, efi_cl, 0);
  dirent16(&efi_dir[32], "..      ", "   ", 0x10, 0, 0);
  dirent16(&efi_dir[64], "BOOT    ", "   ", 0x10, efi_boot_cl, 0);

  dirent16(&efi_boot_dir[0], ".       ", "   ", 0x10, efi_boot_cl, 0);
  dirent16(&efi_boot_dir[32], "..      ", "   ", 0x10, efi_cl, 0);
  dirent16(&efi_boot_dir[64], "BOOTX64 ", "EFI", 0x20, bootx64_cl, (uint32_t)bootx64_sz);

  dirent16(&boot_dir[0], ".       ", "   ", 0x10, bootdir_cl, 0);
  dirent16(&boot_dir[32], "..      ", "   ", 0x10, 0, 0);
  dirent16(&boot_dir[64], "CAPYOS64", "BIN", 0x20, kernel_cl, (uint32_t)kernel_sz);
  if (manifest_data) {
    dirent16(&boot_dir[96], "MANIFEST", "BIN", 0x20, manifest_cl, (uint32_t)manifest_sz);
  }
  if (bootcfg_data) {
    dirent16(&boot_dir[128], "CAPYCFG ", "BIN", 0x20, bootcfg_cl, (uint32_t)bootcfg_sz);
  }

  {
    uint8_t bs[SECTOR];
    char volume_label[12];

    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB;
    bs[1] = 0x3C;
    bs[2] = 0x90;
    memcpy(&bs[3], "MSWIN4.1", 8);
    put_le16(&bs[11], SECTOR);
    bs[13] = (uint8_t)spc;
    put_le16(&bs[14], (uint16_t)reserved);
    bs[16] = (uint8_t)num_fats;
    put_le16(&bs[17], (uint16_t)root_entries);
    if (total_sectors <= 0xFFFF) {
      put_le16(&bs[19], (uint16_t)total_sectors);
    } else {
      put_le32(&bs[32], total_sectors);
    }
    bs[21] = 0xF8;
    put_le16(&bs[22], (uint16_t)fat_size);
    put_le16(&bs[24], 63);
    put_le16(&bs[26], 255);
    bs[36] = 0x80;
    bs[38] = 0x29;
    put_le32(&bs[39], 0x12345678);

    memset(volume_label, ' ', 11);
    volume_label[11] = '\0';
    for (int i = 0; i < 11 && label[i]; i++) {
      volume_label[i] = (label[i] >= 'a' && label[i] <= 'z') ? (char)(label[i] - 32) : label[i];
    }
    memcpy(&bs[43], volume_label, 11);
    memcpy(&bs[54], "FAT16   ", 8);
    bs[510] = 0x55;
    bs[511] = 0xAA;

    for (uint32_t i = 0; i < cluster_count + 2; i++) {
      put_le16(&fat_buf[i * 2], fat_entries[i]);
    }

    fp = fopen(out_path, "wb");
    if (!fp) {
      fprintf(stderr, "[err] Cannot create: %s\n", out_path);
      goto cleanup;
    }

    {
      uint8_t zero[SECTOR];
      memset(zero, 0, sizeof(zero));
      for (uint32_t sector = 0; sector < total_sectors; sector++) {
        fwrite(zero, 1, sizeof(zero), fp);
      }
    }

    write_at(fp, 0, bs, sizeof(bs));
  }

  write_at(fp, (size_t)fat1_lba * SECTOR, fat_buf, (size_t)fat_size * SECTOR);
  write_at(fp, (size_t)(fat1_lba + fat_size) * SECTOR, fat_buf, (size_t)fat_size * SECTOR);
  write_at(fp, (size_t)root_lba * SECTOR, root, root_dir_sectors * SECTOR);
  write_at(fp, (size_t)(data_lba + (uint32_t)(efi_cl - 2) * (uint32_t)spc) * SECTOR,
           efi_dir, cluster_bytes);
  write_at(fp, (size_t)(data_lba + (uint32_t)(efi_boot_cl - 2) * (uint32_t)spc) * SECTOR,
           efi_boot_dir, cluster_bytes);
  write_at(fp, (size_t)(data_lba + (uint32_t)(bootdir_cl - 2) * (uint32_t)spc) * SECTOR,
           boot_dir, cluster_bytes);

  write_cluster_data(fp, data_lba, (uint32_t)spc, cluster_bytes, bootx64_cl,
                     bootx64_data, bootx64_sz);
  write_cluster_data(fp, data_lba, (uint32_t)spc, cluster_bytes, kernel_cl,
                     kernel_data, kernel_sz);
  if (manifest_data) {
    write_cluster_data(fp, data_lba, (uint32_t)spc, cluster_bytes, manifest_cl,
                       manifest_data, manifest_sz);
  }
  if (bootcfg_data) {
    write_cluster_data(fp, data_lba, (uint32_t)spc, cluster_bytes, bootcfg_cl,
                       bootcfg_data, bootcfg_sz);
  }
  write_cluster_data(fp, data_lba, (uint32_t)spc, cluster_bytes, marker_cl,
                     (const uint8_t *)marker_str, marker_sz);

  fclose(fp);
  fp = NULL;

  printf("[ok] EFI boot image ready: %s (%zu bytes)\n", out_path, size_bytes);
  exit_code = 0;

cleanup:
  if (fp) fclose(fp);
  free(fat_buf);
  free(boot_dir);
  free(efi_boot_dir);
  free(efi_dir);
  free(root);
  free(bootcfg_data);
  free(manifest_data);
  free(kernel_data);
  free(bootx64_data);
  return exit_code;
}

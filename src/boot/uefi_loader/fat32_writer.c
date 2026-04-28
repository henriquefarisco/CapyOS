#include "internal/uefi_loader_internal.h"

static BOOLEAN fat32_alloc_contig(UINT32 *fat, UINT32 fat_len,
                                  UINT32 bytes_per_cluster, UINT32 *next_free,
                                  UINTN bytes_needed, UINT32 *out_start,
                                  UINT32 *out_clusters) {
  if (!fat || !next_free || !out_start || !out_clusters)
    return FALSE;
  if (fat_len < 4 || bytes_per_cluster == 0)
    return FALSE;

  UINTN need = (bytes_needed + bytes_per_cluster - 1U) / bytes_per_cluster;
  if (need == 0)
    need = 1;

  UINT64 start64 = (UINT64)(*next_free);
  if (start64 < 2ULL)
    start64 = 2ULL;
  if (start64 + need > (UINT64)fat_len)
    return FALSE;

  UINT32 start = (UINT32)start64;
  for (UINTN i = 0; i < need; i++) {
    UINT32 c = start + (UINT32)i;
    fat[c] = (i + 1 < need) ? (c + 1U) : 0x0FFFFFFFU;
  }
  *next_free = start + (UINT32)need;
  *out_start = start;
  *out_clusters = (UINT32)need;
  return TRUE;
}

static __attribute__((always_inline)) inline VOID fat32_dirent83(
    UINT8 *ent, const char *name8, const char *ext3, UINT8 attr,
    UINT32 cluster, UINT32 size) {
  if (!ent || !name8 || !ext3)
    return;
  for (UINTN i = 0; i < 32; i++)
    ent[i] = 0;
  for (UINTN i = 0; i < 8; i++)
    ent[i] = (UINT8)name8[i];
  for (UINTN i = 0; i < 3; i++)
    ent[8 + i] = (UINT8)ext3[i];
  ent[11] = attr;

  // Cluster high/low words (little-endian)
  ent[20] = (UINT8)((cluster >> 16) & 0xFF);
  ent[21] = (UINT8)((cluster >> 24) & 0xFF);
  ent[26] = (UINT8)(cluster & 0xFF);
  ent[27] = (UINT8)((cluster >> 8) & 0xFF);

  ent[28] = (UINT8)(size & 0xFF);
  ent[29] = (UINT8)((size >> 8) & 0xFF);
  ent[30] = (UINT8)((size >> 16) & 0xFF);
  ent[31] = (UINT8)((size >> 24) & 0xFF);
}

static EFI_STATUS fat32_write_cluster(EFI_BLOCK_IO_PROTOCOL *bio,
                                      UINT64 data_start_lba, UINT8 spc,
                                      UINT32 bytes_per_cluster, UINT8 *scratch,
                                      UINT32 cluster, const UINT8 *data,
                                      UINTN data_len) {
  if (!bio || !bio->Media || !scratch)
    return EFI_INVALID_PARAMETER;
  if (bio->Media->BlockSize != 512)
    return EFI_UNSUPPORTED;
  if (cluster < 2)
    return EFI_INVALID_PARAMETER;
  if (bytes_per_cluster == 0)
    return EFI_INVALID_PARAMETER;

  for (UINTN i = 0; i < bytes_per_cluster; i++)
    scratch[i] = 0;
  if (data && data_len) {
    UINTN n = (data_len > bytes_per_cluster) ? bytes_per_cluster : data_len;
    for (UINTN i = 0; i < n; i++)
      scratch[i] = data[i];
  }

  UINT64 lba = data_start_lba + (UINT64)(cluster - 2U) * (UINT64)spc;
  return uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId, lba,
                           bytes_per_cluster, scratch);
}

static EFI_STATUS fat32_write_chain_contig(EFI_BLOCK_IO_PROTOCOL *bio,
                                           UINT64 data_start_lba, UINT8 spc,
                                           UINT32 bytes_per_cluster,
                                           UINT8 *scratch, UINT32 start_cluster,
                                           UINT32 clusters, const UINT8 *data,
                                           UINTN data_len) {
  if (!bio || !scratch)
    return EFI_INVALID_PARAMETER;
  for (UINT32 i = 0; i < clusters; i++) {
    UINTN off = (UINTN)i * (UINTN)bytes_per_cluster;
    const UINT8 *ptr = NULL;
    UINTN len = 0;
    if (off < data_len) {
      ptr = data + off;
      len = data_len - off;
      if (len > bytes_per_cluster)
        len = bytes_per_cluster;
    }
    EFI_STATUS stt =
        fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                            scratch, start_cluster + i, ptr, len);
    if (EFI_ERROR(stt))
      return stt;
  }
  return EFI_SUCCESS;
}

EFI_STATUS fat32_write_volume(EFI_BLOCK_IO_PROTOCOL *bio,
                                     UINT64 part_lba, UINT64 total_sectors,
                                     const UINT8 *bootx64, UINTN bootx64_sz,
                                     const UINT8 *kernel, UINTN kernel_sz,
                                     const UINT8 *manifest, UINTN manifest_sz,
                                     const UINT8 *bootcfg, UINTN bootcfg_sz) {
  if (!bio || !bio->Media)
    return EFI_INVALID_PARAMETER;
  if (bio->Media->BlockSize != 512)
    return EFI_UNSUPPORTED;
  if (total_sectors < 65536)
    return EFI_INVALID_PARAMETER;
  if (total_sectors > 0xFFFFFFFFULL)
    return EFI_UNSUPPORTED;

  const UINT8 spc = 8; // 8 sectors/cluster
  const UINT16 reserved = 32;
  const UINT8 num_fats = 2;
  const UINT32 root_cluster = 2;
  const UINT32 bytes_per_cluster = (UINT32)spc * 512U;

  // Compute FAT size iteratively.
  UINT32 fat_size = 1;
  UINT32 cluster_count = 0;
  for (;;) {
    UINT64 data_sectors =
        total_sectors - reserved - (UINT64)num_fats * fat_size;
    cluster_count = (UINT32)(data_sectors / spc);
    UINT64 need_bytes = ((UINT64)cluster_count + 2ULL) * 4ULL;
    UINT32 new_fat = (UINT32)((need_bytes + 511ULL) / 512ULL);
    if (new_fat == fat_size)
      break;
    fat_size = new_fat;
  }
  if (cluster_count < 65525) {
    Print(L"[UEFI] ESP muito pequena para FAT32 (clusters=%u)\r\n",
          cluster_count);
    return EFI_INVALID_PARAMETER;
  }

  UINT64 fat_start_lba = part_lba + reserved;
  UINT64 data_start_lba = fat_start_lba + (UINT64)num_fats * fat_size;

  UINT32 fat_len = cluster_count + 2U;
  UINT32 *fat = AllocatePool((UINTN)fat_len * sizeof(UINT32));
  if (!fat)
    return EFI_OUT_OF_RESOURCES;
  for (UINT32 i = 0; i < fat_len; i++)
    fat[i] = 0;
  fat[0] = 0x0FFFFFF8U;
  fat[1] = 0x0FFFFFFFU;
  fat[root_cluster] = 0x0FFFFFFFU;
  UINT32 next_free = 3;

  UINT32 efi_cl = 0, efi_boot_cl = 0, bootdir_cl = 0;
  UINT32 efi_n = 0, efi_boot_n = 0, bootdir_n = 0;
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bytes_per_cluster, &efi_cl, &efi_n)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bytes_per_cluster, &efi_boot_cl, &efi_boot_n)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bytes_per_cluster, &bootdir_cl, &bootdir_n)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }

  UINT32 bootx64_cl = 0, kernel_cl = 0, manifest_cl = 0, bootcfg_cl = 0;
  UINT32 bootx64_need = 0, kernel_need = 0, manifest_need = 0, bootcfg_need = 0;
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          bootx64_sz, &bootx64_cl, &bootx64_need)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                          kernel_sz, &kernel_cl, &kernel_need)) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  if (manifest && manifest_sz) {
    if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                            manifest_sz, &manifest_cl, &manifest_need)) {
      FreePool(fat);
      return EFI_OUT_OF_RESOURCES;
    }
  }
  if (bootcfg && bootcfg_sz) {
    if (!fat32_alloc_contig(fat, fat_len, bytes_per_cluster, &next_free,
                            bootcfg_sz, &bootcfg_cl, &bootcfg_need)) {
      FreePool(fat);
      return EFI_OUT_OF_RESOURCES;
    }
  }

  EFI_STATUS stt;

  // Boot sector
  UINT8 bs[512];
  for (UINTN i = 0; i < 512; i++)
    bs[i] = 0;
  bs[0] = 0xEB;
  bs[1] = 0x58;
  bs[2] = 0x90;
  // "MSWIN4.1"
  const char oem[8] = {'M', 'S', 'W', 'I', 'N', '4', '.', '1'};
  for (UINTN i = 0; i < 8; i++)
    bs[3 + i] = (UINT8)oem[i];
  bs[11] = 0x00;
  bs[12] = 0x02; // 512
  bs[13] = spc;
  bs[14] = (UINT8)(reserved & 0xFF);
  bs[15] = (UINT8)((reserved >> 8) & 0xFF);
  bs[16] = num_fats;
  bs[17] = 0;
  bs[18] = 0; // root entries
  bs[19] = 0;
  bs[20] = 0; // totsec16
  bs[21] = 0xF8;
  bs[22] = 0;
  bs[23] = 0; // fatsz16
  bs[24] = 63;
  bs[25] = 0;
  bs[26] = 255;
  bs[27] = 0;
  // hidden=0
  // totsec32
  UINT32 tot32 = (UINT32)total_sectors;
  bs[32] = (UINT8)(tot32 & 0xFF);
  bs[33] = (UINT8)((tot32 >> 8) & 0xFF);
  bs[34] = (UINT8)((tot32 >> 16) & 0xFF);
  bs[35] = (UINT8)((tot32 >> 24) & 0xFF);
  // fatsz32
  bs[36] = (UINT8)(fat_size & 0xFF);
  bs[37] = (UINT8)((fat_size >> 8) & 0xFF);
  bs[38] = (UINT8)((fat_size >> 16) & 0xFF);
  bs[39] = (UINT8)((fat_size >> 24) & 0xFF);
  // root cluster
  bs[44] = (UINT8)(root_cluster & 0xFF);
  bs[45] = (UINT8)((root_cluster >> 8) & 0xFF);
  bs[46] = (UINT8)((root_cluster >> 16) & 0xFF);
  bs[47] = (UINT8)((root_cluster >> 24) & 0xFF);
  // fsinfo=1, backup=6
  bs[48] = 1;
  bs[49] = 0;
  bs[50] = 6;
  bs[51] = 0;
  bs[64] = 0x80;
  bs[66] = 0x29;
  bs[67] = 0x78;
  bs[68] = 0x56;
  bs[69] = 0x34;
  bs[70] = 0x12;
  // label
  const char lab[11] = {'E', 'F', 'I', 'S', 'Y', 'S', 'P', 'A', 'R', 'T', ' '};
  for (UINTN i = 0; i < 11; i++)
    bs[71 + i] = (UINT8)lab[i];
  const char fst[8] = {'F', 'A', 'T', '3', '2', ' ', ' ', ' '};
  for (UINTN i = 0; i < 8; i++)
    bs[82 + i] = (UINT8)fst[i];
  bs[510] = 0x55;
  bs[511] = 0xAA;

  // FSInfo
  UINT8 fsinfo[512];
  for (UINTN i = 0; i < 512; i++)
    fsinfo[i] = 0;
  fsinfo[0] = 0x52;
  fsinfo[1] = 0x52;
  fsinfo[2] = 0x61;
  fsinfo[3] = 0x41;
  fsinfo[484] = 0x72;
  fsinfo[485] = 0x72;
  fsinfo[486] = 0x41;
  fsinfo[487] = 0x61;
  fsinfo[488] = 0xFF;
  fsinfo[489] = 0xFF;
  fsinfo[490] = 0xFF;
  fsinfo[491] = 0xFF;
  fsinfo[492] = 0xFF;
  fsinfo[493] = 0xFF;
  fsinfo[494] = 0xFF;
  fsinfo[495] = 0xFF;
  fsinfo[508] = 0x00;
  fsinfo[509] = 0x00;
  fsinfo[510] = 0x55;
  fsinfo[511] = 0xAA;

  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 0, 512, bs);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 1, 512, fsinfo);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 6, 512, bs);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          part_lba + 7, 512, fsinfo);
  if (EFI_ERROR(stt)) {
    FreePool(fat);
    return stt;
  }

  // Write FAT1/FAT2
  UINTN fat_bytes = (UINTN)fat_size * 512U;
  UINT8 *fatbuf = AllocatePool(fat_bytes);
  if (!fatbuf) {
    FreePool(fat);
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN i = 0; i < fat_bytes; i++)
    fatbuf[i] = 0;
  for (UINT32 i = 0; i < cluster_count + 2U; i++) {
    UINT32 v = fat[i] & 0x0FFFFFFF;
    UINTN off = (UINTN)i * 4U;
    fatbuf[off + 0] = (UINT8)(v & 0xFF);
    fatbuf[off + 1] = (UINT8)((v >> 8) & 0xFF);
    fatbuf[off + 2] = (UINT8)((v >> 16) & 0xFF);
    fatbuf[off + 3] = (UINT8)((v >> 24) & 0xFF);
  }
  FreePool(fat);
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          fat_start_lba, fat_bytes, fatbuf);
  if (EFI_ERROR(stt)) {
    FreePool(fatbuf);
    return stt;
  }
  stt = uefi_call_wrapper(bio->WriteBlocks, 5, bio, bio->Media->MediaId,
                          fat_start_lba + fat_size, fat_bytes, fatbuf);
  FreePool(fatbuf);
  if (EFI_ERROR(stt))
    return stt;

  UINT8 *scratch = AllocatePool(bytes_per_cluster);
  if (!scratch)
    return EFI_OUT_OF_RESOURCES;

  UINT8 *root = AllocatePool(bytes_per_cluster);
  UINT8 *efi_dir = AllocatePool(bytes_per_cluster);
  UINT8 *efi_boot_dir = AllocatePool(bytes_per_cluster);
  UINT8 *boot_dir = AllocatePool(bytes_per_cluster);
  if (!root || !efi_dir || !efi_boot_dir || !boot_dir) {
    if (root)
      FreePool(root);
    if (efi_dir)
      FreePool(efi_dir);
    if (efi_boot_dir)
      FreePool(efi_boot_dir);
    if (boot_dir)
      FreePool(boot_dir);
    FreePool(scratch);
    return EFI_OUT_OF_RESOURCES;
  }
  for (UINTN i = 0; i < bytes_per_cluster; i++) {
    root[i] = 0;
    efi_dir[i] = 0;
    efi_boot_dir[i] = 0;
    boot_dir[i] = 0;
  }

  fat32_dirent83(&root[0], "EFI     ", "   ", 0x10, efi_cl, 0);
  fat32_dirent83(&root[32], "BOOT    ", "   ", 0x10, bootdir_cl, 0);

  fat32_dirent83(&efi_dir[0], ".       ", "   ", 0x10, efi_cl, 0);
  fat32_dirent83(&efi_dir[32], "..      ", "   ", 0x10, root_cluster, 0);
  fat32_dirent83(&efi_dir[64], "BOOT    ", "   ", 0x10, efi_boot_cl, 0);

  fat32_dirent83(&efi_boot_dir[0], ".       ", "   ", 0x10, efi_boot_cl, 0);
  fat32_dirent83(&efi_boot_dir[32], "..      ", "   ", 0x10, efi_cl, 0);
  fat32_dirent83(&efi_boot_dir[64], "BOOTX64 ", "EFI", 0x20, bootx64_cl,
                 (UINT32)bootx64_sz);

  fat32_dirent83(&boot_dir[0], ".       ", "   ", 0x10, bootdir_cl, 0);
  fat32_dirent83(&boot_dir[32], "..      ", "   ", 0x10, root_cluster, 0);
  fat32_dirent83(&boot_dir[64], "CAPYOS64", "BIN", 0x20, kernel_cl,
                 (UINT32)kernel_sz);
  UINTN boot_file_off = 96;
  if (manifest && manifest_sz) {
    fat32_dirent83(&boot_dir[boot_file_off], "MANIFEST", "BIN", 0x20, manifest_cl,
                   (UINT32)manifest_sz);
    boot_file_off += 32;
  }
  if (bootcfg && bootcfg_sz) {
    fat32_dirent83(&boot_dir[boot_file_off], "CAPYCFG ", "BIN", 0x20,
                   bootcfg_cl, (UINT32)bootcfg_sz);
  }

  stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                            scratch, root_cluster, root, bytes_per_cluster);
  if (!EFI_ERROR(stt))
    stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                              scratch, efi_cl, efi_dir, bytes_per_cluster);
  if (!EFI_ERROR(stt))
    stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                              scratch, efi_boot_cl, efi_boot_dir,
                              bytes_per_cluster);
  if (!EFI_ERROR(stt))
    stt = fat32_write_cluster(bio, data_start_lba, spc, bytes_per_cluster,
                              scratch, bootdir_cl, boot_dir, bytes_per_cluster);
  FreePool(root);
  FreePool(efi_dir);
  FreePool(efi_boot_dir);
  FreePool(boot_dir);
  if (EFI_ERROR(stt)) {
    FreePool(scratch);
    return stt;
  }

  stt = fat32_write_chain_contig(bio, data_start_lba, spc, bytes_per_cluster,
                                 scratch, bootx64_cl, bootx64_need, bootx64,
                                 bootx64_sz);
  if (!EFI_ERROR(stt))
    stt = fat32_write_chain_contig(bio, data_start_lba, spc, bytes_per_cluster,
                                   scratch, kernel_cl, kernel_need, kernel,
                                   kernel_sz);
  if (!EFI_ERROR(stt) && manifest && manifest_sz)
    stt = fat32_write_chain_contig(bio, data_start_lba, spc, bytes_per_cluster,
                                   scratch, manifest_cl, manifest_need,
                                   manifest, manifest_sz);
  if (!EFI_ERROR(stt) && bootcfg && bootcfg_sz)
    stt = fat32_write_chain_contig(bio, data_start_lba, spc, bytes_per_cluster,
                                   scratch, bootcfg_cl, bootcfg_need, bootcfg,
                                   bootcfg_sz);
  if (!EFI_ERROR(stt) && bootcfg && bootcfg_sz) {
    UINTN verify_bytes = (UINTN)bootcfg_need * (UINTN)bytes_per_cluster;
    UINT8 *verify_buf = AllocatePool(verify_bytes);
    if (!verify_buf) {
      FreePool(scratch);
      return EFI_OUT_OF_RESOURCES;
    }
    UINT64 verify_lba =
        data_start_lba + (UINT64)(bootcfg_cl - 2U) * (UINT64)spc;
    stt = uefi_call_wrapper(bio->ReadBlocks, 5, bio, bio->Media->MediaId,
                            verify_lba, verify_bytes, verify_buf);
    if (!EFI_ERROR(stt)) {
      for (UINTN i = 0; i < bootcfg_sz; ++i) {
        if (verify_buf[i] != bootcfg[i]) {
          stt = EFI_CRC_ERROR;
          break;
        }
      }
    }
    FreePool(verify_buf);
    if (EFI_ERROR(stt)) {
      FreePool(scratch);
      return stt;
    }
  }
  FreePool(scratch);
  if (EFI_ERROR(stt))
    return stt;

  uefi_call_wrapper(bio->FlushBlocks, 1, bio);
  return EFI_SUCCESS;
}


#include "internal/uefi_loader_internal.h"

const CHAR16 *installer_language_code(installer_language_t language) {
  switch (language) {
  case INSTALLER_LANG_PT_BR:
    return L"pt-BR";
  case INSTALLER_LANG_ES:
    return L"es";
  case INSTALLER_LANG_EN:
  default:
    return L"en";
  }
}

const CHAR16 *installer_language_name(installer_language_t language) {
  switch (language) {
  case INSTALLER_LANG_PT_BR:
    return L"Portugues (Brasil)";
  case INSTALLER_LANG_ES:
    return L"Espanol";
  case INSTALLER_LANG_EN:
  default:
    return L"English";
  }
}

UINT64 align_up_u64(UINT64 v, UINT64 a) {
  if (a == 0)
    return v;
  UINT64 r = v % a;
  if (r == 0)
    return v;
  return v + (a - r);
}

UINT32 checksum32_words(const UINT8 *data, UINTN len) {
  if (!data || len == 0)
    return 0;
  UINT32 sum = 0;
  UINTN i = 0;
  while (i + 4 <= len) {
    UINT32 v = (UINT32)data[i] | ((UINT32)data[i + 1] << 8) |
               ((UINT32)data[i + 2] << 16) | ((UINT32)data[i + 3] << 24);
    sum += v;
    i += 4;
  }
  while (i < len) {
    sum += data[i];
    i++;
  }
  return sum;
}

VOID build_manifest(struct boot_manifest *m, UINT32 kernel_lba,
                           UINT32 kernel_sectors, UINT32 cksum32) {
  if (!m)
    return;
  // Mirror boot_manifest_init/add without linking extra objects.
  m->magic = BOOT_MANIFEST_MAGIC;
  m->version = BOOT_MANIFEST_VERSION;
  m->entry_count = 1;
  m->reserved = 0;
  for (UINTN i = 0; i < 4; i++) {
    m->entries[i].type = 0;
    m->entries[i].lba_start = 0;
    m->entries[i].sector_count = 0;
    m->entries[i].checksum32 = 0;
    m->entries[i].reserved = 0;
  }
  m->entries[0].type = BOOT_ENTRY_NORMAL;
  m->entries[0].lba_start = kernel_lba;
  m->entries[0].sector_count = kernel_sectors;
  m->entries[0].checksum32 = cksum32;
  m->entries[0].reserved = 0;
}

EFI_STATUS open_boot_volume(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                                   EFI_HANDLE *out_fs_handle,
                                   EFI_FILE_HANDLE *out_root) {
  if (!st || !st->BootServices || !out_fs_handle || !out_root)
    return EFI_INVALID_PARAMETER;
  *out_fs_handle = NULL;
  *out_root = NULL;

  EFI_STATUS stt;
  EFI_LOADED_IMAGE *li = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                          &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || li == NULL)
    return stt;

  EFI_HANDLE fs_handle = li->DeviceHandle;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, fs_handle,
                          &FileSystemProtocol, (VOID **)&sfs);
  if (EFI_ERROR(stt) || sfs == NULL)
    return stt;

  EFI_FILE_HANDLE root = NULL;
  stt = uefi_call_wrapper(sfs->OpenVolume, 2, sfs, &root);
  if (EFI_ERROR(stt) || root == NULL)
    return stt;

  *out_fs_handle = fs_handle;
  *out_root = root;
  return EFI_SUCCESS;
}

BOOLEAN boot_volume_is_readonly(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  EFI_LOADED_IMAGE *li = NULL;
  EFI_STATUS stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                                     &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || !li)
    return FALSE;
  EFI_BLOCK_IO_PROTOCOL *bio = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, li->DeviceHandle,
                          &BlockIoProtocol, (VOID **)&bio);
  if (EFI_ERROR(stt) || !bio || !bio->Media)
    return FALSE;
  return bio->Media->ReadOnly ? TRUE : FALSE;
}

BOOLEAN boot_volume_has_marker(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  EFI_HANDLE fs_handle = NULL;
  EFI_FILE_HANDLE root = NULL;
  EFI_STATUS stt = open_boot_volume(image, st, &fs_handle, &root);
  if (EFI_ERROR(stt) || !root)
    return FALSE;

  CHAR16 *paths[] = {
      L"\\CAPYOS.INI",
      L"CAPYOS.INI",
      L"\\boot\\capyos.ini",
      L"boot\\capyos.ini",
      L"\\BOOT\\CAPYOS.INI",
      L"BOOT\\CAPYOS.INI",
  };
  for (UINTN i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
    EFI_FILE_HANDLE fh = NULL;
    stt = uefi_call_wrapper(root->Open, 5, root, &fh, paths[i],
                            EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(stt) && fh) {
      uefi_call_wrapper(fh->Close, 1, fh);
      uefi_call_wrapper(root->Close, 1, root);
      return TRUE;
    }
  }

  uefi_call_wrapper(root->Close, 1, root);
  return FALSE;
}

static BOOLEAN device_path_has_cdrom_node(VOID *dp_raw) {
  dp_node_hdr_t *node = (dp_node_hdr_t *)dp_raw;
  while (node) {
    UINTN len = (UINTN)node->Length[0] | ((UINTN)node->Length[1] << 8);
    if (len < sizeof(dp_node_hdr_t)) {
      return FALSE;
    }
    if (node->Type == DP_TYPE_END) {
      return FALSE;
    }
    if (node->Type == DP_TYPE_MEDIA && node->SubType == DP_SUBTYPE_CDROM) {
      return TRUE;
    }
    node = (dp_node_hdr_t *)((UINT8 *)node + len);
  }
  return FALSE;
}

BOOLEAN boot_device_is_cdrom(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
  if (!st || !st->BootServices) {
    return FALSE;
  }

  EFI_LOADED_IMAGE *li = NULL;
  EFI_STATUS stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                                     &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || !li) {
    return FALSE;
  }

  if (li->FilePath && device_path_has_cdrom_node((VOID *)li->FilePath)) {
    return TRUE;
  }

  VOID *dp_raw = NULL;
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, li->DeviceHandle,
                          &DevicePathProtocol, (VOID **)&dp_raw);
  if (EFI_ERROR(stt) || !dp_raw) {
    return FALSE;
  }
  return device_path_has_cdrom_node(dp_raw);
}

#define INSTALLER_DISK_MAX_CANDIDATES 16u

struct installer_disk_candidate {
  EFI_HANDLE handle;
  EFI_BLOCK_IO_PROTOCOL *bio;
  UINT32 media_id;
  UINT64 path_id;
  UINT64 disk_bytes;
  struct installer_disk_geometry geometry;
  struct installer_disk_layout layout;
  int preflight_result;
};

static UINT64 installer_disk_path_id(EFI_SYSTEM_TABLE *st, EFI_HANDLE handle) {
  VOID *path_raw = NULL;
  dp_node_hdr_t *node;
  UINT64 hash = installer_disk_path_hash_init();
  UINTN total = 0u;
  EFI_STATUS stt;
  if (!st || !st->BootServices || !handle) {
    return 0u;
  }
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, handle,
                          &DevicePathProtocol, (VOID **)&path_raw);
  if (EFI_ERROR(stt) || !path_raw) {
    return 0u;
  }
  node = (dp_node_hdr_t *)path_raw;
  for (UINTN guard = 0u; guard < 128u; ++guard) {
    UINTN len = (UINTN)node->Length[0] | ((UINTN)node->Length[1] << 8);
    if (len < sizeof(dp_node_hdr_t) || len > 4096u ||
        total > 4096u - len) {
      return 0u;
    }
    hash = installer_disk_path_hash_update(hash, (const uint8_t *)node,
                                           (size_t)len);
    if (hash == 0u) {
      return 0u;
    }
    total += len;
    if (node->Type == DP_TYPE_END &&
        node->SubType == DP_SUBTYPE_END_ENTIRE) {
      return hash ? hash : 1u;
    }
    node = (dp_node_hdr_t *)((UINT8 *)node + len);
  }
  return 0u;
}

static int installer_disk_layout_equal(
    const struct installer_disk_layout *a,
    const struct installer_disk_layout *b) {
  return a && b && a->required_bytes == b->required_bytes &&
         a->total_sectors == b->total_sectors &&
         a->first_usable_lba == b->first_usable_lba &&
         a->last_usable_lba == b->last_usable_lba &&
         a->backup_entries_lba == b->backup_entries_lba &&
         a->esp_lba == b->esp_lba && a->esp_sectors == b->esp_sectors &&
         a->boot_lba == b->boot_lba &&
         a->boot_sectors == b->boot_sectors &&
         a->data_lba == b->data_lba &&
         a->data_sectors == b->data_sectors;
}

static void installer_disk_geometry_from_bio(
    EFI_BLOCK_IO_PROTOCOL *bio, struct installer_disk_geometry *geometry) {
  geometry->block_count =
      bio && bio->Media
          ? (bio->Media->LastBlock == UINT64_MAX
                 ? UINT64_MAX
                 : (UINT64)bio->Media->LastBlock + 1ULL)
          : 0u;
  geometry->block_size = bio && bio->Media ? bio->Media->BlockSize : 0u;
  geometry->media_present =
      bio && bio->Media && bio->Media->MediaPresent ? 1u : 0u;
  geometry->logical_partition =
      bio && bio->Media && bio->Media->LogicalPartition ? 1u : 0u;
  geometry->read_only = bio && bio->Media && bio->Media->ReadOnly ? 1u : 0u;
  geometry->removable =
      bio && bio->Media && bio->Media->RemovableMedia ? 1u : 0u;
}

EFI_STATUS installer_revalidate_target(
    EFI_SYSTEM_TABLE *st, const installer_disk_target_t *target) {
  EFI_BLOCK_IO_PROTOCOL *bio = NULL;
  struct installer_disk_geometry geometry;
  struct installer_disk_layout layout;
  EFI_STATUS stt;
  if (!st || !st->BootServices || !target || !target->handle ||
      !target->bio) {
    return EFI_INVALID_PARAMETER;
  }
  stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, target->handle,
                          &BlockIoProtocol, (VOID **)&bio);
  if (EFI_ERROR(stt) || !bio || !bio->Media || bio != target->bio) {
    return EFI_MEDIA_CHANGED;
  }
  installer_disk_geometry_from_bio(bio, &geometry);
  if (target->media_id != bio->Media->MediaId ||
      target->path_id != installer_disk_path_id(st, target->handle) ||
      target->geometry.block_count != geometry.block_count ||
      target->geometry.block_size != geometry.block_size) {
    return EFI_MEDIA_CHANGED;
  }
  if (installer_disk_plan(&geometry, &layout) !=
          INSTALLER_DISK_PREFLIGHT_OK ||
      !installer_disk_layout_equal(&target->layout, &layout)) {
    return EFI_ACCESS_DENIED;
  }
  return EFI_SUCCESS;
}

EFI_STATUS choose_target_disk(EFI_SYSTEM_TABLE *st,
                              installer_disk_target_t *out_target) {
  EFI_HANDLE *handles = NULL;
  UINTN handle_count = 0;
  struct installer_disk_candidate candidates[INSTALLER_DISK_MAX_CANDIDATES];
  UINTN eligible_map[INSTALLER_DISK_MAX_CANDIDATES];
  UINTN candidate_count = 0;
  UINTN eligible_count = 0;
  EFI_STATUS stt;

  if (!st || !st->BootServices || !out_target) {
    return EFI_INVALID_PARAMETER;
  }
  *out_target = (installer_disk_target_t){0};

  stt = uefi_call_wrapper(st->BootServices->LocateHandleBuffer, 5, ByProtocol,
                          &BlockIoProtocol, NULL, &handle_count, &handles);
  if (EFI_ERROR(stt) || !handles || handle_count == 0u) {
    return EFI_NOT_FOUND;
  }

  for (UINTN i = 0; i < handle_count; ++i) {
    EFI_BLOCK_IO_PROTOCOL *bio = NULL;
    struct installer_disk_geometry geometry;
    struct installer_disk_candidate *candidate;

    stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, handles[i],
                            &BlockIoProtocol, (VOID **)&bio);
    if (EFI_ERROR(stt) || !bio || !bio->Media) {
      continue;
    }
    installer_disk_geometry_from_bio(bio, &geometry);
    if (!geometry.media_present || geometry.logical_partition ||
        geometry.read_only || geometry.removable) {
      continue;
    }
    if (candidate_count >= INSTALLER_DISK_MAX_CANDIDATES) {
      FreePool(handles);
      return EFI_BUFFER_TOO_SMALL;
    }
    candidate = &candidates[candidate_count++];
    candidate->handle = handles[i];
    candidate->bio = bio;
    candidate->media_id = bio->Media->MediaId;
    candidate->path_id = installer_disk_path_id(st, handles[i]);
    candidate->geometry = geometry;
    candidate->disk_bytes =
        geometry.block_size != 0u &&
                geometry.block_count <= UINT64_MAX / geometry.block_size
            ? geometry.block_count * geometry.block_size
            : 0u;
    candidate->preflight_result =
        candidate->path_id == 0u
            ? INSTALLER_DISK_PREFLIGHT_INVALID
            : installer_disk_plan(&geometry, &candidate->layout);
  }
  FreePool(handles);

  if (candidate_count == 0u) {
    return EFI_NOT_FOUND;
  }

  Print(L"\r\n=== Target Disk Selection ===\r\n\r\n");
  Print(L"Select the exact fixed disk that may be erased.\r\n");
  for (UINTN i = 0; i < candidate_count; ++i) {
    struct installer_disk_candidate *candidate = &candidates[i];
    if (candidate->preflight_result == INSTALLER_DISK_PREFLIGHT_OK) {
      eligible_map[eligible_count] = i;
      Print(L"  [%u] PathId %016lx, MediaId %u - %lu MiB - eligible "
            L"(%lu MiB DATA)\r\n",
            (UINT32)(eligible_count + 1u), candidate->path_id,
            candidate->media_id,
            candidate->disk_bytes / (1024ULL * 1024ULL),
            (candidate->layout.data_sectors * INSTALLER_DISK_BLOCK_SIZE) /
                (1024ULL * 1024ULL));
      ++eligible_count;
    } else if (candidate->preflight_result ==
               INSTALLER_DISK_PREFLIGHT_TOO_SMALL) {
      Print(L"  [--] PathId %016lx, MediaId %u - %lu MiB - too small "
            L"(minimum %lu MiB)\r\n",
            candidate->path_id, candidate->media_id,
            candidate->disk_bytes / (1024ULL * 1024ULL),
            (installer_disk_minimum_bytes() + (1024ULL * 1024ULL) - 1ULL) /
                (1024ULL * 1024ULL));
    } else if (candidate->preflight_result ==
               INSTALLER_DISK_PREFLIGHT_BLOCK_SIZE) {
      Print(L"  [--] PathId %016lx, MediaId %u - unsupported block size\r\n",
            candidate->path_id, candidate->media_id);
    } else {
      Print(L"  [--] PathId %016lx, MediaId %u - unavailable\r\n",
            candidate->path_id, candidate->media_id);
    }
  }
  Print(L"[installer] eligible-targets=%u\r\n", (UINT32)eligible_count);
  if (eligible_count == 0u) {
    Print(L"No disk passed the installation preflight.\r\n");
    return EFI_VOLUME_FULL;
  }

  while (1) {
    CHAR16 selection_in[16];
    char selection_ascii[16];
    size_t selected_eligible = 0u;
    UINTN selected_index;
    Print(L"\r\nSelect target disk [1-%u] or 0 to cancel: ",
          (UINT32)eligible_count);
    uefi_readline(st, selection_in, 16u, FALSE);
    char16_to_ascii(selection_ascii, sizeof(selection_ascii), selection_in);
    if (ascii_streq(selection_ascii, "0")) {
      return EFI_ABORTED;
    }
    if (installer_disk_parse_selection(selection_ascii,
                                       (size_t)eligible_count,
                                       &selected_eligible) != 0) {
      Print(L"Invalid disk selection.\r\n");
      continue;
    }
    selected_index = eligible_map[selected_eligible];
    out_target->handle = candidates[selected_index].handle;
    out_target->bio = candidates[selected_index].bio;
    out_target->media_id = candidates[selected_index].media_id;
    out_target->path_id = candidates[selected_index].path_id;
    out_target->geometry = candidates[selected_index].geometry;
    out_target->layout = candidates[selected_index].layout;
    Print(L"Selected PathId %016lx, MediaId %u (%lu MiB, %lu MiB DATA).\r\n",
          out_target->path_id, out_target->media_id,
          candidates[selected_index].disk_bytes / (1024ULL * 1024ULL),
          (out_target->layout.data_sectors * INSTALLER_DISK_BLOCK_SIZE) /
              (1024ULL * 1024ULL));
    return EFI_SUCCESS;
  }
}

static UINTN dp_node_len(const dp_node_hdr_t *node) {
  if (!node) {
    return 0;
  }
  return (UINTN)node->Length[0] | ((UINTN)node->Length[1] << 8);
}

static int get_partition_hint_from_handle(EFI_SYSTEM_TABLE *st,
                                          EFI_HANDLE handle,
                                          UINT64 *out_start,
                                          UINT64 *out_count) {
  if (out_start) {
    *out_start = 0;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!st || !st->BootServices || !handle) {
    return -1;
  }

  VOID *dp_raw = NULL;
  EFI_STATUS stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, handle,
                                     &DevicePathProtocol, (VOID **)&dp_raw);
  if (EFI_ERROR(stt) || !dp_raw) {
    return -1;
  }

  dp_node_hdr_t *node = (dp_node_hdr_t *)dp_raw;
  for (UINTN guard = 0; node && guard < 128; ++guard) {
    UINTN len = dp_node_len(node);
    if (len < sizeof(dp_node_hdr_t)) {
      break;
    }
    if (node->Type == DP_TYPE_END &&
        node->SubType == DP_SUBTYPE_END_ENTIRE) {
      break;
    }
    if (node->Type == DP_TYPE_MEDIA &&
        node->SubType == DP_SUBTYPE_HARDDRIVE &&
        len >= sizeof(dp_hd_node_t)) {
      const dp_hd_node_t *hd = (const dp_hd_node_t *)node;
      if (hd->PartitionStart != 0 && hd->PartitionSize != 0) {
        if (out_start) {
          *out_start = hd->PartitionStart;
        }
        if (out_count) {
          *out_count = hd->PartitionSize;
        }
        return 0;
      }
    }
    node = (dp_node_hdr_t *)((UINT8 *)node + len);
  }

  return -1;
}

static int get_boot_partition_hint(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                                   UINT64 *out_start, UINT64 *out_count) {
  if (out_start) {
    *out_start = 0;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!image || !st || !st->BootServices) {
    return -1;
  }

  EFI_LOADED_IMAGE *li = NULL;
  EFI_STATUS stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, image,
                                     &LoadedImageProtocol, (VOID **)&li);
  if (EFI_ERROR(stt) || !li || !li->DeviceHandle) {
    return -1;
  }
  return get_partition_hint_from_handle(st, li->DeviceHandle, out_start,
                                        out_count);
}

EFI_STATUS gpt_find_capyos_data_partition(EFI_BLOCK_IO_PROTOCOL *bio,
                                                 UINT64 *out_data_start,
                                                 UINT64 *out_data_count,
                                                 UINT64 *out_esp_start,
                                                 UINT64 *out_esp_count) {
  if (!bio || !bio->Media || !out_data_start || !out_data_count) {
    return EFI_INVALID_PARAMETER;
  }
  *out_data_start = 0;
  *out_data_count = 0;
  if (out_esp_start) {
    *out_esp_start = 0;
  }
  if (out_esp_count) {
    *out_esp_count = 0;
  }

  UINT32 bsz = bio->Media->BlockSize;
  if (bsz == 0) {
    return EFI_INVALID_PARAMETER;
  }

  VOID *hdr_buf = AllocatePool(bsz);
  if (!hdr_buf) {
    return EFI_OUT_OF_RESOURCES;
  }
  EFI_STATUS st = uefi_call_wrapper(bio->ReadBlocks, 5, bio,
                                    bio->Media->MediaId, GPT_HEADER_LBA, bsz,
                                    hdr_buf);
  if (EFI_ERROR(st)) {
    FreePool(hdr_buf);
    return st;
  }

  gpt_header_t *hdr = (gpt_header_t *)hdr_buf;
  if (hdr->signature != GPT_SIG || hdr->part_entry_size == 0 ||
      hdr->num_part_entries == 0 || hdr->part_entry_size > bsz) {
    FreePool(hdr_buf);
    return EFI_NOT_FOUND;
  }

  UINT32 entsz = hdr->part_entry_size;
  UINT32 entcnt = hdr->num_part_entries;
  UINT64 ent_lba = hdr->part_entry_lba;
  FreePool(hdr_buf);

  UINT8 esp_guid[16] = EFI_PART_TYPE_ESP;
  UINT8 boot_guid[16] = EFI_PART_TYPE_CAPYOS_BOOT;
  UINT8 data_guid[16] = EFI_PART_TYPE_LINUX_FS;

  UINTN ents_per_block = bsz / entsz;
  UINTN read_entries = 0;
  UINT64 cur_lba = ent_lba;

  UINT64 esp_start = 0;
  UINT64 esp_count = 0;
  UINT64 boot_part_lba = 0;
  UINT64 data_start = 0;
  UINT64 data_count = 0;

  VOID *ent_buf = AllocatePool(bsz);
  if (!ent_buf) {
    return EFI_OUT_OF_RESOURCES;
  }

  while (read_entries < entcnt) {
    st = uefi_call_wrapper(bio->ReadBlocks, 5, bio, bio->Media->MediaId,
                           cur_lba, bsz, ent_buf);
    if (EFI_ERROR(st)) {
      FreePool(ent_buf);
      return st;
    }
    UINTN max_in_block = (entcnt - read_entries) < ents_per_block
                             ? (entcnt - read_entries)
                             : ents_per_block;
    for (UINTN i = 0; i < max_in_block; i++) {
      UINT8 *ptr = (UINT8 *)ent_buf + i * entsz;
      gpt_entry_t *e = (gpt_entry_t *)ptr;
      if (e->first_lba == 0 || e->last_lba < e->first_lba) {
        continue;
      }
      if (guid_eq(e->part_type_guid, esp_guid)) {
        esp_start = e->first_lba;
        esp_count = (e->last_lba - e->first_lba) + 1ULL;
      } else if (guid_eq(e->part_type_guid, boot_guid)) {
        boot_part_lba = e->first_lba;
      } else if (guid_eq(e->part_type_guid, data_guid)) {
        data_start = e->first_lba;
        data_count = (e->last_lba - e->first_lba) + 1ULL;
      }
    }
    read_entries += max_in_block;
    cur_lba++;
  }
  FreePool(ent_buf);

  if (boot_part_lba == 0 || data_start == 0 || data_count == 0) {
    return EFI_NOT_FOUND;
  }

  VOID *mf_buf = AllocatePool(bsz);
  if (!mf_buf) {
    return EFI_OUT_OF_RESOURCES;
  }
  st = uefi_call_wrapper(bio->ReadBlocks, 5, bio, bio->Media->MediaId,
                         boot_part_lba, bsz, mf_buf);
  if (EFI_ERROR(st)) {
    FreePool(mf_buf);
    return st;
  }
  struct boot_manifest *mf = (struct boot_manifest *)mf_buf;
  if (mf->magic != BOOT_MANIFEST_MAGIC || mf->entry_count == 0) {
    FreePool(mf_buf);
    return EFI_NOT_FOUND;
  }
  FreePool(mf_buf);

  *out_data_start = data_start;
  *out_data_count = data_count;
  if (out_esp_start) {
    *out_esp_start = esp_start;
  }
  if (out_esp_count) {
    *out_esp_count = esp_count;
  }
  return EFI_SUCCESS;
}

EFI_STATUS choose_runtime_disk_with_data(EFI_HANDLE image,
                                                EFI_SYSTEM_TABLE *st,
                                                EFI_BLOCK_IO_PROTOCOL **out_bio,
                                                UINT64 *out_data_start,
                                                UINT64 *out_data_count,
                                                EFI_BLOCK_IO_PROTOCOL **out_raw_bio,
                                                UINT64 *out_raw_data_start,
                                                UINT64 *out_raw_data_count) {
  if (!image || !st || !st->BootServices || !out_bio || !out_data_start ||
      !out_data_count || !out_raw_bio || !out_raw_data_start ||
      !out_raw_data_count) {
    return EFI_INVALID_PARAMETER;
  }
  *out_bio = NULL;
  *out_data_start = 0;
  *out_data_count = 0;
  *out_raw_bio = NULL;
  *out_raw_data_start = 0;
  *out_raw_data_count = 0;

  UINT64 boot_part_hint_start = 0;
  UINT64 boot_part_hint_count = 0;
  int has_boot_part_hint =
      (get_boot_partition_hint(image, st, &boot_part_hint_start,
                               &boot_part_hint_count) == 0 &&
       boot_part_hint_start != 0 && boot_part_hint_count != 0);

  EFI_HANDLE *handles = NULL;
  UINTN count = 0;
  EFI_STATUS stt =
      uefi_call_wrapper(st->BootServices->LocateHandleBuffer, 5, ByProtocol,
                        &BlockIoProtocol, NULL, &count, &handles);
  if (EFI_ERROR(stt) || !handles || count == 0) {
    return EFI_NOT_FOUND;
  }

  EFI_BLOCK_IO_PROTOCOL *best = NULL;
  UINT64 best_blocks = 0;
  UINT64 best_data_start = 0;
  UINT64 best_data_count = 0;

  for (UINTN i = 0; i < count; i++) {
    EFI_BLOCK_IO_PROTOCOL *bio = NULL;
    stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3, handles[i],
                            &BlockIoProtocol, (VOID **)&bio);
    if (EFI_ERROR(stt) || !bio || !bio->Media) {
      continue;
    }
    if (bio->Media->LogicalPartition || bio->Media->ReadOnly ||
        bio->Media->RemovableMedia) {
      continue;
    }

    UINT64 data_start = 0;
    UINT64 data_count = 0;
    UINT64 esp_start = 0;
    UINT64 esp_count = 0;
    if (EFI_ERROR(gpt_find_capyos_data_partition(bio, &data_start, &data_count,
                                                 &esp_start, &esp_count))) {
      continue;
    }

    if (has_boot_part_hint && esp_start == boot_part_hint_start &&
        esp_count == boot_part_hint_count) {
      best = bio;
      best_data_start = data_start;
      best_data_count = data_count;
      break;
    }

    UINT64 blocks = (UINT64)bio->Media->LastBlock + 1ULL;
    if (!best || blocks > best_blocks) {
      best = bio;
      best_blocks = blocks;
      best_data_start = data_start;
      best_data_count = data_count;
    }
  }

  FreePool(handles);

  if (!best) {
    return EFI_NOT_FOUND;
  }

  /* Prefer the logical DATA partition handle itself when available.
   * Some firmware/hypervisors are stricter with raw-disk BlockIO reads from
   * high LBAs during runtime and return EFI_DEVICE_ERROR for otherwise valid
   * sectors. Using the partition handle keeps LBA addressing local to DATA. */
  EFI_HANDLE *logical_handles = NULL;
  UINTN logical_count = 0;
  stt = uefi_call_wrapper(st->BootServices->LocateHandleBuffer, 5, ByProtocol,
                          &BlockIoProtocol, NULL, &logical_count,
                          &logical_handles);
  if (!EFI_ERROR(stt) && logical_handles && logical_count > 0) {
    for (UINTN i = 0; i < logical_count; ++i) {
      EFI_BLOCK_IO_PROTOCOL *bio = NULL;
      stt = uefi_call_wrapper(st->BootServices->HandleProtocol, 3,
                              logical_handles[i], &BlockIoProtocol,
                              (VOID **)&bio);
      if (EFI_ERROR(stt) || !bio || !bio->Media) {
        continue;
      }
      if (!bio->Media->LogicalPartition || bio->Media->ReadOnly ||
          bio->Media->RemovableMedia) {
        continue;
      }

      UINT64 part_start = 0;
      UINT64 part_count = 0;
      if (get_partition_hint_from_handle(st, logical_handles[i], &part_start,
                                         &part_count) != 0) {
        continue;
      }
      if (part_start == best_data_start && part_count == best_data_count) {
        FreePool(logical_handles);
        *out_bio = bio;
        *out_data_start = 0;
        *out_data_count = best_data_count;
        *out_raw_bio = best;
        *out_raw_data_start = best_data_start;
        *out_raw_data_count = best_data_count;
        return EFI_SUCCESS;
      }
    }
    FreePool(logical_handles);
  }

  *out_bio = best;
  *out_data_start = best_data_start;
  *out_data_count = best_data_count;
  *out_raw_bio = best;
  *out_raw_data_start = best_data_start;
  *out_raw_data_count = best_data_count;
  return EFI_SUCCESS;
}


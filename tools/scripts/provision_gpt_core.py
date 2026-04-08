#!/usr/bin/env python3
"""Compatibility facade for GPT and disk provisioning helpers."""

from __future__ import annotations

from provision_gpt_disk import (
    SECTOR,
    VHD_FOOTER_SIZE,
    create_image,
    detect_vhd,
    parse_size_to_bytes,
    scrub_data_partition_for_first_boot,
    set_file_size,
)
from provision_gpt_layout import (
    CAPYOS_BOOT_GUID,
    align_up,
    build_gpt_header,
    gpt_part_name_bytes,
    guid_to_bytes_le,
    parse_gpt,
    partition_gpt,
    write_protective_mbr,
)

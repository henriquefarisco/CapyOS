/*
 * CapyFS runtime is split into implementation fragments while keeping a single
 * translation unit for private mount/inode helpers during the first cleanup.
 */

#include "runtime/prelude_ops.inc"
#include "runtime/format_mount.inc"
#include "runtime/file_io.inc"
#include "runtime/namespace_ops.inc"
#include "runtime/inode_block_alloc.inc"
#include "runtime/directory_entries.inc"

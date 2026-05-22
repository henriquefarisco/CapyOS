#ifndef BLOCK_H
#define BLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/storage/block_error.h"

typedef int (*block_read_fn)(void *ctx, uint32_t block_no, void *buffer);
typedef int (*block_write_fn)(void *ctx, uint32_t block_no, const void *buffer);

/* Extended variants (Slice 3E.2.B): return the classifier so the
 * upper layer can apply a unified retry budget. NULL means the
 * driver still uses the legacy 0/-1 path; the wrapper will treat
 * non-zero returns as `BLOCK_IO_ERR_PERMANENT`. */
typedef enum block_io_error_class (*block_read_ex_fn)(void *ctx,
                                                      uint32_t block_no,
                                                      void *buffer);
typedef enum block_io_error_class (*block_write_ex_fn)(void *ctx,
                                                       uint32_t block_no,
                                                       const void *buffer);

/* Reset hook (Slice 3E.2.B): the retry loop calls this once when it
 * sees a `BLOCK_IO_ERR_TIMEOUT` before issuing the single allowed
 * retry. The driver is expected to:
 *  - AHCI: emit COMRESET on the affected port and restart it.
 *  - NVMe: emit Abort for the wedged command and fall through to
 *    Controller Level Reset (CC.EN toggle) if Abort itself fails.
 * Returns 0 if the device is usable for one retry, -1 if the
 * driver gave up (in which case the retry is skipped). NULL when
 * the driver has no reset path; in that case TIMEOUT will not be
 * retried. */
typedef int (*block_reset_fn)(void *ctx);

struct block_device_ops {
    block_read_fn  read_block;
    block_write_fn write_block;
    block_read_ex_fn  read_block_ex;   /* optional */
    block_write_ex_fn write_block_ex;  /* optional */
    block_reset_fn    reset;           /* optional */
};

struct block_device {
    const char *name;
    uint32_t block_size;
    uint32_t block_count;
    void *ctx;                         // backend-specific pointer
    const struct block_device_ops *ops;
};

int block_device_read(struct block_device *dev, uint32_t block_no, void *buffer);
int block_device_write(struct block_device *dev, uint32_t block_no, const void *buffer);

/* Extended I/O with classifier-driven retry policy (Slice 3E.2.B).
 *
 * Retry budget mapped per class:
 *  - BLOCK_IO_OK:           0 retries (immediate success).
 *  - BLOCK_IO_ERR_TRANSIENT: up to 3 plain retries.
 *  - BLOCK_IO_ERR_TIMEOUT:   1 retry, preceded by `ops->reset(ctx)`
 *                            (skipped if `reset` is NULL or returns
 *                            -1, in which case the TIMEOUT becomes
 *                            PERMANENT for the caller).
 *  - BLOCK_IO_ERR_PERMANENT: 0 retries (caller surfaces).
 *  - BLOCK_IO_ERR_DEVICE_GONE: 0 retries (caller marks removed).
 *
 * If `ops->read_block_ex` (resp. `write_block_ex`) is NULL the
 * wrapper falls back to `ops->read_block` (resp. `write_block`)
 * and treats any nonzero return as `BLOCK_IO_ERR_PERMANENT`.
 *
 * The functions return the final class observed after the budget
 * is exhausted. The legacy `block_device_read`/`block_device_write`
 * wrap these and collapse the class into 0 / -1. */
enum block_io_error_class block_device_read_ex(struct block_device *dev,
                                               uint32_t block_no,
                                               void *buffer);
enum block_io_error_class block_device_write_ex(struct block_device *dev,
                                                uint32_t block_no,
                                                const void *buffer);

// Creates a logical device that exposes larger blocks by chunking lower devices.
// Returns a new heap-allocated device or NULL on failure.
struct block_device *block_chunked_wrap(struct block_device *lower, uint32_t chunk_size);

// Creates a logical view over a sub-range of a lower device, starting at
// LBA 'start_lba' and exposing 'lba_count' blocks with the SAME block_size
// as the lower device.
struct block_device *block_offset_wrap(struct block_device *lower, uint32_t start_lba, uint32_t lba_count);

#endif

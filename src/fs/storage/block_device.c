#include "fs/block.h"

#include <stddef.h>

#include "drivers/storage/block_error.h"

/* Slice 3E.2.B — retry budgets per class. The numbers come from
 * docs/architecture/etapa-3-slice-3e-plan.md §3.Slice 3E.2.B. */
#define BLOCK_RETRY_BUDGET_TRANSIENT 3
#define BLOCK_RETRY_BUDGET_TIMEOUT 1

static enum block_io_error_class block_call_read(struct block_device *dev,
                                                 uint32_t block_no,
                                                 void *buffer) {
    if (dev->ops->read_block_ex) {
        return dev->ops->read_block_ex(dev->ctx, block_no, buffer);
    }
    if (!dev->ops->read_block) {
        return BLOCK_IO_ERR_PERMANENT;
    }
    /* Legacy driver: cannot distinguish; non-zero is permanent. */
    return dev->ops->read_block(dev->ctx, block_no, buffer) == 0
               ? BLOCK_IO_OK
               : BLOCK_IO_ERR_PERMANENT;
}

static enum block_io_error_class block_call_write(struct block_device *dev,
                                                  uint32_t block_no,
                                                  const void *buffer) {
    if (dev->ops->write_block_ex) {
        return dev->ops->write_block_ex(dev->ctx, block_no, buffer);
    }
    if (!dev->ops->write_block) {
        return BLOCK_IO_ERR_PERMANENT;
    }
    return dev->ops->write_block(dev->ctx, block_no, buffer) == 0
               ? BLOCK_IO_OK
               : BLOCK_IO_ERR_PERMANENT;
}

/* Apply the unified retry policy. `is_write` selects which of the
 * two buffer slots is active; we keep the policy code flat (instead
 * of templating via callbacks) because the call sites are exactly
 * two and the policy is easier to audit this way. */
static enum block_io_error_class block_retry_loop(struct block_device *dev,
                                                  enum block_io_error_class cls,
                                                  int is_write,
                                                  uint32_t block_no,
                                                  void *buf_rw,
                                                  const void *buf_ro) {
    int budget_transient = BLOCK_RETRY_BUDGET_TRANSIENT;
    int budget_timeout = BLOCK_RETRY_BUDGET_TIMEOUT;
    int reset_attempted = 0;
    while (block_io_should_retry(cls)) {
        if (cls == BLOCK_IO_ERR_TIMEOUT) {
            if (budget_timeout <= 0) {
                return cls;
            }
            budget_timeout--;
            /* Try to recover via driver reset before the single
             * permitted retry. If the driver has no reset path or
             * the reset itself fails, surface the timeout as
             * permanent so the caller does not loop forever. */
            if (!dev->ops->reset || reset_attempted ||
                dev->ops->reset(dev->ctx) != 0) {
                return BLOCK_IO_ERR_PERMANENT;
            }
            reset_attempted = 1;
        } else { /* TRANSIENT */
            if (budget_transient <= 0) {
                return cls;
            }
            budget_transient--;
        }
        cls = is_write ? block_call_write(dev, block_no, buf_ro)
                       : block_call_read(dev, block_no, buf_rw);
    }
    return cls;
}

enum block_io_error_class block_device_read_ex(struct block_device *dev,
                                               uint32_t block_no,
                                               void *buffer) {
    enum block_io_error_class cls;
    if (!dev || !dev->ops || !buffer) {
        return BLOCK_IO_ERR_PERMANENT;
    }
    if (block_no >= dev->block_count) {
        return BLOCK_IO_ERR_PERMANENT;
    }
    cls = block_call_read(dev, block_no, buffer);
    return block_retry_loop(dev, cls, /*is_write=*/0, block_no, buffer, NULL);
}

enum block_io_error_class block_device_write_ex(struct block_device *dev,
                                                uint32_t block_no,
                                                const void *buffer) {
    enum block_io_error_class cls;
    if (!dev || !dev->ops || !buffer) {
        return BLOCK_IO_ERR_PERMANENT;
    }
    if (block_no >= dev->block_count) {
        return BLOCK_IO_ERR_PERMANENT;
    }
    cls = block_call_write(dev, block_no, buffer);
    return block_retry_loop(dev, cls, /*is_write=*/1, block_no, NULL, buffer);
}

int block_device_read(struct block_device *dev, uint32_t block_no, void *buffer) {
    if (!dev || !dev->ops) {
        return -1;
    }
    /* Legacy ABI fast-path: drivers that did not opt in to the
     * extended dispatch keep their original 0/-1 behaviour with no
     * retry overhead. Drivers that DO expose read_block_ex go
     * through the unified retry policy. */
    if (!dev->ops->read_block_ex) {
        if (!dev->ops->read_block || block_no >= dev->block_count) {
            return -1;
        }
        return dev->ops->read_block(dev->ctx, block_no, buffer);
    }
    return block_device_read_ex(dev, block_no, buffer) == BLOCK_IO_OK ? 0 : -1;
}

int block_device_write(struct block_device *dev, uint32_t block_no, const void *buffer) {
    if (!dev || !dev->ops) {
        return -1;
    }
    if (!dev->ops->write_block_ex) {
        if (!dev->ops->write_block || block_no >= dev->block_count) {
            return -1;
        }
        return dev->ops->write_block(dev->ctx, block_no, buffer);
    }
    return block_device_write_ex(dev, block_no, buffer) == BLOCK_IO_OK ? 0 : -1;
}

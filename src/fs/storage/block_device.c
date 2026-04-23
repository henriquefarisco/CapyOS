#include "fs/block.h"

int block_device_read(struct block_device *dev, uint32_t block_no, void *buffer) {
    if (!dev || !dev->ops || !dev->ops->read_block) {
        return -1;
    }
    if (block_no >= dev->block_count) {
        return -1;
    }
    return dev->ops->read_block(dev->ctx, block_no, buffer);
}

int block_device_write(struct block_device *dev, uint32_t block_no, const void *buffer) {
    if (!dev || !dev->ops || !dev->ops->write_block) {
        return -1;
    }
    if (block_no >= dev->block_count) {
        return -1;
    }
    return dev->ops->write_block(dev->ctx, block_no, buffer);
}

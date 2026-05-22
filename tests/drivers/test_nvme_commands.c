/* Host tests for NVMe command builders (Slice 3E.1).
 *
 * Pure exercises of src/drivers/nvme/nvme_commands.c — no MMIO,
 * no kernel state, runnable under the standard host runner.
 */

#include "drivers/nvme/nvme_commands.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[nvme-commands] FAIL: %s\n", msg);
    g_failures++;
}

static void zero_cmd(struct nvme_sqe *cmd) {
    memset(cmd, 0xAA, sizeof(*cmd));
}

static void test_identify_ctrl_rejects_invalid(void) {
    struct nvme_sqe cmd;
    uint8_t buf[16];
    if (nvme_build_identify_ctrl_cmd(NULL, buf) != -1) {
        fail("identify_ctrl must reject NULL cmd");
    }
    if (nvme_build_identify_ctrl_cmd(&cmd, NULL) != -1) {
        fail("identify_ctrl must reject NULL buffer");
    }
}

static void test_identify_ctrl_layout(void) {
    struct nvme_sqe cmd;
    uint8_t buf[4096];
    zero_cmd(&cmd);
    if (nvme_build_identify_ctrl_cmd(&cmd, buf) != 0) {
        fail("identify_ctrl must succeed on valid input");
        return;
    }
    if (cmd.opcode != NVME_ADMIN_IDENTIFY) fail("opcode must be IDENTIFY");
    if (cmd.nsid != 0u) fail("nsid must be 0 for controller identify");
    if (cmd.prp1 != (uint64_t)(uintptr_t)buf) {
        fail("prp1 must point at the identify buffer");
    }
    if (cmd.cdw10 != NVME_IDENTIFY_CNS_CONTROLLER) {
        fail("cdw10 must be CNS=01h (controller)");
    }
    if (cmd.flags != 0u || cmd.cdw11 != 0u || cmd.cdw12 != 0u) {
        fail("unused fields must be zeroed by builder");
    }
}

static void test_identify_ns_rejects_invalid(void) {
    struct nvme_sqe cmd;
    uint8_t buf[16];
    if (nvme_build_identify_ns_cmd(NULL, buf, 1) != -1) {
        fail("identify_ns must reject NULL cmd");
    }
    if (nvme_build_identify_ns_cmd(&cmd, NULL, 1) != -1) {
        fail("identify_ns must reject NULL buffer");
    }
    if (nvme_build_identify_ns_cmd(&cmd, buf, 0) != -1) {
        fail("identify_ns must reject nsid == 0 (reserved)");
    }
}

static void test_identify_ns_layout(void) {
    struct nvme_sqe cmd;
    uint8_t buf[4096];
    zero_cmd(&cmd);
    if (nvme_build_identify_ns_cmd(&cmd, buf, 0xCAFEu) != 0) {
        fail("identify_ns must succeed on valid input");
        return;
    }
    if (cmd.opcode != NVME_ADMIN_IDENTIFY) fail("opcode must be IDENTIFY");
    if (cmd.nsid != 0xCAFEu) fail("nsid must be propagated");
    if (cmd.cdw10 != NVME_IDENTIFY_CNS_NAMESPACE) {
        fail("cdw10 must be CNS=00h (namespace)");
    }
}

static void test_create_cq_rejects_invalid(void) {
    struct nvme_sqe cmd;
    uint8_t buf[16];
    if (nvme_build_create_cq_cmd(&cmd, buf, 0, 64) != -1) {
        fail("create_cq must reject qid == 0 (reserved for admin)");
    }
    if (nvme_build_create_cq_cmd(&cmd, buf, 1, 0) != -1) {
        fail("create_cq must reject qsize == 0");
    }
}

static void test_create_cq_layout(void) {
    struct nvme_sqe cmd;
    uint8_t buf[4096];
    zero_cmd(&cmd);
    if (nvme_build_create_cq_cmd(&cmd, buf, 1u, 64u) != 0) {
        fail("create_cq must succeed on valid input");
        return;
    }
    if (cmd.opcode != NVME_ADMIN_CREATE_IOCQ) {
        fail("opcode must be CREATE_IOCQ");
    }
    if (cmd.prp1 != (uint64_t)(uintptr_t)buf) {
        fail("prp1 must point at the CQ buffer");
    }
    /* qsize=64 → qsize-1=63, QID=1. */
    if (cmd.cdw10 != ((63u << 16) | 1u)) {
        fail("cdw10 must encode QSIZE-1 in high half and QID in low half");
    }
    if ((cmd.cdw11 & NVME_CREATE_QUEUE_PC_BIT) == 0u) {
        fail("PC bit must be set in cdw11");
    }
}

static void test_create_sq_layout(void) {
    struct nvme_sqe cmd;
    uint8_t buf[4096];
    zero_cmd(&cmd);
    if (nvme_build_create_sq_cmd(&cmd, buf, 1u, 64u, 1u) != 0) {
        fail("create_sq must succeed on valid input");
        return;
    }
    if (cmd.opcode != NVME_ADMIN_CREATE_IOSQ) {
        fail("opcode must be CREATE_IOSQ");
    }
    /* cdw11[31:16] = CQID, cdw11[0] = PC. */
    if ((cmd.cdw11 >> 16) != 1u) fail("cdw11 high half must hold CQID");
    if ((cmd.cdw11 & NVME_CREATE_QUEUE_PC_BIT) == 0u) {
        fail("PC bit must be set in cdw11");
    }
}

static void test_rw_rejects_invalid(void) {
    struct nvme_sqe cmd;
    uint8_t buf[16];
    if (nvme_build_rw_cmd(&cmd, NVME_CMD_FLUSH, 1, 0, 1, buf) != -1) {
        fail("rw must reject unsupported opcode");
    }
    if (nvme_build_rw_cmd(&cmd, NVME_CMD_READ, 0, 0, 1, buf) != -1) {
        fail("rw must reject nsid == 0");
    }
    if (nvme_build_rw_cmd(&cmd, NVME_CMD_READ, 1, 0, 0, buf) != -1) {
        fail("rw must reject block_count == 0");
    }
    if (nvme_build_rw_cmd(&cmd, NVME_CMD_READ, 1, 0, 0x10001u, buf) != -1) {
        fail("rw must reject block_count > 65536");
    }
}

static void test_rw_layout_read(void) {
    struct nvme_sqe cmd;
    uint8_t buf[4096];
    uint64_t lba = 0x0000ABCD12345678ull;
    zero_cmd(&cmd);
    if (nvme_build_rw_cmd(&cmd, NVME_CMD_READ, 0x42u, lba, 8u, buf) != 0) {
        fail("rw must succeed on valid READ");
        return;
    }
    if (cmd.opcode != NVME_CMD_READ) fail("opcode must be READ");
    if (cmd.nsid != 0x42u) fail("nsid must be propagated");
    if (cmd.prp1 != (uint64_t)(uintptr_t)buf) {
        fail("prp1 must point at data buffer");
    }
    if (cmd.cdw10 != 0x12345678u) fail("cdw10 must hold LBA[31:0]");
    if (cmd.cdw11 != 0x0000ABCDu) fail("cdw11 must hold LBA[63:32]");
    /* block_count=8 → NLB=7. */
    if ((cmd.cdw12 & 0xFFFFu) != 7u) fail("cdw12[15:0] must hold NLB (count-1)");
}

static void test_rw_layout_write_max_count(void) {
    struct nvme_sqe cmd;
    uint8_t buf[4096];
    if (nvme_build_rw_cmd(&cmd, NVME_CMD_WRITE, 1, 0, 0x10000u, buf) != 0) {
        fail("rw must accept block_count == 65536");
        return;
    }
    if (cmd.opcode != NVME_CMD_WRITE) fail("opcode must be WRITE");
    /* NLB encoding for 65536 blocks is 0xFFFF (count - 1 truncated to 16). */
    if ((cmd.cdw12 & 0xFFFFu) != 0xFFFFu) {
        fail("NLB must wrap to 0xFFFF for block_count == 65536");
    }
}

int run_nvme_commands_tests(void) {
    g_failures = 0;
    test_identify_ctrl_rejects_invalid();
    test_identify_ctrl_layout();
    test_identify_ns_rejects_invalid();
    test_identify_ns_layout();
    test_create_cq_rejects_invalid();
    test_create_cq_layout();
    test_create_sq_layout();
    test_rw_rejects_invalid();
    test_rw_layout_read();
    test_rw_layout_write_max_count();
    if (g_failures == 0) printf("[tests] nvme_commands OK\n");
    return g_failures;
}

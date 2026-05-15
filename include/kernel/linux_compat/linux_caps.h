#ifndef KERNEL_LINUX_COMPAT_LINUX_CAPS_H
#define KERNEL_LINUX_COMPAT_LINUX_CAPS_H

/* Linux ABI capability syscalls.
 *
 *   int capget(cap_user_header_t hdrp, cap_user_data_t datap);
 *   int capset(cap_user_header_t hdrp, const cap_user_data_t datap);
 *
 * Why this matters for the Firefox port:
 *   - Firefox's sandbox layer calls capset() to drop ALL
 *     capabilities (effective = permitted = inheritable = 0)
 *     before exec'ing content processes. -ENOSYS makes the
 *     sandbox fail-closed and refuse to start the renderer.
 *   - musl's `cap_get_proc()` -> capget probes for capability
 *     support; libcap-based code paths gracefully degrade if
 *     -ENOSYS but still emit warnings.
 *   - bubblewrap, firejail and friends all start with a capget
 *     to determine the current set before deciding what to drop.
 *
 * Linux capability data structure (from <linux/capability.h>):
 *
 *   struct cap_user_header {
 *       __u32 version;   // _LINUX_CAPABILITY_VERSION_3 = 0x20080522
 *       int   pid;       // 0 = self
 *   };
 *   struct cap_user_data {
 *       __u32 effective;
 *       __u32 permitted;
 *       __u32 inheritable;
 *   };
 *   // For VERSION_3: caller must provide an array of 2 cap_user_data
 *   // structs to cover the 64-bit cap mask.
 *
 * Marco M1 runs as root with all capabilities. We mirror Linux:
 *   - capget on self (pid == 0 || pid == getpid()) returns
 *     effective = permitted = inheritable = ~0 (all caps).
 *   - capset accepts any well-formed value (we have CAP_SETPCAP
 *     implicitly as root) and discards it (no per-task storage
 *     yet). When per-task creds land this stores via the
 *     provider injected via `linux_caps_install_ops`. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_CAP_VERSION_1   0x19980330u
#define LINUX_CAP_VERSION_2   0x20071026u
#define LINUX_CAP_VERSION_3   0x20080522u

/* The kernel auto-corrects unknown versions by overwriting
 * hdrp->version with the preferred one and returning -EINVAL.
 * Marco M1 prefers VERSION_3. */
#define LINUX_CAP_PREFERRED_VERSION   LINUX_CAP_VERSION_3

struct linux_cap_user_header {
    uint32_t version;
    int32_t  pid;
};

struct linux_cap_user_data {
    uint32_t effective;
    uint32_t permitted;
    uint32_t inheritable;
};

/* Number of cap_user_data entries that VERSION_3 expects. */
#define LINUX_CAP_DATA_ENTRIES        2

struct linux_caps_ops {
    /* Optional callbacks for per-task storage. NULL = caller
     * falls back to "root with all caps" / no-op set. */
    int64_t (*get_caps)(int pid, struct linux_cap_user_data *out_arr,
                        int n_entries);
    int64_t (*set_caps)(int pid, const struct linux_cap_user_data *in_arr,
                        int n_entries);
};

void linux_caps_install_ops(const struct linux_caps_ops *ops);
void linux_caps_reset_for_tests(void);

int64_t linux_capget(struct linux_cap_user_header *hdrp,
                     struct linux_cap_user_data *datap);
int64_t linux_capset(struct linux_cap_user_header *hdrp,
                     const struct linux_cap_user_data *datap);

void linux_caps_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_CAPS_H */

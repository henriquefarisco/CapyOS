#ifndef KERNEL_LINUX_COMPAT_LINUX_CHDIR_H
#define KERNEL_LINUX_COMPAT_LINUX_CHDIR_H

/* Linux ABI working-directory mutation syscalls.
 *
 *   int chdir (const char *path);
 *   int fchdir(int fd);
 *
 * Why this matters for the Firefox port:
 *   - Firefox profile setup chdir's into `~/.mozilla/firefox/<id>`
 *     before calling components that use relative paths
 *     (extensions, prefs.js).
 *   - Bash and ./configure scripts chdir constantly; -ENOSYS
 *     makes them lose track of "where am I" and pick wrong
 *     defaults.
 *
 * Marco M1 has no per-task working-directory tracking yet (the
 * native CapyOS process model assumes everything is rooted at "/"
 * for the ELF loader). To unblock userland we accept chdir/fchdir
 * with full Linux validation and delegate to a provider; without
 * a provider the call returns -ENOSYS so userland sees a
 * deterministic answer rather than silently succeeding into an
 * inconsistent state.
 *
 * Provider shape: a tiny key/value indirection where userland's
 * notion of cwd is stored externally (today: nowhere; future:
 * tmpfs root). chdir hands the path off, fchdir hands the fd. */

#include <stdint.h>
#include <stddef.h>

struct linux_chdir_ops {
    int64_t (*chdir_path)(const char *path);
    int64_t (*chdir_fd)  (int fd);
};

void linux_chdir_install_ops(const struct linux_chdir_ops *ops);
void linux_chdir_reset_for_tests(void);

int64_t linux_chdir (const char *path);
int64_t linux_fchdir(int fd);

void linux_chdir_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_CHDIR_H */

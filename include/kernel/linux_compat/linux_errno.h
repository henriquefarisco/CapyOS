#ifndef KERNEL_LINUX_COMPAT_LINUX_ERRNO_H
#define KERNEL_LINUX_COMPAT_LINUX_ERRNO_H

/* Linux errno values returned to userland under Strategy A.
 *
 * Linux syscalls return errors as the negation of an errno value
 * (e.g. -EINVAL == -22). musl's syscall stubs translate that into
 * `errno = 22; return -1;` for the C-level wrapper. Kernel handlers
 * therefore return the negative of one of these constants.
 *
 * Only the errno values we actually emit today are listed; the set
 * grows as more handlers are wired. Keep the numerical values in
 * sync with `include/uapi/asm-generic/errno-base.h` upstream, which
 * is the authoritative arch-generic Linux source.
 */

#define LINUX_EPERM     1   /* Operation not permitted             */
#define LINUX_ENOENT    2   /* No such file or directory           */
#define LINUX_ESRCH     3   /* No such process                     */
#define LINUX_EINTR     4   /* Interrupted system call             */
#define LINUX_EIO       5   /* I/O error                           */
#define LINUX_ENXIO     6   /* No such device or address           */
#define LINUX_E2BIG     7   /* Argument list too long              */
#define LINUX_ENOEXEC   8   /* Exec format error                   */
#define LINUX_EBADF     9   /* Bad file descriptor                 */
#define LINUX_ECHILD    10  /* No child processes                  */
#define LINUX_EAGAIN    11  /* Try again / would block             */
#define LINUX_ENOMEM    12  /* Out of memory                       */
#define LINUX_EACCES    13  /* Permission denied                   */
#define LINUX_EFAULT    14  /* Bad address                         */
#define LINUX_EBUSY     16  /* Device or resource busy             */
#define LINUX_EEXIST    17  /* File exists                         */
#define LINUX_ENODEV    19  /* No such device                      */
#define LINUX_ENOTDIR   20  /* Not a directory                     */
#define LINUX_EISDIR    21  /* Is a directory                      */
#define LINUX_EINVAL    22  /* Invalid argument                    */
#define LINUX_ENFILE    23  /* File table overflow                 */
#define LINUX_EMFILE    24  /* Too many open files                 */
#define LINUX_ENOTTY    25  /* Not a typewriter                    */
#define LINUX_ETXTBSY   26  /* Text file busy                      */
#define LINUX_EFBIG     27  /* File too large                      */
#define LINUX_ENOSPC    28  /* No space left on device             */
#define LINUX_ESPIPE    29  /* Illegal seek                        */
#define LINUX_EROFS     30  /* Read-only file system               */
#define LINUX_EMLINK    31  /* Too many links                      */
#define LINUX_EPIPE     32  /* Broken pipe                         */
#define LINUX_EDOM      33  /* Math argument out of domain         */
#define LINUX_ERANGE    34  /* Math result not representable       */

/* asm-generic/errno.h next ranges. */
#define LINUX_ENAMETOOLONG 36 /* File name too long                */
#define LINUX_ENOLCK    37  /* No record locks available           */
#define LINUX_ENOSYS    38  /* Function not implemented            */
#define LINUX_ENOTEMPTY 39  /* Directory not empty                 */
#define LINUX_ELOOP     40  /* Too many symbolic links encountered */
#define LINUX_ENOMSG    42  /* No message of desired type          */
#define LINUX_ENODATA   61  /* No data available (xattr missing)   */
#define LINUX_EOVERFLOW 75  /* Value too large for defined type    */

/* asm-generic/errno.h networking range. */
#define LINUX_EBADMSG       74
#define LINUX_ENOTSOCK      88  /* Socket operation on non-socket  */
#define LINUX_EDESTADDRREQ  89  /* Destination address required    */
#define LINUX_EMSGSIZE      90  /* Message too long                */
#define LINUX_EPROTOTYPE    91  /* Protocol wrong type for socket  */
#define LINUX_ENOPROTOOPT   92  /* Protocol not available          */
#define LINUX_EPROTONOSUPPORT 93 /* Protocol not supported         */
#define LINUX_ESOCKTNOSUPPORT 94 /* Socket type not supported      */
#define LINUX_EOPNOTSUPP    95  /* Operation not supported         */
#define LINUX_EAFNOSUPPORT  97  /* AF not supported by protocol    */
#define LINUX_EADDRINUSE    98
#define LINUX_EADDRNOTAVAIL 99
#define LINUX_ENETDOWN      100
#define LINUX_ENETUNREACH   101
#define LINUX_ECONNABORTED  103
#define LINUX_ECONNRESET    104
#define LINUX_ENOBUFS       105
#define LINUX_EISCONN       106
#define LINUX_ENOTCONN      107
#define LINUX_ETIMEDOUT     110 /* Connection timed out            */
#define LINUX_ECONNREFUSED  111

/* Misc. */
#define LINUX_ECANCELED 125

#endif /* KERNEL_LINUX_COMPAT_LINUX_ERRNO_H */

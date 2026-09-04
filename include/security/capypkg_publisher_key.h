#ifndef SECURITY_CAPYPKG_PUBLISHER_KEY_H
#define SECURITY_CAPYPKG_PUBLISHER_KEY_H

/* Public half of the offline CapyPKG publisher key. The private half lives
 * outside every Git repository in CAPYOS_RELEASE_KEYS. */
#define CAPYPKG_PUBLISHER_PUBLIC_KEY_BYTES                              \
    {0x1c, 0x52, 0xcc, 0x62, 0xac, 0x20, 0xb9, 0x4b,                  \
     0xb0, 0xc6, 0x42, 0x91, 0xb6, 0xcd, 0xc5, 0x45,                  \
     0x3b, 0x9a, 0xee, 0x53, 0x39, 0x2a, 0x25, 0xed,                  \
     0x27, 0x99, 0xfc, 0x7e, 0x4a, 0x71, 0x8e, 0xf2}

#define CAPYPKG_PUBLISHER_PUBLIC_KEY_HEX \
    "1c52cc62ac20b94bb0c64291b6cdc5453b9aee53392a25ed2799fc7e4a718ef2"

#endif

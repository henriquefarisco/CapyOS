#ifndef DRIVERS_NET_EFI_SNP_H
#define DRIVERS_NET_EFI_SNP_H

#include <stdint.h>

int efi_snp_probe(uint8_t mac[6], uint16_t *mtu);
int efi_snp_init(uint8_t mac[6], uint16_t *mtu);
int efi_snp_ready(void);
int efi_snp_send_frame(const uint8_t *frame, uint16_t len);
int efi_snp_poll_frame(uint8_t *out, uint16_t cap, uint16_t *len);

#endif /* DRIVERS_NET_EFI_SNP_H */

# CapyOS — inventário de superfície de ataque

Áreas que processam **input não-confiável** (rede, disco, pacote, binário) ou
manipulam **segredos**. É o mapa da auditoria de segurança recorrente: cada área
tem o owner, a cobertura de teste host e o status da última auditoria. Atualize
ao auditar (e registre o achado em [`audit-log.md`](audit-log.md)).

Processo e checklist por área: [`audit-playbook.md`](audit-playbook.md).
Regressão focada: `make security-selftest`.

| Área | Input não-confiável | Owner (arquivo) | Teste host | Última auditoria | Status |
|---|---|---|---|---|---|
| Verifier de assinatura de pacote | descritor + assinatura do repo | `src/services/capypkg/capypkg_signature.c` | `run_capypkg_tests` (signature.inc) | 2026-06-17 | **Auditado, limpo** |
| Gate de install (descritor canônico) | entrada do repo | `src/services/capypkg/capypkg_install.c` | `run_capypkg_tests` | 2026-06-17 | **Auditado, limpo** |
| Parser de manifesto capypkg | manifesto do repo | `src/services/capypkg/capypkg_manifest.c` | `run_capypkg_tests` | 2026-06-17 | **Auditado, limpo** |
| Header/crypto de volume | header on-disk (atacante c/ disco) | `src/security/volume_header.c`, `volume_provider.c` | `run_volume_header_tests`, `run_volume_provider*` | 2026-06-17 | **Auditado, limpo** |
| Parser DNS (resposta) | resposta de rede | `src/net/services/dns.c` | `run_net_dns_tests` | 2026-06-17 | **Auditado, limpo** |
| Parser de opções DHCP | resposta de rede | `src/net/core/dhcp_options.c` | `run_net_dhcp_options_tests` | 2026-06-17 | **Auditado, limpo** |
| ELF loader | binário não-confiável | `src/kernel/elf_loader.c`, `include/kernel/elf_bounds.h` | `run_elf_bounds_tests` | 2026-06-17 | **Auditado + fix (alpha.282)** |
| Parser ICMP | pacote de rede | `src/net/...` (icmp) | `run_net_icmp_tests` | — | Pendente |
| Parser ARP | pacote de rede | `src/net/...` (arp) | `run_net_arp_tests` | — | Pendente |
| CAPYFS (parse/fsck) | filesystem on-disk | `src/fs/...`, `capyfs_check` | `run_capyfs_check_tests`, `run_fsck_geometry_tests` | — | Pendente |
| TLS / X.509 (userland) | handshake de rede | `third_party/bearssl` (+ seam `capy_tls`) | `test_capylibc_tls` | — | Pendente (core BearSSL é vendido/auditado upstream) |
| HTTP parse (URL/headers/chunked) | resposta de rede | `userland/.../http_*` | `run_http_*_tests` | — | Pendente |
| HTML-to-text | conteúdo web | `CapyBrowser` (sister) | sister-side | — | Fora do CapyOS (auditar no sister) |

## Próxima rodada recomendada
CAPYFS (parse/fsck) e ICMP/ARP — as superfícies não-confiáveis ainda não
auditadas que são CapyOS-owned.

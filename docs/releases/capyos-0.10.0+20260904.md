# CapyOS 0.10.0+20260904

Versão: `0.10.0+20260904`. Release stable de fechamento da Etapa 9.

Entrega o índice resolve-at-publish assinado por Ed25519 e vinculado ao ABI
`capyos-base-v3`, package manager com aplicação atômica e rollback, persistência,
CLI `pkg`, Software Center e SDK inicial de pacotes. Integra as releases
imutáveis dos sete produtores e preserva a URL canônica de cada payload.

A candidata alpha.2 foi promovida de forma imutável para provar o pacote, mas o
gate VMware de produção recusou corretamente seu identificador prerelease no
canal stable. Esta versão sem sufixo prerelease é a promoção de produção.

## Evidência de produção

- ISO publicada SHA-256: `4da49439261a71fdd53baed26da561c7f72e6297724a1dd566d1ad3dc317be21`.
- Payload publicado SHA-256: `1c3d3d9a72c766755a5bbe54f608b750cf1b51a7fca411760e06ddd1fd6d6d7e`.
- A release foi promovida como Latest com 12 assets assinados e tornou-se
  imutável.
- O gate VMware Workstation de produção `51a809975d62` partiu da ISO pública
  `0.9.2+20260826` e aprovou quatro boots na ordem rollback-then-confirm,
  incluindo download pela rota pública Latest, verificação Ed25519, escrita no
  slot inativo, consumo da tentativa, rollback, reaplicação pelo cache
  verificado, confirmação de saúde e recusa de versão igual.

#ifndef GRUB_CFG_BUILDER_H
#define GRUB_CFG_BUILDER_H

#include <stddef.h>

struct grub_menu_entry {
    const char *title;
    const char *multiboot_path;
};

struct grub_cfg_options {
    int timeout_seconds;
    int default_entry;
    int enable_serial;
    unsigned int serial_baud;
    int force_text_mode;
};

/**
 * Renderiza um grub.cfg simples com foco em ambientes que não oferecem VESA
 * (ex.: Hyper-V). Força console de texto e permite habilitar saída serial
 * como fallback adicional.
 *
 * Retorna 0 em sucesso, -1 se algum argumento for inválido ou o buffer
 * não comportar o resultado.
 */
int grub_cfg_build(const struct grub_cfg_options *opts,
                   const struct grub_menu_entry *entries,
                   size_t entry_count,
                   char *out,
                   size_t out_size);

/**
 * Gera o conteúdo do grub.cfg e persiste em disco no caminho informado.
 * Retorna 0 em sucesso, -1 em falha.
 */
int grub_cfg_write_file(const char *path,
                        const struct grub_cfg_options *opts,
                        const struct grub_menu_entry *entries,
                        size_t entry_count);

#endif

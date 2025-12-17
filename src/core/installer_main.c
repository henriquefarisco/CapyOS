#include <stddef.h>
#include <stdint.h>

#include "arch/x86/cpu/gdt.h"
#include "arch/x86/cpu/idt.h"
#include "arch/x86/cpu/isr.h"
#include "arch/x86/hw/io.h"
#include "drivers/timer/pit.h"
#include "drivers/video/vga.h"
#include "drivers/input/keyboard.h"
#include "drivers/console/tty.h"

#include "memory/kmem.h"

#include "fs/block.h"
#include "fs/ramdisk.h"
#include "fs/buffer.h"
#include "fs/vfs.h"
#include "fs/noirfs.h"
#include "fs/storage/partition.h"

#include "security/crypt.h"

#include "boot/boot_writer.h"

#include "core/system_init.h"

// Noir Guided Installation System — instalador dedicado

struct dev_choice { struct block_device *dev; const char *name; uint64_t bytes; };

static struct super_block root_sb;

static const uint8_t g_disk_salt[16] = {
    0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53, 0x2d, 0x46,
    0x53, 0x2d, 0x53, 0x61, 0x6c, 0x74, 0x21, 0x00
};
static const uint32_t g_kdf_iterations = 16000;
static const uint32_t NOIRFS_DATA_MIN_SECTORS = 32768u; // ~16MiB em setores de 512B

static void memzero(void *ptr, size_t len) { volatile uint8_t *p=(volatile uint8_t*)ptr; while(len--) *p++=0; }

static size_t utoa10(uint32_t v, char *dst){ char t[10]; size_t n=0; if(!v){ dst[0]='0'; return 1;} while(v&&n<sizeof(t)){ t[n++]=(char)('0'+(v%10)); v/=10;} for(size_t i=0;i<n;++i) dst[i]=t[n-1-i]; return n; }

static int mount_noirfs_root(struct block_device *crypt_dev) {
    if (mount_noirfs(crypt_dev, &root_sb) != 0) return -1;
    if (vfs_mount_root(&root_sb) != 0) return -1;
    vga_write("NoirFS montado em / (dados cifrados)\n");
    return 0;
}

static int format_progress_complete = 0;
static void format_progress(const char *stage, uint32_t percent) {
    if (!stage) stage = "";
    if (percent > 100) percent = 100;
    char numbuf[10]; size_t numlen = utoa10(percent, numbuf); numbuf[numlen] = '\0';
    vga_write("Formatacao: "); vga_write(numbuf); vga_write("% "); vga_write(stage); vga_newline();
     /* menor spin para evitar longas esperas ocupando CPU e "congelando" a saída VGA em emuladores
         mantém um pequeno atraso visual entre atualizações de progresso, sem travar tanto o sistema */
     for (volatile uint32_t spin = 0; spin < 20000; ++spin) { __asm__ volatile(""); }
    if (percent >= 100) format_progress_complete = 1;
}

static int format_and_mount(struct block_device *crypt_dev) {
    vga_write("NoirFS indisponivel. Iniciando formatacao...\n");
    format_progress_complete = 0;
    int fmt = noirfs_format(crypt_dev, 128, crypt_dev->block_count, format_progress);
    if (!format_progress_complete) vga_write("\n");
    if (fmt != 0) { vga_write("Falha ao formatar NoirFS\n"); return -1; }
    if (mount_noirfs_root(crypt_dev) != 0) { vga_write("Falha ao montar NoirFS apos formatacao\n"); return -1; }
    return 0;
}

static void human_size(uint64_t bytes, char *buf, size_t buflen){
    const char *units[] = {"B","KiB","MiB","GiB","TiB"};
    int ui = 0;
    int shift = 0;
    while (ui < 4 && (bytes >> (shift + 10)) > 0) { ui++; shift += 10; }

    uint64_t whole = (shift == 0) ? bytes : (bytes >> shift);
    uint64_t rem   = (shift == 0) ? 0     : (bytes & ((1ULL << shift) - 1));
    uint64_t frac  = (shift == 0) ? 0     : ((rem * 100ULL) >> shift);

    char tmp[32];
    int ti = 0;
    if (!whole) {
        tmp[ti++] = '0';
    } else {
        char r[32];
        int ri = 0;
        uint32_t t = (uint32_t)whole;
        while (t) { r[ri++] = (char)('0' + (t % 10u)); t /= 10u; }
        while (ri) { tmp[ti++] = r[--ri]; }
    }
    tmp[ti] = '\0';

    size_t i = 0, j = 0;
    while (tmp[j] && i + 1 < buflen) buf[i++] = tmp[j++];
    if (ui > 0 && i + 1 < buflen) {
        buf[i++] = '.';
        uint32_t f = (uint32_t)frac;
        char d1 = (char)('0' + ((f / 10u) % 10u));
        char d2 = (char)('0' + (f % 10u));
        if (i + 1 < buflen) buf[i++] = d1;
        if (i + 1 < buflen) buf[i++] = d2;
    }
    if (i + 1 < buflen) {
        buf[i++] = ' ';
    }
    const char *u = units[ui];
    j = 0;
    while (u[j] && i + 1 < buflen) buf[i++] = u[j++];
    buf[i] = '\0';
}

static void copy_string(char *dst, size_t dst_len, const char *src){
    if (!dst || dst_len == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i < dst_len - 1) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static int wizard_prompt_number(const char *label, uint32_t *out_val, uint32_t min, uint32_t max, uint32_t def){
    char buf[128];
    while(1){ tty_set_prompt(label); tty_set_echo(1); tty_show_prompt(); size_t l=tty_readline(buf,sizeof(buf)); if(l==0){ *out_val=def; return 0;} uint32_t v=0; int ok=1; for(size_t i=0;i<l;i++){ char c=buf[i]; if(c<'0'||c>'9'){ ok=0; break;} v=v*10+(uint32_t)(c-'0'); } if(!ok){ *out_val=def; return 0;} if(v<min||v>max){ vga_write("Valor fora do intervalo.\n"); continue;} *out_val=v; return 0; }
}

static int write_mbr_with_partitions(struct block_device *dev,
                                     uint32_t boot_start_lba,
                                     uint32_t boot_sectors,
                                     uint32_t data_start_lba,
                                     uint32_t data_sectors){
    uint8_t mbr[512]; for(size_t i=0;i<512;i++) mbr[i]=0;
    uint8_t *p1=&mbr[446]; p1[0]=0x00; p1[1]=0xFE; p1[2]=0xFF; p1[3]=0xFF; p1[4]=0xDA; p1[5]=0xFE; p1[6]=0xFF; p1[7]=0xFF;
    p1[8]=(uint8_t)(boot_start_lba&0xFF); p1[9]=(uint8_t)((boot_start_lba>>8)&0xFF); p1[10]=(uint8_t)((boot_start_lba>>16)&0xFF); p1[11]=(uint8_t)((boot_start_lba>>24)&0xFF);
    p1[12]=(uint8_t)(boot_sectors&0xFF);   p1[13]=(uint8_t)((boot_sectors>>8)&0xFF);   p1[14]=(uint8_t)((boot_sectors>>16)&0xFF);   p1[15]=(uint8_t)((boot_sectors>>24)&0xFF);
    uint8_t *p2=&mbr[462]; p2[0]=0x00; p2[1]=0xFE; p2[2]=0xFF; p2[3]=0xFF; p2[4]=0x83; p2[5]=0xFE; p2[6]=0xFF; p2[7]=0xFF;
    p2[8]=(uint8_t)(data_start_lba&0xFF); p2[9]=(uint8_t)((data_start_lba>>8)&0xFF); p2[10]=(uint8_t)((data_start_lba>>16)&0xFF); p2[11]=(uint8_t)((data_start_lba>>24)&0xFF);
    p2[12]=(uint8_t)(data_sectors&0xFF);   p2[13]=(uint8_t)((data_sectors>>8)&0xFF);   p2[14]=(uint8_t)((data_sectors>>16)&0xFF);   p2[15]=(uint8_t)((data_sectors>>24)&0xFF);
    mbr[510]=0x55; mbr[511]=0xAA;
    if(block_device_write(dev,0,mbr)!=0){ vga_write("[mbr] write sector0 failed\n"); return -1; }
    return 0;
}

static int create_boot_and_data_partitions(struct block_device *dev,
                                           uint32_t boot_mb,
                                           uint32_t *out_boot_lba,
                                           uint32_t *out_boot_secs,
                                           uint32_t *out_data_lba,
                                           uint32_t *out_data_secs){
    if(!dev || dev->block_size!=512 || dev->block_count<4096){ vga_write("[mbr] device params invalid\n"); return -1; }
    const uint32_t align=2048; // 1MiB de alinhamento para setores de 512B
    const uint32_t min_boot_secs=16384; // 8MiB
    uint32_t total=dev->block_count;
    if (total <= align*3){ vga_write("[mbr] disco pequeno demais para particionar\n"); return -1; }

    uint32_t boot_secs=(boot_mb*1024u*1024u)/512u;
    if(boot_secs<min_boot_secs) boot_secs=min_boot_secs;
    uint32_t max_boot_secs = (total > align*2) ? (total - align*2) : 0;
    if (max_boot_secs < min_boot_secs){ vga_write("[mbr] espaco insuficiente para particao de dados\n"); return -1; }
    if (boot_secs > max_boot_secs){ boot_secs = max_boot_secs; }

    uint32_t boot_start=align;
    if(boot_start+boot_secs+align>=total){ vga_write("[mbr] layout de particao nao cabe no disco\n"); return -1; }

    uint32_t data_start=boot_start+boot_secs;
    if(data_start%align) data_start+= (align-(data_start%align));
    if(data_start>=total){ vga_write("[mbr] data_start out of range\n"); return -1; }

    uint32_t data_secs= total - data_start;
    if (data_secs < NOIRFS_DATA_MIN_SECTORS){ vga_write("[mbr] particao de dados muito pequena para NoirFS\n"); return -1; }

    if(write_mbr_with_partitions(dev,boot_start,boot_secs,data_start,data_secs)!=0) return -1;
    if(out_boot_lba) *out_boot_lba=boot_start;
    if(out_boot_secs) *out_boot_secs=boot_secs;
    if(out_data_lba) *out_data_lba=data_start;
    if(out_data_secs) *out_data_secs=data_secs;
    return 0;
}

/* Cria apenas a partição BOOT preservando uma partição de dados já existente.
   Não mexe na localização da partição de dados; falha se não houver espaço contíguo anterior. */
static int ensure_boot_partition_before_data(struct block_device *dev,
                                             const struct mbr_partition *data_part,
                                             uint32_t boot_mb,
                                             struct mbr_partition *out_boot) {
    if (!dev || !data_part || dev->block_size != 512) {
        return -1;
    }
    const uint32_t align = 2048;
    uint32_t boot_start = align;
    if (data_part->lba_start <= boot_start + align) {
        vga_write("[mbr] Espaco insuficiente para criar particao BOOT antes da de dados.\n");
        return -1;
    }
    uint32_t max_boot_secs = data_part->lba_start - boot_start;
    uint32_t boot_secs = (boot_mb * 1024u * 1024u) / 512u;
    if (boot_secs < 16384) boot_secs = 16384;
    if (boot_secs > max_boot_secs) boot_secs = max_boot_secs;
    /* Reescreve a MBR com entrada BOOT + entrada DATA preservada */
    if (write_mbr_with_partitions(dev,
                                  boot_start,
                                  boot_secs,
                                  data_part->lba_start,
                                  data_part->sector_count) != 0) {
        vga_write("[mbr] Falha ao reescrever MBR com particao BOOT.\n");
        return -1;
    }
    if (out_boot) {
        out_boot->bootable = 0x00;
        out_boot->type = 0xDA;
        out_boot->lba_start = boot_start;
        out_boot->sector_count = boot_secs;
    }
    return 0;
}

void kernel_main(uint32_t mb_magic, uint32_t mb_info_ptr) {
    (void)mb_magic; (void)mb_info_ptr;
    vga_init(); vga_write("Noir Guided Installation System\n\n");
    gdt_init(); idt_install(); pic_remap(0x20,0x28); pic_set_mask(0xFC,0xFF); pit_init(100); sti();
    kinit(); buffer_cache_init(); vfs_init(); ramdisk_init(256);
    extern void ata_init(void); ata_init(); tty_init(); keyboard_init();

    // Escolha de layout antes de qualquer prompt (senhas, tamanhos, etc.)
    {
        vga_write("Layouts disponiveis:\n");
        for (size_t i = 0; i < keyboard_layout_count(); ++i) {
            vga_write("  [");
            char idxbuf[4]; idxbuf[0] = '0' + (char)i; idxbuf[1] = ']'; idxbuf[2] = ' '; idxbuf[3] = '\0';
            vga_write(idxbuf);
            vga_write(keyboard_layout_name(i));
            vga_write(" : ");
            vga_write(keyboard_layout_description(i));
            vga_newline();
        }
        char buf[32];
        while (1) {
            tty_set_prompt("Layout do teclado [0-us]: ");
            tty_set_echo(1);
            tty_set_echo_mask('\0');
            tty_show_prompt();
            size_t l = tty_readline(buf, sizeof(buf));
            if (l == 0) {
                keyboard_set_layout_by_name("us");
                break;
            }
            if (l == 1 && buf[0] >= '0' && buf[0] < '0' + (char)keyboard_layout_count()) {
                size_t pick = (size_t)(buf[0] - '0');
                keyboard_set_layout_by_name(keyboard_layout_name(pick));
                vga_write("Layout aplicado.\n");
                break;
            }
            if (keyboard_set_layout_by_name(buf) == 0) {
                vga_write("Layout aplicado.\n");
                break;
            }
            vga_write("Layout desconhecido. Use um dos listados acima.\n");
        }
    }

    // Detecta discos
    struct dev_choice choices[4]; size_t ndev=0; extern int ata_devices_count(void); extern struct block_device* ata_device_by_index(int);
    int ac=ata_devices_count(); for(int i=0;i<ac && ndev<4;i++){ struct block_device *d=ata_device_by_index(i); if(d){ choices[ndev].dev=d; choices[ndev].name=d->name; choices[ndev].bytes=(uint64_t)d->block_size*(uint64_t)d->block_count; ndev++; }}
    if(ndev==0){ vga_write("Nenhum dispositivo de bloco encontrado.\n"); goto hang; }
    vga_write("Dispositivos encontrados:\n"); for(size_t i=0;i<ndev;i++){ char sz[32]; human_size(choices[i].bytes,sz,sizeof(sz)); vga_write("  ["); char ib[2]; ib[0]='0'+(char)i; ib[1]='\0'; vga_write(ib); vga_write("] "); vga_write(choices[i].name); vga_write("  "); vga_write(sz); vga_write("\n"); }
    uint32_t pick=0; wizard_prompt_number("Selecionar disco [0]: ", &pick, 0, (uint32_t)(ndev-1), 0);
    struct block_device *target=choices[pick].dev; if(!target){ goto hang; }
    if(target->block_size!=512){ vga_write("Dispositivo com block_size !=512.\n"); goto hang; }

    // Particiona (sda1 BOOT, sda2 dados)
    struct mbr_partition data_part;
    struct mbr_partition boot_part;
    int has_data_part = (mbr_read_partition(target, 1, &data_part) == 0);
    if(!has_data_part){
        uint32_t boot_mb=32;
        wizard_prompt_number("Tamanho da particao de BOOT (MiB) [32, 16..100]: ", &boot_mb,16,100,32);
        uint32_t bl,bs,dl,ds;
        if(create_boot_and_data_partitions(target,boot_mb,&bl,&bs,&dl,&ds)!=0){
            vga_write("[mbr] Falha ao criar tabela MBR.\n");
            goto hang;
        }
        boot_part.bootable = 0x00;
        boot_part.type = 0xDA;
        boot_part.lba_start = bl;
        boot_part.sector_count = bs;
        if (mbr_read_partition(target, 1, &data_part) != 0){
            vga_write("[mbr] Falha ao ler particao de dados apos criacao.\n");
            goto hang;
        }
    } else {
        vga_write("MBR existente detectado; reutilizando particao 2 como volume NoirFS.\n");
        if (mbr_read_partition(target, 0, &boot_part) != 0){
            vga_write("[mbr] Particao de BOOT ausente; criando uma antes da particao de dados.\n");
            if (ensure_boot_partition_before_data(target, &data_part, 32, &boot_part) != 0) {
                goto hang;
            }
        }
    }
    if (data_part.sector_count < NOIRFS_DATA_MIN_SECTORS){
        vga_write("Particao de dados menor que o minimo suportado.\n");
        goto hang;
    }
    /* Instala bootloader na particao de BOOT (stage1/2 + manifest + kernels) */
    {
        struct boot_payload_set payloads = boot_embedded_payloads();
        if (bootwriter_write_payloads(target, &boot_part, &payloads) != 0) {
            vga_write("[boot] Falha ao instalar bootloader na particao BOOT.\n");
            goto hang;
        } else {
            vga_write("[boot] Bootloader gravado na particao BOOT.\n");
        }
    }

    struct block_device *part = block_offset_wrap(target,data_part.lba_start,data_part.sector_count);
    if (!part){ vga_write("Falha ao mapear a particao de dados.\n"); goto hang; }
    struct block_device *chunked = block_chunked_wrap(part,NOIRFS_BLOCK_SIZE); struct block_device *dev4096 = chunked?chunked:part;

    // Criptografia + format/mount
    char pass1[128], pass2[128];
    while(1){ vga_write("Defina a senha do volume cifrado NoirFS.\n"); tty_set_prompt("Nova senha: "); tty_set_echo_mask('*'); tty_show_prompt(); size_t l1=tty_readline(pass1,sizeof(pass1)); tty_set_echo(1); tty_set_echo_mask('\0'); if(l1==0){ vga_write("Senha vazia nao permitida.\n"); continue; } tty_set_prompt("Confirmar senha: "); tty_set_echo_mask('*'); tty_show_prompt(); size_t l2=tty_readline(pass2,sizeof(pass2)); tty_set_echo(1); tty_set_echo_mask('\0'); if(l2!=l1){ vga_write("Senhas nao conferem.\n"); continue;} int same=1; for(size_t i=0;i<l1;i++){ if(pass1[i]!=pass2[i]){ same=0; break; } } if(!same){ vga_write("Senhas nao conferem.\n"); continue; } break; }
    uint8_t key1[32], key2[32]; crypt_derive_xts_keys(pass1,g_disk_salt,sizeof(g_disk_salt),g_kdf_iterations,key1,key2); memzero(pass1,sizeof(pass1)); memzero(pass2,sizeof(pass2));
    struct block_device *crypt_dev = crypt_init(dev4096,key1,key2); memzero(key1,sizeof(key1)); memzero(key2,sizeof(key2)); if(!crypt_dev || crypt_dev==dev4096){ vga_write("Falha ao iniciar camada criptografica (volume inseguro).\n"); goto hang; }
    if(format_and_mount(crypt_dev)!=0) goto hang;

    // Wizard de configuracao inicial
    if(system_run_first_boot_setup()!=0){ vga_write("Falha no assistente de configuracao inicial.\n"); goto hang; }
    vga_write("\nSelecione o layout final do teclado (sera salvo em /system/config.ini):\n");
    for (size_t i = 0; i < keyboard_layout_count(); ++i) {
        vga_write("  [");
        char idxbuf[4]; idxbuf[0] = '0' + (char)i; idxbuf[1] = ']'; idxbuf[2] = ' '; idxbuf[3] = '\0';
        vga_write(idxbuf);
        vga_write(keyboard_layout_name(i));
        vga_write(" : ");
        vga_write(keyboard_layout_description(i));
        vga_newline();
    }
    char layout_choice[32];
    const char *current_layout = keyboard_current_layout();
    if (!current_layout) current_layout = "us";
    copy_string(layout_choice, sizeof(layout_choice), current_layout);
    vga_write("Layout atual: ");
    vga_write(current_layout);
    vga_newline();
    while (1) {
        tty_set_prompt("Layout final [atual]: ");
        tty_set_echo(1);
        tty_set_echo_mask('\0');
        tty_show_prompt();
        size_t l = tty_readline(layout_choice, sizeof(layout_choice));
        if (l == 0) {
            copy_string(layout_choice, sizeof(layout_choice), current_layout);
        } else if (l == 1 && layout_choice[0] >= '0' && layout_choice[0] < '0' + (char)keyboard_layout_count()) {
            size_t idx = (size_t)(layout_choice[0] - '0');
            copy_string(layout_choice, sizeof(layout_choice), keyboard_layout_name(idx));
        }
        if (keyboard_set_layout_by_name(layout_choice) == 0) {
            vga_write("Layout aplicado para uso e persistencia.\n");
            break;
        }
        vga_write("Layout desconhecido. Escolha pelos indices ou nomes listados.\n");
    }
    if (system_save_keyboard_layout(layout_choice) != 0) {
        vga_write("Aviso: nao foi possivel salvar layout em /system/config.ini.\n");
    }
    if (system_mark_first_boot_complete()!=0){ vga_write("Nao foi possivel registrar conclusao da instalacao.\n"); goto hang; }
    struct super_block *rsb=vfs_root(); if(rsb&&rsb->bdev) buffer_cache_sync(rsb->bdev);
    vga_write("Instalacao concluida. Para boot direto do disco, instale um bootloader (ex.: GRUB via host).\n");
    vga_write("Caso contrario, mantenha a ISO anexada e use a entrada \"NoirOS\" do GRUB da ISO.\n");
hang: while(1){ __asm__ volatile("hlt"); }
}

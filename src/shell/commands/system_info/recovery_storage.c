#include "internal/system_info_internal.h"

int cmd_recovery_storage(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-storage\nMostra o estado do runtime de storage, do VFS raiz e dos caminhos persistentes usados pela recuperacao.\n",
            "Usage: recovery-storage\nShows the storage runtime, root VFS and persistent paths used by recovery.\n",
            "Uso: recovery-storage\nMuestra el runtime de almacenamiento, el VFS raiz y las rutas persistentes usadas por la recuperacion.\n"));
        return 0;
    }
#if !defined(__x86_64__)
    shell_print_error(localization_select(language,
                                          "recovery-storage indisponivel",
                                          "recovery-storage unavailable",
                                          "recovery-storage no disponible"));
    return -1;
#else
    {
        struct x64_kernel_recovery_status status;
        struct super_block *root = vfs_root();
        struct vfs_stat config_stat;
        struct vfs_stat userdb_stat;
        struct capyfs_check_report fs_report;
        int fs_report_ok = 0;
        int config_ok = 0;
        int userdb_ok = 0;
        x64_kernel_recovery_status_get(&status);
        config_ok = vfs_stat_path("/system/config.ini", &config_stat) == 0;
        userdb_ok = vfs_stat_path(USER_DB_PATH, &userdb_stat) == 0;
        fs_report_ok = system_info_recovery_capyfs_check(&fs_report) == 0;

        shell_print_bool_flag("fs.ready=", status.shell_fs_ready);
        shell_print(" ");
        shell_print_bool_flag("persistent=", status.persistent_storage);
        shell_print(" ");
        shell_print_bool_flag("ram-fallback=", status.recovery_ram_fallback);
        shell_print(" ");
        shell_print_bool_flag("validated-storage=", x64_storage_runtime_has_device());
        shell_newline();

        shell_print("backend=");
        shell_print(x64_storage_runtime_backend_name());
        shell_print(" firmware=");
        shell_print(x64_storage_runtime_uses_firmware() ? "yes" : "no");
        shell_print(" native-candidate=");
        shell_print(x64_storage_runtime_native_candidate_name());
        shell_newline();

        shell_print("data-path=");
        shell_print(x64_storage_runtime_data_path());
        shell_print(" native-data-path=");
        shell_print(x64_storage_runtime_native_data_path());
        shell_newline();

        shell_print("root-mounted=");
        shell_print((root && root->bdev) ? "yes" : "no");
        shell_newline();

        shell_print("capyfs=");
        if (fs_report_ok) {
            shell_print(capyfs_check_result_label(fs_report.result));
            shell_print(" reserved=");
            shell_print_number(fs_report.reserved_blocks_expected);
            shell_print(" root-entries=");
            shell_print_number(fs_report.root_entries);
        } else {
            shell_print("unavailable");
        }
        shell_newline();

        shell_print_path_status("/");
        shell_print_path_status("/system");
        shell_print_path_status("/etc");
        shell_print_path_status("/var/log");
        shell_print_path_status("/system/config.ini");
        shell_print_path_status(USER_DB_PATH);

        shell_print("remediation: ");
        if (!status.shell_fs_ready) {
            shell_print(localization_select(
                language,
                "o runtime do shell ainda nao montou um VFS confiavel; reinicie pela ISO e valide a chave do volume.",
                "the shell runtime has not mounted a trustworthy VFS yet; boot from the ISO and validate the volume key.",
                "el runtime del shell aun no monto un VFS confiable; arranca desde la ISO y valida la clave del volumen."));
        } else if (status.recovery_ram_fallback) {
            shell_print(localization_select(
                language,
                "a recuperacao esta em RAM temporaria; o volume persistente nao abriu. Corrija a chave/volume pela ISO antes de promover targets permanentes.",
                "recovery is running on temporary RAM storage; the persistent volume did not open. Fix the key/volume from the ISO before promoting permanent targets.",
                "la recuperacion se esta ejecutando sobre RAM temporal; el volumen persistente no se abrio. Corrige la clave/el volumen desde la ISO antes de promover objetivos permanentes."));
        } else if (!fs_report_ok || fs_report.result != CAPYFS_CHECK_OK) {
            shell_print(localization_select(
                language,
                "o volume abriu, mas a estrutura CAPYFS esta inconsistente; use recovery-storage-check e depois recupere via ISO antes de tentar repair/resume.",
                "the volume mounted, but the CAPYFS structure is inconsistent; use recovery-storage-check and then recover from the ISO before trying repair/resume.",
                "el volumen se monto, pero la estructura CAPYFS es inconsistente; usa recovery-storage-check y luego recupera desde la ISO antes de intentar repair/resume."));
        } else if (!status.persistent_storage || !x64_storage_runtime_has_device()) {
            shell_print(localization_select(
                language,
                "o volume persistente ainda nao esta validado; prefira VMware com storage AHCI/NVMe e confirme a presenca do volume DATA.",
                "persistent storage is not validated yet; prefer VMware with AHCI/NVMe storage and confirm the DATA volume is present.",
                "el almacenamiento persistente aun no esta validado; prefiere VMware con almacenamiento AHCI/NVMe y confirma la presencia del volumen DATA."));
        } else if (!config_ok || !userdb_ok || userdb_stat.size == 0) {
            shell_print(localization_select(
                language,
                "o volume persistente abriu, mas a base estrutural ainda esta incompleta; use recovery-storage-repair para reconstruir config.ini, diretórios criticos e, se preciso, resetar o admin.",
                "the persistent volume mounted, but the structural base is still incomplete; use recovery-storage-repair to rebuild config.ini, critical directories and, if needed, reset admin.",
                "el volumen persistente se monto, pero la base estructural aun esta incompleta; usa recovery-storage-repair para reconstruir config.ini, directorios criticos y, si hace falta, restablecer admin."));
        } else {
            shell_print(localization_select(
                language,
                "os prerequisitos de storage parecem saudaveis para tentar recovery-login/recovery-resume; se quiser regravar a base persistente, use recovery-storage-repair.",
                "storage prerequisites look healthy enough to try recovery-login/recovery-resume; if you want to rewrite the persistent base, use recovery-storage-repair.",
                "los prerequisitos de almacenamiento parecen saludables para intentar recovery-login/recovery-resume; si quieres regrabar la base persistente, usa recovery-storage-repair."));
        }
        shell_newline();
    }
    return 0;
#endif
}

int cmd_recovery_storage_check(struct shell_context *ctx, int argc, char **argv) {
    const char *language = shell_current_language();
    (void)ctx;
    if (shell_help_requested(argc, argv)) {
        shell_print(localization_select(
            language,
            "Uso: recovery-storage-check\nExecuta uma verificacao estrutural do CAPYFS montado e classifica superbloco, layout, bitmap, inode raiz e diretorio raiz.\n",
            "Usage: recovery-storage-check\nRuns a structural verification of the mounted CAPYFS and classifies superblock, layout, bitmap, root inode and root directory.\n",
            "Uso: recovery-storage-check\nEjecuta una verificacion estructural del CAPYFS montado y clasifica superbloque, layout, bitmap, inode raiz y directorio raiz.\n"));
        return 0;
    }
#if !defined(__x86_64__)
    shell_print_error(localization_select(language,
                                          "recovery-storage-check indisponivel",
                                          "recovery-storage-check unavailable",
                                          "recovery-storage-check no disponible"));
    return -1;
#else
    {
        struct x64_kernel_recovery_status status;
        struct capyfs_check_report report;
        int rc = 0;

        x64_kernel_recovery_status_get(&status);
        if (!status.shell_fs_ready) {
            shell_print_error(localization_select(
                language,
                "o runtime de storage ainda nao montou um VFS legivel",
                "the storage runtime has not mounted a readable VFS yet",
                "el runtime de almacenamiento aun no monto un VFS legible"));
            return -1;
        }

        rc = system_info_recovery_capyfs_check(&report);
        if (rc != 0) {
            shell_print_error(localization_select(
                language,
                "nao foi possivel inspecionar o dispositivo raiz atual",
                "could not inspect the current root device",
                "no fue posible inspeccionar el dispositivo raiz actual"));
            return -1;
        }

        shell_print("capyfs=");
        shell_print(capyfs_check_result_label(report.result));
        shell_print(" super.magic=");
        shell_print_number(report.super.magic);
        shell_print(" version=");
        shell_print_number(report.super.version);
        shell_newline();

        shell_print("layout blocks=");
        shell_print_number(report.super.block_count);
        shell_print(" inodes=");
        shell_print_number(report.super.inode_count);
        shell_print(" data-start=");
        shell_print_number(report.super.data_start);
        shell_print(" reserved=");
        shell_print_number(report.reserved_blocks_expected);
        shell_newline();

        shell_print("root refs=");
        shell_print_number(report.root_referenced_blocks);
        shell_print(" entries=");
        shell_print_number(report.root_entries);
        shell_print(" detail=");
        shell_print_number(report.detail_primary);
        shell_print(":");
        shell_print_number(report.detail_secondary);
        shell_newline();

        shell_print("action: ");
        if (status.recovery_ram_fallback) {
            shell_print(localization_select(
                language,
                "voce esta sobre RAM temporaria; esse check nao representa o volume persistente quebrado. Corrija o disco/chave via ISO.",
                "you are running on temporary RAM; this check does not represent the broken persistent volume. Fix the disk/key from the ISO.",
                "estas ejecutando sobre RAM temporal; esta verificacion no representa el volumen persistente roto. Corrige el disco/la clave desde la ISO."));
        } else if (report.result == CAPYFS_CHECK_OK) {
            shell_print(localization_select(
                language,
                "estrutura CAPYFS consistente; se faltarem arquivos base, use recovery-storage-repair.",
                "CAPYFS structure looks consistent; if base files are missing, use recovery-storage-repair.",
                "la estructura CAPYFS es consistente; si faltan archivos base, usa recovery-storage-repair."));
        } else {
            shell_print(localization_select(
                language,
                "estrutura CAPYFS inconsistente; nao tente promover targets persistentes. Recupere o volume via ISO/backup antes de sair da manutencao.",
                "CAPYFS structure is inconsistent; do not promote persistent targets. Recover the volume from the ISO/backup before leaving maintenance.",
                "la estructura CAPYFS es inconsistente; no promuevas objetivos persistentes. Recupera el volumen desde la ISO/respaldo antes de salir de mantenimiento."));
        }
        shell_newline();
        return report.result == CAPYFS_CHECK_OK ? 0 : -1;
    }
#endif
}


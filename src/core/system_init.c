/* system_init.c: STUB — this module has been split into src/config/.
 *
 * The implementation now lives in:
 *   src/config/system_setup.c          — normalizers, boot defaults, apply theme/keyboard/splash, login
 *   src/config/system_setup_wizard.c   — TUI wizard menus, UI text translations, prompts
 *   src/config/system_settings.c       — settings load/save, config file I/O, update catalog
 *   src/config/first_boot/*.c          — first-boot detection, provisioning, logging, FS helpers
 *   src/config/internal/*.h            — shared internal definitions
 *
 * The public API header remains include/core/system_init.h
 * (unchanged — all 21 consumers keep their existing #include).
 */

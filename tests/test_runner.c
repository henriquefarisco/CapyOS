#include <stdio.h>

int run_block_wrapper_tests(void);
int run_partition_tests(void);
int run_keyboard_layout_tests(void);
int run_grub_cfg_builder_tests(void);
int run_boot_manifest_tests(void);
int run_boot_writer_tests(void);
int run_csprng_tests(void);
int run_crypt_vector_tests(void);
int run_efi_block_tests(void);
int run_klog_tests(void);
int run_login_runtime_tests(void);
int run_localization_tests(void);
int run_capyfs_check_tests(void);
int run_service_manager_tests(void);
int run_service_boot_policy_tests(void);
int run_work_queue_tests(void);
int run_update_agent_tests(void);
int run_gen_boot_config_tests(void);
int run_user_home_tests(void);
int run_html_viewer_tests(void);
int run_http_encoding_tests(void);
int run_net_dns_tests(void);
int run_net_probe_tests(void);
int run_hyperv_runtime_tests(void);
int run_hyperv_runtime_gate_tests(void);
int run_hyperv_runtime_policy_tests(void);
int run_input_hyperv_gate_tests(void);
int run_native_runtime_gate_tests(void);
int run_netvsc_backend_tests(void);
int run_netvsc_runtime_tests(void);
int run_netvsc_session_tests(void);
int run_netvsp_tests(void);
int run_netvsc_control_tests(void);
int run_rndis_tests(void);
int run_storvsp_tests(void);
int run_storvsc_session_tests(void);
int run_storvsc_backend_tests(void);
int run_storvsc_runtime_tests(void);
int run_storage_runtime_hyperv_plan_tests(void);

int test_pmm_run(void);
int test_task_run(void);
int test_dns_cache_run(void);
int test_boot_slot_run(void);

int main(void) {
    int failures = 0;
    failures += run_block_wrapper_tests();
    failures += run_partition_tests();
    failures += run_keyboard_layout_tests();
    failures += run_grub_cfg_builder_tests();
    failures += run_boot_manifest_tests();
    failures += run_boot_writer_tests();
    failures += run_csprng_tests();
    failures += run_crypt_vector_tests();
    failures += run_efi_block_tests();
    failures += run_klog_tests();
    failures += run_login_runtime_tests();
    failures += run_localization_tests();
    failures += run_capyfs_check_tests();
    failures += run_service_manager_tests();
    failures += run_service_boot_policy_tests();
    failures += run_work_queue_tests();
    failures += run_update_agent_tests();
    failures += run_gen_boot_config_tests();
    failures += run_user_home_tests();
    failures += run_html_viewer_tests();
    failures += run_http_encoding_tests();
    failures += run_net_dns_tests();
    failures += run_net_probe_tests();
    failures += run_hyperv_runtime_tests();
    failures += run_hyperv_runtime_gate_tests();
    failures += run_hyperv_runtime_policy_tests();
    failures += run_input_hyperv_gate_tests();
    failures += run_native_runtime_gate_tests();
    failures += run_netvsc_backend_tests();
    failures += run_netvsc_runtime_tests();
    failures += run_netvsc_session_tests();
    failures += run_netvsp_tests();
    failures += run_netvsc_control_tests();
    failures += run_rndis_tests();
    failures += run_storvsp_tests();
    failures += run_storvsc_session_tests();
    failures += run_storvsc_backend_tests();
    failures += run_storvsc_runtime_tests();
    failures += run_storage_runtime_hyperv_plan_tests();

    failures += test_pmm_run();
    failures += test_task_run();
    failures += test_dns_cache_run();
    failures += test_boot_slot_run();

    if (failures == 0) {
        printf("Todos os testes passaram.\n");
        return 0;
    }
    printf("Falhas detectadas: %d\n", failures);
    return 1;
}

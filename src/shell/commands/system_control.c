/*
 * System-control shell commands are grouped as implementation fragments while
 * this file remains the single registration point for the command table.
 */

#include "system_control/config_commands.inc"
#include "system_control/service_helpers.inc"
#include "system_control/jobs_updates.inc"
#include "system_control/service_target_resume.inc"
#include "system_control/recovery_login_verify.inc"
#include "system_control/recovery_storage.inc"
#include "system_control/power_runtime_registry.inc"

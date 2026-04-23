/*
 * x86_64 input runtime is grouped as implementation fragments while backend
 * state and static helpers stay private to this translation unit.
 */

#include "input_runtime/prelude_ports.inc"
#include "input_runtime/keyboard_decode_probe.inc"
#include "input_runtime/backend_management.inc"
#include "input_runtime/polling.inc"
#include "input_runtime/status_hyperv.inc"

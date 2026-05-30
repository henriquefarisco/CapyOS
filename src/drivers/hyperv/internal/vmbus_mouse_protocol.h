#ifndef VMBUS_MOUSE_PROTOCOL_H
#define VMBUS_MOUSE_PROTOCOL_H

#include <stdint.h>

#include "drivers/hyperv/hyperv.h"

int hyperv_mouse_parse_hid_report(const void *report, uint32_t report_len,
                                  struct hyperv_mouse_report *out);

#endif /* VMBUS_MOUSE_PROTOCOL_H */

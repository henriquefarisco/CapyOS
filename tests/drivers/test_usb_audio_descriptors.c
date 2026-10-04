#include "drivers/usb/usb_audio.h"
#include "drivers/usb/usb_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Minimal UAC1 stereo speaker topology, encoded from the USB-IF descriptor
 * tables (no dependency on a host USB stack or attached physical device). */
static const uint8_t valid[] = {
  9,2,100,0,2,1,0,0x80,50,
  9,4,0,0,0,1,1,0,0,
  9,0x24,1,0,1,30,0,1,1,
  12,0x24,2,1,1,1,0,2,3,0,0,0,
  9,0x24,3,2,1,3,0,1,0,
  9,4,1,0,0,1,2,0,0,
  9,4,1,1,1,1,2,0,0,
  7,0x24,1,1,0,1,0,
  11,0x24,2,1,2,2,16,1,0x80,0xbb,0,
  9,5,1,0x0d,192,0,1,0,0,
  7,0x25,1,0,0,0,0
};
_Static_assert(sizeof(valid) == 100, "fixture layout");
static void rejected(const uint8_t *data, size_t n) {
  struct usb_audio_format out, zero = {0};
  memset(&out, 0x55, sizeof(out));
  assert(usb_audio_parse_configuration(data, n, &out) < 0);
  assert(memcmp(&out, &zero, sizeof(out)) == 0);
}
int main(void) {
  struct usb_audio_format out;
  struct usb_device_info device = {0};
  assert(usb_parse_configuration_descriptor(valid, sizeof(valid), &device) == 0);
  assert(device.audio_output.endpoint == 1 && device.audio_output.max_packet == 192);
  uint8_t composite[sizeof(valid) + 16];
  memcpy(composite, valid, sizeof(valid));
  const uint8_t hid[] = {9,4,2,0,1,3,1,1,0, 7,5,0x82,3,8,0,1};
  memcpy(composite + sizeof(valid), hid, sizeof(hid));
  composite[2] = sizeof(composite); composite[4] = 3;
  assert(usb_parse_configuration_descriptor(composite, sizeof(composite), &device) == 0);
  assert(device.is_keyboard && device.class_code == USB_CLASS_HID);
  assert(device.endpoint_count == 1 && device.endpoints[0].address == 0x82);
  assert(device.audio_output.endpoint == 1 && device.audio_output.alternate == 1);
  composite[79] = 24; /* unsupported audio must not disable HID or retain stale audio */
  assert(usb_parse_configuration_descriptor(composite, sizeof(composite), &device) == 0);
  assert(device.is_keyboard && !device.audio_output.alternate);
  assert(usb_audio_parse_configuration(valid, sizeof(valid), &out) == 0);
  assert(out.configuration == 1 && out.control_interface == 0 &&
         out.interface_number == 1 && out.alternate == 1 && out.endpoint == 1 &&
         out.max_packet == 192 && out.interval == 1 && !out.frequency_control);
  for (size_t n = 0; n < sizeof(valid); ++n) rejected(valid, n);
  rejected(NULL, 0);
  assert(usb_audio_parse_configuration(valid, sizeof(valid), NULL) == -1);
  struct { unsigned offset; uint8_t value; } mutations[] = {
    {0,0}, {1,3}, {2,255}, {5,0}, {21,1}, {22,2}, {25,4}, {26,7},
    {30,0}, {34,1}, {52,1}, {60,0}, {61,2}, {64,0x20}, {69,8},
    {71,3}, {77,1}, {78,3}, {79,24}, {80,2}, {81,0},
    {84,7}, {86,0}, {86,0x81}, {86,0x11}, {87,5}, {87,3},
    {88,100}, {89,8}, {90,2}, {91,1}, {92,0x82}, {93,0}, {96,4}, {97,3}, {98,1}
  };
  uint8_t bytes[sizeof(valid) + 16];
  for (size_t i = 0; i < sizeof(mutations)/sizeof(mutations[0]); ++i) {
    memcpy(bytes, valid, sizeof(valid));
    bytes[mutations[i].offset] = mutations[i].value;
    rejected(bytes, sizeof(valid));
  }
  memcpy(bytes, valid, sizeof(valid)); bytes[87] = 9; /* adaptive */
  assert(usb_audio_parse_configuration(bytes, sizeof(valid), &out) == 0);
  bytes[96] = 0x81; bytes[97] = 1; bytes[98] = 12;
  assert(usb_audio_parse_configuration(bytes, sizeof(valid), &out) == 0);
  assert(out.frequency_control && out.pad_packets && out.lock_delay == 12);
  /* A valid candidate must not hide a malformed trailing descriptor. */
  memcpy(bytes, valid, sizeof(valid)); bytes[2] = 102; bytes[100] = 0; bytes[101] = 4;
  rejected(bytes, 102);
  memcpy(bytes, valid, sizeof(valid));
  memmove(bytes + 87, bytes + 84, 16);
  bytes[2] = 103; bytes[73] = 14; bytes[80] = 2;
  bytes[84] = 0x44; bytes[85] = 0xac; bytes[86] = 0;
  rejected(bytes, 103); /* multiple rates require frequency control */
  bytes[99] = 1;
  assert(usb_audio_parse_configuration(bytes, 103, &out) == 0);
  bytes[80] = 0; /* continuous 44.1..48 kHz */
  bytes[81] = 0x44; bytes[82] = 0xac; bytes[83] = 0;
  bytes[84] = 0x80; bytes[85] = 0xbb;
  assert(usb_audio_parse_configuration(bytes, 103, &out) == 0);
  bytes[85] = 1; rejected(bytes, 103); /* inverted range */
  uint32_t seed = 0x41554431u;
  for (unsigned iteration = 0; iteration < 10000; ++iteration) {
    memcpy(bytes, valid, sizeof(valid));
    for (unsigned i = 0; i < 3; ++i) {
      seed = seed * 1664525u + 1013904223u;
      bytes[seed % sizeof(valid)] ^= (uint8_t)(seed >> 24);
    }
    if (usb_audio_parse_configuration(bytes, sizeof(valid), &out) == 0)
      assert(out.alternate && out.endpoint > 0 && out.endpoint < 16 &&
             out.interval == 1 && out.max_packet >= 192 && out.max_packet <= 1023);
  }
  puts("[usb-audio] descriptor selection, truncation, malformed/unsupported formats: PASS");
  return 0;
}

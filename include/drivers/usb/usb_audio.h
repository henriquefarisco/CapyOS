#ifndef DRIVERS_USB_AUDIO_H
#define DRIVERS_USB_AUDIO_H
#include <stddef.h>
#include <stdint.h>

#define USB_AUDIO_CONFIG_MAX 4096u
#define USB_AUDIO_RATE 48000u
#define USB_AUDIO_FRAME_BYTES 4u
#define USB_AUDIO_PACKET_BYTES 192u

struct usb_audio_format {
  uint8_t configuration, control_interface, interface_number, alternate;
  uint8_t endpoint, interval, attributes, frequency_control;
  uint8_t pad_packets, lock_units;
  uint16_t max_packet, lock_delay;
};

/* Pure UAC1 discovery, no allocation/IO. Full-speed Type I stereo S16 at
 * 48 kHz, synchronous/adaptive OUT without a feedback endpoint. Returns 0,
 * -1 malformed, or -2 no supported alternate. Output is zero on failure.
 * Caller must verify the port speed and configure the returned alternate. */
int usb_audio_parse_configuration(const uint8_t *data, size_t size,
                                  struct usb_audio_format *out);
#endif

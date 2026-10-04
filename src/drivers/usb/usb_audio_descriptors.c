#include "drivers/usb/usb_audio.h"

/* USB Audio 1.0 sections 4.3/4.5/4.6; Audio Data Formats 1.0 section 2.2.5. */
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned le24(const uint8_t *p) { return le16(p) | (unsigned)p[2] << 16; }
static void bit_set(uint8_t *bits, unsigned id) { bits[id / 8u] |= 1u << (id % 8u); }
static int bit_get(const uint8_t *bits, unsigned id) { return !!(bits[id / 8u] & (1u << (id % 8u))); }

int usb_audio_parse_configuration(const uint8_t *data, size_t size,
                                  struct usb_audio_format *out) {
  if (!out) return -1;
  *out = (struct usb_audio_format){0};
  if (!data || size < 9 || size > USB_AUDIO_CONFIG_MAX || data[0] != 9 ||
      data[1] != 2 || !data[5]) return -1;
  size_t total = le16(data + 2);
  if (total < 9 || total > size) return -1;
  uint8_t streams[32] = {0}, idle[32] = {0}, terminals[32] = {0};
  struct usb_audio_format selected = {0}, candidate = {0};
  unsigned control = 0, control_seen = 0, in_control = 0, in_stream = 0;
  unsigned general = 0, format = 0, endpoint = 0, cs_endpoint = 0;
  unsigned terminal = 0, multiple_rates = 0, seen = 0;
  for (size_t at = 9; at <= total;) {
    if (at == total || (total - at >= 2 && data[at + 1] == 4)) {
      if (in_stream && general && format && endpoint && cs_endpoint &&
          bit_get(streams, candidate.interface_number) &&
          bit_get(idle, candidate.interface_number) && bit_get(terminals, terminal) &&
          (!multiple_rates || candidate.frequency_control) && !selected.alternate)
        selected = candidate;
      in_stream = 0;
      general = format = endpoint = cs_endpoint = terminal = multiple_rates = 0;
      seen = 0;
    }
    if (at == total) break;
    if (total - at < 2) return -1;
    const uint8_t *d = data + at;
    unsigned n = d[0], type = d[1];
    if (n < 2 || n > total - at) return -1;
    if (type == 4) {
      if (n != 9) return -1;
      in_control = d[5] == 1 && d[6] == 1 && d[7] == 0 && d[3] == 0;
      if (in_control) {
        if (control_seen++) return -2;
        control = d[2];
      }
      if (d[5] == 1 && d[6] == 2 && d[7] == 0) {
        if (!d[3] && !d[4]) bit_set(idle, d[2]);
        if (d[3] && d[4] == 1) {
          in_stream = 1;
          candidate = (struct usb_audio_format){.configuration = data[5],
            .control_interface = (uint8_t)control, .interface_number = d[2],
            .alternate = d[3]};
        }
      }
    } else if (type == 0x24 && in_control) {
      if (n < 3) return -1;
      if (d[2] == 1) {
        if (n < 8 || n != 8u + d[7] || le16(d + 5) < n ||
            le16(d + 5) > total - at) return -1;
        if (le16(d + 3) != 0x100) return -2;
        for (unsigned i = 0; i < d[7]; ++i) bit_set(streams, d[8 + i]);
      } else if (d[2] == 2) {
        if (n != 12 || !d[3]) return -1;
        if (le16(d + 4) == 0x101 && d[7] == 2) bit_set(terminals, d[3]);
      }
    } else if (type == 0x24 && in_stream) {
      if (n < 3) return -1;
      if (d[2] == 1) {
        if (n != 7 || (seen & 1u)) return -1;
        seen |= 1u;
        general = le16(d + 5) == 1;
        terminal = d[3];
      } else if (d[2] == 2) {
        if (n < 8 || (seen & 2u)) return -1;
        seen |= 2u;
        unsigned rates = d[7];
        if (n != (rates ? 8u + 3u * rates : 14u)) return -1;
        unsigned supports_rate = 0;
        if (!rates) {
          unsigned low = le24(d + 8), high = le24(d + 11);
          if (!low || low > high) return -1;
          supports_rate = low <= USB_AUDIO_RATE && USB_AUDIO_RATE <= high;
        } else for (unsigned i = 0; i < rates; ++i)
          if (le24(d + 8 + 3u * i) == USB_AUDIO_RATE) supports_rate = 1;
        format = d[3] == 1 && d[4] == 2 && d[5] == 2 && d[6] == 16 && supports_rate;
        multiple_rates = rates != 1;
      }
    } else if (type == 5 && in_stream) {
      if (n != 9 || (seen & 4u)) return -1;
      seen |= 4u;
      unsigned packet = le16(d + 4), sync = (d[3] >> 2) & 3u;
      endpoint = d[2] > 0 && d[2] < 16 && (d[3] & 3u) == 1 &&
        !(d[3] & 0xf0u) && (sync == 2 || sync == 3) &&
        packet >= USB_AUDIO_PACKET_BYTES && packet <= 1023 &&
        d[6] == 1 && !d[7] && !d[8];
      candidate.endpoint = d[2]; candidate.attributes = d[3];
      candidate.max_packet = (uint16_t)packet; candidate.interval = d[6];
    } else if (type == 0x25 && in_stream) {
      if (n != 7 || d[2] != 1 || (seen & 8u) || !(seen & 4u)) return -1;
      seen |= 8u;
      cs_endpoint = !(d[3] & 0x7eu) && d[4] <= 2;
      candidate.frequency_control = d[3] & 1u;
      candidate.pad_packets = d[3] >> 7;
      candidate.lock_units = d[4]; candidate.lock_delay = (uint16_t)le16(d + 5);
      if (!d[4] && candidate.lock_delay) return -1;
    }
    at += n;
  }
  if (!selected.alternate) return -2;
  *out = selected;
  return 0;
}

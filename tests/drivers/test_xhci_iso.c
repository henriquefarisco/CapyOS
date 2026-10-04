#include "drivers/usb/xhci_iso.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  _Alignas(64) struct xhci_trb ring[XHCI_ISO_RING_TRBS];
  struct xhci_iso_queue q;
  const uint64_t dma = 0x100000;
  assert(xhci_iso_queue_init(&q, ring, dma) == 0);
  assert(ring[255].param == dma && (ring[255].control & 3) == 3);
  for (unsigned round = 0; round < 4; ++round) {
    for (unsigned i = 0; i < 255; ++i) {
      assert(xhci_iso_enqueue(&q, 0x200000 + i * 256, 192) == 0);
      assert((ring[i].control & 1u) == (1u ^ (round & 1u)));
      assert(((ring[i].control >> 10) & 63u) == 5 && ring[i].status == 192);
    }
    assert(q.pending == 255 && xhci_iso_enqueue(&q, 0x200000, 192) == -1);
    for (unsigned i = 0; i < 255; ++i)
      assert(xhci_iso_complete(&q, dma + i * 16u, 1, 0) == 0);
    assert(q.completed_bytes == (uint64_t)(round + 1u) * 255u * 192u);
  }
  assert(xhci_iso_enqueue(&q, 0x20ffff, 192) == -1);
  for (unsigned failure = 0; failure < 4; ++failure) {
    assert(xhci_iso_queue_init(&q, ring, dma) == 0);
    if (failure != 3) assert(xhci_iso_enqueue(&q, 0x200000, 192) == 0);
    assert(xhci_iso_complete(&q, dma + (failure == 0 ? 16 : 0),
                            failure == 1 ? 23 : 1, failure == 2 ? 1 : 0) == -1);
    assert(q.failed && !q.completed_bytes && xhci_iso_enqueue(&q, 0x200000, 192) == -1);
  }
  assert(xhci_iso_queue_init(&q, ring, dma + 1) == -1);
  assert(xhci_iso_queue_init(&q, ring, dma) == 0);
  q.pending = XHCI_ISO_DATA_TRBS + 1;
  assert(xhci_iso_complete(&q, dma, 1, 0) == -1 && q.failed);
  assert(xhci_iso_queue_init(&q, ring, 0x10ffc0) == -1);
  _Alignas(64) uint32_t input[64 * 33 / 4], output[64 / 4] = {0};
  output[0] = (1u << 20) | (1u << 27); output[1] = 3u << 16;
  for (unsigned stride = 32; stride <= 64; stride += 32) {
    memset(input, 0xa5, sizeof(input));
    assert(xhci_build_iso_out_context(input, stride, output, 1, 192, dma) == 0);
    uint32_t *ep = input + stride * 3u / 4u;
    assert(input[1] == 5 && (input[stride / 4] >> 27) == 2);
    assert(input[stride / 4 + 1] == output[1]);
    assert(ep[0] == 3u << 16 && ep[1] == (192u << 16 | 8u));
    assert(ep[2] == (dma | 1u) && !ep[3] && ep[4] == (192u << 16 | 192u));
    assert(ep[5] == 0 && ep[6] == 0 && ep[7] == 0);
  }
  output[0] = 3u << 20;
  memset(input, 0xa5, sizeof(input));
  assert(xhci_build_iso_out_context(input, 32, output, 1, 192, dma) == -1);
  assert(input[0] == 0xa5a5a5a5);
  input[0] = 1u << 20;
  assert(xhci_build_iso_out_context(input, 32, input, 1, 192, dma) == -1);
  assert(input[0] == 1u << 20);
  input[8] = 1u << 20;
  assert(xhci_build_iso_out_context(input, 32, input + 8, 1, 192, dma) == -1);
  assert(input[8] == 1u << 20);
  puts("[xhci-iso] context layout, cycle wraps, capacity, DMA bounds and failures: PASS");
  return 0;
}

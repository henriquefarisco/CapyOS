#include "audio/audio_limits.h"
#include "capy_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t live, peak;
static void *allocate(size_t bytes, void *ctx) {
    (void)ctx;
    if (bytes > AUDIO_SERVICE_MAX_DECODED_BYTES) return NULL;
    size_t *p = malloc(bytes + sizeof(size_t));
    if (!p) return NULL;
    *p = bytes; live += bytes;
    if (live > peak) peak = live;
    return p + 1;
}
static void release(void *ptr, void *ctx) {
    (void)ctx;
    size_t *p = (size_t *)ptr - 1;
    live -= *p; free(p);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f); assert(size > 0 && size <= AUDIO_SERVICE_MAX_FILE_BYTES);
    rewind(f);
    uint8_t *input = malloc((size_t)size); assert(input);
    assert(fread(input, 1, (size_t)size, f) == (size_t)size); fclose(f);
    struct capy_audio_limits limits = {
        AUDIO_SERVICE_MAX_FILE_BYTES, AUDIO_SERVICE_MAX_DECODED_BYTES,
        AUDIO_SERVICE_MAX_DECODED_BYTES / 2u, 48000, 2, AUDIO_SERVICE_MAX_CHUNKS
    };
    struct capy_audio_allocator allocator = {allocate, release, NULL};
    struct capy_audio_pcm pcm;
    int rc = capy_audio_decode_memory_limited(input, (size_t)size, &allocator, &limits, &pcm);
    printf("decode=%d peak=%zu\n", rc, peak); fflush(stdout);
    assert(rc == 0);
    assert(pcm.metadata.sample_rate == 48000 && pcm.metadata.channels == 2);
    assert(peak <= AUDIO_SERVICE_MAX_DECODED_BYTES + (16u << 20));
    f = fopen(argv[2], "wb"); assert(f);
    assert(fwrite(pcm.samples, 1, pcm.metadata.pcm_bytes, f) == pcm.metadata.pcm_bytes);
    fclose(f);
    printf("[builtin-music] frames=%llu pcm_bytes=%zu peak=%zu\n",
        (unsigned long long)pcm.metadata.frame_count, pcm.metadata.pcm_bytes, peak);
    capy_audio_pcm_free(&pcm); free(input); assert(!live);
}

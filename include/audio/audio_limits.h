#ifndef CAPYOS_AUDIO_LIMITS_H
#define CAPYOS_AUDIO_LIMITS_H
/* Bounded whole-track decode policy, including the supplied 177-second songs.
 * Input remains capped separately from decoded output and codec scratch. */
#define AUDIO_SERVICE_MAX_FILE_BYTES (8u * 1024u * 1024u)
#define AUDIO_SERVICE_MAX_DECODED_BYTES (40u * 1024u * 1024u)
#define AUDIO_SERVICE_MAX_CHUNKS 16384u
#endif

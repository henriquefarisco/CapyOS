#ifndef CORE_UPDATE_AGENT_H
#define CORE_UPDATE_AGENT_H

#include <stddef.h>
#include <stdint.h>

#define UPDATE_AGENT_CHANNEL_MAX 16u
#define UPDATE_AGENT_SOURCE_MAX 96u
#define UPDATE_AGENT_VERSION_MAX 40u
#define UPDATE_AGENT_PATH_MAX 96u
#define UPDATE_AGENT_SUMMARY_MAX 80u

struct system_update_status {
  uint8_t configured;
  uint8_t catalog_present;
  uint8_t update_available;
  int32_t last_result;
  char channel[UPDATE_AGENT_CHANNEL_MAX];
  char source[UPDATE_AGENT_SOURCE_MAX];
  char manifest_path[UPDATE_AGENT_PATH_MAX];
  char current_version[UPDATE_AGENT_VERSION_MAX];
  char available_version[UPDATE_AGENT_VERSION_MAX];
  char published_at[24];
  char summary[UPDATE_AGENT_SUMMARY_MAX];
};

typedef int (*update_agent_read_file_fn)(const char *path, char *buffer,
                                         size_t buffer_size,
                                         size_t *out_len);

void update_agent_reset(void);
void update_agent_init(const char *current_version);
void update_agent_set_reader(update_agent_read_file_fn reader);
int update_agent_poll(void);
void update_agent_status_get(struct system_update_status *out);

#endif /* CORE_UPDATE_AGENT_H */

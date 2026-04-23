#ifndef CORE_UPDATE_AGENT_H
#define CORE_UPDATE_AGENT_H

#include <stddef.h>
#include <stdint.h>

#define UPDATE_AGENT_CHANNEL_MAX 16u
#define UPDATE_AGENT_BRANCH_MAX 16u
#define UPDATE_AGENT_SOURCE_MAX 96u
#define UPDATE_AGENT_VERSION_MAX 40u
#define UPDATE_AGENT_PATH_MAX 96u
#define UPDATE_AGENT_REMOTE_MAX 160u
#define UPDATE_AGENT_SUMMARY_MAX 80u

struct system_update_status {
  uint8_t configured;
  uint8_t catalog_present;
  uint8_t update_available;
  uint8_t stage_ready;
  uint8_t pending_activation;
  int32_t last_result;
  char channel[UPDATE_AGENT_CHANNEL_MAX];
  char branch[UPDATE_AGENT_BRANCH_MAX];
  char source[UPDATE_AGENT_SOURCE_MAX];
  char manifest_path[UPDATE_AGENT_PATH_MAX];
  char remote_manifest_url[UPDATE_AGENT_REMOTE_MAX];
  char staged_manifest_path[UPDATE_AGENT_PATH_MAX];
  char current_version[UPDATE_AGENT_VERSION_MAX];
  char available_version[UPDATE_AGENT_VERSION_MAX];
  char staged_version[UPDATE_AGENT_VERSION_MAX];
  char published_at[24];
  char summary[UPDATE_AGENT_SUMMARY_MAX];
};

typedef int (*update_agent_read_file_fn)(const char *path, char *buffer,
                                         size_t buffer_size,
                                         size_t *out_len);
typedef int (*update_agent_write_file_fn)(const char *path, const char *text);
typedef int (*update_agent_remove_file_fn)(const char *path);

void update_agent_reset(void);
void update_agent_init(const char *current_version);
void update_agent_set_reader(update_agent_read_file_fn reader);
void update_agent_set_writer(update_agent_write_file_fn writer);
void update_agent_set_remover(update_agent_remove_file_fn remover);
int update_agent_poll(void);
int update_agent_import_manifest_path(const char *path);
int update_agent_stage_latest(void);
int update_agent_clear_stage(void);
int update_agent_set_pending_activation(int enabled);
void update_agent_status_get(struct system_update_status *out);

#endif /* CORE_UPDATE_AGENT_H */

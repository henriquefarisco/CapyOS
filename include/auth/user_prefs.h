#ifndef CORE_USER_PREFS_H
#define CORE_USER_PREFS_H

#include "auth/user.h"

#define USER_LANGUAGE_MAX 16

struct user_preferences {
  char language[USER_LANGUAGE_MAX];
};

void user_preferences_set_defaults(struct user_preferences *prefs);
const char *user_preferences_language(const struct user_preferences *prefs);
int user_prefs_load(const struct user_record *user, struct user_preferences *out);
int user_prefs_save(const struct user_record *user,
                    const struct user_preferences *prefs);
int user_prefs_save_language(const struct user_record *user, const char *language);

#endif

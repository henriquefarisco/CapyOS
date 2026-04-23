#ifndef USER_H
#define USER_H

#include <stddef.h>
#include <stdint.h>

#define USER_NAME_MAX 32
#define USER_HOME_MAX 96
#define USER_ROLE_MAX 16
#define USER_SALT_SIZE 16
#define USER_HASH_SIZE 32
#define USER_ITERATIONS 64000

#define USER_DB_PATH "/etc/users.db"

struct user_record {
    char username[USER_NAME_MAX];
    uint32_t uid;
    uint32_t gid;
    char home[USER_HOME_MAX];
    uint8_t salt[USER_SALT_SIZE];
    uint8_t hash[USER_HASH_SIZE];
    char role[USER_ROLE_MAX];
};

void user_record_clear(struct user_record *rec);
int user_record_init(const char *username,
                     const char *password,
                     const char *role,
                     uint32_t uid,
                     uint32_t gid,
                     const char *home,
                     struct user_record *out);

int userdb_ensure(void);
int userdb_find(const char *username, struct user_record *out);
int userdb_add(const struct user_record *user);
int userdb_authenticate(const char *username, const char *password, struct user_record *out);
int userdb_set_password(const char *username, const char *new_password);
int userdb_next_ids(uint32_t *out_uid, uint32_t *out_gid);
int userdb_has_any_user(void);

#endif

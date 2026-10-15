#ifndef CHAT_NETWORK_H
#define CHAT_NETWORK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHAT_NETWORK_TEXT_MAX 512
#define CHAT_NETWORK_USERNAME_MAX 64
#define CHAT_NETWORK_EMOJI_NAME_MAX 128
#define CHAT_NETWORK_MEDIA_PATH_MAX 256
#define CHAT_NETWORK_FILE_HASH_MAX 65

typedef enum {
    CHAT_NETWORK_EVENT_STATUS,
    CHAT_NETWORK_EVENT_AUTH_OK,
    CHAT_NETWORK_EVENT_AUTH_FAILED,
    CHAT_NETWORK_EVENT_REGISTER_OK,
    CHAT_NETWORK_EVENT_REGISTER_FAILED,
    CHAT_NETWORK_EVENT_MESSAGE,
    CHAT_NETWORK_EVENT_PEERS_BEGIN,
    CHAT_NETWORK_EVENT_PEER,
    CHAT_NETWORK_EVENT_PEERS_END,
    CHAT_NETWORK_EVENT_HISTORY_BEGIN,
    CHAT_NETWORK_EVENT_HISTORY_LINE,
    CHAT_NETWORK_EVENT_HISTORY_END,
    CHAT_NETWORK_EVENT_EMOJI_BEGIN,
    CHAT_NETWORK_EVENT_EMOJI_COMPLETE,
    CHAT_NETWORK_EVENT_EMOJI_DONE,
    CHAT_NETWORK_EVENT_MEDIA_BEGIN,
    CHAT_NETWORK_EVENT_MEDIA_COMPLETE,
    CHAT_NETWORK_EVENT_MEDIA_END,
    CHAT_NETWORK_EVENT_UPLOAD_REQUIRED,
    CHAT_NETWORK_EVENT_DISCONNECTED
} chat_network_event_type_t;

typedef enum {
    CHAT_NETWORK_MEDIA_NONE,
    CHAT_NETWORK_MEDIA_EMOJI,
    CHAT_NETWORK_MEDIA_FILE
} chat_network_media_kind_t;

typedef struct {
    int id;
    char ip[40];
    int tcp_port;
    int udp_port;
    char username[CHAT_NETWORK_USERNAME_MAX];
} chat_network_peer_t;

typedef struct {
    char name[CHAT_NETWORK_EMOJI_NAME_MAX];
    uint64_t size;
} chat_network_emoji_t;

typedef struct {
    chat_network_media_kind_t kind;
    int sender_id;
    char name[CHAT_NETWORK_EMOJI_NAME_MAX];
    uint64_t size;
    char hash[CHAT_NETWORK_FILE_HASH_MAX];
    char path[CHAT_NETWORK_MEDIA_PATH_MAX];
} chat_network_media_t;

typedef struct {
    chat_network_event_type_t type;
    char text[CHAT_NETWORK_TEXT_MAX];
    chat_network_peer_t peer;
    chat_network_emoji_t emoji;
    chat_network_media_t media;
} chat_network_event_t;

int chat_network_parse_line(const char *line, chat_network_event_t *event);
int chat_network_login(const char *host, const char *username, const char *password);
int chat_network_register(const char *host, const char *username, const char *password);
void chat_network_disconnect(void);
void chat_network_cleanup_emojis(void);
int chat_network_poll_event(chat_network_event_t *event);
int chat_network_request_list(void);
int chat_network_request_history(int id);
int chat_network_send_broadcast(const char *message);
int chat_network_send_private(int id, const char *message);
int chat_network_send_emoji(int id, const char *name);
int chat_network_offer_file(int id, const char *path);

#ifdef __cplusplus
}
#endif

#endif

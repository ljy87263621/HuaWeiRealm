#define _DEFAULT_SOURCE

#include "chat_network.h"
#include "chat_transfer.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CHAT_SERVER_PORT
#define CHAT_SERVER_PORT 8888
#endif
#ifndef CHAT_NETWORK_CONNECT_TIMEOUT_SECONDS
#define CHAT_NETWORK_CONNECT_TIMEOUT_SECONDS 5
#endif
#ifndef CHAT_NETWORK_AUTH_TIMEOUT_SECONDS
#define CHAT_NETWORK_AUTH_TIMEOUT_SECONDS 10
#endif
#define CHAT_QUEUE_CAPACITY 64
#define CHAT_RX_BUFFER_SIZE 8192
#define CHAT_MAX_ATTACHMENT_SIZE (8U * 1024U * 1024U)

static int tcp_socket = -1;
static int udp_socket = -1;
static volatile int network_running;
static pthread_t network_thread;
static int network_thread_started;
static pthread_mutex_t network_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static chat_network_event_t event_queue[CHAT_QUEUE_CAPACITY];
static size_t queue_head;
static size_t queue_tail;
static size_t queue_count;
static char login_host[128];
static char login_username[CHAT_NETWORK_USERNAME_MAX];
static char login_password[CHAT_NETWORK_TEXT_MAX];
static int network_operation;
static char pending_upload_path[CHAT_NETWORK_MEDIA_PATH_MAX];
static char pending_upload_hash[CHAT_NETWORK_FILE_HASH_MAX];

enum {
    NETWORK_OPERATION_LOGIN,
    NETWORK_OPERATION_REGISTER
};

static int valid_emoji_name(const char *name);

static void queue_push(chat_network_event_type_t type, const char *text,
                       const chat_network_peer_t *peer,
                       const chat_network_emoji_t *emoji)
{
    chat_network_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    if(text) snprintf(event.text, sizeof(event.text), "%s", text);
    if(peer) event.peer = *peer;
    if(emoji) event.emoji = *emoji;

    pthread_mutex_lock(&queue_lock);
    if(queue_count == CHAT_QUEUE_CAPACITY) {
        queue_head = (queue_head + 1) % CHAT_QUEUE_CAPACITY;
        queue_count--;
    }
    event_queue[queue_tail] = event;
    queue_tail = (queue_tail + 1) % CHAT_QUEUE_CAPACITY;
    queue_count++;
    pthread_mutex_unlock(&queue_lock);
}

static void queue_push_media(chat_network_event_type_t type, const char *text,
                             const chat_network_media_t *media)
{
    chat_network_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    if(text) snprintf(event.text, sizeof(event.text), "%s", text);
    if(media) event.media = *media;

    pthread_mutex_lock(&queue_lock);
    if(queue_count == CHAT_QUEUE_CAPACITY) {
        queue_head = (queue_head + 1) % CHAT_QUEUE_CAPACITY;
        queue_count--;
    }
    event_queue[queue_tail] = event;
    queue_tail = (queue_tail + 1) % CHAT_QUEUE_CAPACITY;
    queue_count++;
    pthread_mutex_unlock(&queue_lock);
}

int chat_network_poll_event(chat_network_event_t *event)
{
    if(!event) return 0;
    pthread_mutex_lock(&queue_lock);
    if(queue_count == 0) {
        pthread_mutex_unlock(&queue_lock);
        return 0;
    }
    *event = event_queue[queue_head];
    queue_head = (queue_head + 1) % CHAT_QUEUE_CAPACITY;
    queue_count--;
    pthread_mutex_unlock(&queue_lock);
    return 1;
}

int chat_network_parse_line(const char *line, chat_network_event_t *event)
{
    char copy[CHAT_NETWORK_TEXT_MAX];
    if(!line || !event) return 0;
    memset(event, 0, sizeof(*event));
    snprintf(copy, sizeof(copy), "%s", line);
    copy[strcspn(copy, "\r\n")] = '\0';

    if(strncmp(copy, "AUTH:OK ", 8) == 0) {
        int id;
        char username[CHAT_NETWORK_USERNAME_MAX];
        if(sscanf(copy + 8, "ID %d USER %63s", &id, username) == 2) {
            event->type = CHAT_NETWORK_EVENT_AUTH_OK;
            event->peer.id = id;
            snprintf(event->peer.username, sizeof(event->peer.username), "%s", username);
            return 1;
        }
    }
    if(strncmp(copy, "AUTH:FAIL ", 10) == 0) {
        event->type = CHAT_NETWORK_EVENT_AUTH_FAILED;
        snprintf(event->text, sizeof(event->text), "%s", copy + 10);
        return 1;
    }
    if(strncmp(copy, "REGISTER:OK ", 12) == 0) {
        char username[CHAT_NETWORK_USERNAME_MAX];
        if(sscanf(copy + 12, "USER %63s", username) == 1) {
            event->type = CHAT_NETWORK_EVENT_REGISTER_OK;
            snprintf(event->peer.username, sizeof(event->peer.username), "%s", username);
            return 1;
        }
    }
    if(strncmp(copy, "REGISTER:FAIL ", 14) == 0) {
        event->type = CHAT_NETWORK_EVENT_REGISTER_FAILED;
        snprintf(event->text, sizeof(event->text), "%s", copy + 14);
        return 1;
    }
    if(strcmp(copy, "PEERS:BEGIN") == 0) {
        event->type = CHAT_NETWORK_EVENT_PEERS_BEGIN;
        return 1;
    }
    if(strcmp(copy, "PEERS:END") == 0) {
        event->type = CHAT_NETWORK_EVENT_PEERS_END;
        return 1;
    }
    if(strncmp(copy, "PEER ", 5) == 0) {
        int id, tcp_port, udp_port;
        char ip[40];
        char username[CHAT_NETWORK_USERNAME_MAX] = "";
        if(sscanf(copy + 5, "ID %d IP %39s TCP:%d UDP:%d USER:%63s",
                  &id, ip, &tcp_port, &udp_port, username) >= 4) {
            event->type = CHAT_NETWORK_EVENT_PEER;
            event->peer.id = id;
            event->peer.tcp_port = tcp_port;
            event->peer.udp_port = udp_port;
            snprintf(event->peer.ip, sizeof(event->peer.ip), "%s", ip);
            snprintf(event->peer.username, sizeof(event->peer.username), "%s", username);
            return 1;
        }
    }
    if(strncmp(copy, "HISTORY:BEGIN ID ", 17) == 0 &&
       sscanf(copy + 17, "%d", &event->peer.id) == 1) {
        event->type = CHAT_NETWORK_EVENT_HISTORY_BEGIN;
        return 1;
    }
    if(strncmp(copy, "HISTORY:LINE ID ", 16) == 0) {
        char *text = copy + 16;
        char *separator = strchr(text, ' ');
        if(separator) {
            *separator = '\0';
            event->peer.id = atoi(text);
            if(event->peer.id > 0) {
                event->type = CHAT_NETWORK_EVENT_HISTORY_LINE;
                snprintf(event->text, sizeof(event->text), "%s", separator + 1);
                return 1;
            }
        }
    }
    if(strncmp(copy, "HISTORY:END ID ", 15) == 0 &&
       sscanf(copy + 15, "%d", &event->peer.id) == 1) {
        event->type = CHAT_NETWORK_EVENT_HISTORY_END;
        return 1;
    }
    if(strncmp(copy, "EMOJI_BEGIN ", 12) == 0) {
        unsigned long long size;
        if(sscanf(copy + 12, "%127s %llu", event->emoji.name, &size) == 2) {
            event->type = CHAT_NETWORK_EVENT_EMOJI_BEGIN;
            event->emoji.size = (uint64_t)size;
            return 1;
        }
    }
    if(strcmp(copy, "EMOJI_END") == 0) {
        event->type = CHAT_NETWORK_EVENT_EMOJI_DONE;
        return 1;
    }
    if(strncmp(copy, "MEDIA:BEGIN ", 12) == 0) {
        char kind[16];
        unsigned long long size;
        if(sscanf(copy + 12, "KIND %15s FROM %d NAME %127s SIZE %llu HASH %64s",
                  kind, &event->media.sender_id, event->media.name, &size,
                  event->media.hash) == 5) {
            if(strcmp(kind, "EMOJI") == 0) event->media.kind = CHAT_NETWORK_MEDIA_EMOJI;
            else if(strcmp(kind, "FILE") == 0) event->media.kind = CHAT_NETWORK_MEDIA_FILE;
            else return 0;
            event->media.size = (uint64_t)size;
            event->type = CHAT_NETWORK_EVENT_MEDIA_BEGIN;
            return 1;
        }
    }
    if(strcmp(copy, "MEDIA:END") == 0) {
        event->type = CHAT_NETWORK_EVENT_MEDIA_END;
        return 1;
    }
    if(strncmp(copy, "FILE:UPLOAD HASH ", 17) == 0) {
        event->type = CHAT_NETWORK_EVENT_UPLOAD_REQUIRED;
        snprintf(event->text, sizeof(event->text), "%s", copy + 17);
        return 1;
    }
    if(strncmp(copy, "BROADCAST:", 10) == 0) {
        event->type = CHAT_NETWORK_EVENT_MESSAGE;
        snprintf(event->text, sizeof(event->text), "[广播] %s", copy + 10);
        return 1;
    }
    if(strncmp(copy, "MSG:", 4) == 0) {
        int id, tcp_port, udp_port;
        char ip[40];
        char username[CHAT_NETWORK_USERNAME_MAX] = "";
        if(sscanf(copy + 4, "ID %d IP %39s TCP:%d UDP:%d USER:%63s",
                  &id, ip, &tcp_port, &udp_port, username) >= 4) {
            event->type = CHAT_NETWORK_EVENT_PEER;
            event->peer.id = id;
            event->peer.tcp_port = tcp_port;
            event->peer.udp_port = udp_port;
            snprintf(event->peer.ip, sizeof(event->peer.ip), "%s", ip);
            snprintf(event->peer.username, sizeof(event->peer.username), "%s", username);
            return 1;
        }
        event->type = CHAT_NETWORK_EVENT_MESSAGE;
        snprintf(event->text, sizeof(event->text), "%s", copy + 4);
        return 1;
    }
    event->type = CHAT_NETWORK_EVENT_MESSAGE;
    snprintf(event->text, sizeof(event->text), "%s", copy);
    return 1;
}

static int send_all(int fd, const void *data, size_t length)
{
    const char *cursor = data;
    while(length > 0) {
        ssize_t sent = send(fd, cursor, length, MSG_NOSIGNAL);
        if(sent <= 0) return -1;
        cursor += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int send_command(const char *command)
{
    char buffer[CHAT_NETWORK_TEXT_MAX + 32];
    int fd;
    if(!command || strlen(command) > CHAT_NETWORK_TEXT_MAX - 2) return -1;
    snprintf(buffer, sizeof(buffer), "%s\n", command);
    pthread_mutex_lock(&network_lock);
    fd = tcp_socket;
    if(fd < 0 || send_all(fd, buffer, strlen(buffer)) < 0) {
        pthread_mutex_unlock(&network_lock);
        return -1;
    }
    pthread_mutex_unlock(&network_lock);
    return 0;
}

int chat_network_request_list(void)
{
    return send_command("list");
}

int chat_network_request_history(int id)
{
    char command[64];
    if(id <= 0) return -1;
    snprintf(command, sizeof(command), "history %d", id);
    return send_command(command);
}

int chat_network_send_broadcast(const char *message)
{
    char command[CHAT_NETWORK_TEXT_MAX];
    if(!message || !*message) return -1;
    snprintf(command, sizeof(command), "send %s", message);
    return send_command(command);
}

int chat_network_send_private(int id, const char *message)
{
    char command[CHAT_NETWORK_TEXT_MAX];
    if(id <= 0 || !message || !*message) return -1;
    snprintf(command, sizeof(command), "tell %d %s", id, message);
    return send_command(command);
}

int chat_network_send_emoji(int id, const char *name)
{
    char command[CHAT_NETWORK_TEXT_MAX];
    if(id <= 0 || !valid_emoji_name(name)) return -1;
    snprintf(command, sizeof(command), "emoji %d %s", id, name);
    return send_command(command);
}

static int hash_file(const char *path, char *hash, size_t hash_size, uint64_t *size)
{
    struct stat info;
    FILE *file;
    unsigned char *data;
    size_t read_count;

    if(!path || stat(path, &info) < 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
       (uint64_t)info.st_size > CHAT_MAX_ATTACHMENT_SIZE)
        return -1;
    file = fopen(path, "rb");
    if(!file) return -1;
    data = malloc(info.st_size ? (size_t)info.st_size : 1);
    if(!data) {
        fclose(file);
        return -1;
    }
    read_count = fread(data, 1, (size_t)info.st_size, file);
    fclose(file);
    if(read_count != (size_t)info.st_size ||
       chat_transfer_sha256_hex(data, read_count, hash, hash_size) < 0) {
        free(data);
        return -1;
    }
    free(data);
    if(size) *size = (uint64_t)info.st_size;
    return 0;
}

int chat_network_offer_file(int id, const char *path)
{
    const char *name;
    char hash[CHAT_NETWORK_FILE_HASH_MAX];
    char command[CHAT_NETWORK_TEXT_MAX];
    uint64_t size;

    if(id <= 0 || !path || !*path) return -1;
    name = strrchr(path, '/');
    if(!name) name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    if(!valid_emoji_name(name) || hash_file(path, hash, sizeof(hash), &size) < 0) return -1;
    snprintf(pending_upload_path, sizeof(pending_upload_path), "%s", path);
    snprintf(pending_upload_hash, sizeof(pending_upload_hash), "%s", hash);
    snprintf(command, sizeof(command), "FILE:OFFER TARGET %d NAME %s SIZE %llu HASH %s",
             id, name, (unsigned long long)size, hash);
    if(send_command(command) < 0) {
        pending_upload_path[0] = '\0';
        pending_upload_hash[0] = '\0';
        return -1;
    }
    return 0;
}

static void close_sockets(void)
{
    pthread_mutex_lock(&network_lock);
    if(tcp_socket >= 0) {
        shutdown(tcp_socket, SHUT_RDWR);
        close(tcp_socket);
        tcp_socket = -1;
    }
    if(udp_socket >= 0) {
        close(udp_socket);
        udp_socket = -1;
    }
    pthread_mutex_unlock(&network_lock);
}

static int create_emoji_directory(void)
{
    if(mkdir("./Emojis", 0755) < 0 && errno != EEXIST) return -1;
    return 0;
}

static void remove_directory_contents(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    char child[512];
    struct stat info;

    if(!directory) return;
    while((entry = readdir(directory)) != NULL) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if(lstat(child, &info) < 0) continue;
        if(S_ISDIR(info.st_mode)) {
            remove_directory_contents(child);
            rmdir(child);
        } else {
            unlink(child);
        }
    }
    closedir(directory);
}

void chat_network_cleanup_emojis(void)
{
    remove_directory_contents("./Emojis");
    rmdir("./Emojis");
}

static int valid_emoji_name(const char *name)
{
    if(!name || !*name || strlen(name) >= CHAT_NETWORK_EMOJI_NAME_MAX ||
       strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
       strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
        return 0;
    for(const char *cursor = name; *cursor; cursor++)
        if(isspace((unsigned char)*cursor)) return 0;
    return 1;
}

static int create_download_directory(void)
{
    if(mkdir("./Downloads", 0755) < 0 && errno != EEXIST) return -1;
    return 0;
}

static int send_pending_upload(const char *hash)
{
    FILE *file;
    char path[CHAT_NETWORK_MEDIA_PATH_MAX];
    char data[8192];
    size_t amount;
    int fd;

    if(!hash || strcmp(hash, pending_upload_hash) != 0 || !pending_upload_path[0]) return -1;
    snprintf(path, sizeof(path), "%s", pending_upload_path);
    file = fopen(path, "rb");
    if(!file) return -1;
    pthread_mutex_lock(&network_lock);
    fd = tcp_socket;
    while(fd >= 0 && (amount = fread(data, 1, sizeof(data), file)) > 0) {
        if(send_all(fd, data, amount) < 0) {
            fd = -1;
            break;
        }
    }
    pthread_mutex_unlock(&network_lock);
    fclose(file);
    pending_upload_path[0] = '\0';
    pending_upload_hash[0] = '\0';
    return fd >= 0 ? 0 : -1;
}

static int handle_line(char *line, FILE **emoji_file, uint64_t *emoji_remaining,
                       char *emoji_name, FILE **media_file,
                       uint64_t *media_remaining, chat_network_media_t *media)
{
    chat_network_event_t event;
    if(!chat_network_parse_line(line, &event)) return 0;

    if(event.type == CHAT_NETWORK_EVENT_AUTH_OK) {
        queue_push(event.type, event.text, &event.peer, NULL);
        send_command("emojis");
        return 1;
    } else if(event.type == CHAT_NETWORK_EVENT_AUTH_FAILED ||
              event.type == CHAT_NETWORK_EVENT_REGISTER_FAILED) {
        queue_push(event.type, event.text, NULL, NULL);
        return -1;
    } else if(event.type == CHAT_NETWORK_EVENT_REGISTER_OK) {
        queue_push(event.type, event.text, &event.peer, NULL);
        return 1;
    } else if(event.type == CHAT_NETWORK_EVENT_EMOJI_BEGIN) {
        if(*emoji_file) {
            fclose(*emoji_file);
            *emoji_file = NULL;
        }
        if(create_emoji_directory() == 0 && valid_emoji_name(event.emoji.name)) {
            snprintf(emoji_name, CHAT_NETWORK_EMOJI_NAME_MAX, "%s", event.emoji.name);
            char path[512];
            snprintf(path, sizeof(path), "./Emojis/%s", emoji_name);
            *emoji_file = fopen(path, "wb");
            *emoji_remaining = *emoji_file ? event.emoji.size : 0;
            queue_push(event.type, event.text, NULL, &event.emoji);
        }
    } else if(event.type == CHAT_NETWORK_EVENT_EMOJI_DONE) {
        queue_push(event.type, "表情包同步完成", NULL, NULL);
    } else if(event.type == CHAT_NETWORK_EVENT_MEDIA_BEGIN) {
        if(*media_file) {
            fclose(*media_file);
            *media_file = NULL;
        }
        *media = event.media;
        *media_remaining = event.media.size;
        if(create_download_directory() == 0 && valid_emoji_name(media->name)) {
            snprintf(media->path, sizeof(media->path), "./Downloads/%d_%s",
                     media->sender_id, media->name);
            *media_file = fopen(media->path, "wb");
        }
        queue_push_media(event.type, event.text, media);
    } else if(event.type == CHAT_NETWORK_EVENT_UPLOAD_REQUIRED) {
        if(send_pending_upload(event.text) == 0)
            queue_push(CHAT_NETWORK_EVENT_MESSAGE, "附件上传完成", NULL, NULL);
        else
            queue_push(CHAT_NETWORK_EVENT_MESSAGE, "附件上传失败", NULL, NULL);
    } else if(event.type == CHAT_NETWORK_EVENT_MEDIA_END) {
        return 0;
    } else {
        queue_push(event.type, event.text,
                   event.type == CHAT_NETWORK_EVENT_PEER ||
                   event.type == CHAT_NETWORK_EVENT_HISTORY_BEGIN ||
                   event.type == CHAT_NETWORK_EVENT_HISTORY_LINE ||
                   event.type == CHAT_NETWORK_EVENT_HISTORY_END ? &event.peer : NULL,
                   NULL);
    }
    return 0;
}

static int process_received_bytes(unsigned char *buffer, size_t *length,
                                  FILE **emoji_file, uint64_t *emoji_remaining,
                                  char *emoji_name, FILE **media_file,
                                  uint64_t *media_remaining, chat_network_media_t *media)
{
    int auth_result = 0;
    while(*length > 0) {
        if(*emoji_remaining > 0) {
            size_t amount = *emoji_remaining < *length
                          ? (size_t)*emoji_remaining : *length;
            if(*emoji_file) fwrite(buffer, 1, amount, *emoji_file);
            memmove(buffer, buffer + amount, *length - amount);
            *length -= amount;
            *emoji_remaining -= amount;
            if(*emoji_remaining == 0 && *emoji_file) {
                fclose(*emoji_file);
                *emoji_file = NULL;
                queue_push(CHAT_NETWORK_EVENT_EMOJI_COMPLETE, emoji_name, NULL, NULL);
            }
            continue;
        }
        if(*media_remaining > 0) {
            size_t amount = *media_remaining < *length
                          ? (size_t)*media_remaining : *length;
            if(*media_file) fwrite(buffer, 1, amount, *media_file);
            memmove(buffer, buffer + amount, *length - amount);
            *length -= amount;
            *media_remaining -= amount;
            if(*media_remaining == 0) {
                if(*media_file) {
                    fclose(*media_file);
                    *media_file = NULL;
                }
                queue_push_media(CHAT_NETWORK_EVENT_MEDIA_COMPLETE, media->name, media);
            }
            continue;
        }

        unsigned char *newline = memchr(buffer, '\n', *length);
        if(!newline) break;
        size_t raw_line_length = (size_t)(newline - buffer);
        size_t line_length = raw_line_length;
        char line[CHAT_NETWORK_TEXT_MAX];
        if(line_length >= sizeof(line)) line_length = sizeof(line) - 1;
        memcpy(line, buffer, line_length);
        line[line_length] = '\0';
        memmove(buffer, newline + 1, *length - raw_line_length - 1);
        *length -= raw_line_length + 1;
        int line_auth_result = handle_line(line, emoji_file, emoji_remaining, emoji_name,
                                           media_file, media_remaining, media);
        if(line_auth_result != 0) auth_result = line_auth_result;
    }
    return auth_result;
}

static int connect_tcp(const char *host)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *item;
    int fd = -1;
    int timed_out = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port[16];
    snprintf(port, sizeof(port), "%d", CHAT_SERVER_PORT);
    if(getaddrinfo(host, port, &hints, &result) != 0) return -1;
    for(item = result; item; item = item->ai_next) {
        int flags;
        int connect_result;
        fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if(fd < 0) continue;
        flags = fcntl(fd, F_GETFL, 0);
        if(flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            fd = -1;
            continue;
        }
        connect_result = connect(fd, item->ai_addr, item->ai_addrlen);
        if(connect_result < 0 && errno == EINPROGRESS) {
            fd_set write_set;
            struct timeval timeout = {CHAT_NETWORK_CONNECT_TIMEOUT_SECONDS, 0};
            int ready;
            int socket_error = 0;
            socklen_t socket_error_length = sizeof(socket_error);

            FD_ZERO(&write_set);
            FD_SET(fd, &write_set);
            ready = select(fd + 1, NULL, &write_set, NULL, &timeout);
            if(ready == 0) timed_out = 1;
            if(ready > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                                       &socket_error_length) == 0 && socket_error == 0)
                connect_result = 0;
        }
        if(connect_result == 0 && fcntl(fd, F_SETFL, flags) == 0) break;
        if(fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    return fd >= 0 ? fd : (timed_out ? -2 : -1);
}

static void *network_worker(void *unused)
{
    unsigned char buffer[CHAT_RX_BUFFER_SIZE];
    size_t buffer_length = 0;
    FILE *emoji_file = NULL;
    uint64_t emoji_remaining = 0;
    char emoji_name[CHAT_NETWORK_EMOJI_NAME_MAX] = "";
    FILE *media_file = NULL;
    uint64_t media_remaining = 0;
    chat_network_media_t media;
    memset(&media, 0, sizeof(media));
    (void)unused;

    queue_push(CHAT_NETWORK_EVENT_STATUS, "正在连接服务器...", NULL, NULL);
    int new_tcp = connect_tcp(login_host);
    if(new_tcp < 0) {
        network_running = 0;
        queue_push(CHAT_NETWORK_EVENT_AUTH_FAILED,
                   new_tcp == -2 ? "连接服务器超时，请检查服务器地址或网络连接" :
                                   "无法连接服务器",
                   NULL, NULL);
        return NULL;
    }

    int new_udp = -1;
    if(network_operation == NETWORK_OPERATION_LOGIN)
        new_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if(new_udp < 0) {
        if(network_operation == NETWORK_OPERATION_LOGIN) {
            close(new_tcp);
            network_running = 0;
            queue_push(CHAT_NETWORK_EVENT_AUTH_FAILED, "无法创建UDP套接字", NULL, NULL);
            return NULL;
        }
    }
    struct sockaddr_in udp_address;
    memset(&udp_address, 0, sizeof(udp_address));
    if(network_operation == NETWORK_OPERATION_LOGIN) {
        udp_address.sin_family = AF_INET;
        udp_address.sin_addr.s_addr = htonl(INADDR_ANY);
        udp_address.sin_port = htons(0);
        socklen_t udp_length = sizeof(udp_address);
        if(bind(new_udp, (struct sockaddr *)&udp_address, sizeof(udp_address)) < 0 ||
           getsockname(new_udp, (struct sockaddr *)&udp_address, &udp_length) < 0) {
            close(new_udp);
            close(new_tcp);
            network_running = 0;
            queue_push(CHAT_NETWORK_EVENT_AUTH_FAILED, "无法绑定UDP端口", NULL, NULL);
            return NULL;
        }
    }

    pthread_mutex_lock(&network_lock);
    tcp_socket = new_tcp;
    udp_socket = new_udp;
    pthread_mutex_unlock(&network_lock);

    char auth[CHAT_NETWORK_TEXT_MAX + 16];
    snprintf(auth, sizeof(auth), "%s %.63s %.447s\n",
             network_operation == NETWORK_OPERATION_REGISTER ? "REGISTER" : "AUTH",
             login_username, login_password);
    if(send_all(new_tcp, auth, strlen(auth)) < 0) {
        network_running = 0;
        close_sockets();
        queue_push(CHAT_NETWORK_EVENT_AUTH_FAILED, "认证请求发送失败", NULL, NULL);
        return NULL;
    }
    if(network_operation == NETWORK_OPERATION_LOGIN) {
        char udp_command[64];
        snprintf(udp_command, sizeof(udp_command), "udpport %d\n", ntohs(udp_address.sin_port));
        send_all(new_tcp, udp_command, strlen(udp_command));
    }

    time_t auth_deadline = time(NULL) + CHAT_NETWORK_AUTH_TIMEOUT_SECONDS;
    int authentication_pending = 1;
    queue_push(CHAT_NETWORK_EVENT_STATUS, "正在验证身份...", NULL, NULL);
    while(network_running) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(new_tcp, &read_set);
        int max_fd = new_tcp;
        if(new_udp >= 0) {
            FD_SET(new_udp, &read_set);
            if(new_udp > max_fd) max_fd = new_udp;
        }
        struct timeval timeout = {0, 500000};
        int ready = select(max_fd + 1, &read_set, NULL, NULL, &timeout);
        if(ready < 0) {
            if(errno == EINTR) continue;
            break;
        }
        if(ready == 0) {
            if(authentication_pending && time(NULL) >= auth_deadline) {
                network_running = 0;
                queue_push(CHAT_NETWORK_EVENT_AUTH_FAILED,
                           "登录超时，请检查服务器地址或网络连接", NULL, NULL);
                break;
            }
            continue;
        }
        if(FD_ISSET(new_tcp, &read_set)) {
            ssize_t received = recv(new_tcp, buffer + buffer_length,
                                    sizeof(buffer) - buffer_length, 0);
            if(received <= 0) break;
            buffer_length += (size_t)received;
            int auth_result = process_received_bytes(buffer, &buffer_length, &emoji_file,
                                                      &emoji_remaining, emoji_name, &media_file,
                                                      &media_remaining, &media);
            if(authentication_pending && auth_result != 0) {
                authentication_pending = 0;
                if(auth_result < 0) {
                    network_running = 0;
                    break;
                }
            }
        }
        if(new_udp >= 0 && FD_ISSET(new_udp, &read_set)) {
            ssize_t received = recvfrom(new_udp, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
            if(received > 0) {
                buffer[received] = '\0';
                char message[CHAT_NETWORK_TEXT_MAX];
                snprintf(message, sizeof(message), "[UDP] %.*s",
                         (int)sizeof(message) - 7, buffer);
                queue_push(CHAT_NETWORK_EVENT_MESSAGE, message, NULL, NULL);
            }
        }
    }

    if(emoji_file) fclose(emoji_file);
    if(media_file) fclose(media_file);
    network_running = 0;
    close_sockets();
    queue_push(CHAT_NETWORK_EVENT_DISCONNECTED, "服务器连接已断开", NULL, NULL);
    return NULL;
}

int chat_network_login(const char *host, const char *username, const char *password)
{
    if(!host || !*host || !username || !*username || !password) return -1;
    chat_network_disconnect();
    snprintf(login_host, sizeof(login_host), "%s", host);
    snprintf(login_username, sizeof(login_username), "%s", username);
    snprintf(login_password, sizeof(login_password), "%s", password);
    network_operation = NETWORK_OPERATION_LOGIN;
    network_running = 1;
    if(pthread_create(&network_thread, NULL, network_worker, NULL) != 0) {
        network_running = 0;
        return -1;
    }
    network_thread_started = 1;
    return 0;
}

int chat_network_register(const char *host, const char *username, const char *password)
{
    if(!host || !*host || !username || !*username || !password || !*password)
        return -1;
    chat_network_disconnect();
    snprintf(login_host, sizeof(login_host), "%s", host);
    snprintf(login_username, sizeof(login_username), "%s", username);
    snprintf(login_password, sizeof(login_password), "%s", password);
    network_operation = NETWORK_OPERATION_REGISTER;
    network_running = 1;
    if(pthread_create(&network_thread, NULL, network_worker, NULL) != 0) {
        network_running = 0;
        return -1;
    }
    network_thread_started = 1;
    return 0;
}

void chat_network_disconnect(void)
{
    network_running = 0;
    close_sockets();
    if(network_thread_started && !pthread_equal(pthread_self(), network_thread)) {
        pthread_join(network_thread, NULL);
        network_thread_started = 0;
    }
}

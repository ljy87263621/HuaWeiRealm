#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef PORT
#define PORT 8888
#endif
#define BUF 4096
#define MAX_ACCOUNTS 128
#define USERNAME_MAX 64
#define HISTORY_DIRECTORY "./ChatHistory"
#define SERVER_LOG_DIRECTORY "./ServerLogs"
#define CONFIG_DIRECTORY "./config"
#define EMOJIS_DIRECTORY "./Emojis"
#define ATTACHMENTS_DIRECTORY "./Attachments"
#define MAX_ATTACHMENT_SIZE (8U * 1024U * 1024U)

typedef struct {
    char username[USERNAME_MAX];
    char password[USERNAME_MAX];
} account_t;

typedef struct node {
    int fd;
    int id;
    int port;
    int udp_port;
    int authenticated;
    char username[USERNAME_MAX];
    char ip[40];
    struct node *next;
} node;

typedef struct {
    int active;
    int target_id;
    uint64_t remaining;
    char name[128];
    char hash[65];
    char temporary_path[512];
    FILE *file;
} upload_context_t;

static node *head;
static account_t accounts[MAX_ACCOUNTS];
static size_t account_count;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t account_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t console_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t socket_write_lock = PTHREAD_MUTEX_INITIALIZER;
static int next_id = 1;
static volatile int running = 1;

static void append_server_log(const char *message);

static int write_console_message(FILE *stream, const char *message)
{
    return fprintf(stream, "%s\n", message) < 0 ? -1 : 0;
}

static void log_msg(const char *format, ...)
{
    char message[BUF];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    pthread_mutex_lock(&console_lock);
    write_console_message(stdout, message);
    append_server_log(message);
    fflush(stdout);
    pthread_mutex_unlock(&console_lock);
}

static int send_all_unlocked(int fd, const void *data, size_t length)
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

static int send_all(int fd, const void *data, size_t length)
{
    int result;
    pthread_mutex_lock(&socket_write_lock);
    result = send_all_unlocked(fd, data, length);
    pthread_mutex_unlock(&socket_write_lock);
    return result;
}

static int send_line(int fd, const char *line)
{
    char buffer[BUF];
    int length = snprintf(buffer, sizeof(buffer), "%s\n", line);
    if(length < 0 || (size_t)length >= sizeof(buffer)) return -1;
    return send_all(fd, buffer, (size_t)length);
}

static void ensure_runtime_directories(void)
{
    mkdir("./config", 0755);
    mkdir("./Emojis", 0755);
    mkdir(HISTORY_DIRECTORY, 0755);
    mkdir(SERVER_LOG_DIRECTORY, 0755);
    mkdir(ATTACHMENTS_DIRECTORY, 0755);
}

static void append_server_log(const char *message)
{
    char timestamp[32];
    time_t now = time(NULL);
    struct tm local_time;
    FILE *file;

    if(!message || !*message || !localtime_r(&now, &local_time)) return;
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
    file = fopen(SERVER_LOG_DIRECTORY "/server.log", "a");
    if(!file) return;
    fprintf(file, "[%s] %s\n", timestamp, message);
    fclose(file);
}

static int encode_username(const char *username, char *encoded, size_t encoded_size)
{
    size_t length;

    if(!username || !*username) return -1;
    length = strlen(username);
    if(length > (encoded_size - 1) / 2) return -1;
    for(size_t index = 0; index < length; index++)
        snprintf(encoded + index * 2, encoded_size - index * 2, "%02x",
                 (unsigned char)username[index]);
    return 0;
}

static int history_path_for_users(const char *first_user, const char *second_user,
                                  char *path, size_t path_size)
{
    char first_encoded[USERNAME_MAX * 2 + 1];
    char second_encoded[USERNAME_MAX * 2 + 1];
    const char *left = first_user;
    const char *right = second_user;
    int length;

    if(!path || path_size == 0 || !first_user || !second_user) return -1;
    if(strcmp(left, right) > 0) {
        left = second_user;
        right = first_user;
    }
    if(encode_username(left, first_encoded, sizeof(first_encoded)) < 0 ||
       encode_username(right, second_encoded, sizeof(second_encoded)) < 0)
        return -1;
    length = snprintf(path, path_size, HISTORY_DIRECTORY "/%s--%s.txt",
                      first_encoded, second_encoded);
    return length < 0 || (size_t)length >= path_size ? -1 : 0;
}

static void append_chat_history(const char *sender, const char *receiver,
                                const char *message)
{
    char path[512];
    char timestamp[32];
    time_t now = time(NULL);
    struct tm local_time;
    FILE *file;

    if(!message || history_path_for_users(sender, receiver, path, sizeof(path)) < 0 ||
       !localtime_r(&now, &local_time))
        return;
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
    file = fopen(path, "a");
    if(!file) return;
    fprintf(file, "[%s] %s: %s\n", timestamp, sender, message);
    fclose(file);
}

static void load_accounts(void)
{
    FILE *file;
    char username[USERNAME_MAX];
    char password[USERNAME_MAX];

    ensure_runtime_directories();
    file = fopen("./config/Accounts.txt", "a+");
    if(!file) {
        log_msg("[账号] 无法打开 ./config/Accounts.txt");
        return;
    }
    fseek(file, 0, SEEK_END);
    if(ftell(file) == 0) {
        fputs("admin admin\n", file);
        fflush(file);
    }
    rewind(file);
    while(account_count < MAX_ACCOUNTS &&
          fscanf(file, "%63s %63s", username, password) == 2) {
        if(username[0] == '#') {
            char ignored[USERNAME_MAX];
            fscanf(file, "%63s", ignored);
            continue;
        }
        snprintf(accounts[account_count].username, USERNAME_MAX, "%s", username);
        snprintf(accounts[account_count].password, USERNAME_MAX, "%s", password);
        account_count++;
    }
    fclose(file);
    log_msg("[账号] 已加载 %u 个账号", (unsigned)account_count);
}

static int authenticate(const char *username, const char *password)
{
    int result = 0;
    pthread_mutex_lock(&account_lock);
    for(size_t index = 0; index < account_count; index++) {
        if(strcmp(accounts[index].username, username) == 0 &&
           strcmp(accounts[index].password, password) == 0)
            result = 1;
    }
    pthread_mutex_unlock(&account_lock);
    return result;
}

static int valid_account_field(const char *value)
{
    if(!value || !*value || value[0] == '#' || strlen(value) >= USERNAME_MAX) return 0;
    for(const char *cursor = value; *cursor; cursor++) {
        if(isspace((unsigned char)*cursor) || !isgraph((unsigned char)*cursor))
            return 0;
    }
    return 1;
}

static int register_account(const char *username, const char *password,
                            char *reason, size_t reason_size)
{
    if(!valid_account_field(username) || !valid_account_field(password)) {
        snprintf(reason, reason_size, "用户名和密码只能包含非空白字符");
        return 0;
    }

    pthread_mutex_lock(&account_lock);
    for(size_t index = 0; index < account_count; index++) {
        if(strcmp(accounts[index].username, username) == 0) {
            pthread_mutex_unlock(&account_lock);
            snprintf(reason, reason_size, "用户名已存在");
            return 0;
        }
    }
    if(account_count >= MAX_ACCOUNTS) {
        pthread_mutex_unlock(&account_lock);
        snprintf(reason, reason_size, "账号数量已达到上限");
        return 0;
    }

    FILE *file = fopen("./config/Accounts.txt", "a");
    if(!file) {
        pthread_mutex_unlock(&account_lock);
        snprintf(reason, reason_size, "无法保存账号");
        return 0;
    }
    int written = fprintf(file, "%s %s\n", username, password);
    if(written < 0 || fclose(file) != 0) {
        pthread_mutex_unlock(&account_lock);
        snprintf(reason, reason_size, "无法保存账号");
        return 0;
    }
    snprintf(accounts[account_count].username, USERNAME_MAX, "%s", username);
    snprintf(accounts[account_count].password, USERNAME_MAX, "%s", password);
    account_count++;
    pthread_mutex_unlock(&account_lock);
    return 1;
}

static node *find_by_fd(int fd)
{
    node *result = NULL;
    pthread_mutex_lock(&lock);
    for(node *current = head; current; current = current->next) {
        if(current->fd == fd) {
            result = current;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static node *find_by_id(int id)
{
    node *result = NULL;
    pthread_mutex_lock(&lock);
    for(node *current = head; current; current = current->next) {
        if(current->id == id) {
            result = current;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static void send_peer_list_locked(int fd)
{
    send_line(fd, "PEERS:BEGIN");
    for(node *current = head; current; current = current->next) {
        char line[BUF];
        if(!current->authenticated) continue;
        snprintf(line, sizeof(line), "PEER ID %d IP %s TCP:%d UDP:%d USER:%s",
                 current->id, current->ip, current->port, current->udp_port,
                 current->username);
        if(send_line(fd, line) < 0) break;
    }
    send_line(fd, "PEERS:END");
}

static void send_peer_list(int fd)
{
    pthread_mutex_lock(&lock);
    send_peer_list_locked(fd);
    pthread_mutex_unlock(&lock);
}

static void broadcast_peer_list(void)
{
    pthread_mutex_lock(&lock);
    for(node *current = head; current; current = current->next)
        if(current->authenticated) send_peer_list_locked(current->fd);
    pthread_mutex_unlock(&lock);
}

static void add_client(int fd, const struct sockaddr_in *address)
{
    node *client = calloc(1, sizeof(*client));
    if(!client) {
        close(fd);
        return;
    }
    client->fd = fd;
    client->id = next_id++;
    client->port = ntohs(address->sin_port);
    inet_ntop(AF_INET, &address->sin_addr, client->ip, sizeof(client->ip));
    pthread_mutex_lock(&lock);
    client->next = head;
    head = client;
    pthread_mutex_unlock(&lock);
    log_msg("+ [连接 %d] %s:%d，等待认证", client->id, client->ip, client->port);
}

static void remove_client(int fd)
{
    int removed_id = 0;
    int membership_changed = 0;
    char description[USERNAME_MAX];

    pthread_mutex_lock(&lock);
    node *previous = NULL;
    node *current = head;
    while(current && current->fd != fd) {
        previous = current;
        current = current->next;
    }
    if(current) {
        if(previous) previous->next = current->next;
        else head = current->next;
        removed_id = current->id;
        membership_changed = current->authenticated;
        snprintf(description, sizeof(description), "%s",
                 current->authenticated ? current->username : "未认证客户端");
        close(current->fd);
        free(current);
    }
    pthread_mutex_unlock(&lock);
    if(removed_id) log_msg("- [%d] %s", removed_id, description);
    if(membership_changed) broadcast_peer_list();
}

static void send_private(int id, const char *message)
{
    node *client = find_by_id(id);
    if(!client || !client->authenticated) return;
    char buffer[BUF];
    snprintf(buffer, sizeof(buffer), "MSG:%s", message);
    send_line(client->fd, buffer);
}

static void broadcast_message(const char *message)
{
    char buffer[BUF];
    snprintf(buffer, sizeof(buffer), "BROADCAST:%s", message);
    pthread_mutex_lock(&lock);
    for(node *current = head; current; current = current->next) {
        if(current->authenticated) send_line(current->fd, buffer);
    }
    pthread_mutex_unlock(&lock);
    log_msg("[广播] %s", message);
}

static int valid_filename(const char *name)
{
    if(!name || !*name || strlen(name) >= 128 ||
       strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
       strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
        return 0;
    for(const char *cursor = name; *cursor; cursor++)
        if(isspace((unsigned char)*cursor)) return 0;
    return 1;
}

static int valid_file_hash(const char *hash)
{
    if(!hash || strlen(hash) != 64) return 0;
    for(const char *cursor = hash; *cursor; cursor++)
        if(!isxdigit((unsigned char)*cursor)) return 0;
    return 1;
}

static int attachment_path_for_hash(const char *hash, char *path, size_t path_size)
{
    int length;
    if(!valid_file_hash(hash) || !path || path_size == 0) return -1;
    length = snprintf(path, path_size, ATTACHMENTS_DIRECTORY "/%s", hash);
    return length < 0 || (size_t)length >= path_size ? -1 : 0;
}

static int send_media_file(node *receiver, const char *kind, int sender_id,
                           const char *name, const char *path, const char *hash)
{
    struct stat info;
    FILE *file;
    char header[512];
    char buffer[BUF];
    size_t amount;
    int result = -1;

    if(!receiver || !receiver->authenticated || !valid_filename(name) ||
       !kind || !hash || stat(path, &info) < 0 || !S_ISREG(info.st_mode) ||
       info.st_size < 0 || (uint64_t)info.st_size > MAX_ATTACHMENT_SIZE)
        return -1;
    file = fopen(path, "rb");
    if(!file) return -1;
    if(snprintf(header, sizeof(header),
                "MEDIA:BEGIN KIND %s FROM %d NAME %s SIZE %llu HASH %s\n",
                kind, sender_id, name, (unsigned long long)info.st_size, hash) >=
       (int)sizeof(header)) {
        fclose(file);
        return -1;
    }

    pthread_mutex_lock(&socket_write_lock);
    if(send_all_unlocked(receiver->fd, header, strlen(header)) < 0) goto done;
    while((amount = fread(buffer, 1, sizeof(buffer), file)) > 0)
        if(send_all_unlocked(receiver->fd, buffer, amount) < 0) goto done;
    if(ferror(file) || send_all_unlocked(receiver->fd, "MEDIA:END\n", 10) < 0) goto done;
    result = 0;
done:
    pthread_mutex_unlock(&socket_write_lock);
    fclose(file);
    return result;
}

static void reset_upload(upload_context_t *upload)
{
    if(!upload) return;
    if(upload->file) fclose(upload->file);
    memset(upload, 0, sizeof(*upload));
}

static void finish_upload(node *sender, upload_context_t *upload)
{
    char cache_path[512];
    node *receiver;

    if(!sender || !upload) return;
    if(upload->file) {
        fclose(upload->file);
        upload->file = NULL;
    }
    if(attachment_path_for_hash(upload->hash, cache_path, sizeof(cache_path)) < 0 ||
       rename(upload->temporary_path, cache_path) < 0) {
        unlink(upload->temporary_path);
        send_private(sender->id, "附件保存失败");
        reset_upload(upload);
        return;
    }
    receiver = find_by_id(upload->target_id);
    if(!receiver || send_media_file(receiver, "FILE", sender->id, upload->name,
                                    cache_path, upload->hash) < 0)
        send_private(sender->id, "附件已缓存，但目标客户端不可用");
    else {
        char history[256];
        snprintf(history, sizeof(history), "[附件] %s %s", upload->name, upload->hash);
        append_chat_history(sender->username, receiver->username, history);
        send_private(sender->id, "附件已上传并转发");
        log_msg("[附件] %s 上传并转发给 %s", upload->name, receiver->username);
    }
    reset_upload(upload);
}

static void offer_attachment(node *sender, int target_id, const char *name,
                             uint64_t size, const char *hash, upload_context_t *upload)
{
    char cache_path[512];
    char temporary_path[512];
    struct stat info;
    node *receiver;

    if(!sender || !upload || !valid_filename(name) || !valid_file_hash(hash) ||
       size > MAX_ATTACHMENT_SIZE || target_id <= 0) {
        send_private(sender ? sender->id : 0, "附件请求无效");
        return;
    }
    receiver = find_by_id(target_id);
    if(!receiver || !receiver->authenticated) {
        send_private(sender->id, "目标客户端不存在");
        return;
    }
    if(attachment_path_for_hash(hash, cache_path, sizeof(cache_path)) < 0) return;
    if(stat(cache_path, &info) == 0 && S_ISREG(info.st_mode) &&
       (uint64_t)info.st_size == size) {
        if(send_media_file(receiver, "FILE", sender->id, name, cache_path, hash) == 0) {
            char history[256];
            snprintf(history, sizeof(history), "[附件] %s %s", name, hash);
            append_chat_history(sender->username, receiver->username, history);
            send_private(sender->id, "附件命中服务器缓存并已转发");
            log_msg("[附件缓存] %s 转发给 %s", name, receiver->username);
        } else send_private(sender->id, "目标客户端不可用");
        return;
    }
    snprintf(temporary_path, sizeof(temporary_path), ATTACHMENTS_DIRECTORY "/.%s.part", hash);
    reset_upload(upload);
    upload->file = fopen(temporary_path, "wb");
    if(!upload->file) {
        send_private(sender->id, "服务器无法创建附件");
        return;
    }
    upload->active = 1;
    upload->target_id = target_id;
    upload->remaining = size;
    snprintf(upload->name, sizeof(upload->name), "%s", name);
    snprintf(upload->hash, sizeof(upload->hash), "%s", hash);
    snprintf(upload->temporary_path, sizeof(upload->temporary_path), "%s", temporary_path);
    {
        char response[128];
        snprintf(response, sizeof(response), "FILE:UPLOAD HASH %s", hash);
        if(size == 0) finish_upload(sender, upload);
        else if(send_line(sender->fd, response) < 0) reset_upload(upload);
    }
}

static void send_emoji_to_peer(node *sender, int target_id, const char *name)
{
    char path[512];
    node *receiver;

    if(!sender || !valid_filename(name)) {
        if(sender) send_private(sender->id, "表情名称无效");
        return;
    }
    receiver = find_by_id(target_id);
    if(!receiver || !receiver->authenticated) {
        send_private(sender->id, "目标客户端不存在");
        return;
    }
    snprintf(path, sizeof(path), EMOJIS_DIRECTORY "/%s", name);
    if(send_media_file(receiver, "EMOJI", sender->id, name, path, "-") < 0) {
        send_private(sender->id, "表情转发失败");
        return;
    }
    {
        char history[160];
        snprintf(history, sizeof(history), "[表情] %s", name);
        append_chat_history(sender->username, receiver->username, history);
    }
    send_private(sender->id, "表情已转发");
}

static void send_file_to_client(int id, const char *name)
{
    node *client = find_by_id(id);
    if(!client || !client->authenticated) return;
    if(!valid_filename(name)) {
        send_private(id, "文件名无效");
        return;
    }
    FILE *file = fopen(name, "rb");
    if(!file) {
        send_private(id, "文件不存在");
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char header[128];
    snprintf(header, sizeof(header), "FILESIZE:%ld", size);
    if(send_line(client->fd, header) < 0) {
        fclose(file);
        return;
    }
    char buffer[BUF];
    size_t amount;
    while((amount = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if(send_all(client->fd, buffer, amount) < 0) break;
    }
    fclose(file);
    log_msg("[文件] 发送 %s 给 %d", name, id);
}

static void send_emoji_bundle(int fd)
{
    DIR *directory = opendir("./Emojis");
    struct dirent *entry;
    if(!directory) {
        send_line(fd, "EMOJI_END");
        return;
    }
    while((entry = readdir(directory)) != NULL) {
        if(!valid_filename(entry->d_name)) continue;
        char path[512];
        snprintf(path, sizeof(path), "./Emojis/%s", entry->d_name);
        struct stat info;
        if(stat(path, &info) < 0 || !S_ISREG(info.st_mode)) continue;
        FILE *file = fopen(path, "rb");
        if(!file) continue;
        char header[256];
        snprintf(header, sizeof(header), "EMOJI_BEGIN %.127s %lld",
                 entry->d_name, (long long)info.st_size);
        if(send_line(fd, header) < 0) {
            fclose(file);
            break;
        }
        char buffer[BUF];
        size_t amount;
        while((amount = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            if(send_all(fd, buffer, amount) < 0) break;
        }
        fclose(file);
    }
    closedir(directory);
    send_line(fd, "EMOJI_END");
}

static void tell_client(int sender_id, int receiver_id, const char *message)
{
    node *sender = find_by_id(sender_id);
    node *receiver = find_by_id(receiver_id);
    if(!sender || !receiver || !sender->authenticated || !receiver->authenticated) {
        send_private(sender_id, "目标客户端不存在");
        return;
    }
    char line[BUF];
    snprintf(line, sizeof(line), "[%d] %s -> [%d] %s : %s",
             sender_id, sender->username, receiver_id, receiver->username, message);
    if(send_line(receiver->fd, line) < 0)
        send_private(sender_id, "消息发送失败");
    else {
        append_chat_history(sender->username, receiver->username, message);
        send_private(sender_id, "消息已转发成功");
    }
}

static void send_chat_history(node *requester, int peer_id)
{
    char path[512];
    char line[BUF];
    char response[BUF];
    node *peer;
    FILE *file;

    if(!requester || peer_id <= 0) return;
    peer = find_by_id(peer_id);
    if(!peer || !peer->authenticated ||
       history_path_for_users(requester->username, peer->username, path, sizeof(path)) < 0) {
        send_private(requester->id, "目标客户端不存在");
        return;
    }

    snprintf(response, sizeof(response), "HISTORY:BEGIN ID %d", peer_id);
    if(send_line(requester->fd, response) < 0) return;
    file = fopen(path, "r");
    if(file) {
        while(fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\r\n")] = '\0';
            snprintf(response, sizeof(response), "HISTORY:LINE ID %d %.*s", peer_id,
                     (int)sizeof(response) - 32, line);
            if(send_line(requester->fd, response) < 0) break;
        }
        fclose(file);
    }
    snprintf(response, sizeof(response), "HISTORY:END ID %d", peer_id);
    send_line(requester->fd, response);
}

static void handle_client_line(node *self, char *line, upload_context_t *upload)
{
    if(strcmp(line, "quit") == 0) {
        remove_client(self->fd);
        return;
    }

    if(!self->authenticated) {
        char username[USERNAME_MAX];
        char password[USERNAME_MAX];
        char reason[256];
        if(sscanf(line, "REGISTER %63s %63s", username, password) == 2) {
            if(register_account(username, password, reason, sizeof(reason))) {
                char response[BUF];
                snprintf(response, sizeof(response), "REGISTER:OK USER %s", username);
                send_line(self->fd, response);
                log_msg("[注册成功] %s", username);
            } else {
                char response[BUF];
                snprintf(response, sizeof(response), "REGISTER:FAIL %s", reason);
                send_line(self->fd, response);
                log_msg("[注册失败] %s: %s", username, reason);
            }
            return;
        }
        if(sscanf(line, "AUTH %63s %63s", username, password) != 2) {
            send_line(self->fd, "AUTH:FAIL 请先进行认证");
            return;
        }
        if(!authenticate(username, password)) {
            send_line(self->fd, "AUTH:FAIL 用户名或密码错误");
            log_msg("[认证失败] %s", username);
            return;
        }
        self->authenticated = 1;
        snprintf(self->username, sizeof(self->username), "%s", username);
        char response[BUF];
        snprintf(response, sizeof(response), "AUTH:OK ID %d USER %s",
                 self->id, self->username);
        send_line(self->fd, response);
        log_msg("[认证成功] [%d] %s", self->id, self->username);
        broadcast_peer_list();
        return;
    }

    if(strncmp(line, "udpport ", 8) == 0) {
        int udp_port;
        if(sscanf(line + 8, "%d", &udp_port) == 1 &&
           udp_port > 0 && udp_port <= 65535) {
            self->udp_port = udp_port;
            send_private(self->id, "UDP端口注册成功");
        } else {
            send_private(self->id, "UDP端口无效");
        }
    } else if(strncmp(line, "history ", 8) == 0) {
        int peer_id;
        if(sscanf(line + 8, "%d", &peer_id) == 1)
            send_chat_history(self, peer_id);
        else
            send_private(self->id, "用法: history <id>");
    } else if(strcmp(line, "list") == 0) {
        send_peer_list(self->fd);
    } else if(strcmp(line, "emojis") == 0) {
        send_emoji_bundle(self->fd);
    } else if(strncmp(line, "emoji ", 6) == 0) {
        int target_id;
        char name[128];
        if(sscanf(line + 6, "%d %127s", &target_id, name) == 2)
            send_emoji_to_peer(self, target_id, name);
        else
            send_private(self->id, "用法: emoji <id> <name>");
    } else if(strncmp(line, "FILE:OFFER ", 11) == 0) {
        int target_id;
        char name[128];
        char hash[65];
        unsigned long long size;
        if(sscanf(line + 11, "TARGET %d NAME %127s SIZE %llu HASH %64s",
                  &target_id, name, &size, hash) == 4)
            offer_attachment(self, target_id, name, (uint64_t)size, hash, upload);
        else
            send_private(self->id, "附件请求格式无效");
    } else if(strcmp(line, "help") == 0) {
        send_private(self->id,
                     "命令: list, emojis, emoji <id> <name>, tell <id> <message>, quit");
    } else if(strncmp(line, "get ", 4) == 0) {
        char *name = line + 4;
        while(*name == ' ') name++;
        send_file_to_client(self->id, name);
    } else if(strncmp(line, "tell ", 5) == 0) {
        int target_id;
        char *message = line + 5;
        if(sscanf(message, "%d", &target_id) != 1) {
            send_private(self->id, "用法: tell <id> <message>");
        } else {
            while(*message && *message != ' ') message++;
            while(*message == ' ') message++;
            if(*message) tell_client(self->id, target_id, message);
            else send_private(self->id, "用法: tell <id> <message>");
        }
    } else if(strncmp(line, "broadcast ", 10) == 0) {
        char message[BUF];
        snprintf(message, sizeof(message), "[%d] %s", self->id, line + 10);
        broadcast_message(message);
    } else if(strncmp(line, "send ", 5) == 0) {
        char message[BUF];
        snprintf(message, sizeof(message), "[%d] %s", self->id, line + 5);
        broadcast_message(message);
    } else {
        send_private(self->id, "未知命令，请输入 help 查看可用命令");
    }
}

static void *client_thread(void *argument)
{
    int fd = (int)(intptr_t)argument;
    char buffer[BUF];
    char line[BUF];
    size_t line_length = 0;
    upload_context_t upload;
    memset(&upload, 0, sizeof(upload));
    while(running) {
        ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        if(received <= 0) break;
        for(ssize_t index = 0; index < received; index++) {
            if(upload.active) {
                size_t available = (size_t)(received - index);
                size_t amount = upload.remaining < available ? (size_t)upload.remaining : available;
                if(fwrite(buffer + index, 1, amount, upload.file) != amount) {
                    send_private(fd, "附件写入失败");
                    unlink(upload.temporary_path);
                    reset_upload(&upload);
                    break;
                }
                index += (ssize_t)amount - 1;
                upload.remaining -= amount;
                if(upload.remaining == 0) finish_upload(find_by_fd(fd), &upload);
                continue;
            }
            if(buffer[index] == '\n') {
                line[line_length] = '\0';
                node *self = find_by_fd(fd);
                if(!self) return NULL;
                handle_client_line(self, line, &upload);
                if(strcmp(line, "quit") == 0) return NULL;
                line_length = 0;
            } else if(line_length < sizeof(line) - 1) {
                line[line_length++] = buffer[index];
            }
        }
    }
    if(upload.active) {
        unlink(upload.temporary_path);
        reset_upload(&upload);
    }
    remove_client(fd);
    return NULL;
}

static void handle_server_command(const char *command)
{
    if(strcmp(command, "quit") == 0) {
        running = 0;
        log_msg("[系统] 服务器退出");
    } else if(strcmp(command, "list") == 0) {
        pthread_mutex_lock(&lock);
        for(node *current = head; current; current = current->next)
            if(current->authenticated)
                log_msg("ID %d  %s  %s:%d  UDP:%d",
                        current->id, current->username, current->ip,
                        current->port, current->udp_port);
        pthread_mutex_unlock(&lock);
    } else if(strncmp(command, "send ", 5) == 0) {
        int id;
        char *message = (char *)command + 5;
        if(sscanf(message, "%d", &id) != 1) {
            log_msg("用法: send <id> <message>");
            return;
        }
        while(*message && *message != ' ') message++;
        while(*message == ' ') message++;
        if(*message) send_private(id, message);
        else log_msg("用法: send <id> <message>");
    } else if(strncmp(command, "broadcast ", 10) == 0) {
        broadcast_message(command + 10);
    } else {
        log_msg("未知命令: %s", command);
    }
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    load_accounts();

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if(server < 0) {
        perror("socket");
        return 1;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);
    if(bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 ||
       listen(server, 10) < 0) {
        perror("bind/listen");
        close(server);
        return 1;
    }

    log_msg("[系统] 服务器启动，监听端口 %d", PORT);

    char command[BUF];
    int console_open = 1;
    while(running) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(server, &set);
        if(console_open) FD_SET(STDIN_FILENO, &set);
        int max_fd = console_open && STDIN_FILENO > server ? STDIN_FILENO : server;
        int ready = select(max_fd + 1, &set, NULL, NULL, NULL);
        if(ready < 0) {
            if(errno == EINTR) continue;
            perror("select");
            break;
        }

        if(FD_ISSET(server, &set)) {
            struct sockaddr_in client_address;
            socklen_t length = sizeof(client_address);
            int client = accept(server, (struct sockaddr *)&client_address, &length);
            if(client >= 0) {
                add_client(client, &client_address);
                pthread_t thread;
                if(pthread_create(&thread, NULL, client_thread,
                                  (void *)(intptr_t)client) == 0)
                    pthread_detach(thread);
                else
                    remove_client(client);
            }
        }

        if(console_open && FD_ISSET(STDIN_FILENO, &set)) {
            if(!fgets(command, sizeof(command), stdin)) {
                console_open = 0;
            } else {
                command[strcspn(command, "\r\n")] = '\0';
                if(command[0]) handle_server_command(command);
            }
        }
    }

    close(server);
    pthread_mutex_lock(&lock);
    for(node *current = head; current; current = current->next)
        shutdown(current->fd, SHUT_RDWR);
    pthread_mutex_unlock(&lock);
    return 0;
}

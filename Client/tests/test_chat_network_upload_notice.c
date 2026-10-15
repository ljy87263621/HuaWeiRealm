#define _DEFAULT_SOURCE

#include "../mycode/chat_network.c"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    const char *path = "./upload-notice-test.txt";
    const char *hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    int sockets[2];
    FILE *file = fopen(path, "wb");
    chat_network_event_t event;
    FILE *emoji_file = NULL;
    FILE *media_file = NULL;
    uint64_t emoji_remaining = 0;
    uint64_t media_remaining = 0;
    char emoji_name[CHAT_NETWORK_EMOJI_NAME_MAX] = "";
    chat_network_media_t media;
    char command[96];
    char byte;

    assert(file != NULL);
    assert(fputs("x", file) >= 0);
    assert(fclose(file) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    tcp_socket = sockets[0];
    snprintf(pending_upload_path, sizeof(pending_upload_path), "%s", path);
    snprintf(pending_upload_hash, sizeof(pending_upload_hash), "%s", hash);

    memset(&media, 0, sizeof(media));
    snprintf(command, sizeof(command), "FILE:UPLOAD HASH %s", hash);
    handle_line(command, &emoji_file, &emoji_remaining, emoji_name,
                &media_file, &media_remaining, &media);
    assert(read(sockets[1], &byte, 1) == 1 && byte == 'x');
    assert(chat_network_poll_event(&event));
    assert(event.type == CHAT_NETWORK_EVENT_MESSAGE);
    assert(strcmp(event.text, "附件上传完成") == 0);

    close(sockets[0]);
    close(sockets[1]);
    tcp_socket = -1;
    unlink(path);
    return 0;
}

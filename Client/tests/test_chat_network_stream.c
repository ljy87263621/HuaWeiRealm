#include "../mycode/chat_network.c"

#include <assert.h>
#include <string.h>

int main(void)
{
    unsigned char buffer[1024];
    size_t length = 0;
    FILE *emoji_file = NULL;
    uint64_t emoji_remaining = 0;
    char emoji_name[CHAT_NETWORK_EMOJI_NAME_MAX] = "";
    FILE *media_file = NULL;
    uint64_t media_remaining = 0;
    chat_network_media_t media;
    chat_network_event_t event;

    memset(buffer, 'x', 600);
    buffer[600] = '\n';
    memcpy(buffer + 601, "MSG:next\n", 9);
    length = 610;
    process_received_bytes(buffer, &length, &emoji_file, &emoji_remaining, emoji_name,
                           &media_file, &media_remaining, &media);

    assert(chat_network_poll_event(&event));
    assert(event.type == CHAT_NETWORK_EVENT_MESSAGE);
    assert(chat_network_poll_event(&event));
    assert(event.type == CHAT_NETWORK_EVENT_MESSAGE);
    assert(strcmp(event.text, "next") == 0);
    assert(length == 0);

    const char *media_frame = "MEDIA:BEGIN KIND FILE FROM 7 NAME hello.txt SIZE 4 HASH abc\nDATA"
                              "MEDIA:END\n";
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, media_frame, strlen(media_frame));
    length = strlen(media_frame);
    process_received_bytes(buffer, &length, &emoji_file, &emoji_remaining, emoji_name,
                           &media_file, &media_remaining, &media);

    assert(chat_network_poll_event(&event));
    assert(event.type == CHAT_NETWORK_EVENT_MEDIA_BEGIN);
    assert(event.media.kind == CHAT_NETWORK_MEDIA_FILE);
    assert(event.media.sender_id == 7);
    assert(strcmp(event.media.name, "hello.txt") == 0);
    assert(chat_network_poll_event(&event));
    assert(event.type == CHAT_NETWORK_EVENT_MEDIA_COMPLETE);
    assert(event.media.size == 4);
    assert(strcmp(event.media.name, "hello.txt") == 0);
    assert(length == 0);
    return 0;
}

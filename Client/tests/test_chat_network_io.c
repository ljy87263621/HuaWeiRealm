#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../mycode/chat_network.h"

int main(void)
{
    chat_network_event_t event;
    int saw_auth = 0;
    int saw_peer = 0;
    int saw_message = 0;
    int saw_emoji = 0;

    assert(chat_network_login("127.0.0.1", "alice", "secret") == 0);
    for(int attempt = 0; attempt < 30 && !saw_auth; attempt++) {
        while(chat_network_poll_event(&event)) {
            if(event.type == CHAT_NETWORK_EVENT_AUTH_OK) {
                saw_auth = 1;
                break;
            }
        }
        usleep(100000);
    }

    assert(saw_auth);

    assert(chat_network_request_list() == 0);
    assert(chat_network_send_broadcast("io-test") == 0);

    for(int attempt = 0; attempt < 30 &&
        (!saw_peer || !saw_message || !saw_emoji); attempt++) {
        while(chat_network_poll_event(&event)) {
            if(event.type == CHAT_NETWORK_EVENT_PEER && event.peer.id > 0)
                saw_peer = 1;
            if(event.type == CHAT_NETWORK_EVENT_MESSAGE &&
               strstr(event.text, "io-test") != NULL)
                saw_message = 1;
            if(event.type == CHAT_NETWORK_EVENT_EMOJI_COMPLETE &&
               strcmp(event.text, "wave.txt") == 0)
                saw_emoji = 1;
        }
        usleep(100000);
    }

    chat_network_disconnect();
    chat_network_cleanup_emojis();
    assert(saw_peer);
    assert(saw_message);
    assert(saw_emoji);
    puts("chat_network_io=ok");
    return 0;
}

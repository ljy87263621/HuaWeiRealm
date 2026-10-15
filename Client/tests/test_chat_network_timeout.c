#define _DEFAULT_SOURCE
#define CHAT_SERVER_PORT 18888
#define CHAT_NETWORK_AUTH_TIMEOUT_SECONDS 1

#include "../mycode/chat_network.c"

#include <assert.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define TEST_SERVER_PORT 18888

static void *silent_server(void *argument)
{
    int listener = *(int *)argument;
    fd_set read_set;
    struct timeval timeout = {4, 0};
    int client = -1;

    FD_ZERO(&read_set);
    FD_SET(listener, &read_set);
    if(select(listener + 1, &read_set, NULL, NULL, &timeout) > 0)
        client = accept(listener, NULL, NULL);

    if(client >= 0) {
        sleep(3);
        close(client);
    }
    return NULL;
}

int main(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    struct sockaddr_in address;
    pthread_t thread;
    chat_network_event_t event;
    int saw_timeout = 0;

    assert(listener >= 0);
    assert(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(TEST_SERVER_PORT);
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    assert(pthread_create(&thread, NULL, silent_server, &listener) == 0);
    assert(chat_network_login("127.0.0.1", "timeout-user", "secret") == 0);

    for(int attempt = 0; attempt < 30 && !saw_timeout; attempt++) {
        while(chat_network_poll_event(&event)) {
            if(event.type == CHAT_NETWORK_EVENT_AUTH_FAILED &&
               strcmp(event.text, "登录超时，请检查服务器地址或网络连接") == 0)
                saw_timeout = 1;
        }
        usleep(100000);
    }

    chat_network_disconnect();
    pthread_join(thread, NULL);
    close(listener);
    assert(saw_timeout);
    return 0;
}

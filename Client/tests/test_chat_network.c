#include <assert.h>
#include <string.h>

#include "../mycode/chat_network.h"

static void test_broadcast_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("BROADCAST:[3] hello", &event));
    assert(event.type == CHAT_NETWORK_EVENT_MESSAGE);
    assert(strcmp(event.text, "[广播] [3] hello") == 0);
}

static void test_peer_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("MSG:ID 7 IP 10.0.0.8 TCP:41234 UDP:54321", &event));
    assert(event.type == CHAT_NETWORK_EVENT_PEER);
    assert(event.peer.id == 7);
    assert(strcmp(event.peer.ip, "10.0.0.8") == 0);
    assert(event.peer.tcp_port == 41234);
    assert(event.peer.udp_port == 54321);
}

static void test_private_message_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("MSG:消息已转发成功", &event));
    assert(event.type == CHAT_NETWORK_EVENT_MESSAGE);
    assert(strcmp(event.text, "消息已转发成功") == 0);
}

static void test_server_feedback_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("未知命令，请输入 help 查看可用命令", &event));
    assert(event.type == CHAT_NETWORK_EVENT_MESSAGE);
    assert(strcmp(event.text, "未知命令，请输入 help 查看可用命令") == 0);
}

static void test_auth_success_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("AUTH:OK ID 4 USER alice", &event));
    assert(event.type == CHAT_NETWORK_EVENT_AUTH_OK);
    assert(event.peer.id == 4);
    assert(strcmp(event.peer.username, "alice") == 0);
}

static void test_auth_failure_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("AUTH:FAIL invalid credentials", &event));
    assert(event.type == CHAT_NETWORK_EVENT_AUTH_FAILED);
    assert(strcmp(event.text, "invalid credentials") == 0);
}

static void test_register_success_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("REGISTER:OK USER alice", &event));
    assert(event.type == CHAT_NETWORK_EVENT_REGISTER_OK);
    assert(strcmp(event.peer.username, "alice") == 0);
}

static void test_register_failure_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("REGISTER:FAIL username exists", &event));
    assert(event.type == CHAT_NETWORK_EVENT_REGISTER_FAILED);
    assert(strcmp(event.text, "username exists") == 0);
}

static void test_emoji_begin_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("EMOJI_BEGIN wave.png 1234", &event));
    assert(event.type == CHAT_NETWORK_EVENT_EMOJI_BEGIN);
    assert(strcmp(event.emoji.name, "wave.png") == 0);
    assert(event.emoji.size == 1234);
}

static void test_peer_snapshot_lines(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("PEERS:BEGIN", &event));
    assert(event.type == CHAT_NETWORK_EVENT_PEERS_BEGIN);
    assert(chat_network_parse_line("PEER ID 7 IP 10.0.0.8 TCP:41234 UDP:54321 USER:alice", &event));
    assert(event.type == CHAT_NETWORK_EVENT_PEER);
    assert(event.peer.id == 7);
    assert(strcmp(event.peer.username, "alice") == 0);
    assert(chat_network_parse_line("PEERS:END", &event));
    assert(event.type == CHAT_NETWORK_EVENT_PEERS_END);
}

static void test_history_lines(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line("HISTORY:BEGIN ID 7", &event));
    assert(event.type == CHAT_NETWORK_EVENT_HISTORY_BEGIN);
    assert(event.peer.id == 7);
    assert(chat_network_parse_line("HISTORY:LINE ID 7 [2026-09-08 12:00:00] alice: hello", &event));
    assert(event.type == CHAT_NETWORK_EVENT_HISTORY_LINE);
    assert(event.peer.id == 7);
    assert(strcmp(event.text, "[2026-09-08 12:00:00] alice: hello") == 0);
    assert(chat_network_parse_line("HISTORY:END ID 7", &event));
    assert(event.type == CHAT_NETWORK_EVENT_HISTORY_END);
    assert(event.peer.id == 7);
}

static void test_media_begin_line(void)
{
    chat_network_event_t event;

    assert(chat_network_parse_line(
        "MEDIA:BEGIN KIND EMOJI FROM 7 NAME wave.png SIZE 123 HASH -", &event));
    assert(event.type == CHAT_NETWORK_EVENT_MEDIA_BEGIN);
    assert(event.media.kind == CHAT_NETWORK_MEDIA_EMOJI);
    assert(event.media.sender_id == 7);
    assert(event.media.size == 123);
    assert(strcmp(event.media.name, "wave.png") == 0);
}

int main(void)
{
    test_broadcast_line();
    test_peer_line();
    test_private_message_line();
    test_server_feedback_line();
    test_auth_success_line();
    test_auth_failure_line();
    test_register_success_line();
    test_register_failure_line();
    test_emoji_begin_line();
    test_peer_snapshot_lines();
    test_history_lines();
    test_media_begin_line();
    return 0;
}

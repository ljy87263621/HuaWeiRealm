#include "chat_frontend.h"

#include <stdbool.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chat_network.h"
#include "chat_login_ui.h"
#include "squareline_ui/ui.h"

static const lv_font_t *const chat_font = &ui_font_Font1;
static const lv_font_t *const login_font = &ui_font_Font1;
static lv_font_t soft_keyboard_font;
static lv_obj_t *message_log;
static lv_obj_t *server_log;
static lv_obj_t *message_input;
static lv_obj_t *notice_popup;
static lv_timer_t *notice_timer;
static lv_obj_t *emoji_popup;
static lv_obj_t *attachment_popup;
static lv_obj_t *attachment_explorer;
static lv_obj_t *theme_dropdown;
static lv_obj_t *login_background;
static lv_obj_t *login_scrim;
static lv_obj_t *login_panel;
static lv_obj_t *g_kb_login;
static lv_obj_t *g_kb_chat;
static lv_obj_t *logout_button;
static lv_obj_t *logout_button_label;
static lv_obj_t *exit_button;
static lv_obj_t *exit_button_label;
static int selected_peer_id;
static int local_peer_id;
static bool english;
static bool light_theme;
static bool showing_server_log;
static bool history_received;
static bool exit_requested;
#define CHAT_MAX_PEERS 128
#define CHAT_MAX_EMOJIS 32
static chat_network_peer_t peers[CHAT_MAX_PEERS];
static chat_network_peer_t pending_peers[CHAT_MAX_PEERS];
static size_t peer_count;
static size_t pending_peer_count;
static unsigned unread_messages[CHAT_MAX_PEERS];
static bool shutdown_registered;
static char emoji_names[CHAT_MAX_EMOJIS][CHAT_NETWORK_EMOJI_NAME_MAX];
static char emoji_paths[CHAT_MAX_EMOJIS][256];

static void hide_soft_keyboard(void)
{
    if(g_kb_login) {
        lv_keyboard_set_textarea(g_kb_login, NULL);
        lv_obj_add_flag(g_kb_login, LV_OBJ_FLAG_HIDDEN);
    }
    if(g_kb_chat) {
        lv_keyboard_set_textarea(g_kb_chat, NULL);
        lv_obj_add_flag(g_kb_chat, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_soft_keyboard(lv_obj_t *ta)
{
    lv_obj_t *kb;

    if(ta == NULL) return;

    kb = lv_obj_get_screen(ta) == ui_LoginScreen ? g_kb_login : g_kb_chat;
    if(kb == NULL) return;

    hide_soft_keyboard();
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kb);
}

static void soft_keyboard_textarea_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *ta = lv_event_get_target(event);
    lv_obj_t *kb = lv_event_get_user_data(event);

    if(code == LV_EVENT_FOCUSED) {
        show_soft_keyboard(ta);
    }
    else if(code == LV_EVENT_DEFOCUSED && kb) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void prepare_soft_keyboard_font(void)
{
    /* Font1 renders regular key labels.  LVGL's default Montserrat font
     * provides the private-use glyphs used by LV_SYMBOL_* keyboard keys. */
    soft_keyboard_font = ui_font_Font1;
    soft_keyboard_font.fallback = LV_FONT_DEFAULT;
}

static void create_soft_keyboards(void)
{
    g_kb_login = lv_keyboard_create(ui_LoginScreen);
    g_kb_chat = lv_keyboard_create(ui_ChatScreen);
    if(g_kb_login == NULL || g_kb_chat == NULL) return;

    lv_obj_set_size(g_kb_login, LV_HOR_RES, 210);
    lv_obj_set_size(g_kb_chat, LV_HOR_RES, 210);
    lv_obj_set_style_text_font(g_kb_login, login_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(g_kb_chat, chat_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(g_kb_login, &soft_keyboard_font, LV_PART_ITEMS);
    lv_obj_set_style_text_font(g_kb_chat, &soft_keyboard_font, LV_PART_ITEMS);
    lv_obj_add_flag(g_kb_login, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_kb_chat, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(ui_login_username, soft_keyboard_textarea_event, LV_EVENT_FOCUSED, g_kb_login);
    lv_obj_add_event_cb(ui_login_username, soft_keyboard_textarea_event, LV_EVENT_DEFOCUSED, g_kb_login);
    lv_obj_add_event_cb(ui_login_password, soft_keyboard_textarea_event, LV_EVENT_FOCUSED, g_kb_login);
    lv_obj_add_event_cb(ui_login_password, soft_keyboard_textarea_event, LV_EVENT_DEFOCUSED, g_kb_login);
    lv_obj_add_event_cb(ui_login_server_ip, soft_keyboard_textarea_event, LV_EVENT_FOCUSED, g_kb_login);
    lv_obj_add_event_cb(ui_login_server_ip, soft_keyboard_textarea_event, LV_EVENT_DEFOCUSED, g_kb_login);
    lv_obj_add_event_cb(message_input, soft_keyboard_textarea_event, LV_EVENT_FOCUSED, g_kb_chat);
    lv_obj_add_event_cb(message_input, soft_keyboard_textarea_event, LV_EVENT_DEFOCUSED, g_kb_chat);
}

static void style_login_field(lv_obj_t *field)
{
    lv_obj_set_style_radius(field, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(field, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(0x9FB8D6), LV_PART_MAIN);
    lv_obj_set_style_border_opa(field, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_text_color(field, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(field, login_font, LV_PART_MAIN);
    lv_obj_set_style_pad_left(field, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_right(field, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_top(field, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(field, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(0x63B3ED), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(field, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
}

static void create_login_runtime_decoration(void)
{
    login_background = lv_image_create(ui_LoginScreen);
    lv_obj_set_size(login_background, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(login_background, 0, 0);
    lv_image_set_src(login_background, chat_login_background_path());
    lv_image_set_inner_align(login_background, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_remove_flag(login_background, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(login_background, 0);

    login_scrim = lv_obj_create(ui_LoginScreen);
    lv_obj_set_size(login_scrim, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(login_scrim, 0, 0);
    lv_obj_remove_flag(login_scrim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(login_scrim, lv_color_hex(0x07111F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(login_scrim, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(login_scrim, 0, LV_PART_MAIN);
    lv_obj_move_to_index(login_scrim, 1);

    login_panel = lv_obj_create(ui_LoginScreen);
    lv_obj_set_size(login_panel, 804, 510);
    lv_obj_set_pos(login_panel, 110, 34);
    lv_obj_remove_flag(login_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(login_panel, lv_color_hex(0x0B1220), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(login_panel, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_color(login_panel, lv_color_hex(0xD7E7FA), LV_PART_MAIN);
    lv_obj_set_style_border_opa(login_panel, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(login_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(login_panel, 22, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(login_panel, 28, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(login_panel, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(login_panel, lv_color_hex(0x08111F), LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(login_panel, 12, LV_PART_MAIN);
    lv_obj_move_to_index(login_panel, 2);

    lv_obj_set_pos(ui_login_title, 310, 58);
    lv_obj_set_size(ui_login_title, 404, 48);
    lv_obj_set_pos(ui_login_username_label, 190, 139);
    lv_obj_set_pos(ui_login_username, 330, 128);
    lv_obj_set_size(ui_login_username, 500, 50);
    lv_obj_set_pos(ui_login_password_label, 190, 209);
    lv_obj_set_pos(ui_login_password, 330, 198);
    lv_obj_set_size(ui_login_password, 500, 50);
    lv_obj_set_pos(ui_login_server_label, 190, 279);
    lv_obj_set_pos(ui_login_server_ip, 330, 268);
    lv_obj_set_size(ui_login_server_ip, 500, 50);
    lv_obj_set_pos(ui_login_button, 350, 330);
    lv_obj_set_size(ui_login_button, 174, 52);
    lv_obj_set_pos(ui_register_button, 538, 330);
    lv_obj_set_size(ui_register_button, 174, 52);
    lv_obj_set_style_text_align(ui_login_button_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ui_register_button_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(ui_login_button_label);
    lv_obj_center(ui_register_button_label);

    lv_obj_set_style_text_color(ui_login_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(ui_login_title, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_login_username_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_login_password_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_login_server_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    style_login_field(ui_login_username);
    style_login_field(ui_login_password);
    style_login_field(ui_login_server_ip);
    lv_textarea_set_password_mode(ui_login_password, true);
    lv_textarea_set_max_length(ui_login_username, 32);
    lv_textarea_set_max_length(ui_login_password, 64);
    lv_textarea_set_max_length(ui_login_server_ip, 64);
    lv_textarea_set_placeholder_text(ui_login_username, english ? "Username" : "请输入用户名");
    lv_textarea_set_placeholder_text(ui_login_password, english ? "Password" : "请输入密码");
    lv_textarea_set_placeholder_text(ui_login_server_ip, "192.168.1.100");
    lv_obj_set_style_text_color(ui_login_username, lv_color_hex(0xCBD5E1), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(ui_login_password, lv_color_hex(0xCBD5E1), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(ui_login_server_ip, lv_color_hex(0xCBD5E1), LV_PART_TEXTAREA_PLACEHOLDER);

}

static void refresh_peer_rows(void);

static void apply_settings_language(bool use_english)
{
    english = use_english;
    lv_dropdown_set_options(ui_language_dropdown, "中文\nEnglish");
    lv_dropdown_set_selected(ui_language_dropdown, use_english ? 1 : 0);
    lv_label_set_text(ui_settings_title, use_english ? "Settings" : "设置");
    lv_label_set_text(ui_settings_tab_1_label, use_english ? "General" : "常规");
    lv_label_set_text(ui_settings_tab_2_label, use_english ? "Display" : "显示");
    lv_label_set_text(ui_settings_tab_3_label, use_english ? "Network" : "网络");
    lv_label_set_text(ui_settings_tab_4_label, use_english ? "About" : "关于");
    lv_label_set_text(ui_settings_section_title, use_english ? "General settings" : "常规设置");
    lv_label_set_text(ui_language_label, use_english ? "Language" : "语言");
    lv_label_set_text(ui_notifications_label, use_english ? "Notifications" : "消息通知");
    lv_label_set_text(ui_theme_label, use_english ? "Theme" : "主题");
    lv_label_set_text(ui_theme_placeholder_label, use_english ? "Dark theme" : "深色主题");
    if(logout_button_label)
        lv_label_set_text(logout_button_label, use_english ? "Log out" : "退出登录");
    if(exit_button_label)
        lv_label_set_text(exit_button_label, use_english ? "Exit program" : "退出程序");
    lv_label_set_text(ui_title, use_english ? "Embedded Chat" : "嵌入式聊天");
    lv_label_set_text(ui_login_username_label, use_english ? "Username" : "用户名");
    lv_label_set_text(ui_login_password_label, use_english ? "Password" : "密码");
    lv_label_set_text(ui_login_server_label, use_english ? "Server address" : "服务器地址");
    lv_label_set_text(ui_login_button_label, use_english ? "Login" : "登录");
    lv_label_set_text(ui_register_button_label, use_english ? "Register" : "注册");
    lv_label_set_text(ui_toolbar_1_label, use_english ? "Emoji" : "表情");
    lv_label_set_text(ui_toolbar2label, use_english ? "Attach" : "附件");
    lv_label_set_text(ui_toolbar3label, use_english ? "Chat" : "聊天");
    lv_label_set_text(ui_toolbar4label, use_english ? "Server log" : "服务器日志");
    lv_label_set_text(ui_sendlabel, use_english ? "Send" : "发送");
    lv_label_set_text(ui_status_1_title, use_english ? "Target" : "聊天对象");
    lv_label_set_text(ui_status_2_title, use_english ? "Server" : "服务器");
    lv_label_set_text(ui_status3title, use_english ? "Connection" : "连接");
    lv_label_set_text(ui_status_4_title, use_english ? "Protocol" : "协议");
    lv_label_set_text(ui_status_4_value, "TCP");
    if(message_input)
        lv_textarea_set_placeholder_text(message_input, use_english ? "Type a message..." : "输入消息...");
    if(theme_dropdown) {
        lv_dropdown_set_options(theme_dropdown, use_english ? "Dark\nLight" : "深色\n浅色");
        lv_dropdown_set_selected(theme_dropdown, light_theme ? 1 : 0);
    }
}

static void apply_theme(void)
{
    lv_color_t background = lv_color_hex(light_theme ? 0xF1F5F9 : 0x161A21);
    lv_color_t text = lv_color_hex(light_theme ? 0x172033 : 0xF2F5F7);
    lv_color_t user_panel = lv_color_hex(light_theme ? 0xE0F2FE : 0x20333A);
    lv_color_t chat_panel = lv_color_hex(light_theme ? 0xFFFFFF : 0x1D2430);
    lv_color_t status_panel = lv_color_hex(light_theme ? 0xFEF3C7 : 0x2B2638);
    lv_color_t topbar = lv_color_hex(light_theme ? 0xDBEAFE : 0x202B36);
    lv_color_t bottombar = lv_color_hex(light_theme ? 0xDCFCE7 : 0x2D3D33);
    lv_obj_t *const panels[] = {ui_user_panel, ui_chat_panel, ui_status_panel};
    lv_color_t const colors[] = {user_panel, chat_panel, status_panel};

    lv_obj_set_style_bg_color(ui_ChatScreen, background, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_ChatScreen, text, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_topbar, topbar, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_bottombar, bottombar, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_messagearea, chat_panel, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_inputarea, light_theme ? lv_color_hex(0xE8EEF5) : lv_color_hex(0x111821), LV_PART_MAIN);
    for(size_t index = 0; index < sizeof(panels) / sizeof(panels[0]); index++)
        lv_obj_set_style_bg_color(panels[index], colors[index], LV_PART_MAIN);
    for(size_t index = 0; index < 4; index++) {
        lv_obj_t *card = index == 0 ? ui_status_card_1 : index == 1 ? ui_statuscard2 :
                         index == 2 ? ui_status_card_3 : ui_status_card_4;
        lv_obj_set_style_bg_color(card, light_theme ? lv_color_hex(0xFFFDF6) : lv_color_hex(0x252D38), LV_PART_MAIN);
    }
    if(message_log) lv_obj_set_style_text_color(message_log, text, LV_PART_MAIN);
    if(server_log) lv_obj_set_style_text_color(server_log, text, LV_PART_MAIN);
    refresh_peer_rows();
}

static void theme_changed(lv_event_t *event)
{
    light_theme = lv_dropdown_get_selected(lv_event_get_target(event)) == 1;
    apply_theme();
}

static void set_font_recursive(lv_obj_t *object, const lv_font_t *font)
{
    if(object == NULL) return;
    lv_obj_set_style_text_font(object, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    uint32_t child_count = lv_obj_get_child_count(object);
    for(uint32_t index = 0; index < child_count; index++)
        set_font_recursive(lv_obj_get_child(object, index), font);
}

static void style_title(lv_obj_t *object)
{
    if(object == NULL) return;
    lv_obj_set_style_text_font(object, login_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void close_notice_timer(lv_timer_t *timer)
{
    lv_obj_t *popup = lv_timer_get_user_data(timer);
    if(popup) lv_msgbox_close(popup);
    if(notice_popup == popup) notice_popup = NULL;
    if(notice_timer == timer) notice_timer = NULL;
}

static void show_notice(const char *message)
{
    if(!message || !*message) return;
    if(notice_timer) {
        lv_timer_del(notice_timer);
        notice_timer = NULL;
    }
    if(notice_popup) {
        lv_msgbox_close(notice_popup);
        notice_popup = NULL;
    }
    notice_popup = lv_msgbox_create(NULL);
    lv_msgbox_add_title(notice_popup, "提示");
    lv_msgbox_add_text(notice_popup, message);
    lv_obj_set_size(notice_popup, 430, 180);
    lv_obj_center(notice_popup);
    set_font_recursive(notice_popup, login_font);
    notice_timer = lv_timer_create(close_notice_timer, 2400, notice_popup);
    lv_timer_set_repeat_count(notice_timer, 1);
}

static void set_connection_state(const char *state, bool connected)
{
    lv_label_set_text(ui_connection_indicator, connected ? "ON" : "OFF");
    lv_label_set_text(ui_connection_text, state);
    lv_label_set_text(ui_bottom_status, connected ?
                      (english ? "Connected | Ready" : "已连接 | 就绪") : state);
    lv_label_set_text(ui_status3value, connected ?
                      (english ? "Connected" : "已连接") : (english ? "Offline" : "离线"));
}

static void append_to_log(lv_obj_t *log, const char *message)
{
    const char *old_text;
    char combined[8192];

    if(!message || !*message || !log) return;
    old_text = lv_textarea_get_text(log);
    if(old_text && *old_text)
        snprintf(combined, sizeof(combined), "%s\n%s", old_text, message);
    else
        snprintf(combined, sizeof(combined), "%s", message);
    lv_textarea_set_text(log, combined);
    lv_textarea_set_cursor_pos(log, LV_TEXTAREA_CURSOR_LAST);
}

static void clear_chat_messages(void)
{
    if(message_log) lv_obj_clean(message_log);
}

static void append_message(const char *message)
{
    lv_obj_t *label;
    if(!message_log || !message || !*message) return;
    label = lv_label_create(message_log);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_font(label, chat_font, LV_PART_MAIN);
    lv_obj_scroll_to_view(label, LV_ANIM_OFF);
}

static void append_media_message(const chat_network_media_t *media, bool outgoing)
{
    lv_obj_t *card;
    lv_obj_t *label;
    char title[192];

    if(!message_log || !media || !media->name[0]) return;
    card = lv_obj_create(message_log);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, media->kind == CHAT_NETWORK_MEDIA_EMOJI ? 106 : 62);
    lv_obj_set_style_bg_color(card, outgoing ? lv_color_hex(0xD1FAE5) : lv_color_hex(0xE0F2FE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 4, LV_PART_MAIN);
    label = lv_label_create(card);
    snprintf(title, sizeof(title), media->kind == CHAT_NETWORK_MEDIA_EMOJI ?
             (outgoing ? "我发送的表情: %s" : "收到表情: %s") :
             (outgoing ? "我发送的附件: %s" : "收到附件: %s"), media->name);
    lv_label_set_text(label, title);
    lv_obj_set_pos(label, 8, 6);
    lv_obj_set_style_text_font(label, chat_font, LV_PART_MAIN);
    if(media->kind == CHAT_NETWORK_MEDIA_EMOJI) {
        char source[CHAT_NETWORK_MEDIA_PATH_MAX + CHAT_NETWORK_EMOJI_NAME_MAX + 4];
        const char *path = media->path[0] ? media->path : "./Emojis";
        if(!media->path[0]) snprintf(source, sizeof(source), "A:%s/%s", path, media->name);
        else snprintf(source, sizeof(source), "A:%s", path);
        lv_obj_t *image = lv_image_create(card);
        lv_image_set_src(image, source);
        lv_obj_set_size(image, 64, 64);
        lv_obj_set_pos(image, 10, 34);
    } else {
        char detail[CHAT_NETWORK_MEDIA_PATH_MAX + CHAT_NETWORK_FILE_HASH_MAX + 4];
        snprintf(detail, sizeof(detail), "%s\n%s", media->path[0] ? media->path : "./Downloads",
                 media->hash[0] ? media->hash : "");
        lv_obj_t *path_label = lv_label_create(card);
        lv_label_set_text(path_label, detail);
        lv_obj_set_pos(path_label, 8, 28);
        lv_obj_set_style_text_font(path_label, chat_font, LV_PART_MAIN);
    }
    lv_obj_scroll_to_view(card, LV_ANIM_OFF);
}

static void append_server_message(const char *message)
{
    append_to_log(server_log, message);
}

static void set_message_view(bool show_server_log)
{
    showing_server_log = show_server_log;
    if(message_log) {
        if(show_server_log) lv_obj_add_flag(message_log, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(message_log, LV_OBJ_FLAG_HIDDEN);
    }
    if(server_log) {
        if(show_server_log) lv_obj_clear_flag(server_log, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(server_log, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_state(ui_toolbar_3, LV_STATE_CHECKED);
    lv_obj_clear_state(ui_toolbar_4, LV_STATE_CHECKED);
    lv_obj_add_state(show_server_log ? ui_toolbar_4 : ui_toolbar_3, LV_STATE_CHECKED);
}

static const chat_network_peer_t *find_peer(int id)
{
    for(size_t index = 0; index < peer_count; index++) {
        if(peers[index].id == id) return &peers[index];
    }
    return NULL;
}

static void update_target_display(void)
{
    const chat_network_peer_t *peer = find_peer(selected_peer_id);
    char detail[80];

    if(!peer) {
        lv_label_set_text(ui_status_1_value, english ? "No target" : "未选择");
        lv_label_set_text(ui_status1port, "-");
        lv_label_set_text(ui_chat_header, english ? "Select a user to start chatting" : "请选择聊天对象");
        return;
    }
    lv_label_set_text(ui_status_1_value, peer->username);
    snprintf(detail, sizeof(detail), "%s:%d", peer->ip, peer->tcp_port);
    lv_label_set_text(ui_status1port, detail);
    snprintf(detail, sizeof(detail), english ? "Chat with %s" : "与 %s 对话", peer->username);
    lv_label_set_text(ui_chat_header, detail);
}

static void refresh_peer_rows(void);

static void peer_selected(lv_event_t *event)
{
    selected_peer_id = (int)(intptr_t)lv_event_get_user_data(event);
    if(!find_peer(selected_peer_id)) return;
    for(size_t index = 0; index < peer_count; index++)
        if(peers[index].id == selected_peer_id) unread_messages[index] = 0;
    clear_chat_messages();
    history_received = false;
    update_target_display();
    set_message_view(false);
    refresh_peer_rows();
    if(chat_network_request_history(selected_peer_id) < 0)
        append_server_message(english ? "Unable to request chat history" : "无法请求聊天记录");
}

static void refresh_peer_rows(void)
{
    char header[48];

    lv_obj_clean(ui_userlist);
    lv_obj_set_layout(ui_userlist, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui_userlist, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ui_userlist, LV_DIR_VER);
    lv_obj_set_style_pad_row(ui_userlist, 6, LV_PART_MAIN);
    for(size_t index = 0; index < peer_count; index++) {
        char address[80];
        lv_obj_t *row = lv_button_create(ui_userlist);
        lv_obj_t *name = lv_label_create(row);
        lv_obj_t *ip = lv_label_create(row);

        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 62);
        lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x232A35), LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1C563F), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_event_cb(row, peer_selected, LV_EVENT_CLICKED, (void *)(intptr_t)peers[index].id);
        if(peers[index].id == selected_peer_id) lv_obj_add_state(row, LV_STATE_CHECKED);

        lv_label_set_text(name, peers[index].username);
        lv_obj_set_pos(name, 12, 7);
        lv_obj_set_style_text_font(name, chat_font, LV_PART_MAIN);
        snprintf(address, sizeof(address), "%s:%d", peers[index].ip, peers[index].tcp_port);
        lv_label_set_text(ip, address);
        lv_obj_set_pos(ip, 12, 32);
        lv_obj_set_style_text_font(ip, chat_font, LV_PART_MAIN);
        if(unread_messages[index] > 0) {
            char unread[12];
            lv_obj_t *badge = lv_label_create(row);
            snprintf(unread, sizeof(unread), unread_messages[index] > 99 ? "99+" : "%u",
                     unread_messages[index]);
            lv_label_set_text(badge, unread);
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -8, 6);
            lv_obj_set_style_bg_color(badge, lv_color_hex(0xE05263), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(badge, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_pad_left(badge, 5, LV_PART_MAIN);
            lv_obj_set_style_pad_right(badge, 5, LV_PART_MAIN);
            lv_obj_set_style_radius(badge, 8, LV_PART_MAIN);
        }
    }
    snprintf(header, sizeof(header), english ? "Online users (%u)" : "在线用户 (%u)",
             (unsigned)peer_count);
    lv_label_set_text(ui_user_header, header);
}

static bool is_emoji_image(const char *name)
{
    const char *extension = strrchr(name, '.');
    return extension && (!strcmp(extension, ".png") || !strcmp(extension, ".bmp") ||
                         !strcmp(extension, ".jpg") || !strcmp(extension, ".jpeg"));
}

static void close_emoji_popup(lv_event_t *event)
{
    (void)event;
    if(emoji_popup) lv_obj_delete(emoji_popup);
    emoji_popup = NULL;
}

static void emoji_selected(lv_event_t *event)
{
    const char *name = lv_event_get_user_data(event);
    chat_network_media_t media;

    if(!name || selected_peer_id <= 0) {
        show_notice(english ? "Select a chat target first" : "请先选择聊天对象");
        return;
    }
    if(chat_network_send_emoji(selected_peer_id, name) < 0) {
        append_server_message(english ? "Emoji send failed" : "表情发送失败");
        return;
    }
    memset(&media, 0, sizeof(media));
    media.kind = CHAT_NETWORK_MEDIA_EMOJI;
    snprintf(media.name, sizeof(media.name), "%s", name);
    snprintf(media.path, sizeof(media.path), "./Emojis/%s", name);
    append_media_message(&media, true);
    close_emoji_popup(NULL);
}

static void show_emoji_popup(lv_event_t *event)
{
    DIR *directory;
    struct dirent *entry;
    lv_obj_t *title;
    lv_obj_t *close_button;
    lv_obj_t *content;
    size_t count = 0;
    (void)event;

    close_emoji_popup(NULL);
    emoji_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(emoji_popup, 560, 420);
    lv_obj_center(emoji_popup);
    lv_obj_set_style_bg_color(emoji_popup, lv_color_hex(0x1B2028), LV_PART_MAIN);
    lv_obj_set_style_border_color(emoji_popup, lv_color_hex(0x36B487), LV_PART_MAIN);
    lv_obj_set_style_border_width(emoji_popup, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(emoji_popup, 6, LV_PART_MAIN);

    title = lv_label_create(emoji_popup);
    lv_label_set_text(title, english ? "Emoji" : "表情");
    lv_obj_set_pos(title, 18, 14);
    lv_obj_set_style_text_font(title, chat_font, LV_PART_MAIN);
    close_button = lv_button_create(emoji_popup);
    lv_obj_set_size(close_button, 42, 36);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_add_event_cb(close_button, close_emoji_popup, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, "X");
    lv_obj_center(close_label);

    content = lv_obj_create(emoji_popup);
    lv_obj_set_size(content, 520, 342);
    lv_obj_set_pos(content, 18, 58);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_style_pad_row(content, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(content, 8, LV_PART_MAIN);

    directory = opendir("./Emojis");
    if(directory) {
        while(count < CHAT_MAX_EMOJIS && (entry = readdir(directory)) != NULL) {
            char *name = entry->d_name;
            if(name[0] == '.') continue;
            snprintf(emoji_names[count], sizeof(emoji_names[count]), "%.*s",
                     (int)sizeof(emoji_names[count]) - 1, name);
            snprintf(emoji_paths[count], sizeof(emoji_paths[count]), "A:./Emojis/%.*s",
                     (int)sizeof(emoji_paths[count]) - 12, emoji_names[count]);
            lv_obj_t *item = lv_button_create(content);
            lv_obj_set_size(item, 118, 88);
            lv_obj_add_event_cb(item, emoji_selected, LV_EVENT_CLICKED, emoji_names[count]);
            if(is_emoji_image(emoji_names[count])) {
                lv_obj_t *image = lv_image_create(item);
                lv_image_set_src(image, emoji_paths[count]);
                lv_obj_set_size(image, 40, 40);
                lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 4);
            }
            lv_obj_t *label = lv_label_create(item);
            lv_label_set_text(label, emoji_names[count]);
            lv_obj_set_width(label, 106);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -6);
            lv_obj_set_style_text_font(label, chat_font, LV_PART_MAIN);
            count++;
        }
        closedir(directory);
    }
    if(count == 0) {
        lv_obj_t *empty = lv_label_create(content);
        lv_label_set_text(empty, english ? "No emoji files received" : "尚未接收表情文件");
        lv_obj_set_style_text_font(empty, chat_font, LV_PART_MAIN);
    }
}

static void close_attachment_picker(lv_event_t *event)
{
    (void)event;
    if(attachment_popup) lv_obj_delete(attachment_popup);
    attachment_popup = NULL;
    attachment_explorer = NULL;
}

static void attachment_file_selected(lv_event_t *event)
{
    const char *name;
    const char *directory;
    char lv_path[CHAT_NETWORK_MEDIA_PATH_MAX];
    char native_path[CHAT_NETWORK_MEDIA_PATH_MAX];
    (void)event;

    if(!attachment_explorer || selected_peer_id <= 0) return;
    name = lv_file_explorer_get_selected_file_name(attachment_explorer);
    directory = lv_file_explorer_get_current_path(attachment_explorer);
    if(!name || !*name || !directory || !*directory) return;
    snprintf(lv_path, sizeof(lv_path), "%s%s", directory, name);
    snprintf(native_path, sizeof(native_path), "%s", lv_path[0] == 'A' && lv_path[1] == ':' ?
             lv_path + 2 : lv_path);
    if(chat_network_offer_file(selected_peer_id, native_path) < 0) {
        show_notice(english ? "Unable to prepare this file" : "无法准备此文件");
        return;
    }
    append_server_message(english ? "File fingerprint sent; waiting for server cache" :
                          "文件指纹已发送，正在检查服务器缓存");
    close_attachment_picker(NULL);
}

static void show_attachment_picker(lv_event_t *event)
{
    lv_obj_t *title;
    lv_obj_t *close_button;
    (void)event;

    if(selected_peer_id <= 0) {
        show_notice(english ? "Select a chat target first" : "请先选择聊天对象");
        return;
    }
    close_attachment_picker(NULL);
    attachment_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(attachment_popup, 650, 500);
    lv_obj_center(attachment_popup);
    lv_obj_set_style_bg_color(attachment_popup, lv_color_hex(light_theme ? 0xFFFFFF : 0x1B2028), LV_PART_MAIN);
    lv_obj_set_style_border_color(attachment_popup, lv_color_hex(0x36B487), LV_PART_MAIN);
    lv_obj_set_style_border_width(attachment_popup, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(attachment_popup, 6, LV_PART_MAIN);
    title = lv_label_create(attachment_popup);
    lv_label_set_text(title, english ? "Select attachment" : "选择附件");
    lv_obj_set_pos(title, 18, 14);
    lv_obj_set_style_text_font(title, chat_font, LV_PART_MAIN);
    close_button = lv_button_create(attachment_popup);
    lv_obj_set_size(close_button, 42, 36);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_add_event_cb(close_button, close_attachment_picker, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *label = lv_label_create(close_button);
        lv_label_set_text(label, "X");
        lv_obj_center(label);
    }
    attachment_explorer = lv_file_explorer_create(attachment_popup);
    lv_obj_set_size(attachment_explorer, 610, 420);
    lv_obj_set_pos(attachment_explorer, 18, 58);
    lv_file_explorer_open_dir(attachment_explorer, "A:./");
    lv_obj_add_event_cb(attachment_explorer, attachment_file_selected,
                        LV_EVENT_VALUE_CHANGED, NULL);
}

static void show_chat_tab(lv_event_t *event)
{
    (void)event;
    set_message_view(false);
}

static void show_server_log_tab(lv_event_t *event)
{
    (void)event;
    set_message_view(true);
}

static void load_settings(lv_event_t *event)
{
    (void)event;
    lv_screen_load(ui_SettingsScreen);
}

static void load_chat(lv_event_t *event)
{
    (void)event;
    lv_screen_load(ui_ChatScreen);
}

static void logout_clicked(lv_event_t *event)
{
    (void)event;

    hide_soft_keyboard();
    chat_network_disconnect();
    local_peer_id = 0;
    selected_peer_id = 0;
    peer_count = 0;
    pending_peer_count = 0;
    history_received = false;
    showing_server_log = false;
    memset(peers, 0, sizeof(peers));
    memset(pending_peers, 0, sizeof(pending_peers));
    memset(unread_messages, 0, sizeof(unread_messages));
    clear_chat_messages();
    refresh_peer_rows();
    update_target_display();
    lv_textarea_set_text(ui_login_password, "");
    lv_label_set_text(ui_login_button_label, english ? "Login" : "登录");
    lv_label_set_text(ui_register_button_label, english ? "Register" : "注册");
    lv_label_set_text(ui_status_1_value, english ? "Not selected" : "未选择");
    lv_label_set_text(ui_status1port, "-");
    lv_label_set_text(ui_status_2_value, english ? "Disconnected" : "未连接");
    set_connection_state(english ? "Disconnected" : "已断开", false);
    lv_screen_load(ui_LoginScreen);
}

static void exit_program_clicked(lv_event_t *event)
{
    (void)event;

    hide_soft_keyboard();
    chat_network_disconnect();
    exit_requested = true;
}

static void login_clicked(lv_event_t *event)
{
    const char *host;
    const char *username;
    const char *password;
    (void)event;

    host = lv_textarea_get_text(ui_login_server_ip);
    username = lv_textarea_get_text(ui_login_username);
    password = lv_textarea_get_text(ui_login_password);
    if(!host || !*host || !username || !*username || !password) {
        lv_label_set_text(ui_login_button_label, "登录");
        show_notice("请输入服务器IP、用户名和密码");
        return;
    }
    lv_label_set_text(ui_login_button_label, "连接中");
    if(chat_network_login(host, username, password) < 0) {
        lv_label_set_text(ui_login_button_label, "登录");
        show_notice("无法启动登录连接");
        return;
    }
}

static void register_clicked(lv_event_t *event)
{
    const char *host;
    const char *username;
    const char *password;
    (void)event;

    host = lv_textarea_get_text(ui_login_server_ip);
    username = lv_textarea_get_text(ui_login_username);
    password = lv_textarea_get_text(ui_login_password);
    if(!host || !*host || !username || !*username || !password) {
        lv_label_set_text(ui_register_button_label, "注册");
        show_notice("请输入服务器IP、用户名和密码");
        return;
    }
    lv_label_set_text(ui_register_button_label, "注册中");
    if(chat_network_register(host, username, password) < 0) {
        lv_label_set_text(ui_register_button_label, "注册");
        show_notice("无法启动注册连接");
    }
}

static void send_clicked(lv_event_t *event)
{
    const char *message;
    int result;
    char outgoing[CHAT_NETWORK_TEXT_MAX + 16];
    (void)event;

    message = lv_textarea_get_text(message_input);
    if(!message || !*message) return;
    if(selected_peer_id <= 0) {
        show_notice(english ? "Select a chat target first" : "请先选择聊天对象");
        return;
    }
    result = chat_network_send_private(selected_peer_id, message);
    if(result < 0) {
        append_server_message(english ? "Send failed: server is disconnected" : "发送失败：当前未连接服务器");
        return;
    }
    snprintf(outgoing, sizeof(outgoing), "[我] %s", message);
    append_message(outgoing);
    lv_textarea_set_text(message_input, "");
}

static void language_changed(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target(event);
    bool use_english = lv_dropdown_get_selected(dropdown) == 1;
    apply_settings_language(use_english);
    set_connection_state(lv_label_get_text(ui_connection_indicator)[0] == 'O' &&
                         lv_label_get_text(ui_connection_indicator)[1] == 'N' ?
                         (use_english ? "Authenticated" : "已认证") :
                         (use_english ? "Disconnected" : "已断开"),
                         lv_label_get_text(ui_connection_indicator)[0] == 'O' &&
                         lv_label_get_text(ui_connection_indicator)[1] == 'N');
    update_target_display();
    refresh_peer_rows();
    set_message_view(showing_server_log);
}

static void network_event_timer(lv_timer_t *timer)
{
    chat_network_event_t event;
    (void)timer;

    while(chat_network_poll_event(&event)) {
        switch(event.type) {
        case CHAT_NETWORK_EVENT_STATUS:
            set_connection_state(event.text, false);
            append_server_message(event.text);
            break;
        case CHAT_NETWORK_EVENT_AUTH_OK:
            local_peer_id = event.peer.id;
            selected_peer_id = 0;
            peer_count = 0;
            pending_peer_count = 0;
            lv_label_set_text(ui_login_button_label, english ? "Login" : "登录");
            lv_label_set_text(ui_status_2_value, english ? "Authenticated" : "已认证");
            lv_label_set_text(ui_status1port, "Port: 8888");
            set_connection_state(english ? "Authenticated" : "已认证", true);
            update_target_display();
            refresh_peer_rows();
            lv_screen_load(ui_ChatScreen);
            append_server_message(english ? "Authenticated. Synchronizing users and emoji files." :
                                  "认证成功，正在同步在线用户和表情包");
            break;
        case CHAT_NETWORK_EVENT_AUTH_FAILED:
            lv_label_set_text(ui_login_button_label, english ? "Login" : "登录");
            lv_label_set_text(ui_register_button_label, english ? "Register" : "注册");
            set_connection_state(event.text, false);
            show_notice(event.text);
            append_server_message(event.text);
            chat_network_disconnect();
            break;
        case CHAT_NETWORK_EVENT_REGISTER_OK:
            lv_label_set_text(ui_register_button_label, english ? "Register" : "注册");
            show_notice(english ? "Registration succeeded. Please log in." : "注册成功，请使用新账号登录");
            chat_network_disconnect();
            break;
        case CHAT_NETWORK_EVENT_REGISTER_FAILED:
            lv_label_set_text(ui_register_button_label, english ? "Register" : "注册");
            show_notice(event.text);
            append_server_message(event.text);
            chat_network_disconnect();
            break;
        case CHAT_NETWORK_EVENT_MESSAGE:
        {
            int sender = 0;
            int receiver = 0;
            if(sscanf(event.text, "[%d] %*63s -> [%d]", &sender, &receiver) == 2) {
                int peer_id = sender == local_peer_id ? receiver : sender;
                if(peer_id == selected_peer_id) append_message(event.text);
                else {
                    for(size_t index = 0; index < peer_count; index++)
                        if(peers[index].id == peer_id && unread_messages[index] < 100)
                            unread_messages[index]++;
                    refresh_peer_rows();
                    append_server_message(event.text);
                }
            } else {
                append_server_message(event.text);
            }
            break;
        }
        case CHAT_NETWORK_EVENT_PEERS_BEGIN:
            pending_peer_count = 0;
            break;
        case CHAT_NETWORK_EVENT_PEER:
            if(event.peer.id != local_peer_id && pending_peer_count < CHAT_MAX_PEERS) {
                bool replaced = false;
                for(size_t index = 0; index < pending_peer_count; index++) {
                    if(pending_peers[index].id == event.peer.id) {
                        pending_peers[index] = event.peer;
                        replaced = true;
                        break;
                    }
                }
                if(!replaced) pending_peers[pending_peer_count++] = event.peer;
            }
            break;
        case CHAT_NETWORK_EVENT_PEERS_END:
        {
            unsigned updated_unread[CHAT_MAX_PEERS] = {0};
            for(size_t next = 0; next < pending_peer_count; next++)
                for(size_t previous = 0; previous < peer_count; previous++)
                    if(pending_peers[next].id == peers[previous].id)
                        updated_unread[next] = unread_messages[previous];
            memcpy(peers, pending_peers, sizeof(peers));
            memcpy(unread_messages, updated_unread, sizeof(unread_messages));
            peer_count = pending_peer_count;
            if(selected_peer_id > 0) {
                bool selected_still_online = false;
                for(size_t index = 0; index < peer_count; index++)
                    if(peers[index].id == selected_peer_id) selected_still_online = true;
                if(!selected_still_online) {
                    selected_peer_id = 0;
                    clear_chat_messages();
                }
            }
            update_target_display();
            refresh_peer_rows();
            append_server_message(english ? "Online-user list synchronized" : "在线用户列表已同步");
            break;
        }
        case CHAT_NETWORK_EVENT_HISTORY_BEGIN:
            if(event.peer.id == selected_peer_id) {
                history_received = false;
                clear_chat_messages();
            }
            break;
        case CHAT_NETWORK_EVENT_HISTORY_LINE:
            if(event.peer.id == selected_peer_id) {
                append_message(event.text);
                history_received = true;
            }
            break;
        case CHAT_NETWORK_EVENT_HISTORY_END:
            if(event.peer.id == selected_peer_id && !history_received)
                append_message(english ? "No chat history" : "暂无聊天记录");
            break;
        case CHAT_NETWORK_EVENT_EMOJI_BEGIN:
            append_server_message(english ? "Receiving emoji files..." : "正在接收表情包...");
            break;
        case CHAT_NETWORK_EVENT_EMOJI_COMPLETE:
            append_server_message(event.text);
            break;
        case CHAT_NETWORK_EVENT_EMOJI_DONE:
            append_server_message(event.text);
            break;
        case CHAT_NETWORK_EVENT_MEDIA_BEGIN:
            append_server_message(english ? "Receiving media..." : "正在接收媒体...");
            break;
        case CHAT_NETWORK_EVENT_MEDIA_COMPLETE:
            if(event.media.sender_id == selected_peer_id) append_media_message(&event.media, false);
            else {
                for(size_t index = 0; index < peer_count; index++)
                    if(peers[index].id == event.media.sender_id && unread_messages[index] < 100)
                        unread_messages[index]++;
                refresh_peer_rows();
            }
            break;
        case CHAT_NETWORK_EVENT_MEDIA_END:
        case CHAT_NETWORK_EVENT_UPLOAD_REQUIRED:
            break;
        case CHAT_NETWORK_EVENT_DISCONNECTED:
            local_peer_id = 0;
            selected_peer_id = 0;
            peer_count = 0;
            pending_peer_count = 0;
            refresh_peer_rows();
            update_target_display();
            set_connection_state(english ? "Disconnected" : "已断开", false);
            append_server_message(event.text);
            break;
        }
    }
}

static void create_runtime_controls(void)
{
    lv_obj_add_flag(ui_system_message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_incoming_message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_outgoingmessage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_textinputplaceholder, LV_OBJ_FLAG_HIDDEN);

    message_log = lv_obj_create(ui_messagearea);
    lv_obj_set_size(message_log, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(message_log, 0, 0);
    lv_obj_set_layout(message_log, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(message_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(message_log, LV_DIR_VER);
    lv_obj_set_style_pad_row(message_log, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(message_log, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(message_log, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(message_log, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(message_log, LV_OPA_TRANSP, LV_PART_MAIN);

    message_input = lv_textarea_create(ui_inputarea);
    lv_obj_set_size(message_input, 490, 46);
    lv_obj_set_pos(message_input, 8, 10);
    lv_textarea_set_one_line(message_input, true);
    lv_textarea_set_placeholder_text(message_input, "输入消息...");
    lv_obj_set_style_text_font(message_input, chat_font, LV_PART_MAIN);
    lv_obj_set_style_bg_color(message_input, lv_color_hex(0x1B2028), LV_PART_MAIN);
    lv_obj_set_style_border_width(message_input, 0, LV_PART_MAIN);

    server_log = lv_textarea_create(ui_messagearea);
    lv_obj_set_size(server_log, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(server_log, 0, 0);
    lv_textarea_set_one_line(server_log, false);
    lv_textarea_set_cursor_click_pos(server_log, false);
    lv_obj_clear_flag(server_log, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(server_log, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(server_log, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_font(server_log, chat_font, LV_PART_MAIN);
    lv_obj_add_flag(ui_theme_placeholder_label, LV_OBJ_FLAG_HIDDEN);
    theme_dropdown = lv_dropdown_create(ui_theme_placeholder);
    lv_obj_set_size(theme_dropdown, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(theme_dropdown, 0, 0);
    lv_obj_set_style_text_font(theme_dropdown, chat_font, LV_PART_MAIN);
    lv_obj_clean(ui_userlist);
    set_message_view(false);
}

static void create_settings_action_buttons(void)
{
    logout_button = lv_button_create(ui_settings_content);
    lv_obj_set_size(logout_button, 320, 56);
    lv_obj_set_pos(logout_button, 52, 340);
    lv_obj_remove_flag(logout_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(logout_button, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(logout_button, lv_color_hex(0xD97706), LV_PART_MAIN);
    lv_obj_set_style_bg_color(logout_button, lv_color_hex(0xB45309),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    logout_button_label = lv_label_create(logout_button);
    lv_label_set_text(logout_button_label, "退出登录");
    lv_obj_set_style_text_color(logout_button_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(logout_button_label, chat_font, LV_PART_MAIN);
    lv_obj_center(logout_button_label);

    exit_button = lv_button_create(ui_settings_content);
    lv_obj_set_size(exit_button, 320, 56);
    lv_obj_set_pos(exit_button, 388, 340);
    lv_obj_remove_flag(exit_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(exit_button, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(exit_button, lv_color_hex(0xC2414A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(exit_button, lv_color_hex(0x9F1239),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    exit_button_label = lv_label_create(exit_button);
    lv_label_set_text(exit_button_label, "退出程序");
    lv_obj_set_style_text_color(exit_button_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(exit_button_label, chat_font, LV_PART_MAIN);
    lv_obj_center(exit_button_label);
}

static void apply_runtime_fonts(void)
{
    lv_obj_t *const chinese_labels[] = {
        ui_connection_text,
        ui_title,
        ui_user_header,
        ui_chat_header,
        ui_toolbar_1_label,
        ui_toolbar2label,
        ui_toolbar3label,
        ui_toolbar4label,
        ui_sendlabel,
        ui_status_1_title,
        ui_status_1_value,
        ui_status1port,
        ui_status_2_title,
        ui_status_2_value,
        ui_status_2_port,
        ui_status3title,
        ui_status3value,
        ui_status_4_title,
        ui_status_4_value,
        ui_bottom_status,
        ui_settings_button_label,
        ui_settings_title,
        ui_settings_tab_1_label,
        ui_settings_tab_2_label,
        ui_settings_tab_3_label,
        ui_settings_tab_4_label,
        ui_settings_section_title,
        ui_language_label,
        ui_notifications_label,
        ui_theme_label,
        ui_theme_placeholder_label,
        logout_button_label,
        exit_button_label,
    };

    for(size_t index = 0; index < sizeof(chinese_labels) / sizeof(chinese_labels[0]); index++)
        lv_obj_set_style_text_font(chinese_labels[index], chat_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_language_dropdown, chat_font, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void chat_frontend_init(void)
{
    ui_init();
    create_login_runtime_decoration();
    set_font_recursive(ui_LoginScreen, login_font);
    lv_obj_set_style_text_font(ui_login_title, login_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_login_button_label, login_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_register_button_label, login_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    style_title(ui_login_title);
    create_runtime_controls();
    create_settings_action_buttons();
    prepare_soft_keyboard_font();
    create_soft_keyboards();
    apply_runtime_fonts();
    set_connection_state("未连接", false);
    lv_label_set_text(ui_status_1_value, "未选择");
    lv_label_set_text(ui_status1port, "-");
    lv_label_set_text(ui_status_2_value, "未连接");
    lv_label_set_text(ui_status_2_port, "Port: 8888");
    peer_count = 0;
    refresh_peer_rows();
    apply_settings_language(false);
    apply_theme();
    lv_obj_add_event_cb(ui_login_button, login_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_register_button, register_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_settings_button, load_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_settings_close, load_chat, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(logout_button, logout_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(exit_button, exit_program_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_sendbutton, send_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_toolbar1, show_emoji_popup, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_toolbar_2, show_attachment_picker, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_toolbar_3, show_chat_tab, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_toolbar_4, show_server_log_tab, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_language_dropdown, language_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(theme_dropdown, theme_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_timer_create(network_event_timer, 100, NULL);
    if(!shutdown_registered) {
        atexit(chat_frontend_shutdown);
        shutdown_registered = true;
    }
    lv_screen_load(ui_LoginScreen);
}

void chat_frontend_shutdown(void)
{
    chat_network_disconnect();
    chat_network_cleanup_emojis();
}

bool chat_frontend_exit_requested(void)
{
    return exit_requested;
}

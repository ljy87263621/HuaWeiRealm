"""Regression checks for the settings-page logout and program-exit actions."""

from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[1]
FRONTEND_SOURCE = (PROJECT_ROOT / "mycode" / "chat_frontend.c").read_text(encoding="utf-8")
FRONTEND_HEADER = (PROJECT_ROOT / "mycode" / "chat_frontend.h").read_text(encoding="utf-8")
MAIN_SOURCE = (PROJECT_ROOT / "main.c").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0

    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1:index]

    raise AssertionError(f"Unterminated function body for {signature}")


class SettingsSessionActionsTest(unittest.TestCase):
    def test_settings_page_creates_translated_session_action_buttons(self) -> None:
        self.assertIn("static void create_settings_action_buttons(void)", FRONTEND_SOURCE)
        self.assertIn("logout_button = lv_button_create(ui_settings_content);", FRONTEND_SOURCE)
        self.assertIn("exit_button = lv_button_create(ui_settings_content);", FRONTEND_SOURCE)
        self.assertIn("logout_button_label", FRONTEND_SOURCE)
        self.assertIn("exit_button_label", FRONTEND_SOURCE)
        self.assertIn('use_english ? "Log out" : "退出登录"', FRONTEND_SOURCE)
        self.assertIn('use_english ? "Exit program" : "退出程序"', FRONTEND_SOURCE)

    def test_logout_disconnects_and_returns_to_login(self) -> None:
        self.assertIn("static void logout_clicked(lv_event_t *event)", FRONTEND_SOURCE)
        self.assertIn("chat_network_disconnect();", FRONTEND_SOURCE)
        self.assertIn("lv_textarea_set_text(ui_login_password, \"\");", FRONTEND_SOURCE)
        self.assertIn("lv_screen_load(ui_LoginScreen);", FRONTEND_SOURCE)
        self.assertIn("lv_obj_add_event_cb(logout_button, logout_clicked, LV_EVENT_CLICKED, NULL);",
                      FRONTEND_SOURCE)

    def test_exit_program_requests_normal_main_loop_shutdown(self) -> None:
        self.assertIn("bool chat_frontend_exit_requested(void);", FRONTEND_HEADER)
        self.assertIn("static void exit_program_clicked(lv_event_t *event)", FRONTEND_SOURCE)
        self.assertIn("exit_requested = true;", FRONTEND_SOURCE)
        self.assertIn("lv_obj_add_event_cb(exit_button, exit_program_clicked, LV_EVENT_CLICKED, NULL);",
                      FRONTEND_SOURCE)
        self.assertIn("while(!chat_frontend_exit_requested())", MAIN_SOURCE)

    def test_exit_clears_and_flushes_the_lcd_before_lvgl_deinitializes(self) -> None:
        signature = "static void clear_display_before_exit(void)"
        self.assertIn(signature, MAIN_SOURCE)
        clear_body = function_body(MAIN_SOURCE, signature + "\n{")

        self.assertIn("lv_obj_t *blank_screen = lv_obj_create(NULL);", clear_body)
        self.assertIn("lv_obj_set_style_bg_color(blank_screen, lv_color_hex(0x000000),",
                      clear_body)
        self.assertIn("lv_obj_clean(lv_layer_bottom());", clear_body)
        self.assertIn("lv_obj_clean(lv_layer_top());", clear_body)
        self.assertIn("lv_obj_clean(lv_layer_sys());", clear_body)
        self.assertIn("lv_screen_load(blank_screen);", clear_body)
        self.assertIn("lv_refr_now(NULL);", clear_body)
        self.assertLess(MAIN_SOURCE.index("clear_display_before_exit();"),
                        MAIN_SOURCE.index("lv_deinit();"))


if __name__ == "__main__":
    unittest.main()

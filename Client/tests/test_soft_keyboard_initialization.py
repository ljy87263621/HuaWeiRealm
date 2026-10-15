"""Regression checks for the runtime soft-keyboard initialization path."""

from pathlib import Path
import unittest


SOURCE_PATH = Path(__file__).resolve().parents[1] / "mycode" / "chat_frontend.c"
PROJECT_ROOT = SOURCE_PATH.parents[1]
LVGL_ROOT = PROJECT_ROOT / "lvgl"
if not LVGL_ROOT.exists():
    LVGL_ROOT = PROJECT_ROOT / "third_party" / "lvgl"


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


class SoftKeyboardInitializationTest(unittest.TestCase):
    def test_runtime_controls_are_followed_by_soft_keyboard_creation(self) -> None:
        init_body = function_body(SOURCE_PATH.read_text(encoding="utf-8"),
                                  "void chat_frontend_init(void)")

        self.assertIn("create_runtime_controls();", init_body)
        self.assertIn("create_soft_keyboards();", init_body)
        self.assertGreater(init_body.index("create_soft_keyboards();"),
                           init_body.index("create_runtime_controls();"))

    def test_keyboard_button_labels_use_font1_with_symbol_fallback(self) -> None:
        source = SOURCE_PATH.read_text(encoding="utf-8")
        font_body = function_body(source,
                                  "static void prepare_soft_keyboard_font(void)")
        keyboard_body = function_body(source,
                                      "static void create_soft_keyboards(void)")

        self.assertIn(
            "soft_keyboard_font = ui_font_Font1;",
            font_body,
        )
        self.assertIn(
            "soft_keyboard_font.fallback = LV_FONT_DEFAULT;",
            font_body,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(g_kb_login, &soft_keyboard_font, LV_PART_ITEMS);",
            keyboard_body,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(g_kb_chat, &soft_keyboard_font, LV_PART_ITEMS);",
            keyboard_body,
        )

    def test_default_font_contains_every_keyboard_symbol(self) -> None:
        config = (PROJECT_ROOT / "lv_conf.h").read_text(encoding="utf-8")
        fallback_font = (LVGL_ROOT / "src" / "font" /
                         "lv_font_montserrat_14.c").read_text(encoding="utf-8")
        required_symbols = ("F053", "F054", "F00C", "F11C", "F55A", "F8A2", "F00D")

        self.assertIn("#define LV_FONT_DEFAULT &lv_font_montserrat_14", config)
        for symbol in required_symbols:
            self.assertIn(f"U+{symbol}", fallback_font)


if __name__ == "__main__":
    unittest.main()

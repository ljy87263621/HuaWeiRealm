from pathlib import Path


source = Path(__file__).resolve().parents[1] / "mycode" / "chat_frontend.c"
auth_failed_block = source.read_text(encoding="utf-8").split(
    "case CHAT_NETWORK_EVENT_AUTH_FAILED:", 1
)[1].split("break;", 1)[0]

assert "lv_label_set_text(ui_login_button_label" in auth_failed_block
assert "lv_label_set_text(ui_register_button_label" in auth_failed_block
assert "set_connection_state(event.text, false);" in auth_failed_block
assert "show_notice(event.text);" in auth_failed_block
assert "chat_network_disconnect();" in auth_failed_block

SET(SOURCES screens/ui_ChatScreen.c
    screens/ui_LoginScreen.c
    screens/ui_SettingsScreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    fonts/ui_font_Bold.c
    fonts/ui_font_H1.c
    fonts/ui_font_H2.c
    fonts/ui_font_Light.c
    fonts/ui_font_Number.c
    fonts/ui_font_Bold.c
    fonts/ui_font_H1.c
    fonts/ui_font_H2.c
    fonts/ui_font_Light.c
    fonts/ui_font_Number.c
    fonts/ui_font_Font1.c)

add_library(ui ${SOURCES})

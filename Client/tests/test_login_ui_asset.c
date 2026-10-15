#include <assert.h>
#include <string.h>

#include "../mycode/chat_login_ui.h"

int main(void)
{
    assert(strcmp(chat_login_background_path(), "A:./Assets/login_background.png") == 0);
    return 0;
}

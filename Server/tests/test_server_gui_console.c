#define main server_gui_main
#include "../server_gui.c"
#undef main

#include <assert.h>

int main(void)
{
    char line[128];
    char path[128];
    FILE *stream = tmpfile();

    assert(stream != NULL);
    assert(write_console_message(stream, "[系统] 服务器启动") == 0);
    rewind(stream);
    assert(fgets(line, sizeof(line), stream) != NULL);
    assert(strcmp(line, "[系统] 服务器启动\n") == 0);
    fclose(stream);

    assert(history_path_for_users("bob", "alice", path, sizeof(path)) == 0);
    assert(strcmp(path, "./ChatHistory/616c696365--626f62.txt") == 0);
    assert(valid_file_hash("21ff0cd3454403b393d201c99d8534628e29d6bf5e89d7fb7c904b52f9a85ac2"));
    assert(!valid_file_hash("not-a-hash"));
    assert(attachment_path_for_hash("21ff0cd3454403b393d201c99d8534628e29d6bf5e89d7fb7c904b52f9a85ac2",
                                    path, sizeof(path)) == 0);
    assert(strcmp(path, "./Attachments/21ff0cd3454403b393d201c99d8534628e29d6bf5e89d7fb7c904b52f9a85ac2") == 0);
    return 0;
}

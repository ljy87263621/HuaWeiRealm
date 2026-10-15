#include <assert.h>
#include <string.h>

#include "../mycode/chat_transfer.h"

int main(void)
{
    char hash[CHAT_TRANSFER_SHA256_HEX_SIZE];

    assert(chat_transfer_sha256_hex("", 0, hash, sizeof(hash)) == 0);
    assert(strcmp(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    assert(chat_transfer_sha256_hex("abc", 3, hash, sizeof(hash)) == 0);
    assert(strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    return 0;
}

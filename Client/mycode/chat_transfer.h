#ifndef CHAT_TRANSFER_H
#define CHAT_TRANSFER_H

#include <stddef.h>

#define CHAT_TRANSFER_SHA256_HEX_SIZE 65

int chat_transfer_sha256_hex(const void *data, size_t length, char *output, size_t output_size);

#endif

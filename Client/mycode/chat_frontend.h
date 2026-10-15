#ifndef CHAT_FRONTEND_H
#define CHAT_FRONTEND_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void chat_frontend_init(void);
void chat_frontend_shutdown(void);
bool chat_frontend_exit_requested(void);

#ifdef __cplusplus
}
#endif

#endif

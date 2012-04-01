#ifndef _COMMON_H_
#define _COMMON_H_

#include <errno.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define COMMON_BUF_SIZE 8192

void console_logger (int dbglevel, struct mdemux *f, const char *msg, ...);
void* mem_alloc(struct mdemux_buffer *buffer, void *userdata);
void mem_free(struct mdemux_buffer *buffer, void *userdata);

void dbglevel_set(int n);

uint64_t system_time(struct mdemux* f);

#endif



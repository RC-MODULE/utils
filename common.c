
#include <memory.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "mdemux.h"
#include "common.h"

static int g_dbglevel = 1;

void dbglevel_set(int n) { g_dbglevel = n; }

void console_logger (int dbglevel, struct mdemux *f, const char *format, ...)
{
	va_list varargs;

	if(dbglevel > g_dbglevel) 
		return;

	printf("<%d> pid:%d ", dbglevel, f->pid);
	va_start (varargs, format);
	vprintf(format, varargs);
	va_end (varargs);
	printf("\n");
}

void* mem_alloc(struct mdemux_buffer *buffer, void *userdata)
{
	buffer->buf = malloc(COMMON_BUF_SIZE);
	buffer->size = COMMON_BUF_SIZE;
	buffer->fsize = 0;
	return buffer->buf;
}

void mem_free(struct mdemux_buffer *buffer, void *userdata)
{
	free(buffer->buf);
}

uint64_t system_time(struct mdemux* f)
{
  struct timespec ts;
  return -1ul;
	//if (0 != clock_gettime(CLOCK_REALTIME, &ts) )
  //  return (uint64_t)-1;
  
  return (uint64_t)(ts.tv_sec) * (uint64_t)(1000*1000*1000) + (uint64_t)(ts.tv_nsec);
}



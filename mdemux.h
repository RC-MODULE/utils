#ifndef GST_MDEMUX_H
#define GST_MDEMUX_H

#include <stdint.h>
#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

struct mdemux;

struct mdemux_buffer {
  /* allocated by client */
  unsigned char* buf;
  /* allocated by client */
  size_t size;
  /* userdata can be used by clients */
  void *userdata;
  /* readonly: filled size */
  size_t fsize;
  /* private: owner */
  struct mdemux *owner;
};

/* should be called to release the buffer */
//void mdemux_release_buffer(struct mdemux_buffer *buf);

/* 
 * Callbacks of filter. User should change them after call to init, but before
 * calling any other function. 
 */
struct mdemux_callback {

  /* Log some message. dbglevel: 0-error, 1-warning, >=2 - debug */
  void (*logger) (int dbglevel, struct mdemux *f, const char *msg, ...);

  /* Allocate the buffer. Demux will call either free_space() or send_buffer()
   * on buffer obtained. Should return buffer->buf or NULL. */
  void* (*alloc_space) (struct mdemux_buffer *buffer, void *userdata);

  /* Free buffer space without sending (in cause of error, for example) */
  void (*free_space) (struct mdemux_buffer *buffer, void *userdata);

  /* Send the buffer data downstream. Buffer can be partly filled. */
  void (*send_buffer) (struct mdemux_buffer* buffer, void *userdata);

  /* Should returns some system time in nanosecs. */
  uint64_t (*time) (struct mdemux *f);
};

enum mdemux_filter_type {
  /* PES stream, i.e. TS payloads only */
  MDEMUX_PES, 
  /* TS stream */
  MDEMUX_TS 
};

/* 
 * Settings of filter. User may change them after call to init, but before
 * calling any other function. 
 */
struct mdemux_settings {
  enum mdemux_filter_type ftype;
  int pes_type;
  int adapter_id;
  int demux_id;
  /* size of hardware demux buffer */
  size_t hw_buf_size;
  /* min size of buffer to send */
  size_t min_acceptable_size;
};

struct mdemux {
  /* write_once - filled by user after init */
  struct mdemux_settings s;
  /* write_once - filled by user after init */
  struct mdemux_callback *c;
  /* private - user data */
  void *userdata; 
  /* private - file descriptor */
  int fd;
  /* readonly - current pid filter */
  int pid;
  /* readonly - bytes read since last setpid call */
  int64_t bytes_read;
  /* readonly - times are in ns */
  int64_t start_time;
  int64_t stop_time;
  /* private - current buffer to fill */
  struct mdemux_buffer *b;

  /*TODO: Implement statistics recorder */
};

enum mdemux_retcode {
  OK = 0,
  ESYS
};

/* Initializes the filter and sets it's settings to their default values */
void mdemux_init(struct mdemux *f, void *userdata);

/* Opens device (if it is not opened) and sets the filter. 
 * If newpid == -1 then closes the device. Returns 0 on success. */
int mdemux_setpid(struct mdemux *f, int newpid);

/* Closes the device */
void mdemux_close(struct mdemux *f);

/* Return data rate in bits/sec or error_code<0 in case of error (or lack of
 * data). Safe to call without MT locks.  */
static inline int64_t mdemux_datarate(struct mdemux *f)
{
  int64_t br, start, stop;
  br = f->bytes_read;
  start = f->start_time;
  stop = f->stop_time;
  if(start < 0 || stop < 0) {
    return -1;
  }
  if(start == stop) {
    return -2;
  }
  return (br * 1000*1000*1000 * 8) / (stop - start);
}

#define MDEMUX_MAXSIZE 5
#define MDEMUX_BUFSIZE 8192
#define SZ_4M (4*1024*1024)
#define MDEMUX_HWBUFSIZE SZ_4M

struct mdemux_stat_item {
  enum{mdemux_t_poll, mdemux_t_push} type;
  uint64_t time;
  uint64_t period;
};

#define MDEMUX_STATSZ 100
struct mdemux_stat {
  uint32_t nitems;
  uint32_t nitems_saved;
  int fd;
  struct mdemux_stat_item items[MDEMUX_STATSZ];
  uint64_t time_start;
  uint64_t time_start_written;
  // FIXME: protect with mutex
};

void mdemux_stat_init(struct mdemux_stat *stat, int id);
void mdemux_stat_uninit(struct mdemux_stat *stat);

struct mdemux_poller {
  /* poll timeout, in ms*/
  int timeout;
  /* filters to poll */
  struct mdemux *filters;
  /* number of filters to poll < mdemux_MAXSIZE */
  int filter_count;
  /* private stat */
  struct mdemux_stat *stat;
};

/* Perform one iteration of poll-loop */
int mdemux_loop(struct mdemux_poller *poller);

int mdemux_loop1(struct mdemux *mdemux, int timeout_ms, struct mdemux_stat *stat);

#endif


#include "mdemux.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/video.h>

/*{{{ mdemux_stat*/

void mdemux_stat_init(struct mdemux_stat *stat, int id)
{
  char fname[51];
	snprintf(fname, 50, "statfile%d", id);
  stat->nitems = 0;
  stat->nitems_saved = 0;
  stat->time_start = 0;
  stat->time_start_written = 0;
  stat->fd = open(fname,O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
}

static struct mdemux_stat_item * mdemux_stat_item_new(
  struct mdemux_stat *stat, int type)
{
  uint32_t n = stat->nitems++;
  struct mdemux_stat_item *item;
  if(n>=MDEMUX_STATSZ) return NULL;
  item = &stat->items[n];
  item->type = type;
  item->time = 0;
  item->period = 0;
  return item;
}

static void mdemux_stat_flush(struct mdemux_stat *stat)
{
  int n, i, ret;
  char str[256];
  if(stat->time_start>0 && !stat->time_start_written) {
    stat->time_start_written = 1;
    n = snprintf(str, 256, "start %llu\n", stat->time_start);
    ret = write(stat->fd, str, n);
  }
  for(i=0; i<stat->nitems_saved; i++) {
    struct mdemux_stat_item *it = &stat->items[i];
    n = snprintf(str, 256, "type %d time %llu period %llu\n",
      it->type, it->time, it->period);
    ret = write(stat->fd, str, n);
  }
  stat->nitems = 0;
  stat->nitems_saved = 0;
}

static void mdemux_stat_item_save(struct mdemux_stat *stat, struct mdemux_stat_item *item)
{
  stat->nitems_saved++;
  if(stat->nitems_saved >= MDEMUX_STATSZ) {
    mdemux_stat_flush(stat);
    return;
  }
}

void mdemux_stat_uninit(struct mdemux_stat *stat)
{
    mdemux_stat_flush(stat);
    close(stat->fd);
}
/*}}}*/

static struct mdemux_buffer* mdemux_obtain_buffer(struct mdemux *f)
{
  struct mdemux_buffer* b;
  if(f->b != NULL) {
    /* Grab saved buffer */
    b = f->b;
    return b;
  }

  b = (struct mdemux_buffer*) malloc(sizeof(struct mdemux_buffer));
  if(b == NULL)
    return NULL;
  b->owner = f;
  b->size = 0;
  b->buf = b->owner->c->alloc_space(b, f->userdata);
  if(b->buf == NULL) {
    free(b);
    return NULL;
  }
  b->fsize = 0;
  f->b = b;
  return b;
}

static void mdemux_release_buffer(struct mdemux *f, struct mdemux_buffer *b)
{
  if(f->b != b || b == NULL ) {
    f->c->logger(0,f,"mdemux_release_buffer ASSERT: invalid buffer");
    return;
  }

  if(f->b->buf) {
    f->b->owner->c->free_space(b, b->owner->userdata);
  }

  free(f->b);
  f->b = NULL;
}

static void mdemux_push_buffer(struct mdemux *f, struct mdemux_buffer *b, struct mdemux_stat *stat)
{
  struct mdemux_stat_item *sitem;

  if(f->b != b || b == NULL ) {
    f->c->logger(0,f,"mdemux_push_buffer ASSERT: invalid buffer");
    return;
  }
  
  if(stat) {
    sitem = mdemux_stat_item_new(stat, mdemux_t_push);
    if(sitem == NULL) {
      f->c->logger(0, f,"pid %d: unable to create stat item!", f->pid);
      return;
    }

    sitem->time = f->c->time(f);
  }

  f->c->send_buffer(b, f->userdata);

  if(stat) {
    sitem->period = f->c->time(f) - sitem->time;
    mdemux_stat_item_save(stat, sitem);
  }

  free(f->b);
  f->b = NULL;
}

void mdemux_init(struct mdemux *f, void *u)
{
  f->b = NULL;
  f->fd = -1;
  f->pid = -1;
  f->userdata = u;
  f->bytes_read = 0;
  f->start_time = -1;
  f->stop_time = -1;
  f->c = NULL;

  f->s.ftype = MDEMUX_PES;
  f->s.hw_buf_size = MDEMUX_HWBUFSIZE;
  f->s.min_acceptable_size = 1;
  f->s.demux_id = 0;
  f->s.adapter_id = 0;
  f->s.pes_type = 0;
}

int mdemux_setpid(struct mdemux *f, int newpid)
{
  int ret;
  int savedfd;
  char demux_name[51];
	struct dmx_pes_filter_params pes_params;

  if(f->c == NULL) {
    return -EINVAL;
  }

  if(f->fd > 0 && newpid < 0) {
    mdemux_close(f);
    return 0;
  }

  savedfd = f->fd;
  if(savedfd < 0) {
    snprintf(demux_name, 50, "/dev/dvb/adapter%d/demux%d", 
      f->s.adapter_id, f->s.demux_id);

    f->fd = open(demux_name, O_RDWR | O_NONBLOCK);
    if(f->fd < 0) {
      f->c->logger(0, f, "Error opening device");
      return -ESYS;
    }

    ret = ioctl(f->fd, DMX_SET_BUFFER_SIZE, f->s.hw_buf_size);
    if(ret != 0) {
      f->c->logger(0, f, "Error setting buffer size (errno: %d)", errno);
      goto close_demux;
    }
  }
  else {
    if(f->b)
      mdemux_release_buffer(f, f->b);
  }

  if(newpid >= 0) {
    memset(&pes_params, 0, sizeof(pes_params));
    pes_params.pid = newpid;
    pes_params.input = DMX_IN_FRONTEND;
    /* PES stream */
    switch(f->s.ftype) {
      case MDEMUX_PES: pes_params.output = DMX_OUT_TAP; break;
      default:
      case MDEMUX_TS: pes_params.output = DMX_OUT_TAP|DMX_OUT_TS_TAP; break;
    }
    /*FIXME: set DMX_PES_VIDEO*/
    pes_params.pes_type = f->s.pes_type;
    pes_params.flags = DMX_IMMEDIATE_START;

    ret = ioctl(f->fd, DMX_SET_PES_FILTER, &pes_params);
    if ( ret < 0) {
      f->c->logger(0, f, "Error sending ioctl 'DMX_SET_PES_FILTER' to demux (errno: %d)", errno);
      goto close_demux;
    }
    f->pid = newpid;
    f->bytes_read = 0;
    f->start_time = -1;
    f->stop_time = -1;
  }
  return 0;

close_demux:
  if(savedfd<0)
    close(f->fd);
  f->fd = savedfd;
  return ret;
}

void mdemux_close(struct mdemux *f)
{
  if(f->b)
    mdemux_release_buffer(f, f->b);
  if(f->fd > 0) {
    close(f->fd);
    f->fd = -1;
    f->pid = -1;
  }
}

static int mdemux_read(struct mdemux *f, struct mdemux_buffer *b)
{
  int ret;

  ret = read(f->fd, b->buf+b->fsize, (b->size-b->fsize));
  if(ret<0) {
    f->c->logger(0, f,"pid %d: read failed, errno:%d", f->pid, errno);
    return ret;
  }
  f->stop_time = f->c->time(f);
  f->c->logger(2, f,"pid %d: time %lli ns", f->pid, f->stop_time);
  if(f->start_time < 0) {
    f->start_time = f->stop_time;
  }
  f->bytes_read += ret;
  f->c->logger(2, f,"pid %d: got %d bytes (total: %llu)", f->pid, ret, f->bytes_read);
  b->fsize += ret;
  return 0;
}

int mdemux_loop(struct mdemux_poller *poller)
{
  int i;
  int ret;
	struct pollfd pfd[MDEMUX_MAXSIZE];
  struct mdemux *fs = poller->filters;
  struct mdemux *def = &fs[0];
  int n = poller->filter_count;
  int _errno;

  for(i=0; i<n; i++) {
    struct mdemux *f = &fs[i];
    if(f->fd < 0) {
      // TODO: mdemux_loop should not return when some filters are not
      // initialized.
      f->c->logger(2, f, "pes filter is not ready yet. Aborting poll.");
      return -EAGAIN;
    }
    pfd[i].fd = f->fd;
    pfd[i].events = POLLIN;
    pfd[i].revents = 0;
  }

  if(poller->stat) {
    struct mdemux_stat_item *sitem;
    sitem = mdemux_stat_item_new(poller->stat, mdemux_t_poll);
    if(sitem == NULL) {
      def->c->logger(0, def,"poller: unable to create stat item!");
      return -1;
    }
    sitem->time = def->c->time(def);
    if(poller->stat->time_start == 0) {
      poller->stat->time_start = sitem->time;
    }
    ret = poll(pfd,n,poller->timeout);
    _errno = errno;
    sitem->period = def->c->time(def) - sitem->time;
    mdemux_stat_item_save(poller->stat, sitem);
  }
  else {
    ret = poll(pfd,n,poller->timeout);
    _errno = errno;
  }

  /* error */
  if(ret<0 ) {
    if(errno == EINTR) { ret = 0; return 0; }
    def->c->logger(0, def, "poll error (errno:%d)", _errno);
    goto err;
  }

  /* timeout */
  if (ret == 0) { 
    def->c->logger(1, def, "poll timeout");
    ret = -EAGAIN;
    /*TODO: Reset BPS counters here? */
    goto err;
  }

  /* got events */
  for(i=0; i<n; i++) {
    struct mdemux *f = &fs[i];
    if(pfd[i].revents & POLLIN) {

      struct mdemux_buffer* b = mdemux_obtain_buffer(f);
      if(b==NULL) {
        f->c->logger(0, f, "failed to obtain a buffer");
        return -ENOMEM;
      }

      ret = mdemux_read(f, b);
      if(ret < 0) {
        f->c->logger(0, f,"mdemux_read failed with error %d", ret);
        goto err;
      }

      if(b->fsize >= f->s.min_acceptable_size) {
        mdemux_push_buffer(f, b, poller->stat);
      }
    }
  }
  return 0;

err:
  for(i=0;i<n;i++) {
    struct mdemux *f = &fs[i];
    struct mdemux_buffer *b;
    b = f->b;
    if(b) {
      if(b->fsize > 0)
        mdemux_push_buffer(f, b, poller->stat);
      else
        mdemux_release_buffer(f, b);
    }
  }
  return ret;
}

int mdemux_loop1(struct mdemux *mdemux, int timeout_ms, struct mdemux_stat *stat)
{
  struct mdemux_poller poller;
  poller.timeout = timeout_ms;
  poller.filters = mdemux;
  poller.filter_count = 1;
  poller.stat = stat;
  return mdemux_loop(&poller);
}


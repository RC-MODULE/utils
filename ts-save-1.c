#include <stdio.h>
#include <unistd.h>

#include "mdemux.h"
#include "common.h"

void send_buffer (struct mdemux_buffer* buffer, void *userdata);

struct mdemux_callback g_cb1 = {
	.logger = console_logger,
	.alloc_space = mem_alloc,
	.free_space = mem_free,
	.send_buffer = send_buffer,
	.time = system_time
};

void send_buffer(struct mdemux_buffer* buffer, void *userdata)
{
	int ret;
	int fd = (int)userdata;
	int wsize = 0;

	while(wsize < buffer->fsize) {
		ret = write(fd, buffer->buf + wsize, buffer->fsize-wsize);
		if(ret<0) {
			int e = errno;
			if(e == EINTR)
				continue;
			fprintf(stderr,"Got error %d while sending buffer\n", e);
			exit(-1);
		}
		if(ret == 0) {
			fprintf(stderr,"Got 0 result while sending buffer\n");
			exit(-1);
		}
		wsize += ret;
	}
	free(buffer->buf);
}

int open_file(int pid)
{
	char fname[51];
	int ret;
	snprintf(fname, 50, "ts-save-1.%d.out", pid);
	ret = open(fname,O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if(ret <0) {
		fprintf(stderr,"Unable to open file: %s\n", fname);
		exit(-1);
	}
	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	struct mdemux filter[1];
	struct mdemux_poller poller;
	int pid0;
	int fd0;

	if(argc != 2) {
		fprintf(stderr,"usage: %s PID\n", argv[0]);
		exit(-1);
	}

	dbglevel_set(1);

	pid0 = atoi(argv[1]);

	fprintf(stderr, "pid %d\n", pid0);

	//fd0 = 1; //stdout 
	fd0 = open_file(pid0);

	mdemux_init(&filter[0], (void*)fd0);
	filter[0].s.pes_type = 0;
	filter[0].s.adapter_id = 0;
	filter[0].s.demux_id = 1;
	filter[0].s.ftype = MDEMUX_TS;
	filter[0].c = &g_cb1;

	ret = mdemux_setpid(&filter[0], pid0);
	if(ret < 0) {
		fprintf(stderr,"Unable to set pid %d\n", pid0);
		exit(-1);
	}

	poller.timeout = 1000;
	poller.filters = filter;
	poller.filter_count = 1;
	poller.stat = 0;

	while(1) {
		ret = mdemux_loop(&poller);
		if(ret == -EAGAIN) {
			sleep(1);
			continue;
		}

		if(ret < 0) {
			fprintf(stderr,"mdemux_loop returns error %d\n", ret);
			break;
		}
	}

	mdemux_close(&filter[0]);
	close(fd0);
	return 0;
}


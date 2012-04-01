#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <linux/dvb/dmx.h>
#include <signal.h>
#include <arpa/inet.h>

void sighandler(int n) {
	_exit(n);
}

int main(int argc, char* argv[]) {
	sigset_t sigs;

	sigfillset(&sigs);
	
	sigprocmask(SIG_UNBLOCK, &sigs, 0);

	struct sigaction sa = {{0}};
	sa.sa_handler = sighandler;

	sigaction(SIGTERM, &sa, 0);
	
	if(argc < 5) { 
		fprintf(stderr, "usage: sec-filter device_name pid filter mask\n");
		return 1;
	}

	int fd = open(argv[1], O_RDWR);

	if(fd < 0) {
		perror("failed to open demuxer\n");
		return 1;
	}

	int pid;
	if(1 != sscanf(argv[2], "%d", &pid)) {
		fprintf(stderr, "invalid pid\n");
		return 1;
	}

	dmx_sct_filter_params params;
	memset(&params, 0, sizeof(params));

	params.pid = pid;
	params.flags = DMX_IMMEDIATE_START | DMX_CHECK_CRC;

	for(size_t i = 0; argv[3][i*2] != 0; ++i) {
		int v;
		if(1 != sscanf(argv[3]+i*2, "%2x", &v)) {
			fprintf(stderr, "invalid filter\n");
			return 1;
		}
		
		params.filter.filter[i] = v;
	}

	for(size_t i = 0; argv[4][i*2] != 0; ++i) {
    int v;
    if(1 != sscanf(argv[4]+i*2, "%2x", &v)) {
      fprintf(stderr, "invalid mask\n");
      return 1;
    }

    params.filter.mask[i] = v;
	}

	for(size_t i = 0; i < sizeof(params.filter.filter);++i) {
		fprintf(stderr, "%02x ", params.filter.filter[i]);
	}
	fprintf(stderr,"\n");
	
	for(size_t i = 0; i < sizeof(params.filter.mask);++i) {
    fprintf(stderr, "%02x ", params.filter.mask[i]);
  }
	fprintf(stderr,"%04x\n",(int)htons(2602));

	if(ioctl(fd, DMX_SET_BUFFER_SIZE, 64*1024) != 0) {
		fprintf(stderr, "failed to set buffer size\n");
		return 1;
	}

	if(ioctl(fd, DMX_SET_FILTER, &params) != 0) {
		fprintf(stderr, "failed to set filter\n");
		return 1;
	}

	char buf[4096];

	for(;;) {
		int n;
		n = read(fd, buf, sizeof(buf));

		if(n <= 0) {
			perror("read");
			break;
		}

		if(-1 == write(STDOUT_FILENO, buf, n))
			break;
		break;
	}

	return 0;
}


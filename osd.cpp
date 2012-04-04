#define this __this
#include <linux/module_vdu.h>
#undef this
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
  struct mvdu_fb_area area = {0};
	struct mvdu_fb_areas areas = {1, &area};
	
	if(argc < 6
			|| sscanf(argv[1], "%d", &area.x) !=1
			|| sscanf(argv[2], "%d", &area.y) != 1
			|| sscanf(argv[3], "%d", &area.w) != 1
			|| sscanf(argv[4], "%d", &area.h) != 1
			|| sscanf(argv[5], "%d", &area.alpha) != 1
		)
	{
		fprintf(stderr, "Usage: osd left top width height alpha\n");
		exit(1);
	}

	int fd = open("/dev/fb0", O_RDWR);
	
	if(-1 == fd) {
		perror("open");
		exit(1);
	}

	if(ioctl(fd, FBIOPUT_OSDAREAS, &areas)) {
		perror("ioctl");
		exit(1);
	}

	exit(0);
}


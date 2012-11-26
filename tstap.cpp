#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/dvb/dmx.h>

#include <stdio.h>

#include <stdexcept>
#include <system_error>

int main(int argc, char* argv[]) {
  int fd = open(argv[1], O_RDWR);
  if(fd == -1) throw std::system_error(errno, std::system_category());

  size_t pid;
  if(sscanf(argv[2], "%lu", &pid) != 1) throw std::runtime_error("invalid pid value");

  ioctl(fd, DMX_SET_BUFFER_SIZE, 128*1024);

  dmx_pes_filter_params params = {0};
  params.pid = pid;
  params.input = DMX_IN_FRONTEND;
  params.output = static_cast<dmx_output_t>(DMX_OUT_TAP | DMX_OUT_TS_TAP);
  params.pes_type = static_cast<dmx_pes_type_t>(0);
  params.flags = DMX_IMMEDIATE_START;
  
  if(ioctl(fd, DMX_SET_PES_FILTER, &params) < 0) throw std::system_error(errno, std::system_category());
  
  for(;;) {
    char buf[4096];
    int r = read(fd, buf, sizeof(buf));
    
    if(r < 0)
      throw std::system_error(errno, std::system_category());
    else if(r == 0)
      break;

    if(write(STDOUT_FILENO, buf, r) < 0) throw std::system_error(errno, std::system_category()); 
  }

  return 0;
}


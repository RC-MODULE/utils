#include <linux/i2c-dev.h>
#include "i2cbusses.h"

#define BUS 1
#define SLAVE 0x0
#define REG 0
#define VALUE 0

int main()
{
	char filename[20];
	int fd;

	fd = open_i2c_dev(1, filename, 0);
	if (fd < 0)
		exit(1);
	
	if (set_slave_addr(fd, SLAVE, 0))
		exit(1);

	i2c_smbus_write_byte_data(fd, REG, VALUE);
	return 0;
}

#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string.h>
#include <stdio.h>
#include <map>
#include <stdexcept>
#include <array>

template<typename T>
void write_pdu(int fd, T const& t) {
	if(write(fd, &t, sizeof(t)) < 0) 
		throw std::runtime_error("write_pdu failed");
}

template<typename T>
T read_pdu(int fd) {
	T t;
	if(read(fd, &t, sizeof(t)) != sizeof(t))
		throw std::runtime_error("read_pdu failed");
	return t;
}

int main(int argc, char* argv[]) {
	bool debug = false;
	std::map<std::array<char, 4>, int> codes;

	for(int i = 1; i < argc; ++i) {
		if(strcmp("-d", argv[i]) == 0) {
			debug = true; 
		}
		else {
			std::array<char, 4> id;
			int code;
			if(5 == sscanf(argv[i], "%02hx%02hx%02hx%02hx:%d", &id[0], &id[1], &id[2], &id[3], &code))
				codes[id] = code;
			else {
				fprintf(stderr, "Use %s [-d] [hexadecimal_remote_code:decimal_key_code ...]\n", argv[0]);
				return 1;
			}
		}
	}

	int port = open("/dev/ttyS1", O_RDONLY);
	if(port < 0) 
		throw std::runtime_error("failed to open serial port");	

	termios tios = {0};

	tios.c_cflag =  CS8 | CLOCAL | CREAD | B115200;
  tios.c_iflag = IGNPAR;
  cfmakeraw(&tios);
  tcflush(port, TCIFLUSH);
  tcsetattr(port, TCSANOW, &tios);

	int uinput = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if(uinput < 0)
		throw std::runtime_error("failed to open uinput device");

	if(ioctl(uinput, UI_SET_EVBIT, EV_KEY) < 0)
		throw std::runtime_error("ioctl(uinput, UI_SET_EVBIT, EV_KEY)");

	uinput_user_dev uidev = {0};
	
	strncpy(uidev.name, "RC Module 5909 front panel", sizeof(uidev.name));
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor  = 0x1;
	uidev.id.product = 0x1;
	uidev.id.version = 1;

	write_pdu(uinput, uidev);

	for(int i = 0; i < 256; i++)
      ioctl(uinput, UI_SET_KEYBIT, i);
	
	if(ioctl(uinput, UI_DEV_CREATE) < 0)
		throw std::runtime_error("ioctl(uinput, UI_DEV_CREATE)");

	for(;;) {
		auto port_pdu = read_pdu<std::array<char, 8>>(port);
	
		auto i = codes.find(*reinterpret_cast<std::array<char,4>	const*>(port_pdu.begin() + 3));
		if(i != codes.end()) {
			auto send_uievent = [=](int type, int code, int value) {
				input_event ev = {0};
				ev.type = type;
				ev.code = code;
				ev.value = value;
				write_pdu(uinput, ev);
			};

			if(debug) fprintf(stderr, "%02x%02x%02x%02x:%d\n", port_pdu[3], port_pdu[4], port_pdu[5], port_pdu[6], i->second);

			send_uievent(EV_KEY, i->second, 1);
			send_uievent(EV_SYN, SYN_REPORT, 0);
      send_uievent(EV_KEY, i->second, 0);
      send_uievent(EV_SYN, SYN_REPORT, 0);
		}
		else 
			if(debug) fprintf(stderr, "%02x%02x%02x%02x\n", port_pdu[3], port_pdu[4], port_pdu[5], port_pdu[6]);
	}

	return 0;
}


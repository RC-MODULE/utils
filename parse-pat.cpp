#include <stdio.h>
#include <iostream>
#include <arpa/inet.h>

uint32_t ntoh(uint32_t t) {
	return ntohl(t);
}

uint16_t ntoh(uint16_t t) {
	return ntohs(t);
}

uint8_t ntoh(uint8_t t) {
	return t;
}

template<typename T>
struct ReadWrapper {
	ReadWrapper(T& t) : t_(t) {}
	T& t_;
};

template<typename T>
std::istream& operator >> (std::istream& is, ReadWrapper<T> const& r) {
	is.read(reinterpret_cast<char*>(&r.t_), sizeof(r.t_));
	r.t_ = ntoh(r.t_);
	return is;
}

template<typename T>
ReadWrapper<T> read(T& t) {
	return ReadWrapper<T>(t);
}

struct PatSectionHeader {
	uint8_t tableid_;
	uint16_t length_;

	static const size_t header_length = 8;

	uint16_t streamid_;
	
	uint8_t version_;
	bool current_next_;

	uint8_t section_number_;
	uint8_t last_section_number_;
};

std::istream& operator >> (std::istream& is, PatSectionHeader& h) {
	uint16_t d;
	uint8_t v;
	is >> read(h.tableid_) >> read(d) >> read(h.streamid_) >> read(v) >> read(h.section_number_) >> read(h.last_section_number_);

	h.length_ = d & ((1 << 12) - 1);
	h.version_ = (v & ((1 << 5) -1)) >> 1;
	h.current_next_ = v & 1;

	return is;
}

int main(int argc, char* argv[]) {
	PatSectionHeader hdr = {0};
 	if(std::cin >> hdr) {
		std::cout << "{";		

		std::cout << "\"tableid\":" << (uint32_t)hdr.tableid_ << ",";
		std::cout << "\"streamid\":" << hdr.streamid_ << ",";
		std::cout << "\"version\":" << (uint32_t)hdr.version_ << ",";
		std::cout << "\"number\":" << (uint32_t)hdr.section_number_ << ",";
		std::cout << "\"lastnumber\":" << (uint32_t)hdr.last_section_number_ << ",";

		std::cout << "\"programs\":[";
		for(int i = 0; std::cin && hdr.length_ > PatSectionHeader::header_length && i < (hdr.length_ - PatSectionHeader::header_length)/4; ++i) {
			uint16_t program, pid;
			std::cin >> read(program) >> read(pid);

			std::cout << ((i!=0) ? ","  : "") << "{\"program\":" << program << ",\"pid\":" << (pid & ((1 << 12) -1)) << "}";
		}
		std::cout << "]";

		uint32_t crc;
		std::cin >> read(crc);

		std::cout << "}" << std::endl;	
	}
}


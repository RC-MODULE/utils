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

struct PmtSectionHeader {
	uint8_t tableid_;
	uint16_t length_;

	static const size_t header_length = 9;

	uint16_t program_number_;
	
	uint8_t version_;
	bool current_next_;

	uint8_t section_number_;
	uint8_t last_section_number_;

	uint16_t pcr_pid_;
	uint16_t program_info_length_;
};

std::istream& operator >> (std::istream& is, PmtSectionHeader& h) {
	uint16_t d;
	uint8_t v;
	is >> read(h.tableid_) >> read(d) >> read(h.program_number_) >> read(v) >> read(h.section_number_)
		>> read(h.last_section_number_) >> read(h.pcr_pid_) >> read(h.program_info_length_);

	h.length_ = d & ((1 << 12) - 1);
	h.version_ = (v & ((1 << 5) -1)) >> 1;
	h.current_next_ = v & 1;
	h.pcr_pid_ &= ((1 << 12) - 1);
	h.program_info_length_ &= ((1 << 9) - 1);

	return is;
}

int main(int argc, char* argv[]) {
	std::cin.rdbuf()->pubsetbuf(0,0);

	PmtSectionHeader hdr = {0};
 		
	if(std::cin >> hdr) {
		std::cout << "{";		

		std::cout << "\"tableid\":" << (uint32_t)hdr.tableid_ << ",";
		std::cout << "\"program_number\":" << hdr.program_number_ << ",";
		std::cout << "\"version\":" << (uint32_t)hdr.version_ << ",";
		std::cout << "\"number\":" << (uint32_t)hdr.section_number_ << ",";
		std::cout << "\"lastnumber\":" << (uint32_t)hdr.last_section_number_ << ",";
		std::cout << "\"pcrpid\":" << hdr.pcr_pid_ << ",";

		std::cout << "\"descriptors\":[";
		for(size_t count = 0; count != hdr.program_info_length_;) {
			if(count) std::cout << ",";

			uint8_t tag;
			uint8_t length;
			std::cin >> read(tag) >> read(length);
			std::cin.ignore(length);

			std::cout << "{\"tag\":" << (uint32_t)tag << "}";

			count += sizeof(tag) + sizeof(length) + length;
		}

		std::cout << "], \"streams\": [";
		for(size_t count = 0; count != hdr.length_ - PmtSectionHeader::header_length - hdr.program_info_length_ - sizeof(uint32_t);) {
			if(count) std::cout << ',';	
			
			uint8_t type;
			uint16_t pid;
			uint16_t len;
			
			std::cin >> read(type) >> read(pid) >> read(len);
			pid &= ((1 << 13)-1);
			len &= ((1 << 10)-1);

			count += sizeof(type) + sizeof(pid) + sizeof(len) + len;
	
			std::cout << "{" << "\"type\":" << (uint32_t)type << ",\"pid\":" << pid << ",\"descriptors\":[";
			for(size_t count = 0; count < len;) {
				if(count) std::cout << ",";

				uint8_t tag;
       	uint8_t length;
       	std::cin >> read(tag) >> read(length);
				std::cin.ignore(length);
				count += sizeof(tag) + sizeof(length) + length;				
	
				std::cout << "{\"tag\":" << (uint32_t)tag << "}";	
			}
			std::cout << "]}";
		}
		std::cout << "]";

		uint32_t crc;
		std::cin >> read(crc);

		std::cout << "}" << std::endl;	
	}
}


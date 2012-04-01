#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <arpa/inet.h>
#include <errno.h>
#include <iomanip>
#include <iconv.h>

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

struct SdtHeader {
	uint8_t tableid_;
	uint16_t length_;

	uint16_t streamid_;
	
	uint8_t version_;
	bool current_next_;

	uint8_t section_number_;
	uint8_t last_section_number_;

	uint16_t original_network_id_;
};

std::istream& operator >> (std::istream& is, SdtHeader& h) {
	uint16_t d;
	uint8_t v;
	uint8_t r;
	is >> read(h.tableid_) >> read(d) >> read(h.streamid_) >> read(v) >> read(h.section_number_)
		>> read(h.last_section_number_) >> read(h.original_network_id_) >> read(r);

	h.length_ = d & ((1 << 12) - 1);
	h.version_ = (v & ((1 << 5) -1)) >> 1;
	h.current_next_ = v & 1;

	return is;
}

std::string expand(std::string const& s) {
	std::ostringstream os;
	for(size_t i = 0; i < s.size(); ++i) {
		if((s[i] > 0 && s[i] < 32)) { // || s[i] == '\\' || s[i] == '\"' || s[i] == '/') {
			switch(s[i]) {
			case '\"':
				os << "\\\"";
				break;
			case '/':
				os << "\\/";
				break;
			case '\\':
				os << "\\\\";
				break;
			case '\b':
				os << "\\b";
				break;
			case '\n':
				os << "\\n";
				break;
			case '\r':
				os << "\\r";
				break;
			case '\t':
				os << "\\t";
				break;
			default:
				os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (uint16_t)s[i];
				break;
			}
		}
		else
			os << s[i];
	}

	return os.str();
}

std::string conv(size_t n, std::string const& in) {
	static iconv_t cds[5] = {(iconv_t)-1, (iconv_t)-1, (iconv_t)-1, (iconv_t)-1, (iconv_t)-1};

	if(n > 5) throw std::runtime_error("unsupported encoding");
	
	if((iconv_t)-1 == cds[n]) {
		const char* from[6] = {
			"ISO-8859-2", "ISO-8859-5", "ISO8859-6", "ISO8859-7", "ISO8859-8", "ISO8859-9"
		};
		cds[n] = iconv_open("UTF-8", from[n]);
		if((iconv_t)-1 == cds[n])
			throw std::runtime_error("failed to allocate iconv converter");
	}

	std::string out;
	out.resize(in.length()*2);

	char* inp = const_cast<char*>(&in[0]);
	size_t insize = in.size();

	char* outp = &out[0];
	size_t outsize = out.size();

	iconv(cds[n], 0, 0, 0, 0);
	size_t r = iconv(cds[n], &inp, &insize, &outp, &outsize);

	if(r == -1ul) throw std::runtime_error("conversion failed");

	out.resize(outp - &out[0]);

	return expand(out);
} 

std::string conv(const char* in) {
	if(in[0] < 1 || in[0] > 5) 
		return conv(0, in);
	
	return conv(in[0], in + 1);
}

int main(int argc, char* argv[]) {
	SdtHeader hdr = {0};
 		
	std::streampos s = std::cin.tellg();

	if(std::cin >> hdr) {
		std::cout << "{";		

		std::cout << "\"tableid\":" << (uint32_t)hdr.tableid_ << ",";
		std::cout << "\"streamid\":" << hdr.streamid_ << ",";
		std::cout << "\"version\":" << (uint32_t)hdr.version_ << ",";
		std::cout << "\"number\":" << (uint32_t)hdr.section_number_ << ",";
		std::cout << "\"lastnumber\":" << (uint32_t)hdr.last_section_number_ << ",";
		std::cout << "\"original_network_id\":" << (uint32_t)hdr.original_network_id_ << ",";

		std::cout << "\"services\":[";
		for(size_t count = 0; count < hdr.length_ - 4 - 8;) {
			if(count) std::cout << ",";

			uint16_t service;
			uint8_t flags;
			uint16_t length;

			std::cin >> read(service) >> read(flags) >> read(length);
		
			uint8_t running_status = (length >> 13) & 0x7;
			bool free_ca_mode = (length >> 12) & 1;

			length &= (1 << 12)-1;
			count += sizeof(uint16_t) + sizeof(uint8_t) +sizeof(uint16_t) + length;
				
			std::cout << "{\"service\":" << service << ",\"EIT_schedule_flag\":" << bool(flags & 2) 
				<< ",\"EIT_present_followin_flag\":" << bool(flags & 1) << ",\"descriptors\":[";
			for(size_t count2 = 0; count2 < length;) {
				if(count2) std::cout << ",";

				uint8_t tag;
				uint8_t length;

				std::cin >> read(tag) >> read(length);
				count2 += sizeof(tag) + sizeof(length) + length;
		
				if(tag == 0x48) {
					uint8_t type;
					uint8_t sp_name_len; 
					std::cin >> read(type) >> read(sp_name_len);
		
					char spname[257] = {0};
					std::cin.read(spname, sp_name_len);

					uint8_t name_len;
					std::cin >> read(name_len);

					char name[257] = {0};
					std::cin.read(name, name_len);
					
					std:: cout << "{\"tag\":" << (uint32_t)tag << ",\"type\":" << (uint32_t)type
						<<  ",\"provider\":\"" << conv(spname) << "\",\"name\":\"" << conv(name) << "\"}";
				
					std::cin.ignore(length - 1 - 1 -1 - sp_name_len - 1 -name_len);
				}
				else {
					std::cout << "{\"tag\":" << (uint32_t)tag << "}";
					std::cin.ignore(length);
				} 
			}
			//std::cin.ignore(length);
			std::cout << "]}";
		}
		std::cout << "]";

		uint32_t crc;
		std::cin >> read(crc);

		std::cout << "}" << std::endl;	
	}
}


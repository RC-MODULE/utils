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

struct EitHeader {
	uint8_t tableid_;
	uint16_t length_;

	uint16_t service_id_;
	
	uint8_t version_;
	bool current_next_;

	uint8_t section_number_;
	uint8_t last_section_number_;

	uint16_t transport_stream_id_;
	uint16_t original_network_id_;
	uint8_t segment_last_section_number_;
	uint8_t last_table_id_;
};

std::istream& operator >> (std::istream& is, EitHeader& h) {
	uint16_t d;
	uint8_t v;
	
	is >> read(h.tableid_) >> read(d) >> read(h.service_id_) >> read(v) 
		>> read(h.section_number_) >> read(h.last_section_number_)
		>> read(h.transport_stream_id_) >> read(h.original_network_id_) 
		>> read(h.segment_last_section_number_) >> read(h.last_table_id_);

	h.length_ = d & ((1 << 12) - 1);
	h.version_ = (v & ((1 << 5) -1)) >> 1;
	h.current_next_ = v & 1;

	return is;
}

std::string expand(std::string const& s) {
	std::ostringstream os;
	for(size_t i = 0; i < s.size(); ++i) {
		if((s[i] > 0 && s[i] < 32) || s[i] == '\\' || s[i] == '"' || s[i] == '/') {
			switch(s[i]) {
			case '"':
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

std::string conv(std::string const& in) {
	if(in[0] < 1 || in[0] > 5) 
		return conv(0, in);
	
	return conv(in[0], in.c_str() + 1);
}

std::istream& read_mjd(std::istream& is, int& year, int& month, int& day) {
	uint16_t mjd;

	is >> read(mjd);

	if(mjd < 0xFE00) {
		year = int((mjd - 15078.2) / 365.25);
		month = int((mjd - 14956.1 - int(year*365.25)) / 30.6001);
		day = mjd - 14956 - int(year*365.25) - int(month * 30.60001);
		if(month == 14 || month == 15) {
			year += 1;
			month -= 12;
		}
		month -= 1;
		year += 1900;
	}
	else {
		year = month = day = -1;
	}

	return is;
}

uint8_t from_bcd(uint8_t v) {
	return v/16*10+v%16;
}

std::istream& read_time(std::istream& is, int& hours, int& minutes, int& seconds) {
	uint8_t h,m,s;
	std::ios_base::fmtflags flags = is.flags();
	is >> std::dec >> read(h) >> read(m) >> read(s) >> std::resetiosflags(flags);

	hours = from_bcd(h);
	minutes = from_bcd(m);
	seconds = from_bcd(s);

	return is;
}

std::ostream& write_iso_date_time(std::ostream& os, int year, int month, int day, int hour, int minute, int second) {
	std::streamsize w  = os.width();
	char fill = os.fill();
	return os << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2) << month << '-'<< std::setw(2) << day << 'T' <<
		std::setw(2) << hour << ':' << std::setw(2) << minute << ':' << std::setw(2) << second << 'Z' << std::setw(w) << std::setfill(fill); 
}

std::ostream& write_iso_duration(std::ostream& os, int hours, int minutes, int seconds) {
	return os << "PT" << hours << "H" << minutes << "M" << seconds << "S";
}

int main(int argc, char* argv[]) {
	EitHeader hdr = {0};
 		
	while(std::cout && std::cin && (std::cin >> hdr)) {
		std::cout << "{";		

		std::cout 
			<< "\"table_id\":" << (uint32_t)hdr.tableid_ << ","
			<< "\"service_id\":" << hdr.service_id_ << ","
			<< "\"version\":" << (uint32_t)hdr.version_ << ","
			<< "\"number\":" << (uint32_t)hdr.section_number_ << ","
			<< "\"lastnumber\":" << (uint32_t)hdr.last_section_number_ << ","
			<< "\"transport_stream_id\":" << (uint32_t)hdr.transport_stream_id_ << ","
			<< "\"original_network_id\":" << (uint32_t)hdr.original_network_id_ << ","
			<< "\"segment_last_section_number\":" << (uint32_t)hdr.segment_last_section_number_ << ","
			<< "\"last_table_id\":" << (uint32_t)hdr.last_table_id_ << ","
			<< "\"events\":[";
		
		for(size_t count = 0; count < size_t(hdr.length_) - 4 - 11;) {
			if(count) std::cout << ",";

			uint16_t event_id;
			int start_year;
			int start_month;
			int start_day;
			int start_hour;
			int start_minute;
			int start_second;
			int duration_hours;
			int duration_minutes;
			int duration_seconds;
			uint16_t length;
			
			std::cin >> read(event_id);
			read_mjd(std::cin, start_year, start_month, start_day);
			read_time(std::cin, start_hour, start_minute, start_second);
			read_time(std::cin, duration_hours, duration_minutes, duration_seconds);
			std::cin >> read(length);
		
			uint8_t running_status = (length >> 13) & 0x7;
			bool free_ca_mode = (length >> 12) & 1;

			length &= (1 << 12)-1;
			count += sizeof(uint16_t) + 5 + 3 + sizeof(length) + length;
				
			std::cout << "{\"event_id\":" << event_id << ",\"start_time\":\"";
			if(start_year != -1)
				write_iso_date_time(std::cout, start_year, start_month, start_day, start_hour, start_minute, start_second);
			std::cout << "\",\"duration\":\"";
			
			if(start_year != -1)
				write_iso_duration(std::cout, duration_hours, duration_minutes, duration_seconds);
			std::cout << "\",\"running_status\":" << (uint32_t)running_status << ",\"free_ca_mode\":" << free_ca_mode 
				<< ",\"descriptors\":[";

			for(size_t count2 = 0; count2 < length;) {
				if(count2) std::cout << ",";

				uint8_t tag;
				uint8_t length;

				std::cin >> read(tag) >> read(length);
				count2 += sizeof(tag) + sizeof(length) + length;
	
				if(tag == 0x4d) {
					char language[4] = {0};
					std::cin.read(language, sizeof(language) - 1);
					uint8_t namelen;
					std::cin >> read(namelen);

					std::string name;
					name.resize(namelen);
					std::cin.read(&name[0], namelen);

					uint8_t textlen;
					std::cin >> read(textlen);

					std::string text;
					text.resize(textlen);
					std::cin.read(&text[0], textlen);

					std::cout << "{\"tag\":" << (uint32_t)tag <<",\"ISO_639_language_code\":\"" << language << "\",\"name\":\"" << conv(name) << "\",\"text\":\"" << conv(text) << "\"}";
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
		break;
	}
}


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

struct NitSectionHeader {
	uint8_t tableid_;
	uint16_t length_;

	uint16_t network_id_;
	
	uint8_t version_;
	bool current_next_;

	uint8_t section_number_;
	uint8_t last_section_number_;

	uint16_t network_descriptors_length_;
};

std::istream& operator >> (std::istream& is, NitSectionHeader& h) {
	uint16_t d;
	uint8_t v;
	is >> read(h.tableid_) >> read(d) >> read(h.network_id_) >> read(v) >> read(h.section_number_)
		>> read(h.last_section_number_) >> read(h.network_descriptors_length_);

	h.length_ = d & ((1 << 12) - 1);
	h.version_ = (v & ((1 << 5) -1)) >> 1;
	h.current_next_ = v & 1;
	h.network_descriptors_length_ &= ((1 << 12) - 1);

	return is;
}

int main(int argc, char* argv[]) {
	std::cin.rdbuf()->pubsetbuf(0,0);

	//for(;std::cin && std::cout;) {
	NitSectionHeader hdr = {0};
 		
	if(std::cin >> hdr) {
		std::cout << "{";		

		std::cout << "\"tableid\":" << (uint32_t)hdr.tableid_ << ",";
		std::cout << "\"network_id\":" << hdr.network_id_ << ",";
		std::cout << "\"version\":" << (uint32_t)hdr.version_ << ",";
		std::cout << "\"number\":" << (uint32_t)hdr.section_number_ << ",";
		std::cout << "\"lastnumber\":" << (uint32_t)hdr.last_section_number_ << ",";

		std::cout << "\"descriptors\":[";
		for(size_t count = 0; count != hdr.network_descriptors_length_;) {
			if(count) std::cout << ",";

			uint8_t tag;
			uint8_t length;
			std::cin >> read(tag) >> read(length);
			std::cin.ignore(length);

			std::cout << "{\"tag\":" << (uint32_t)tag << "}";

			count += sizeof(tag) + sizeof(length) + length;
		}

		uint16_t stream_length;
		std::cin >> read(stream_length);
		stream_length &= ((1 << 12)-1);

		std::cout << "],\"streams\":[";
		for(size_t count = 0; count != stream_length;) {
			if(count) std::cout << ',';	
			
			uint16_t transport_stream_id;
			uint16_t original_network_id;
			uint16_t len;
			
			std::cin >> read(transport_stream_id) >> read(original_network_id) >> read(len);
			len &= ((1 << 12)-1);

			count += sizeof(transport_stream_id) + sizeof(original_network_id) + sizeof(len) + len;
	
			std::cout << "{" << "\"transport_stream_id\":" << (uint32_t)transport_stream_id << ",\"original_network_id\":" << original_network_id << ",\"descriptors\":[";
			for(size_t count = 0; count < len;) {
				if(count) std::cout << ",";

				uint8_t tag;
       	uint8_t length;
       	std::cin >> read(tag) >> read(length);
				
				if(tag == 0x43) { // satellite_delivery_system_descriptor
					uint32_t frequency;
					uint16_t orbital_position;
					uint8_t options;
					uint32_t symbol_rate;

					std::cin >> read(frequency) >> read(orbital_position) >> read(options) >> read(symbol_rate);

					std::cout << "{\"tag\":" << (uint32_t)tag 
						<< std::hex 
						<< ",\"frequency\":" << frequency << ",\"orbital_position\":" << orbital_position
						<< std::dec 
						<< ",\"west_east_flag\":" << (options >> 7) << ",\"polarization\":" << ((options >> 5) & 3) << ", \"modulation\":" << (options & ((1 << 5)-1))
						<< std::hex
						<< ",\"symbol_rate\":" << (symbol_rate >> 4) << ",\"FEC_inner\":" << (symbol_rate & 15) 
						<< std::dec << "}";
				}
				else if(tag == 0x5a) { // terrestrial_delivery_system_descriptor
					uint32_t centre_frequency;
					uint8_t bandwidth;
					uint8_t constellation__hierarchy_information__code_rate_HP_stream;
					uint8_t code_rate_LP_stream__guard_interval__transmission_mode__other_frequency_flag;

					std::cin >> read(centre_frequency) >> read(bandwidth) 
						>> read(constellation__hierarchy_information__code_rate_HP_stream)
						>> read(code_rate_LP_stream__guard_interval__transmission_mode__other_frequency_flag);
				
					std::cin.ignore(4);

					std::cout << "{\"tag\":" << (uint32_t)tag 
						<< ",\"centre_frequency\":" << centre_frequency << std::dec << ",\"bandwith\":" << (bandwidth >> 5) 
						<< ",\"constelation\":" << (constellation__hierarchy_information__code_rate_HP_stream >> 6)
						<< ",\"hierarchy_information\":" << ((constellation__hierarchy_information__code_rate_HP_stream >> 3) & 7)
						<< ",\"code_rate-HP_stream\":" << (constellation__hierarchy_information__code_rate_HP_stream & 7)
						<< ",\"code_rate-LP_stream\":" << (code_rate_LP_stream__guard_interval__transmission_mode__other_frequency_flag >> 5)
						<< ",\"guard_interval\":" << ((code_rate_LP_stream__guard_interval__transmission_mode__other_frequency_flag >> 3) & 7)
						<< ",\"transmission_mode\":" << ((code_rate_LP_stream__guard_interval__transmission_mode__other_frequency_flag >> 1) & 3)
						<< ",\"other_frequency_frequency_flag\":" << (code_rate_LP_stream__guard_interval__transmission_mode__other_frequency_flag & 1)
						<< "}";
				}
				else {
					std::cin.ignore(length);
					std::cout << "{\"tag\":" << (uint32_t)tag << "}";	
				}

				count += sizeof(tag) + sizeof(length) + length;				
			}
			std::cout << "]}";
		}
		std::cout << "]";

		uint32_t crc;
		std::cin >> read(crc);

		std::cout << "}" << std::endl;	
	}
	//}
}


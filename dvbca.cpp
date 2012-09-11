#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/dmx.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <exception>
#include <stdexcept>
#include <sstream>
#include <map>

#define T_SB                0x80  // sb                           primitive   h<--m
#define T_RCV               0x81  // receive                      primitive   h-->m
#define T_CREATE_T_C        0x82  // create transport connection  primitive   h-->m
#define T_C_T_C_REPLY       0x83  // ctc reply                    primitive   h<--m
#define T_DELETE_T_C        0x84  // delete tc                    primitive   h<->m
#define T_D_T_C_REPLY       0x85  // dtc reply                    primitive   h<->m
#define T_REQUEST_T_C       0x86  // request transport connection primitive   h<--m
#define T_NEW_T_C           0x87  // new tc / reply to t_request  primitive   h-->m
#define T_T_C_ERROR         0x77  // error creating tc            primitive   h-->m
#define T_DATA_LAST					0xA0

bool g_debug = false;

static const uint32_t RSRCID_RESOURCE_MANAGER = 0x10041;

enum {
	STAG_OPEN_SESSION_REQUEST = 0x91,
	STAG_OPEN_SESSION_RESPONSE = 0x92
};

template<typename I>
I& write_uint32(I& i, uint32_t v) {
	for(size_t c = 0; c < sizeof(v); ++c)
		*i++ = (v >> ((3 - c) * 8))&0xFF;
	return i;
}

template<typename I>
I& write_uint16(I& i, uint16_t v) {
	*i++ = (v >> 8) & 0xFF;
	*i++ = v & 0xFF;
	return i;
}

template<typename I>
I& t_create_t_c(I& i, uint8_t n) {
	*i++ =  T_CREATE_T_C;
	*i++ = 1;
	*i++ = n;
	return i;
}

template<typename I>
I& t_recv_data(I& i, uint8_t connection_id) {
	*i++ = T_RCV;
	*i++ = 1;
	*i++ = connection_id;
	return i;
}

template<typename I>
I& t_data_last(I& i, uint8_t connection_id) {
	*i++ = T_DATA_LAST;
	*i++ = 1;
	*i++ = connection_id;
	return i;
}

template<typename I>
I& write_length(I& i, size_t length) {
	if(length > 127) {
		if(length > 0xFFFF) {
			if(length > 0xFFFFFF) {
				*i++ = 0x84;
				*i++ = (length >> 24) & 0xFF;
				*i++ = (length >> 16) & 0xFF;
				*i++ = (length >> 8) & 0xFF;
				*i++ = length & 0xFF;
			}
			else {
				*i = 0x83;
				*i++ = (length >> 16) & 0xFF;
				*i++ = (length >> 8) & 0xFF;
				*i++ = length & 0xFF;
			}
		}
		else {
			*i = 0x82;
			*i++ = (length >> 8) & 0xFF;
       *i++ = length & 0xFF;
		}
	}
	else {
	 *i++ = length & 0xFF;	
	}

	return i;
}

template<typename I>
I& t_send_data(I& i, uint8_t connection_id, size_t length) {
	*i++ = T_DATA_LAST;
	write_length(i, length + 1);
	*i++ = connection_id;
	return i;
}

template<typename I>
I& open_session_response(I& w, uint8_t status, uint32_t resource, uint16_t nb) {
	*w++ = STAG_OPEN_SESSION_RESPONSE;
	*w++ = 7;
	*w++ = status;
	write_uint32(w, resource);
	write_uint16(w, nb); 
	return w;
}


template<typename I>
I& session_create(I& i, uint32_t resource, uint16_t nb) {
	*i++ = 0x93;
	*i++ = 0x6;

	i = write_uint32(i, resource);
	return write_uint16(i, nb);	
}

template<typename I>
I& session_nb(I& i, uint16_t nb) {
	*i++ = 0x90;
	*i++ = 0x2;
	
	return write_uint16(i, nb);
}

template<typename I>
I& session_close(I& i, uint16_t nb) {
	*i++ = 0x95;
	*i++ = 0x2;
	return write_uint16(i, nb);
}

template<typename I>
I& app_tag(I& i, uint32_t tag) {
	*i++ = tag & 0xFF;
	*i++ = (tag >> 8) & 0xFF;
	*i++ = (tag >> 16) & 0xFF;
	return i;
}

template<typename I>
I& ca_info_enq(I& i) {
	i = app_tag(i, 0x9f8010);
	*i++ = 0;
	return i;
}

uint32_t parse_uint32(const uint8_t*& b, const uint8_t* e) {
	if(e-b < 4) throw std::runtime_error("parse_uint32: data unit is too short");
	uint32_t t = (b[0] << 24) + (b[1] << 16) + (b[2] << 8) + b[3];
	b += 4;
	return t;
}

uint16_t parse_uint16(const uint8_t*& b, const uint8_t* e) {
	if(e-b < 2) throw std::runtime_error("parse_uint16: data unit is too short");
	uint16_t t = (b[0] << 8) + b[1];
	b+=2;
	return t;
}

size_t parse_length(const uint8_t*& b, const uint8_t* e) {
	if(e-b < 1) throw std::runtime_error("parse_length: data unit is too short");

	size_t r = *b++;
	if(r & 0x80) {
		r &= 0x7F;
		if(e-b < r) throw std::runtime_error("parse_length: data unit is too short");

		size_t l = 0;
		for(size_t i = 0; i != r; ++i)
			l = (l << 8) + (*b++);
	
		return l;
	}
	else
		return r;
}

struct TRPDU {
	uint8_t tag_;
	uint8_t connection_id_;
	const uint8_t* body_;
	size_t body_len_;
	bool message_available_;
};

TRPDU parse_trpdu(const uint8_t* begin, const uint8_t* end) {
	TRPDU r = {0};

	if(end - begin > 4) {
		if(end - begin < 7) throw std::runtime_error("invalid trdpu: too small");
		r.tag_ = *begin++;
		r.body_len_ = parse_length(begin, end) - 1;

		if(end - begin < 5 + r.body_len_) throw std::runtime_error("invalid trpdu: invalid length");

		r.connection_id_ = *begin++;
		r.body_ = begin;

		begin += r.body_len_;
	}

	if(*begin++ != T_SB) throw std::runtime_error("invalid trpdu: no status block");
	if(*begin++ != 2) throw std::runtime_error("invalid trpdu: invalid status block length");
	r.connection_id_= *begin++;
	//if(*begin++ != r.connection_id_) throw std::runtime_error("invalid trpdu: invalid connectio id in status block");
	r.message_available_ = ((*begin) && 0x80) != 0;

	return r;
}

struct SPDU {
	uint8_t tag_;
	union {
		struct {
			uint32_t resource_;
		} open_session_request_;
	
		struct {
			uint8_t status_;
			uint32_t resource_;
			uint16_t nb_;
		} create_session_response_;

		struct {
			uint16_t nb_;
		} close_session_request_;

		struct {
			uint8_t status_;
			uint16_t nb_;
		} close_session_response_;

		struct {
			uint16_t nb_;
			const uint8_t* apdu_;
			size_t len_;
		} session_number_;
	};	
};

SPDU parse_spdu(const uint8_t* b, const uint8_t* e) {
	if(e-b < 2) throw std::runtime_error("spdu is too small");

	SPDU spdu = {0};

	switch(spdu.tag_ = *b++) {
	case STAG_OPEN_SESSION_REQUEST:
		if(parse_length(b,e) != 0x4) throw std::runtime_error("spdu: open session request has invalid length");
		spdu.open_session_request_.resource_ = parse_uint32(b, e);
		break;
	case 0x94:
		if(parse_length(b,e) != 0x6) throw std::runtime_error("spdu: create session response has invalid length");
		spdu.create_session_response_.status_ = *b++;
		spdu.create_session_response_.resource_ = parse_uint32(b, e);
		spdu.create_session_response_.nb_ = parse_uint16(b, e);
		break;
	case 0x95:
		if(parse_length(b,e) != 2) throw std::runtime_error("spdu: close session request has invalid length");
		spdu.close_session_request_.nb_ = parse_uint16(b, e);
		break;
	case 0x96:
		if(parse_length(b,e) != 3) throw std::runtime_error("spdu: close session response has invalid length");
		spdu.close_session_response_.status_ = *b++;
		spdu.close_session_response_.nb_ = parse_uint16(b, e);
		break;
	case 0x90:
		if(*b++ != 2) throw std::runtime_error("spdu: session number pdu is too short");
		spdu.session_number_.nb_ = parse_uint16(b, e);
		spdu.session_number_.apdu_ = b;
		spdu.session_number_.len_ = e-b;
		b = e;
		break;
	default:
		throw std::runtime_error("unknown spdu tag");
	}

	return spdu;
}

int g_fd;

struct Connection {
	Connection(uint8_t id) : id_(id) {}

	uint8_t* start_spdu() {
		uint8_t* b = begin_tpdu();
		return t_data_last(b, id_);
	}

	void end_spdu(uint8_t* e) {
		end_tpdu(e);
	}

	virtual void on_spdu(const uint8_t* b, size_t l) = 0;

	void read() {
		int l = ::read(g_fd, rdata_, sizeof(rdata_));
		if(l == -1) {
			perror("read");
			exit(1);
		}

		if(g_debug) {
			fprintf(stderr, "connection read:");
			for(int i = 0; i < l; ++i) {
				fprintf(stderr, " %02X", rdata_[i]);
			}
			fprintf(stderr, "\n");
		}

		const uint8_t* r = rdata_ + 2;
		TRPDU trpdu = parse_trpdu(r, r + l - 2);

		switch(trpdu.tag_) {
		case T_C_T_C_REPLY:
			break;
		case T_DATA_LAST:
			on_spdu(trpdu.body_, trpdu.body_len_); 
			break; 
		default:	
			break;
		}

		if(trpdu.message_available_)
			recv_data();
	}

	void create_t_c() {
		uint8_t* b = begin_tpdu();
		end_tpdu(t_create_t_c(b, id_));
	}

	void recv_data() {
		uint8_t* b = begin_tpdu();
		end_tpdu(t_recv_data(b, id_));
	}
private:
	uint8_t* begin_tpdu() {
		return wdata_;
	} 
	
	void end_tpdu(uint8_t* w) {
		const uint8_t* plen = begin_tpdu() + 1;
		parse_length(plen, w);
		
		uint8_t header[4096];
		uint8_t* p = header;
		*p++ = 0;
		*p++ = id_;
		*p++ = wdata_[0];
		write_length(p, w - plen);
		memmove(p, plen, w-plen);
		p+= w - plen;
	
		if(g_debug) {
			fprintf(stderr, "tpdu:");
			for(size_t i =0; i != (p - header); ++i)
				fprintf(stderr, " %02X", header[i]);
			fprintf(stderr, "\n");
		}

		if(write(g_fd, header, p - header) < 0) {
			perror("write");
			exit(1);
		}
		read();
	}

	uint8_t wdata_[4096];
	uint8_t rdata_[4096];
	
	uint8_t id_;
};

struct SessionManager;

struct Session {
	Session(SessionManager* m, uint16_t nb) : m_(m), nb_(nb) {}

	uint8_t* start_apdu();
	void end_apdu(uint8_t* end);

	virtual void on_apdu(const uint8_t* b, size_t n) = 0;
private:
	uint16_t nb_;
	SessionManager* m_;
};


struct ResourceManager : Session {
	ResourceManager(SessionManager* m, uint16_t nb) : Session(m,nb) {}

	void on_apdu(const uint8_t* b, size_t n) {
		uint32_t tag = 0;
		tag += *b++ << 16;
		tag += *b++ << 8;
		tag += *b++;

		uint8_t* a;
		switch(tag) {
		case 0x9f8011:
			a = start_apdu();
			*a++ = 0x9F;
			*a++ = 0x80;
			*a++ = 0x12;
			*a++ = 0x0;
			end_apdu(a);
			break;
		case 0x9f8010: // profile enq;
			a = start_apdu();
			*a++ = 0x9F;
			*a++ = 0x80;
			*a++ = 0x11;
			write_length(a, sizeof(uint32_t)*5);
			write_uint32(a, 0x010041); // resource manager
			write_uint32(a, 0x020041); // application info
			write_uint32(a, 0x030041); // ca
			write_uint32(a, 0x240041);
			write_uint32(a, 0x400041);
			end_apdu(a);
			break;
		}
	}	

	void profile_enq() {
		uint8_t* a = start_apdu();
		*a++ = 0x9F;
		*a++ = 0x80;
		*a++ = 0x10;
		*a++ = 0;
		end_apdu(a); 
	}
};

struct ApplicationManager : Session {
	ApplicationManager(SessionManager* m, uint16_t nb) : Session(m,nb) {}
	
	void on_apdu(const uint8_t* b, size_t n) {
	}
	
	void application_enq() {
		uint8_t* a = start_apdu();
		*a++ = 0x9F;
		*a++ = 0x80;
		*a++ = 0x20;
		*a++ = 0;
		end_apdu(a);
	}
};
struct CAManager;
CAManager* g_ca = 0;

struct CAManager : Session {
	CAManager(SessionManager* m, uint16_t nb) : Session(m, nb), program_number_(-1), version_(0) {}
	
	void on_apdu(const uint8_t* b, size_t n) {
  	g_ca = this;
	}

	void ca_info_enq() {
	 	uint8_t* a = start_apdu();
    *a++ = 0x9F;
    *a++ = 0x80;
    *a++ = 0x30;
    *a++ = 0;
    end_apdu(a);
	}

	void pmt(uint8_t const * pmt, size_t n) {
		if(n < 9) 
			throw std::runtime_error("invalid pmt");

		uint8_t const* epmt = pmt + n;
		uint8_t* a = start_apdu();
		*a++ = 0x9F;
		*a++ = 0x80;
		*a++ = 0x32;
		uint8_t* plen = a++;
		
		
		// program number	
		pmt += 3;
		
		uint16_t program_number = parse_uint16(pmt, epmt);
		uint8_t version = *pmt++;

		if(version == version_ && program_number_ == program_number)
			return;

		*a++ = program_number_ == program_number? 0x05 :0x3; // list management

		version_ = version;
		program_number_ = program_number;
	
		write_uint16(a, program_number);
		*a++ = version;

		pmt += 4;

		size_t pilen = parse_uint16(pmt, epmt) & 0xFFF;

		if(pilen) {
			size_t ca_pilen = 0;
			for(size_t i = 0; i != pilen;) {
				if(0x09 == pmt[i]) 
					ca_pilen += 2+pmt[i+1];
				i += pmt[i+1] + 2;
			}

			write_uint16(a, ca_pilen + (ca_pilen != 0));

			if(ca_pilen) {
				*a++ = 0x01; //ok_descrambling //query
				for(size_t i = 0; i != pilen;) {
        	if(0x09 == pmt[i]) {
						*a++ = 0x09;
						*a++ = pmt[i+1];
						for(size_t j = 0; j < pmt[i+1]; ++j)
							*a++ = pmt[i+2+j];
					}
					i += pmt[i+1] + 2;
      	}
			}
			pmt += pilen;
		}
		else
			write_uint16(a, 0);

		while(pmt != epmt - 4) {
			*a++ = *pmt++;
			uint16_t pid = parse_uint16(pmt, epmt);
			write_uint16(a, pid);
			size_t eslen = parse_uint16(pmt, epmt) & 0xFFF;
	
			size_t ca_eslen = 0;
			for(size_t i = 0; i < eslen && (i < eslen - 1);) {
        if(0x09 == pmt[i]) 
          ca_eslen += pmt[i+1] + 2;
        i += 1+pmt[i+1];
      }

			if(ca_eslen) {
				write_uint16(a, ca_eslen + 1);
        *a++ = 0x01;
				for(size_t i = 0; i < eslen-1;) {
					if(0x09 == pmt[i]) {
            *a++ = 0x09;
            *a++ = pmt[i+1];
            for(size_t j = 0; j < pmt[i+1]; ++j)
              *a++ = pmt[i+2+j];
          }
          i += pmt[i+1];
        }
      }
			else
				write_uint16(a, 0);

			pmt += eslen;
		}

		*plen = a - plen - 1;
	
		end_apdu(a);
	}

	uint16_t program_number_;
	uint8_t version_;
};

struct SessionManager : Connection {
	SessionManager() : Connection(1), next_session_(1) {}

	void create_session(Session* session);
	void delete_session(uint16_t);	
	
	uint8_t* start_apdu(uint16_t nb) {
		uint8_t* s = start_spdu();
		*s++ = 0x90;
		*s++ = 2;
		return write_uint16(s, nb);
	}

	void end_apdu(uint8_t* e) {
		end_spdu(e);
	}
private:
	uint16_t next_session_;
	std::map<uint16_t, Session*> sessions_;

	void on_spdu(const uint8_t* b, size_t n) {
		SPDU spdu = parse_spdu(b, b+n);
		
		uint8_t* s = 0;
		int sn = next_session_;
		switch(spdu.tag_) {
		case STAG_OPEN_SESSION_REQUEST:
			switch(spdu.open_session_request_.resource_) {	
			case 0x10041:
				s = start_spdu();
				open_session_response(s, 0, RSRCID_RESOURCE_MANAGER, next_session_);
				end_spdu(s);
				sn = next_session_++;
				((ResourceManager*)(sessions_[sn] = new ResourceManager(this, sn)))->profile_enq();
				break;
			case 0x20041:
				s = start_spdu();
        open_session_response(s, 0, 0x20041, next_session_);
        end_spdu(s);
				sn = next_session_++;
				((ApplicationManager*)(sessions_[sn] = new ApplicationManager(this, sn)))->application_enq();
				break;
			case 0x30041:
				s = start_spdu();
				open_session_response(s, 0, 0x30041, next_session_);
        end_spdu(s);
				sn = next_session_++;
				((CAManager*)(sessions_[sn] = new CAManager(this, sn)))->ca_info_enq();
				break;
			default:
				s = start_spdu();
				open_session_response(s, 0xF0, 0, next_session_);
				end_spdu(s);
				break;
			}
			break;
		case 0x90: {// session number
			std::map<uint16_t, Session*>::iterator i = sessions_.find(spdu.session_number_.nb_);
			if(i != sessions_.end())
				i->second->on_apdu(spdu.session_number_.apdu_, spdu.session_number_.len_);
			else {
				std::ostringstream err;
				err << "on_sdpu: nonexistent session number " << spdu.session_number_.nb_;
				throw std::runtime_error(err.str());
			}
			break;}
		default:
			break;
		}			
	}
};

uint8_t* Session::start_apdu() {
	return m_->start_apdu(nb_);
}

void Session::end_apdu(uint8_t* end) {
	m_->end_apdu(end);
}

int main(int argc, char* argv[]) {
	g_fd = open("/dev/dvb/adapter0/ca0", O_RDWR);
	
	if(-1 == g_fd) {
		perror("open");
		exit(1);
	}

	if(ioctl(g_fd, CA_RESET, 1)) {
		perror("reset");
		exit(1);
	}
	sleep(10);

	ca_slot_info_t info;
	info.num = 0;
	if(ioctl(g_fd, CA_GET_SLOT_INFO, &info) == -1) {
		perror("CA_GET_SLOT_INFO");
		exit(1);
	}

	fprintf(stderr, "type 0x%08X, flags 0x%08X\n", info.type, info.flags);

	SessionManager s;

	s.create_t_c();

	int demux = open(argv[1], O_RDWR);
	if(demux < 0) {
		perror("open demux");
		exit(1);
	}

	for(;;) {
		pollfd p[2] = {{STDIN_FILENO, POLLIN, 0}, {demux, POLLIN, 0}};
		
		int r = poll(p, 2, 300);
	
		if(r == -1) {
			perror("poll");
			exit(1);
		}
		else if(r == 0) {
			s.end_spdu(s.start_spdu());
		}
		else {
			if(p[0].revents & POLLIN) {
				char pid[50] = {0};
				int r = read(STDIN_FILENO, pid, 50);
				if(r < 0) {
					perror("read");
					exit(1);
				}

				int pmt;
				if(1 != sscanf(pid, "%d\n", &pmt)) {
					perror("syntax error");
					exit(1);
				}

				dmx_sct_filter_params params;
  			memset(&params, 0, sizeof(params));

				params.pid = pmt;
				params.flags = DMX_IMMEDIATE_START | DMX_CHECK_CRC;
				params.filter.filter[0] = 2;
				params.filter.mask[0] = 0xFF;

  			if(ioctl(demux, DMX_SET_FILTER, &params) != 0) {
    			perror("failed to set filter\n");
    			return 1;
  			}
			}

			if(p[1].revents & POLLIN) {
				uint8_t pmt[4096];
				int r = read(demux, pmt, sizeof(pmt));
				if(r < 0) {
					perror("demux read");
					exit(1);
				}

				if(g_ca) g_ca->pmt(pmt, r);
			}
		}
	}
}


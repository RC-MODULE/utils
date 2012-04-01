#include <algorithm>

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

#include <libucsi/section.h>
#include <libucsi/mpeg/section.h>
#include <libdvben50221/en50221_stdcam.h>

struct CAM {
	CAM() : tl_(0), sl_(0), stdcam_(0), connected_(false), seen_(false) {}

	int open() {
	 	tl_ = en50221_tl_create(1, 16);
  	if(!tl_) {
    	fprintf(stderr, "Failed to create transport layer\n");
    	return -1;
  	}
		sl_ = en50221_sl_create(tl_, 16);
  	if(!sl_) {
    	fprintf(stderr, "Failed to create session layer\n");
    	return -1; 
  	}

		stdcam_= en50221_stdcam_create(0, 0, tl_, sl_);
		if(!stdcam_) {
			fprintf(stderr, "Failed to create stdcam\n");
			return -1;
		}

		en50221_app_ca_register_info_callback(stdcam_->ca_resource, &info_callback, this);
		en50221_app_ca_register_pmt_reply_callback(stdcam_->ca_resource, pmt_callback, this);

		pthread_create(&thread_, NULL, thread_func, this);		

		struct sigaction action;
		memset(&action, 0, sizeof(action));
		action.sa_handler = on_alarm;
		
		sigaction(SIGALRM, &action, 0);
		alarm(10);
	
		return 0;
	}

	~CAM() {
		if(stdcam_) stdcam_->destroy(stdcam_, 1);
		if(sl_) en50221_sl_destroy(sl_); 
		if(tl_) en50221_tl_destroy(tl_);
	
		pthread_join(thread_, NULL);
	}

	static int info_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint32_t ca_id_count, uint16_t *ca_ids) {
		alarm(0);
		reinterpret_cast<CAM*>(arg)->connected_ = true;
		return 0;
	}

	static int pmt_callback(void *arg, uint8_t slot_id, uint16_t session_number, struct en50221_app_pmt_reply *reply, uint32_t reply_size) {
		if(!reply->CA_enable_flag || reply->CA_enable != 1) {
			fprintf(stderr, "Unable to descramble. ca_enable_flag: %d, ca_enable: 0x%02x\n", reply->CA_enable_flag, reply->CA_enable);
			exit(1); 
		}

		fprintf(stderr, "OK descrambling\n");

		return 0;
	}
	
	static void on_alarm(int) {
		fprintf(stderr, "CAM initialization timeout\n");
		exit(1);
	}

	static void *thread_func(void* arg) {
 		CAM* this_ = reinterpret_cast<CAM*>(arg);
		for(;;) {
    	this_->stdcam_->poll(this_->stdcam_);
		}

		return 0;
  }   

	int on_new_pmt(mpeg_pmt_section *pmt) {
		fprintf(stderr, "on_new_pmt\n");
		if(connected_) {
			uint8_t buf[1024];
			
			int size = en50221_ca_format_pmt(pmt, buf, sizeof(buf), 0, seen_ ? CA_LIST_MANAGEMENT_UPDATE : CA_LIST_MANAGEMENT_ONLY, CA_PMT_CMD_ID_QUERY);
			if(size < 0) { 
      	fprintf(stderr, "Failed to format PMT\n");
      	return -1;
    	}
    	
			if(en50221_app_ca_pmt(stdcam_->ca_resource, stdcam_->ca_session_number, buf, size)) {
      	fprintf(stderr, "Failed to send PMT\n");
      	return -1;
    	}

			uint8_t buf2[1024];
			size = en50221_ca_format_pmt(pmt, buf2, sizeof(buf2), 0, seen_ ? CA_LIST_MANAGEMENT_UPDATE : CA_LIST_MANAGEMENT_ONLY, CA_PMT_CMD_ID_OK_DESCRAMBLING);
			
			if(en50221_app_ca_pmt(stdcam_->ca_resource, stdcam_->ca_session_number, buf2, size)) {
        fprintf(stderr, "Failed to send PMT\n");
        return -1;
      }
			fprintf(stderr, "PMT sent to CAM\n");

    	seen_ = true;

    	return 1;
		}
		return 0;
	}

	en50221_transport_layer *tl_;
	en50221_session_layer *sl_;
	en50221_stdcam *stdcam_;
	bool connected_;
	bool seen_;

	pthread_t thread_;

private:
	CAM(CAM const&);
	CAM& operator = (CAM const&);
};

int main(int argc, char* argv[]) {
	CAM cam;

	if(cam.open() < 0) {
		fprintf(stderr, "Failed to open CAM\n");
		return -1;
	}	

	uint8_t buf[4096];
	int version = -1;
	
	size_t o =0;
	for(uint8_t* p = buf;;) {
		p = buf;
		ssize_t size = read(STDIN_FILENO, p+o, sizeof(buf) - (o));

		if(size == 0)
			break;

		if((p-buf) + size < 3)
			break;

		size += o;

		for(;size > 3;) {
			section* sec = reinterpret_cast<section*>(p);

			bswap16(p+1);
			
			if(sec->length < 3) {
				fprintf(stderr, "Failed to parse section header\n");
				exit(-1);
			}

			section_ext* ext = section_ext_decode(sec, 0);

			if(!ext) {
				fprintf(stderr, "Failed to decode section extension\n");
				pause();
				return -1;
			}

			if(size >= (ssize_t)section_length(sec)) {
				fprintf(stderr, "%d\n", ext->version_number);
				if(ext->version_number != version) {
					mpeg_pmt_section *pmt = mpeg_pmt_section_codec(ext);
					if(pmt) {
						if(cam.on_new_pmt(pmt) == 1)
							version = pmt->head.version_number;
					}
				}
			
				p += section_length(sec);
				size -= section_length(sec);
			} else {
				break;
			}
		}

		if(size > 3) {
			std::swap(*(p+1), *(p+2));
		}
			
		memmove(buf, p, size);
		p = buf;
		o = size;
	}	
}


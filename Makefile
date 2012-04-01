CC=$(GNU_TARGET_NAME)-gcc
CXX=$(GNU_TARGET_NAME)-g++

all: tuneqpsk ts-save-1 sec-filter parse-pmt parse-pat parse-nit parse-sdt parse-eit dvbca

install: tuneqpsk
	cp tuneqpsk $(DESTDIR)/usr/bin

tuneqpsk: tuneqpsk.c
	$(CC) $^ -o $@

ts-save-1: ts-save-1.c mdemux.c common.c
	$(CC) $^ -o $@

%: %.cpp
	$(CXX) $^ -o $@

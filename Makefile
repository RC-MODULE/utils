CC=$(GNU_TARGET_NAME)-gcc
CXX=$(GNU_TARGET_NAME)-g++
PKG_CONFIG=$(GNU_TARGET_NAME)-pkg-config

targets = tuneqpsk ts-save-1 sec-filter parse-pmt parse-pat parse-nit parse-sdt parse-eit dvbca mwatch 5909 osd

all: $(targets)

install: $(targets)
	cp $^ $(DESTDIR)/usr/bin

tuneqpsk: tuneqpsk.c
	$(CC) $^ -o $@

ts-save-1: ts-save-1.c mdemux.c common.c
	$(CC) $^ -o $@

mwatch: mwatch.c
	$(CC) $^ $(shell $(PKG_CONFIG) gstreamer-0.10 --cflags --libs) -o $@

clean:
	rm -f $(targets)
	rm -f *.o

%: %.cpp
	$(CXX) --std=c++0x $^ -o $@

.PHONY: all clean

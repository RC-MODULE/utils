CC=$(GNU_TARGET_NAME)-gcc

all: tuneqpsk

install: tuneqpsk
	cp tuneqpsk $(DESTDIR)/usr/bin

tuneqpsk: tuneqpsk.c
	$(CC) $^ -o $@


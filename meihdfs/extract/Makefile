# arch-tag: Makefile - basic CFLAGS - fileutils/DVDrecorder directory

CFLAGS += -mtune=native -pipe

all: extract

%.S: %.c
	$(CC) $(CFLAGS) $^ -o $@

extract:: extract_meihdfs

# arch-tag: Makefile - basic CFLAGS - fileutils/DVDrecorder directory

CFLAGS += -mtune=native -march=i486 -m32 -pipe

all: extract

%.S: %.c
	$(CC) $(CFLAGS) $^ -o $@

extract:: extract_meihdfs

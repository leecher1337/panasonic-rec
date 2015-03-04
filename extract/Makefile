# arch-tag: Makefile - basic CFLAGS - fileutils/DVDrecorder directory

CFLAGS=-O3 -mtune=i686 -march=i486 -pipe

all: extract

%.S: %.c
	$(CC) $(CFLAGS) $^ -o $@

extract:: extract_meihdfs

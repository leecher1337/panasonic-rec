# arch-tag: Makefile - basic CFLAGS - fileutils/DVDrecorder directory

CFLAGS=-O3 -mtune=i686 -march=i486 -pipe -g

all: extract

%.S: %.c
	$(CC) $(CFLAGS) -S $^ -o $@

extract:: extract_meihdfs.o

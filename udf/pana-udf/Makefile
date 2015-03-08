CFLAGS=-O3 -mtune=i686 -march=i486 -pipe

all: udf_dump

udf_dump: udf_file.c udf_fs.c udf_time.c udf_dump.c
	$(CC) $(CFLAGS) udf_file.c udf_fs.c udf_time.c udf_dump.c -o udf_dump

debug: udf_file.c udf_fs.c udf_time.c udf_dump.c
	$(CC) udf_file.c udf_fs.c udf_time.c udf_dump.c -g -O0 -o udf_dump

clean:
	rm -f udf_dump

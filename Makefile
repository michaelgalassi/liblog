# build liblog library

CFLAGS=-g -O -Wall -Wextra -Werror

all: liblog.a llcat

liblog.a: liblog.c liblog.h
	$(CC) $(CFLAGS) -c liblog.c
	$(AR) cr liblog.a liblog.o
	ranlib liblog.a

llcat: llcat.c liblog.h
	$(CC) $(CFLAGS) -o llcat llcat.c

clean:
	rm -f liblog.o liblog.a llcat

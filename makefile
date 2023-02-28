CC = gcc
CFLAGS = -g -Wall

all: test

OBJS = test.o memlib.o

test: $(OBJS)
	$(CC) $(CFLAGS) -o test $(OBJS)

memlib.o: memlib.c memlib.h
test.o: test.c
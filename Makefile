CC = gcc
CFLAGS = -O3 -pthread -Iinclude

SRC = src/tml.c
OBJ = $(SRC:.c=.o)

all: libtml.a example

libtml.a: $(OBJ)
	ar rcs libtml.a $(OBJ)

example: libtml.a
	$(CC) examples/demo.c -L. -ltml -o demo $(CFLAGS)

clean:
	rm -f $(OBJ) libtml.a demo

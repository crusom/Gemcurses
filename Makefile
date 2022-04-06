CC=gcc
CFLAGS=-I . -Wall -ggdb3

DEPS = tofu.h tls.h
OBJ = tofu.o tls.o tui.c

LIBS = -lssl -lcrypto -lncurses -lform
#openssl <3.0 default location
LDFLAGS = -L/usr/local/ssl/lib

#openssl >=3.0 default location
#LDFLAGS = -L/usr/local/lib64/

%.o: %.c $(DEPS)
		$(CC) -c -o $@ $< $(CFLAGS)

gemini: $(OBJ)
		$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS) $(LIBS)

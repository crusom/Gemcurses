CC=gcc
CFLAGS=-I . -Wall
#debugging flag
CFLAGS += -ggdb3

OBJ = tofu.o tls.o util.o tui.o

LIBS = -lssl -lcrypto -lncurses -lform -lpanel

#openssl <3.0 default location
LDFLAGS = -L/usr/local/ssl/lib

#openssl >=3.0 default location
#LDFLAGS = -L/usr/local/lib64/

gemcurses: $(OBJ)
		$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS) $(LIBS)

clean:
	rm -rf netsim $(OBJ)

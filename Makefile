CC=gcc
CFLAGS=-I . -Wextra -Wall -Wpedantic -rdynamic
#CFLAGS += -Wconversion
#optional debugging flag
#CFLAGS += -ggdb3

OBJ = tofu.o tls.o bookmarks.o util.o tui.o

LIBS = -lssl -lcrypto -lncursesw -lformw -lpanelw

#openssl <3.0 default location
LDFLAGS = -L/usr/local/ssl/lib

#openssl >=3.0 default location
#LDFLAGS = -L/usr/local/lib64/

gemcurses: $(OBJ)
		$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS) $(LIBS)

clean:
	rm -rf netsim $(OBJ)

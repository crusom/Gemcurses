CC=gcc
#CC=afl-gcc
CFLAGS=-I . -Wextra -Wall -Wpedantic -rdynamic
#CFLAGS += -Wconversion
#optional debugging flag
CFLAGS += -ggdb3

SRC_DIR = src
_OBJ = tofu.o tls.o bookmarks.o util.o tui.o wcwidth.o utf8.o
OBJ = $(patsubst %,$(SRC_DIR)/%,$(_OBJ))

LIBS = -lssl -lcrypto -lncursesw -lformw -lpanelw

#openssl <3.0 default location
LDFLAGS = -L/usr/local/ssl/lib

#openssl >=3.0 default location
#LDFLAGS = -L/usr/local/lib64/

gemcurses: $(OBJ)
		$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS) $(LIBS)

install:
	cp gemcurses /usr/local/bin/

clean:
	rm -rf netsim $(SRC_DIR)/$(OBJ)

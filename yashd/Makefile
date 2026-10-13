CC     = gcc
CFLAGS = -Wall -Wextra -g -std=gnu99 -pthread

# Shell side: swap session_stub.o for the prj1 shell's objects once they
# implement session.h (e.g. parse.o redirect.o pipe.o job.o session.o)
SHELL_OBJS = session_stub.o
SERVER_OBJS = yashd.o log.o $(SHELL_OBJS)

all: yashd yash

yashd: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o yashd $(SERVER_OBJS)

yash: client.o
	$(CC) $(CFLAGS) -o yash client.o

$(SERVER_OBJS): session.h

clean:
	rm -f yashd yash *.o

.PHONY: all clean

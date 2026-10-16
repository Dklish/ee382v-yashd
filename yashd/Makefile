CC     = gcc
CFLAGS = -Wall -Wextra -g -std=gnu99 -pthread

SHELL_OBJS = session.o parse.o redirect.o exec.o job.o
SERVER_OBJS = yashd.o log.o $(SHELL_OBJS)

all: yashd yash

yashd: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o yashd $(SERVER_OBJS)

yash: client.o
	$(CC) $(CFLAGS) -o yash client.o

$(SERVER_OBJS): session.h yash.h shell.h

clean:
	rm -f yashd yash *.o

.PHONY: all clean

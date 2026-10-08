CC     = gcc
CFLAGS = -Wall -Wextra -g -std=gnu99
OBJS   = yash.o parse.o redirect.o pipe.o signals.o job.o

yash: $(OBJS)
	$(CC) $(CFLAGS) -o yash $(OBJS)

$(OBJS): yash.h

clean:
	rm -f yash $(OBJS)

.PHONY: clean

PORT=50426
CFLAGS = -DPORT=$(PORT) -g -Wall -std=gnu99

all: friend_server friendme

friend_server: friend_server.o friends.o server.o
	gcc ${CFLAGS} -o $@ $^

friendme: friendme.o friends.o
	gcc ${CFLAGS} -o $@ $^

%.o: %.c friends.h server.h
	gcc ${CFLAGS} -c $<

clean:
	rm *.o friend_server friendme

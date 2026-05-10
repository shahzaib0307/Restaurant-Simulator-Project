CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g -pthread
LDFLAGS = -lncurses -lpthread
TARGET  = restaurant_sim
SRCS    = main.c queue.c waiter.c chef.c dashboard.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

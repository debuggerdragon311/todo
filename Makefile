CC     ?= cc
TARGET := todo
SRCS   := todo-tracker.c
CFLAGS := -Wall -Wextra -I.

.PHONY: all debug release clean

all: debug

debug:
	$(CC) $(CFLAGS) -O0 -g $(SRCS) -o $(TARGET)

release:
	$(CC) $(CFLAGS) -O3 -DNDEBUG -s $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

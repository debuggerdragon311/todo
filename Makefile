CC      ?= cc
TARGET  := todo
SRCS    := todo-tracker.c
HEADERS := log.h
CFLAGS  := -Wall -Wextra -I.

# Colors
CYAN    := \033[1;36m
GREEN   := \033[1;32m
YELLOW  := \033[1;33m
RED     := \033[1;31m
RESET   := \033[0m

.PHONY: all debug release clean run

# compile in debug mode, then run immediately
all: debug run

debug:
	@printf "$(CYAN)[BUILD]$(RESET) Compiling %s (debug mode)...\n" "$(TARGET)"
	@$(CC) $(CFLAGS) -O0 -g $(SRCS) -o $(TARGET)
	@printf "$(GREEN)[INFO]$(RESET) Target ./%s successfully built\n" "$(TARGET)"

release:
	@printf "$(CYAN)[BUILD]$(RESET) Compiling %s (optimized release)...\n" "$(TARGET)"
	@$(CC) $(CFLAGS) -O3 -DNDEBUG -s $(SRCS) -o $(TARGET)
	@printf "$(GREEN)[INFO]$(RESET) Created production binary: ./%s\n" "$(TARGET)"

run:
	@printf "$(YELLOW)[RUN]$(RESET) Executing ./%s...\n\n" "$(TARGET)"
	@./$(TARGET) $(ARGS)

clean:
	@printf "$(RED)[CLEAN]$(RESET) Purging artifacts...\n"
	@rm -f $(TARGET)
	@printf "$(GREEN)[INFO]$(RESET) Cleaned build artifacts.\n"

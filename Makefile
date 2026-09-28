CC      ?= clang
TARGET  := todo
SRCS    := $(wildcard src/*.c)
CFLAGS  := -std=gnu23 -Wall -Wextra -Iinclude -I.

RELEASE_FLAGS := -O3 -DNDEBUG -flto -static -s

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
	@printf "$(CYAN)[BUILD]$(RESET) Compiling %s (optimized static release)...\n" "$(TARGET)"
	@$(CC) $(CFLAGS) $(RELEASE_FLAGS) $(SRCS) -o $(TARGET)
	@strip --strip-all $(TARGET) 2>/dev/null || true
	@printf "$(GREEN)[INFO]$(RESET) Created standalone static binary: ./%s\n" "$(TARGET)"

run:
	@printf "$(YELLOW)[RUN]$(RESET) Executing ./%s...\n\n" "$(TARGET)"
	@./$(TARGET) $(ARGS)

clean:
	@printf "$(RED)[CLEAN]$(RESET) Purging artifacts...\n"
	@rm -f $(TARGET)
	@printf "$(GREEN)[INFO]$(RESET) Cleaned build artifacts.\n"

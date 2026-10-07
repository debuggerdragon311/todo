# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

CC      ?= clang
TARGET  := todo
SRCS    := $(wildcard src/*.c)
ASSET_C := assets.c
ASSET_O := assets.o

BASE_CFLAGS := -std=gnu23 -Wall -Wextra -Iinclude -I.

DEBUG_CFLAGS := $(BASE_CFLAGS) -O0 -g3 -fno-omit-frame-pointer \
                -fsanitize=address,undefined

RELEASE_CFLAGS := $(BASE_CFLAGS) -O3 -DNDEBUG -flto -static -s

CYAN    := \033[1;36m
GREEN   := \033[1;32m
YELLOW  := \033[1;33m
RED     := \033[1;31m
RESET   := \033[0m

.PHONY: all debug release clean run

all: debug run

# Compile the tiny C embedder tool
tools/embed: tools/embed.c
	@mkdir -p tools
	@$(CC) -O2 tools/embed.c -o tools/embed

# Generate assets.c from the 5 root files
$(ASSET_C): tools/embed index.html style.css script.js favicon.ico icon.png
	@./tools/embed $(ASSET_C) \
		index.html asset_index_html \
		style.css asset_style_css \
		script.js asset_script_js \
		favicon.ico asset_favicon \
		icon.png asset_icon

# Compile assets.c into an object file
$(ASSET_O): $(ASSET_C)
	@$(CC) -c $(ASSET_C) -o $(ASSET_O)

debug: $(ASSET_O)
	@printf "$(CYAN)[BUILD]$(RESET) Compiling %s (pure C debug + ASan)...\n" "$(TARGET)"
	@$(CC) $(DEBUG_CFLAGS) $(SRCS) $(ASSET_O) -o $(TARGET)
	@printf "$(GREEN)[INFO]$(RESET) Target ./%s successfully built\n" "$(TARGET)"

release: $(ASSET_O)
	@printf "$(CYAN)[BUILD]$(RESET) Compiling %s (pure C static release)...\n" "$(TARGET)"
	@$(CC) $(RELEASE_CFLAGS) $(SRCS) $(ASSET_O) -o $(TARGET)
	@strip --strip-all $(TARGET) 2>/dev/null || true
	@printf "$(GREEN)[INFO]$(RESET) Created standalone static binary: ./%s\n" "$(TARGET)"

profile: $(ASSET_O)
	@printf "$(CYAN)[BUILD]$(RESET) Compiling %s (profile: -O2 + symbols)...\n" "$(TARGET)"
	@$(CC) $(BASE_CFLAGS) -O2 -g3 -fno-omit-frame-pointer $(SRCS) $(ASSET_O) -o $(TARGET)
	@printf "$(GREEN)[INFO]$(RESET) Profile build created: ./%s\n" "$(TARGET)"

run: $(TARGET)
	@printf "$(YELLOW)[RUN]$(RESET) Executing ./%s...\n\n" "$(TARGET)"
	@./$(TARGET) $(ARGS)

clean:
	@printf "$(RED)[CLEAN]$(RESET) Purging artifacts...\n"
	@rm -f $(TARGET) $(ASSET_C) $(ASSET_O) tools/embed
	@printf "$(GREEN)[INFO]$(RESET) Cleaned build artifacts.\n"

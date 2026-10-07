// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (c) 2026 Soumayjit Bala <ayushkantibala2020@gmail.com>

#ifndef SERVER_H
#define SERVER_H

#include "todo.h"

// Starts the HTTP daemon on the specified port
void server_start(uint16_t port, const todo_db *todos);

#endif // SERVER_H

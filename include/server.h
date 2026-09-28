#ifndef SERVER_H
#define SERVER_H

#include "Todo.h"

// Starts the HTTP daemon on the specified port
void server_start(uint16_t port, const todo_db *todos);

#endif // SERVER_H

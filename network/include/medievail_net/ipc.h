// Abstractions IPC partagées entre le daemon et ses backends.
// Fournit les types et signatures utilisés par unix_socket.c et futurs backends.

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum IPCMessageType {
    IPC_MESSAGE_NONE = 0,
    IPC_MESSAGE_CONTROL = 1,
    IPC_MESSAGE_EVENT = 2,
    IPC_MESSAGE_SHUTDOWN = 3
} IPCMessageType;

typedef struct IPCMessage {
    IPCMessageType type;
    uint32_t payload_size;
    unsigned char *payload;
} IPCMessage;

typedef struct IPCConnection IPCConnection;

IPCConnection *ipc_connection_create(const char *endpoint);
void ipc_connection_destroy(IPCConnection *connection);
int ipc_connection_poll(IPCConnection *connection, IPCMessage *out_message, unsigned int timeout_ms);
int ipc_connection_send(IPCConnection *connection, const IPCMessage *message);
void ipc_message_free(IPCMessage *message);
void ipc_sleep(unsigned int milliseconds);

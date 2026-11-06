#include "loopback.h"
#include "kernel.h"
#include "kernel_log.h"

#define SOCKET_LIMIT 32
#define SOCKET_BUFFER_SIZE 1024
enum SocketState { SOCKET_FREE, SOCKET_NEW, SOCKET_LISTEN, SOCKET_PENDING, SOCKET_CONNECTED };
typedef struct
{
    enum SocketState state;
    unsigned int owner, port, peer, listener;
    unsigned int read_pos, count;
    unsigned char *data;
} Socket;

static Socket sockets[SOCKET_LIMIT];

static Socket *owned(unsigned int owner, unsigned int handle)
{
    Socket *socket;
    if (!handle || handle > SOCKET_LIMIT) return 0;
    socket = &sockets[handle - 1];
    return socket->state != SOCKET_FREE && socket->owner == owner ? socket : 0;
}

int loopback_socket(unsigned int owner)
{
    unsigned int i;
    if (!owner) return -1;
    for (i = 0; i < SOCKET_LIMIT; ++i)
        if (sockets[i].state == SOCKET_FREE)
        {
            unsigned int page = memory_allocate_page();
            if (!page) return -1;
            sockets[i].data = (unsigned char *)page;
            sockets[i].state = SOCKET_NEW;
            sockets[i].owner = owner;
            sockets[i].port = sockets[i].peer = sockets[i].listener = 0;
            sockets[i].read_pos = sockets[i].count = 0;
            return (int)i + 1;
        }
    return -1;
}

int loopback_listen(unsigned int owner, unsigned int handle, unsigned int port)
{
    unsigned int i;
    Socket *socket = owned(owner, handle);
    if (!socket || socket->state != SOCKET_NEW || port == 0 || port > 65535) return -1;
    for (i = 0; i < SOCKET_LIMIT; ++i)
        if (sockets[i].state == SOCKET_LISTEN && sockets[i].port == port) return -1;
    socket->port = port;
    socket->state = SOCKET_LISTEN;
    return 0;
}

int loopback_connect(unsigned int owner, unsigned int handle, unsigned int port)
{
    unsigned int i;
    int server_handle;
    Socket *client = owned(owner, handle);
    Socket *listener = 0, *server;
    if (!client || client->state != SOCKET_NEW || !port || port > 65535) return -1;
    for (i = 0; i < SOCKET_LIMIT; ++i)
        if (sockets[i].state == SOCKET_LISTEN && sockets[i].port == port)
        { listener = &sockets[i]; break; }
    if (!listener) return -1;
    server_handle = loopback_socket(listener->owner);
    if (server_handle < 0) return LOOPBACK_AGAIN;
    server = &sockets[server_handle - 1];
    server->state = SOCKET_PENDING;
    server->peer = handle;
    server->listener = i + 1;
    client->state = SOCKET_CONNECTED;
    client->peer = (unsigned int)server_handle;
    return 0;
}

int loopback_accept(unsigned int owner, unsigned int handle)
{
    unsigned int i;
    Socket *listener = owned(owner, handle);
    if (!listener || listener->state != SOCKET_LISTEN) return -1;
    for (i = 0; i < SOCKET_LIMIT; ++i)
        if (sockets[i].state == SOCKET_PENDING && sockets[i].listener == handle)
        {
            sockets[i].state = SOCKET_CONNECTED;
            sockets[i].listener = 0;
            return (int)i + 1;
        }
    return LOOPBACK_AGAIN;
}

int loopback_send(unsigned int owner, unsigned int handle, const unsigned int *data, unsigned int length)
{
    Socket *socket = owned(owner, handle), *peer;
    unsigned int count, i;
    if (!socket || socket->state != SOCKET_CONNECTED || (!data && length)) return -1;
    if (!length) return 0;
    if (!socket->peer) return -1;
    peer = &sockets[socket->peer - 1];
    count = SOCKET_BUFFER_SIZE - peer->count;
    if (!count) return LOOPBACK_AGAIN;
    if (count > length) count = length;
    for (i = 0; i < count; ++i)
        peer->data[(peer->read_pos + peer->count + i) % SOCKET_BUFFER_SIZE] = (unsigned char)data[i];
    peer->count += count;
    return (int)count;
}

int loopback_recv(unsigned int owner, unsigned int handle, unsigned int *data, unsigned int length)
{
    Socket *socket = owned(owner, handle);
    unsigned int count, i;
    if (!socket || socket->state != SOCKET_CONNECTED || (!data && length)) return -1;
    if (!length) return 0;
    if (!socket->count) return socket->peer ? LOOPBACK_AGAIN : 0;
    count = socket->count < length ? socket->count : length;
    for (i = 0; i < count; ++i)
        data[i] = socket->data[(socket->read_pos + i) % SOCKET_BUFFER_SIZE];
    socket->read_pos = (socket->read_pos + count) % SOCKET_BUFFER_SIZE;
    socket->count -= count;
    return (int)count;
}

int loopback_close(unsigned int owner, unsigned int handle)
{
    unsigned int i;
    Socket *socket = owned(owner, handle);
    if (!socket) return -1;
    if (socket->state == SOCKET_LISTEN)
        for (i = 0; i < SOCKET_LIMIT; ++i)
            if (sockets[i].state == SOCKET_PENDING && sockets[i].listener == handle)
                loopback_close(owner, i + 1);
    if (socket->peer) sockets[socket->peer - 1].peer = 0;
    if (!memory_free_page((unsigned int)socket->data))
        kernel_log(KERNEL_LOG_ERROR, "socket", "could not free socket buffer");
    socket->state = SOCKET_FREE;
    return 0;
}

void loopback_close_owner(unsigned int owner)
{
    unsigned int i;
    for (i = 0; i < SOCKET_LIMIT; ++i)
        if (sockets[i].state != SOCKET_FREE && sockets[i].owner == owner)
            loopback_close(owner, i + 1);
}

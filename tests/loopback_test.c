#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../src/kernel_log.h"
#include "../src/loopback.h"

static int allocated;
unsigned int memory_allocate_page(void)
{
    void *p = malloc(4096);
    if (p) ++allocated;
    return (unsigned int)p;
}
int memory_free_page(unsigned int p)
{
    free((void *)p);
    --allocated;
    return 1;
}
void kernel_log(enum KernelLogLevel level, const char *subsystem, const char *message)
{
    (void)level; (void)subsystem; (void)message;
    assert(0);
}

int main(void)
{
    unsigned int data[1024], output[1024];
    int listener, client, server, other, i, handles[32];
    client = loopback_socket(2);
    assert(loopback_connect(2, client, 8000) == -1);
    listener = loopback_socket(1);
    assert(loopback_listen(1, listener, 8000) == 0);
    other = loopback_socket(3);
    assert(loopback_listen(3, other, 8000) == -1);
    assert(loopback_close(3, other) == 0);
    assert(loopback_accept(1, listener) == LOOPBACK_AGAIN);
    assert(loopback_connect(2, client, 8000) == 0);
    assert(loopback_accept(2, listener) == -1);
    server = loopback_accept(1, listener);
    assert(server > 0);
    assert(loopback_recv(2, server, output, 1) == -1);
    assert(loopback_recv(1, server, output, 1) == LOOPBACK_AGAIN);
    for (i = 0; i < 1024; ++i) data[i] = i & 255;
    assert(loopback_send(2, client, data, 1024) == 1024);
    assert(loopback_send(2, client, data, 1) == LOOPBACK_AGAIN);
    assert(loopback_recv(1, server, output, 500) == 500);
    for (i = 0; i < 500; ++i) assert(output[i] == data[i]);
    assert(loopback_send(2, client, data, 600) == 500);
    assert(loopback_close(2, client) == 0);
    assert(loopback_recv(1, server, output, 1024) == 1024);
    for (i = 0; i < 524; ++i) assert(output[i] == data[i + 500]);
    for (i = 0; i < 500; ++i) assert(output[i + 524] == data[i]);
    assert(loopback_recv(1, server, output, 1) == 0);
    assert(loopback_send(1, server, data, 1) == -1);
    assert(loopback_send(1, server, 0, 0) == 0);
    assert(loopback_recv(1, server, 0, 0) == 0);
    assert(loopback_close(1, server) == 0);
    client = loopback_socket(2);
    assert(loopback_connect(2, client, 8000) == 0);
    assert(loopback_send(2, client, data, 3) == 3);
    assert(loopback_close(2, client) == 0);
    server = loopback_accept(1, listener);
    assert(server > 0);
    assert(loopback_recv(1, server, output, 3) == 3);
    assert(loopback_recv(1, server, output, 1) == 0);
    assert(loopback_close(1, server) == 0);
    client = loopback_socket(2);
    assert(loopback_connect(2, client, 8000) == 0);
    assert(loopback_close(1, listener) == 0);
    assert(loopback_recv(2, client, output, 1) == 0);
    assert(loopback_close(2, client) == 0);
    listener = loopback_socket(1);
    assert(loopback_listen(1, listener, 8000) == 0);
    client = loopback_socket(2);
    assert(loopback_connect(2, client, 8000) == 0);
    loopback_close_owner(1);
    assert(loopback_recv(2, client, output, 1) == 0);
    loopback_close_owner(2);
    for (i = 0; i < 32; ++i) {
        handles[i] = loopback_socket(3);
        assert(handles[i] > 0);
    }
    assert(loopback_socket(3) == -1);
    loopback_close_owner(3);
    assert(allocated == 0);
    puts("loopback tests passed");
    return 0;
}

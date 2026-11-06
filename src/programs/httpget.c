#include <stdio.h>
#include <minos.h>

int main() {
    int buffer[128];
    char *request = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    int length = 0;
    int offset = 0;
    int elapsed = 0;
    int client = socket();
    if (client < 0) {
        printf("httpget: socket allocation failed.\n");
        return 1;
    }
    if (socket_connect(client, 8000) != 0) {
        printf("httpget: cannot connect to localhost:8000.\n");
        socket_close(client);
        return 1;
    }
    while (request[length] != 0) {
        length++;
    }
    while (offset < length && elapsed < 10000) {
        int count = 0;
        while (offset + count < length && count < 128) {
            buffer[count] = request[offset + count];
            count++;
        }
        int sent = socket_send(client, buffer, count);
        if (sent == -1) {
            printf("httpget: send failed.\n");
            socket_close(client);
            return 1;
        }
        if (sent == -2) {
            sleep(10);
            elapsed += 10;
        } else {
            offset += sent;
        }
    }
    if (offset != length) {
        printf("httpget: send timed out.\n");
        socket_close(client);
        return 1;
    }
    elapsed = 0;
    while (elapsed < 10000) {
        int count = socket_recv(client, buffer, 128);
        if (count == 0) {
            socket_close(client);
            printf("\n");
            return 0;
        }
        if (count == -1) {
            printf("httpget: receive failed.\n");
            socket_close(client);
            return 1;
        }
        if (count == -2) {
            sleep(10);
            elapsed += 10;
        } else {
            for (int i = 0; i < count; i++) {
                putchar(buffer[i]);
            }
        }
    }
    printf("httpget: receive timed out.\n");
    socket_close(client);
    return 1;
}

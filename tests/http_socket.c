#include <stdio.h>
#include <minos.h>

int exchange(char *request, char *expected, int idle) {
    int buffer[128];
    int client = socket();
    int offset = 0;
    int elapsed = 0;
    if (client < 0 || socket_connect(client, 8000) != 0) {
        printf("http test: connect failed.\n");
        return 0;
    }
    while (request[offset] != 0) {
        buffer[0] = request[offset];
        if (socket_send(client, buffer, 1) != 1) {
            printf("http test: fragmented send failed.\n");
            socket_close(client);
            return 0;
        }
        offset++;
        sleep(1);
    }
    if (idle) {
        sleep(6000);
    }
    offset = 0;
    while (elapsed < 10000) {
        int count = socket_recv(client, buffer, 128);
        if (count == 0) {
            socket_close(client);
            if (expected[offset] != 0) {
                printf("http test: truncated response.\n");
                return 0;
            }
            return 1;
        }
        if (count == -1) {
            printf("http test: receive failed.\n");
            socket_close(client);
            return 0;
        }
        if (count == -2) {
            sleep(10);
            elapsed += 10;
        } else {
            for (int i = 0; i < count; i++) {
                if (expected[offset] == 0 || expected[offset] != buffer[i]) {
                    printf("http test: wrong response.\n");
                    socket_close(client);
                    return 0;
                }
                offset++;
            }
        }
    }
    printf("http test: timed out.\n");
    socket_close(client);
    return 0;
}

int main() {
    char *ok = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\nHello World";
    char *bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    if (!exchange("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", ok, 0) ||
        !exchange("GET /other HTTP/1.1\r\n\r\n", bad, 0) ||
        !exchange("GET /", bad, 1) ||
        !exchange("GET / HTTP/1.0\r\n\r\n", ok, 0)) {
        return 1;
    }
    printf("http socket tests passed.\n");
    return 0;
}

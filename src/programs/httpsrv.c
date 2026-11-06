#include <stdio.h>
#include <minos.h>

int send_response(int client, char *response) {
    int buffer[128];
    int offset = 0;
    int elapsed = 0;
    int length = 0;
    while (response[length] != 0) {
        length++;
    }
    while (offset < length && elapsed < 5000) {
        int count = 0;
        while (count < 128 && offset + count < length) {
            buffer[count] = response[offset + count];
            count++;
        }
        int sent = socket_send(client, buffer, count);
        if (sent == -1) {
            return -1;
        }
        if (sent == -2) {
            sleep(10);
            elapsed += 10;
        } else {
            offset += sent;
        }
    }
    if (offset != length) {
        return -1;
    }
    return 0;
}

void serve_client(int client) {
    int buffer[128];
    int request[512];
    int length = 0;
    int complete = 0;
    int elapsed = 0;
    while (!complete && length < 512 && elapsed < 5000) {
        int count = socket_recv(client, buffer, 128);
        if (count == -1 || count == 0) {
            return;
        }
        if (count > 0) {
            for (int i = 0; i < count && length < 512 && !complete; i++) {
                request[length] = buffer[i];
                length++;
                if (length >= 4 && request[length - 4] == 13 &&
                    request[length - 3] == 10 && request[length - 2] == 13 &&
                    request[length - 1] == 10) {
                    complete = 1;
                }
            }
        }
        sleep(10);
        elapsed += 10;
    }
    if (!complete) {
        if (send_response(client, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n") < 0)
            printf("httpsrv: response failed.\n");
    } else if (length >= 18 && request[0] == 'G' && request[1] == 'E' &&
        request[2] == 'T' && request[3] == ' ' && request[4] == '/' &&
        request[5] == ' ' && request[6] == 'H' && request[7] == 'T' &&
        request[8] == 'T' && request[9] == 'P' && request[10] == '/' &&
        request[11] == '1' && request[12] == '.' &&
        (request[13] == '0' || request[13] == '1') &&
        request[14] == 13 && request[15] == 10) {
        if (send_response(client, "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\nHello World") < 0)
            printf("httpsrv: response failed.\n");
    } else {
        if (send_response(client, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n") < 0)
            printf("httpsrv: response failed.\n");
    }
}

int main() {
    int listener = socket();
    if (listener < 0) {
        printf("httpsrv: socket allocation failed.\n");
        return 1;
    }
    if (socket_listen(listener, 8000) < 0) {
        printf("httpsrv: cannot listen on port 8000.\n");
        socket_close(listener);
        return 1;
    }
    while (1) {
        int client = socket_accept(listener);
        if (client == -2) {
            sleep(10);
        } else if (client < 0) {
            printf("httpsrv: accept failed.\n");
            socket_close(listener);
            return 1;
        } else {
            serve_client(client);
            socket_close(client);
        }
    }
}

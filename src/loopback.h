#ifndef MINOS_LOOPBACK_H
#define MINOS_LOOPBACK_H

#define LOOPBACK_AGAIN (-2)
int loopback_socket(unsigned int owner);
int loopback_listen(unsigned int owner, unsigned int handle, unsigned int port);
int loopback_connect(unsigned int owner, unsigned int handle, unsigned int port);
int loopback_accept(unsigned int owner, unsigned int handle);
int loopback_send(unsigned int owner, unsigned int handle, const unsigned int *data, unsigned int length);
int loopback_recv(unsigned int owner, unsigned int handle, unsigned int *data, unsigned int length);
int loopback_close(unsigned int owner, unsigned int handle);
void loopback_close_owner(unsigned int owner);
#endif

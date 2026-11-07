#ifndef MINOS_DESKTOP_H
#define MINOS_DESKTOP_H

void desktop_set_server(unsigned int pid);
void desktop_server_exit(unsigned int pid);
unsigned int desktop_server_id(void);
void desktop_mouse_event(unsigned int x, unsigned int y, unsigned int kind);
int desktop_next(unsigned int *event);
int desktop_create(unsigned int client_id, unsigned int approve);

#endif

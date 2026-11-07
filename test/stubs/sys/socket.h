#ifndef STUB_SYS_SOCKET_H
#define STUB_SYS_SOCKET_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
typedef unsigned socklen_t;
struct sockaddr { unsigned char sa_len, sa_family; char sa_data[14]; };
#define AF_INET      2
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOL_SOCKET   0xffff
#define SO_REUSEADDR 0x0004
#define SO_SNDTIMEO  0x1005
#define SO_RCVTIMEO  0x1006
int socket(int, int, int);
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *);
int connect(int, const struct sockaddr *, socklen_t);
int getsockname(int, struct sockaddr *, socklen_t *);
int setsockopt(int, int, int, const void *, socklen_t);
#endif

#ifndef STUB_NETINET_IN_H
#define STUB_NETINET_IN_H
#include <stdint.h>
#include <sys/socket.h>
struct in_addr { uint32_t s_addr; };
struct sockaddr_in { uint8_t sin_len, sin_family; uint16_t sin_port; struct in_addr sin_addr; char sin_zero[8]; };
#define INADDR_ANY      0u
#define INADDR_LOOPBACK 0x7f000001u
uint16_t htons(uint16_t);
uint32_t htonl(uint32_t);
uint16_t ntohs(uint16_t);
uint32_t ntohl(uint32_t);
#endif

#ifndef STUB_ARPA_INET_H
#define STUB_ARPA_INET_H
#include <netinet/in.h>
uint32_t    inet_addr(const char *);
const char *inet_ntop(int, const void *, char *, socklen_t);
#endif

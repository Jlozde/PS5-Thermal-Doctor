/* POSIX declarations missing from MinGW; only for -fsyntax-only (via -include). */
#ifndef STUB_SYNTAX_COMPAT_H
#define STUB_SYNTAX_COMPAT_H
#include <signal.h>
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGHUP
#define SIGHUP 1
#endif
#ifndef SIGCHLD
#define SIGCHLD 20
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
int kill(int pid, int sig);
typedef long ssize_t_stub;
#endif

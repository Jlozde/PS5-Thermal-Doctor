#ifndef STUB_DLFCN_H
#define STUB_DLFCN_H
#define RTLD_DEFAULT ((void *)0)
void *dlsym(void *, const char *);
#endif

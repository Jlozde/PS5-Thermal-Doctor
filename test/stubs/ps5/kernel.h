/* ps5-payload-sdk stand-in for the syntax check (never compiled on a PC, -fsyntax-only only). */
#ifndef STUB_PS5_KERNEL_H
#define STUB_PS5_KERNEL_H
#include <stdint.h>
#include <sys/types.h>
intptr_t kernel_get_proc(pid_t pid);
int      kernel_set_ucred_uid(pid_t pid, uint32_t v);
int      kernel_set_ucred_ruid(pid_t pid, uint32_t v);
int      kernel_set_ucred_svuid(pid_t pid, uint32_t v);
int      kernel_set_ucred_rgid(pid_t pid, uint32_t v);
int      kernel_set_ucred_svgid(pid_t pid, uint32_t v);
int      kernel_set_ucred_authid(pid_t pid, uint64_t v);
int      kernel_set_ucred_caps(pid_t pid, const uint8_t caps[16]);
int      kernel_set_ucred_attrs(pid_t pid, const uint8_t attrs[32]);
intptr_t kernel_get_root_vnode(void);
int      kernel_set_proc_rootdir(pid_t pid, intptr_t vnode);
int      kernel_set_proc_jaildir(pid_t pid, intptr_t vnode);
uint32_t kernel_get_fw_version(void);
#endif

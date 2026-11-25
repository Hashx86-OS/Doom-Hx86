#ifndef _SYS_STAT_H_
#define _SYS_STAT_H_

#include <sys/types.h>
#include <Hx86/Hsyscalls/syscalls.h>

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

int fstat(int fd, struct stat *statbuf);
int stat(const char *path, struct stat *statbuf);
int mkdir(const char *path, mode_t mode);

#endif

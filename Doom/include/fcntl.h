#ifndef _FCNTL_H_
#define _FCNTL_H_

#include <Hx86/Hsyscalls/syscalls.h>

#ifndef O_RDONLY
#define O_RDONLY 0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY 0x0001
#endif
#ifndef O_RDWR
#define O_RDWR 0x0002
#endif
#ifndef O_CREAT
#define O_CREAT 0x0040
#endif
#ifndef O_EXCL
#define O_EXCL 0x0080
#endif
#ifndef O_TRUNC
#define O_TRUNC 0x0200
#endif
#ifndef O_APPEND
#define O_APPEND 0x0400
#endif

#define O_BINARY 0

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#endif

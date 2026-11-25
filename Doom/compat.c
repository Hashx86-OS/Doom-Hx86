/*
 * Minimal freestanding C runtime shims for the Hashx86 DOOM port.
 *
 * The engine is compiled as C++ and linked against libhx86 (whose helpers are
 * C++ symbols) plus libmbedtls (which supplies the plain-C memory/string
 * helpers used by the vendored archive decoders). Everything declared here is
 * therefore defined with C++ linkage, matching the engine's calls.
 */

#include <Hx86/Hx86.h>
#include <stdarg.h>
#include <stddef.h>
#include <time.h>

#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "math.h"
#include "sys/stat.h"

/* ------------------------------------------------------------------------- */
/* Formatted output                                                           */
/* ------------------------------------------------------------------------- */

struct FormatSink {
    char* buffer;
    size_t capacity;
    size_t length;
};

static void sink_put(FormatSink* sink, char c) {
    if (sink->length + 1 < sink->capacity) sink->buffer[sink->length] = c;
    sink->length++;
}

static void sink_text(FormatSink* sink, const char* text) {
    if (text == 0) text = "(null)";
    while (*text) sink_put(sink, *text++);
}

static void sink_unsigned(FormatSink* sink, unsigned int value, unsigned int base,
                          const char* digits) {
    char tmp[16];
    int count = 0;
    if (value == 0) {
        tmp[count++] = '0';
    } else {
        while (value != 0 && count < (int)sizeof(tmp)) {
            tmp[count++] = digits[value % base];
            value /= base;
        }
    }
    while (count > 0) sink_put(sink, tmp[--count]);
}

static void sink_signed(FormatSink* sink, int value) {
    unsigned int magnitude;
    if (value < 0) {
        sink_put(sink, '-');
        magnitude = (unsigned int)(-(long long)value);
    } else {
        magnitude = (unsigned int)value;
    }
    sink_unsigned(sink, magnitude, 10, "0123456789");
}

static void sink_double(FormatSink* sink, double value) {
    if (value < 0) {
        sink_put(sink, '-');
        value = -value;
    }
    unsigned int whole = (unsigned int)value;
    sink_unsigned(sink, whole, 10, "0123456789");
    sink_put(sink, '.');
    double frac = value - (double)whole;
    for (int i = 0; i < 6; i++) {
        frac *= 10.0;
        int digit = (int)frac;
        if (digit > 9) digit = 9;
        sink_put(sink, (char)('0' + digit));
        frac -= (double)digit;
    }
}

/* Emit an unsigned value with width/precision padding. */
static void sink_number(FormatSink* sink, int negative, unsigned int magnitude, unsigned int base,
                        const char* digits, int width, int zeroPad, int leftAlign,
                        int precision) {
    char tmp[32];
    int n = 0;

    // '0' padding is ignored when an explicit precision is given.
    if (precision >= 0) zeroPad = 0;

    if (magnitude == 0) {
        if (precision != 0) tmp[n++] = '0';
    } else {
        while (magnitude != 0) {
            tmp[n++] = digits[magnitude % base];
            magnitude /= base;
        }
    }
    while (precision > n) tmp[n++] = '0';

    int sign = negative ? 1 : 0;
    int pad = width > n + sign ? width - (n + sign) : 0;

    if (!leftAlign) {
        if (zeroPad) {
            if (sign) sink_put(sink, '-');
            sign = 0;
            for (int k = 0; k < pad; k++) sink_put(sink, '0');
            pad = 0;
        } else {
            for (int k = 0; k < pad; k++) sink_put(sink, ' ');
            pad = 0;
        }
    }

    if (sign) sink_put(sink, '-');
    for (int k = n - 1; k >= 0; k--) sink_put(sink, tmp[k]);
    if (leftAlign) {
        for (int k = 0; k < pad; k++) sink_put(sink, ' ');
    }
}

static int format_v(char* buffer, size_t capacity, const char* format, va_list args) {
    FormatSink sink = {buffer, capacity, 0};
    if (capacity != 0) buffer[0] = '\0';

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            sink_put(&sink, format[i]);
            continue;
        }
        i++;
        if (format[i] == '%') {
            sink_put(&sink, '%');
            continue;
        }

        int leftAlign = 0;
        int zeroPad = 0;
        while (format[i] == '-' || format[i] == '+' || format[i] == ' ' ||
               format[i] == '#' || format[i] == '0') {
            if (format[i] == '-') leftAlign = 1;
            if (format[i] == '0') zeroPad = 1;
            i++;
        }

        int width = 0;
        while (format[i] >= '0' && format[i] <= '9') {
            width = width * 10 + (format[i] - '0');
            i++;
        }

        int precision = -1;
        if (format[i] == '.') {
            i++;
            precision = 0;
            while (format[i] >= '0' && format[i] <= '9') {
                precision = precision * 10 + (format[i] - '0');
                i++;
            }
        }

        int isLong = 0;
        while (format[i] == 'l' || format[i] == 'h' || format[i] == 'L') {
            if (format[i] == 'l') isLong = 1;
            i++;
        }
        switch (format[i]) {
            case 'd':
            case 'i': {
                int value = va_arg(args, int);
                int negative = value < 0;
                unsigned int magnitude =
                    negative ? (unsigned int)(-(long long)value) : (unsigned int)value;
                sink_number(&sink, negative, magnitude, 10, "0123456789", width, zeroPad,
                            leftAlign, precision);
                break;
            }
            case 'u':
                sink_number(&sink, 0, va_arg(args, unsigned int), 10, "0123456789", width, zeroPad,
                            leftAlign, precision);
                break;
            case 'x':
                sink_number(&sink, 0, va_arg(args, unsigned int), 16, "0123456789abcdef", width,
                            zeroPad, leftAlign, precision);
                break;
            case 'X':
                sink_number(&sink, 0, va_arg(args, unsigned int), 16, "0123456789ABCDEF", width,
                            zeroPad, leftAlign, precision);
                break;
            case 's': {
                const char* text = va_arg(args, const char*);
                // strlen() is defined later in this file; count inline.
                int len = 0;
                if (text) {
                    while (text[len] != '\0') len++;
                }
                if (precision >= 0 && len > precision) len = precision;
                if (!leftAlign) {
                    for (int k = len; k < width; k++) sink_put(&sink, ' ');
                }
                for (int k = 0; k < len; k++) sink_put(&sink, text[k]);
                if (leftAlign) {
                    for (int k = len; k < width; k++) sink_put(&sink, ' ');
                }
                break;
            }
            case 'c':
                sink_put(&sink, (char)va_arg(args, int));
                break;
            case 'f':
                sink_double(&sink, va_arg(args, double));
                break;
            case 'p': {
                void* ptr = va_arg(args, void*);
                unsigned int addr = (unsigned int)ptr;
                sink_text(&sink, "0x");
                sink_unsigned(&sink, addr, 16, "0123456789abcdef");
                break;
            }
            case '\0':
                sink_put(&sink, '%');
                i--;
                break;
            default:
                sink_put(&sink, '%');
                sink_put(&sink, format[i]);
                break;
        }
    }

    if (capacity != 0) {
        size_t end = sink.length < capacity - 1 ? sink.length : capacity - 1;
        buffer[end] = '\0';
    }
    return (int)sink.length;
}

int vsprintf(char* buffer, const char* format, va_list args) {
    return format_v(buffer, (size_t)-1, format, args);
}

int vsnprintf(char* buffer, size_t size, const char* format, va_list args) {
    return format_v(buffer, size, format, args);
}

int sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsprintf(buffer, format, args);
    va_end(args);
    return result;
}

int snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

/* ------------------------------------------------------------------------- */
/* Character helpers                                                          */
/* ------------------------------------------------------------------------- */

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }

int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isprint(int c) { return c >= 0x20 && c <= 0x7E; }
int isgraph(int c) { return c > 0x20 && c <= 0x7E; }
int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
int isascii(int c) { return (unsigned int)c <= 0x7F; }

/* ------------------------------------------------------------------------- */
/* Floating point                                                             */
/* ------------------------------------------------------------------------- */

double fabs(double x) { return x < 0 ? -x : x; }

double sqrt(double x) {
    double result;
    __asm__ volatile("fsqrt" : "=t"(result) : "0"(x));
    return result;
}

double floor(double x) {
    double result;
    unsigned short oldcw, newcw;
    __asm__ volatile("fstcw %0" : "=m"(oldcw));
    newcw = (oldcw & ~0x0C00) | 0x0400;
    __asm__ volatile("fldcw %0" : : "m"(newcw));
    __asm__ volatile("frndint" : "=t"(result) : "0"(x));
    __asm__ volatile("fldcw %0" : : "m"(oldcw));
    return result;
}

double ceil(double x) {
    double result;
    unsigned short oldcw, newcw;
    __asm__ volatile("fstcw %0" : "=m"(oldcw));
    newcw = (oldcw & ~0x0C00) | 0x0800;
    __asm__ volatile("fldcw %0" : : "m"(newcw));
    __asm__ volatile("frndint" : "=t"(result) : "0"(x));
    __asm__ volatile("fldcw %0" : : "m"(oldcw));
    return result;
}

double sin(double x) {
    double result;
    __asm__ volatile("fsin" : "=t"(result) : "0"(x));
    return result;
}

double cos(double x) {
    double result;
    __asm__ volatile("fcos" : "=t"(result) : "0"(x));
    return result;
}

double tan(double x) {
    double result;
    __asm__ volatile(
        "fptan\n\t"
        "fstp %%st(0)"
        : "=t"(result)
        : "0"(x));
    return result;
}

double atan(double x) {
    double result;
    __asm__ volatile("fld1; fpatan" : "=t"(result) : "0"(x) : "st(1)");
    return result;
}

double atan2(double y, double x) {
    double result;
    __asm__ volatile("fpatan" : "=t"(result) : "0"(x), "u"(y) : "st(1)");
    return result;
}

double asin(double x) {
    if (x >= 1.0) return 1.57079632679489661923;
    if (x <= -1.0) return -1.57079632679489661923;
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x >= 1.0) return 0.0;
    if (x <= -1.0) return 3.14159265358979323846;
    return atan2(sqrt(1.0 - x * x), x);
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double result = x;
    while (result >= y) result -= y;
    while (result < 0) result += y;
    return result;
}

double exp(double x) {
    double result;
    __asm__ volatile(
        "fldl2e\n\t"
        "fmulp\n\t"
        "fld %%st(0)\n\t"
        "frndint\n\t"
        "fxch %%st(1)\n\t"
        "fsub %%st(1), %%st(0)\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)"
        : "=t"(result)
        : "0"(x));
    return result;
}

double log(double x) {
    double result;
    __asm__ volatile("fldln2; fxch; fyl2x" : "=t"(result) : "0"(x) : "st(1)");
    return result;
}

double log10(double x) {
    double result;
    __asm__ volatile("fldlg2; fxch; fyl2x" : "=t"(result) : "0"(x) : "st(1)");
    return result;
}

double pow(double x, double y) {
    if (x <= 0.0) return 0.0;
    return exp(y * log(x));
}

/* ------------------------------------------------------------------------- */
/* Conversions                                                                */
/* ------------------------------------------------------------------------- */

double atof(const char* str) {
    double result = 0.0;
    double sign = 1.0;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '-') { sign = -1.0; str++; }
    else if (*str == '+') str++;
    while (*str >= '0' && *str <= '9') {
        result = result * 10.0 + (*str - '0');
        str++;
    }
    if (*str == '.') {
        str++;
        double frac = 1.0;
        while (*str >= '0' && *str <= '9') {
            frac /= 10.0;
            result += (*str - '0') * frac;
            str++;
        }
    }
    return result * sign;
}

int atoi(const char* str) { return (int)atof(str); }
long atol(const char* str) { return (long)atof(str); }
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

/* ------------------------------------------------------------------------- */
/* Random numbers                                                             */
/* ------------------------------------------------------------------------- */

static unsigned int rand_next = 1;

int rand(void) {
    rand_next = rand_next * 1103515245u + 12345u;
    return (int)((rand_next / 65536u) % 32768u);
}

void srand(unsigned int seed) { rand_next = seed; }

/* ------------------------------------------------------------------------- */
/* Strings                                                                    */
/* ------------------------------------------------------------------------- */

int strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    int i = 0;
    while (src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, int n) {
    int i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

char* strcat(char* dest, const char* src) {
    int dlen = strlen(dest);
    int i = 0;
    while (src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    size_t dlen = strlen(dest);
    size_t i = 0;
    while (i < n && src[i]) { dest[dlen + i] = src[i]; i++; }
    dest[dlen + i] = '\0';
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    while (n > 0 && *s1 && *s2 && *s1 == *s2) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : 0;
}

char* strrchr(const char* s, int c) {
    const char* found = 0;
    while (*s) {
        if (*s == (char)c) found = s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return (char*)found;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    while (*haystack) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (toupper((unsigned char)*s1) != toupper((unsigned char)*s2)) return -1;
        s1++; s2++;
    }
    return (*s1) ? 1 : (*s2) ? -1 : 0;
}

int strncasecmp(const char* s1, const char* s2, int n) {
    while (n > 0 && *s1 && *s2) {
        if (toupper((unsigned char)*s1) != toupper((unsigned char)*s2)) return -1;
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return (*s1) ? 1 : (*s2) ? -1 : 0;
}

char* strdup(const char* s) {
    if (!s) return 0;
    int len = strlen(s);
    char* copy = (char*)kmalloc(len + 1);
    if (copy) {
        int i;
        for (i = 0; i <= len; i++) copy[i] = s[i];
    }
    return copy;
}

char* strtok(char* str, const char* delim) {
    static char* last = NULL;
    if (str) last = str;
    if (!last || !*last) return NULL;
    char* start = last;
    while (*last) {
        for (const char* d = delim; *d; d++) {
            if (*last == *d) {
                *last = '\0';
                last++;
                return start;
            }
        }
        last++;
    }
    last = NULL;
    return start;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Memory                                                                     */
/* ------------------------------------------------------------------------- */

void* malloc(size_t size) { return kmalloc((int)size); }
void free(void* ptr) { kfree(ptr); }
void* realloc(void* ptr, size_t size) { return krealloc(ptr, (int)size); }
void* calloc(size_t nmemb, size_t size) { return kcalloc((int)nmemb, (int)size); }

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
    (void)base;
    (void)nmemb;
    (void)size;
    (void)compar;
}

char* getenv(const char* name) {
    (void)name;
    return NULL;
}

int putenv(char* string) {
    (void)string;
    return -1;
}

/* ------------------------------------------------------------------------- */
/* File descriptors                                                           */
/* ------------------------------------------------------------------------- */

int open(const char* pathname, int flags, ...) {
    return syscall_open(pathname, flags);
}

int close(int fd) { return syscall_close((uint32_t)fd); }

int read(int fd, void* buf, int count) {
    return syscall_read((uint32_t)fd, (char*)buf, (uint32_t)count);
}

int write(int fd, const void* buf, int count) {
    return syscall_write((uint32_t)fd, (const char*)buf, (uint32_t)count);
}

int lseek(int fd, int offset, int whence) {
    return syscall_lseek((uint32_t)fd, offset, whence);
}

int unlink(const char* pathname) { return syscall_unlink(pathname); }

/* The kernel resolves absolute paths reliably; relative names like
 * "./doom1.wad" do not reach the FAT directory. Doom uses getcwd() to build
 * absolute IWAD and config paths. */
char* getcwd(char* buf, size_t size) {
    if (buf == nullptr || size == 0) return nullptr;
    if (syscall_getcwd(buf, (uint32_t)size) <= 0) return nullptr;
    return buf;
}

int access(const char* pathname, int mode) {
    (void)mode;
    struct stat st;
    return syscall_stat(pathname, &st) == 0 ? 0 : -1;
}

int stat(const char* path, struct stat* statbuf) {
    return syscall_stat(path, statbuf);
}

/*
 * Hashx86 exposes stat by path only. The engine only needs the size of an
 * already-open WAD, so recover it with lseek() and restore the position.
 */
int fstat(int fd, struct stat* statbuf) {
    if (!statbuf) return -1;
    int current = syscall_lseek(fd, 0, SEEK_CUR);
    int end = syscall_lseek(fd, 0, SEEK_END);
    if (current >= 0) syscall_lseek(fd, current, SEEK_SET);
    if (end < 0) return -1;
    memset(statbuf, 0, sizeof(*statbuf));
    statbuf->st_mode = S_IFREG | 0644;
    statbuf->st_nlink = 1;
    statbuf->st_size = (uint32_t)end;
    statbuf->st_blksize = 512;
    statbuf->st_blocks = ((uint32_t)end + 511) / 512;
    return 0;
}

int mkdir(const char* path, mode_t mode) {
    return syscall_mkdir(path, (uint32_t)mode);
}

int usleep(unsigned int usec) {
    struct timespec req;
    req.tv_sec = (int32_t)(usec / 1000000u);
    req.tv_nsec = (int32_t)((usec % 1000000u) * 1000u);
    syscall_nanosleep(&req, NULL);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* stdio streams                                                              */
/* ------------------------------------------------------------------------- */

static FILE stdin_file = {0, 0, 0, 0, 0, 0};
static FILE stdout_file = {1, 0, 0, 0, 0, 0};
static FILE stderr_file = {2, 0, 0, 0, 0, 0};

FILE* stdin = &stdin_file;
FILE* stdout = &stdout_file;
FILE* stderr = &stderr_file;

FILE* fopen(const char* path, const char* mode) {
    int flags = O_RDONLY;
    if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (mode[0] == 'r' && mode[1] == '+') flags = O_RDWR;
    int fd = syscall_open(path, flags);
    if (fd < 0) return NULL;
    FILE* f = (FILE*)kmalloc(sizeof(FILE));
    if (!f) {
        syscall_close((uint32_t)fd);
        return NULL;
    }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->pos = 0;
    f->ungetbuf = 0;
    f->ungot = 0;
    return f;
}

int fclose(FILE* f) {
    if (!f) return EOF;
    syscall_close((uint32_t)f->fd);
    if (f != stdin && f != stdout && f != stderr) kfree(f);
    return 0;
}

int fputc(int c, FILE* f) {
    if (!f) return EOF;
    char ch = (char)c;
    int ret = syscall_write((uint32_t)f->fd, &ch, 1);
    if (ret != 1) {
        f->error = 1;
        return EOF;
    }
    f->pos++;
    return (unsigned char)ch;
}

int fputs(const char* s, FILE* f) {
    if (!f) return EOF;
    int len = strlen(s);
    int ret = syscall_write((uint32_t)f->fd, s, (uint32_t)len);
    if (ret != len) {
        f->error = 1;
        return EOF;
    }
    f->pos += ret;
    return ret;
}

int fprintf(FILE* f, const char* format, ...) {
    if (!f) return 0;
    va_list args;
    va_start(args, format);
    char buffer[2048];
    int len = vsprintf(buffer, format, args);
    va_end(args);
    int ret = syscall_write((uint32_t)f->fd, buffer, (uint32_t)len);
    if (ret != len) f->error = 1;
    f->pos += ret;
    return ret;
}

int vfprintf(FILE* f, const char* format, va_list ap) {
    if (!f) return 0;
    char buffer[2048];
    int len = vsprintf(buffer, format, ap);
    int ret = syscall_write((uint32_t)f->fd, buffer, (uint32_t)len);
    if (ret != len) f->error = 1;
    f->pos += ret;
    return ret;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (!f) return 0;
    int total = (int)(size * nmemb);
    int ret = syscall_read((uint32_t)f->fd, (char*)ptr, (uint32_t)total);
    if (ret <= 0) {
        f->eof = 1;
        return 0;
    }
    f->pos += ret;
    return (size_t)ret / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (!f) return 0;
    int total = (int)(size * nmemb);
    int ret = syscall_write((uint32_t)f->fd, (const char*)ptr, (uint32_t)total);
    if (ret != total) f->error = 1;
    f->pos += ret;
    return (size_t)ret / size;
}

char* fgets(char* s, int size, FILE* f) {
    if (!f || size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        char ch;
        int ret = syscall_read((uint32_t)f->fd, &ch, 1);
        if (ret <= 0) {
            f->eof = 1;
            break;
        }
        f->pos++;
        s[i++] = ch;
        if (ch == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}

int fgetc(FILE* f) {
    if (!f) return EOF;
    if (f->ungot) {
        f->ungot = 0;
        return (unsigned char)f->ungetbuf;
    }
    unsigned char ch;
    int ret = syscall_read((uint32_t)f->fd, (char*)&ch, 1);
    if (ret <= 0) {
        f->eof = 1;
        return EOF;
    }
    f->pos++;
    return ch;
}

int ungetc(int c, FILE* f) {
    if (!f || c == EOF) return EOF;
    f->ungetbuf = (char)c;
    f->ungot = 1;
    return (unsigned char)c;
}

int getchar(void) { return fgetc(stdin); }

int feof(FILE* f) { return f ? f->eof : 1; }
int ferror(FILE* f) { return f ? f->error : 1; }

void clearerr(FILE* f) {
    if (f) {
        f->eof = 0;
        f->error = 0;
    }
}

int fflush(FILE* f) {
    (void)f;
    return 0;
}

void setbuf(FILE* f, char* buf) {
    (void)f;
    (void)buf;
}

int fseek(FILE* f, long offset, int whence) {
    if (!f) return -1;
    int ret = lseek(f->fd, (int)offset, whence);
    if (ret < 0) return -1;
    f->pos = ret;
    f->eof = 0;
    return 0;
}

long ftell(FILE* f) {
    if (!f) return -1;
    return (long)lseek(f->fd, 0, SEEK_CUR);
}

void rewind(FILE* f) {
    if (f) {
        fseek(f, 0, SEEK_SET);
        clearerr(f);
    }
}

int remove(const char* path) { return syscall_unlink(path); }

int rename(const char* oldpath, const char* newpath) {
    return syscall_rename(oldpath, newpath);
}

void perror(const char* s) {
    if (s) printf("%s: error\n", s);
    else printf("error\n");
}

char* strerror(int errnum) {
    (void)errnum;
    return "Unknown error";
}

/* ------------------------------------------------------------------------- */
/* scanf family                                                               */
/* ------------------------------------------------------------------------- */

static int scan_skip_ws_str(const char** s) {
    int c;
    while ((c = (unsigned char)**s) != 0 && isspace(c)) (*s)++;
    return c;
}

static int scan_read_int_str(const char** s, int* val) {
    scan_skip_ws_str(s);
    int c = (unsigned char)**s;
    if (c == 0) return 0;
    int neg = 0;
    if (c == '-') { neg = 1; (*s)++; c = (unsigned char)**s; }
    else if (c == '+') { (*s)++; c = (unsigned char)**s; }
    if (!isdigit(c)) return 0;
    int v = 0;
    while (isdigit(c)) {
        v = v * 10 + (c - '0');
        (*s)++;
        c = (unsigned char)**s;
    }
    *val = neg ? -v : v;
    return 1;
}

int sscanf(const char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const char* s = str;
    int count = 0;
    while (*format) {
        if (isspace((unsigned char)*format)) { format++; continue; }
        if (*format != '%') {
            if ((unsigned char)*s != (unsigned char)*format) break;
            s++;
            format++;
            continue;
        }
        format++;
        switch (*format) {
            case 'd':
            case 'i': {
                int* val = va_arg(args, int*);
                if (scan_read_int_str(&s, val)) count++;
                else goto done;
                break;
            }
            case 's': {
                char* val = va_arg(args, char*);
                scan_skip_ws_str(&s);
                int i = 0;
                while (*s && !isspace((unsigned char)*s) && i < 1023) val[i++] = *s++;
                val[i] = '\0';
                if (i > 0) count++;
                else goto done;
                break;
            }
            case 'c': {
                char* val = va_arg(args, char*);
                if (*s) { *val = *s++; count++; }
                else goto done;
                break;
            }
            case 'f': {
                char* end = 0;
                double v = 0.0;
                int neg = 1;
                const char* p = s;
                scan_skip_ws_str(&p);
                if (*p == '-') { neg = -1; p++; }
                else if (*p == '+') p++;
                while (*p >= '0' && *p <= '9') { v = v * 10.0 + (*p - '0'); p++; }
                if (*p == '.') {
                    p++;
                    double frac = 1.0;
                    while (*p >= '0' && *p <= '9') { frac /= 10.0; v += (*p - '0') * frac; p++; }
                }
                (void)end;
                *va_arg(args, float*) = (float)(v * neg);
                s = p;
                count++;
                break;
            }
            case 'x': {
                unsigned int* val = va_arg(args, unsigned int*);
                scan_skip_ws_str(&s);
                unsigned int v = 0;
                while (isxdigit((unsigned char)*s)) {
                    int d = isdigit((unsigned char)*s) ? *s - '0'
                                                        : (tolower((unsigned char)*s) - 'a' + 10);
                    v = v * 16u + (unsigned int)d;
                    s++;
                }
                *val = v;
                count++;
                break;
            }
            default:
                goto done;
        }
        format++;
    }
done:
    va_end(args);
    return count;
}

static int fscan_skip_ws(FILE* f) {
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (!isspace(c)) return c;
    }
    return EOF;
}

int fscanf(FILE* f, const char* format, ...) {
    if (!f) return 0;
    va_list args;
    va_start(args, format);
    int count = 0;
    int c = 0;
    int haveChar = 0;

    while (*format) {
        if (isspace((unsigned char)*format)) {
            while (*format && isspace((unsigned char)*format)) format++;
            c = fscan_skip_ws(f);
            haveChar = (c != EOF);
            continue;
        }
        if (*format != '%') {
            if (!haveChar) {
                c = fgetc(f);
                haveChar = 1;
            }
            if (c == EOF || c != (unsigned char)*format) {
                if (c != EOF) ungetc(c, f);
                goto done;
            }
            haveChar = 0;
            format++;
            continue;
        }

        format++;
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        if (*format == 'l' || *format == 'h') format++;

        switch (*format) {
            case 'd':
            case 'i': {
                if (!haveChar) { c = fscan_skip_ws(f); haveChar = 1; }
                if (c == EOF) goto done;
                int neg = 0;
                if (c == '-') { neg = 1; c = fgetc(f); }
                else if (c == '+') c = fgetc(f);
                if (!isdigit(c)) {
                    if (c != EOF) ungetc(c, f);
                    goto done;
                }
                int v = 0;
                while (isdigit(c)) { v = v * 10 + (c - '0'); c = fgetc(f); }
                if (c != EOF) ungetc(c, f);
                *va_arg(args, int*) = neg ? -v : v;
                count++;
                haveChar = 0;
                break;
            }
            case 'x': {
                if (!haveChar) { c = fscan_skip_ws(f); haveChar = 1; }
                if (c == EOF) goto done;
                unsigned int v = 0;
                while (isxdigit(c)) {
                    int d = isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10);
                    v = v * 16u + (unsigned int)d;
                    c = fgetc(f);
                }
                if (c != EOF) ungetc(c, f);
                *va_arg(args, unsigned int*) = v;
                count++;
                haveChar = 0;
                break;
            }
            case 'f': {
                if (!haveChar) { c = fscan_skip_ws(f); haveChar = 1; }
                if (c == EOF) goto done;
                int neg = 1;
                if (c == '-') { neg = -1; c = fgetc(f); }
                else if (c == '+') c = fgetc(f);
                double v = 0.0;
                while (isdigit(c)) { v = v * 10.0 + (c - '0'); c = fgetc(f); }
                if (c == '.') {
                    c = fgetc(f);
                    double frac = 1.0;
                    while (isdigit(c)) { frac /= 10.0; v += (c - '0') * frac; c = fgetc(f); }
                }
                if (c != EOF) ungetc(c, f);
                *va_arg(args, float*) = (float)(v * neg);
                count++;
                haveChar = 0;
                break;
            }
            case 's': {
                if (!haveChar) { c = fscan_skip_ws(f); haveChar = 1; }
                if (c == EOF) goto done;
                char* out = va_arg(args, char*);
                int i = 0;
                int limit = width > 0 ? width : 1023;
                while (c != EOF && !isspace(c) && i < limit) {
                    out[i++] = (char)c;
                    c = fgetc(f);
                }
                if (c != EOF) ungetc(c, f);
                out[i] = '\0';
                if (i > 0) count++;
                else goto done;
                haveChar = 0;
                break;
            }
            case '[': {
                format++;
                int negate = 0;
                if (*format == '^') { negate = 1; format++; }
                char set[256];
                memset(set, 0, sizeof(set));
                while (*format && *format != ']') {
                    set[(unsigned char)*format] = 1;
                    format++;
                }
                if (!haveChar) { c = fgetc(f); haveChar = 1; }
                if (c == EOF) goto done;
                char* out = va_arg(args, char*);
                int i = 0;
                int limit = width > 0 ? width : 1023;
                while (c != EOF && i < limit && (set[(unsigned char)c] != 0) != negate) {
                    out[i++] = (char)c;
                    c = fgetc(f);
                }
                if (c != EOF) ungetc(c, f);
                out[i] = '\0';
                if (i > 0) count++;
                else goto done;
                haveChar = 0;
                break;
            }
            case 'c': {
                if (!haveChar) { c = fgetc(f); haveChar = 1; }
                if (c == EOF) goto done;
                *va_arg(args, char*) = (char)c;
                count++;
                haveChar = 0;
                break;
            }
            default:
                goto done;
        }
        format++;
    }
done:
    if (haveChar && c != EOF) ungetc(c, f);
    va_end(args);
    return count;
}

/* ------------------------------------------------------------------------- */
/* Misc                                                                       */
/* ------------------------------------------------------------------------- */

int VCR_Init(void) { return -1; }

void exit(int status) { syscall_exit((uint32_t)status); }
void abort(void) { syscall_exit(1); }

int errno;

time_t time(time_t* t) {
    int32_t now = syscall_time(NULL);
    if (t) *t = (time_t)now;
    return (time_t)now;
}

struct tm* localtime(const time_t* timep) {
    (void)timep;
    return NULL;
}

struct tm* gmtime(const time_t* timep) {
    (void)timep;
    return NULL;
}

__attribute__((used)) void __assert_fail(const char* assertion, const char* file,
                                         unsigned int line, const char* function) {
    printf("Assertion failed: %s, file %s, line %d, function %s\n", assertion, file, (int)line,
           function);
    syscall_exit(1);
}

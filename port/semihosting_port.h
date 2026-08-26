#ifndef PORT_SEMIHOSTING_PORT_H
#define PORT_SEMIHOSTING_PORT_H

#include <stddef.h>
#include <stdbool.h>

/* true if semihosting file i/o (open/read/write/...) is allowed */
bool semihosting_fileio_enabled(void);

/* true if semihosting shell command execution is allowed */
bool semihosting_shell_enabled(void);

/* write len bytes from buf to the console */
void semihosting_putstr(const char *buf, size_t len);

#endif

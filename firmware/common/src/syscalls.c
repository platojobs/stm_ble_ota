// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    syscalls.c
 * @brief   Minimal newlib syscall stubs for the bare-metal target.
 *
 * The firmware never writes to stdout/stderr: every byte goes through the
 * explicit BSP UART helpers. These strong definitions replace the weak
 * libnosys stubs so the linker does not emit "_write is not implemented"
 * warnings, and any accidental stdio use fails cleanly.
 */

#include <stdint.h>
#include <sys/stat.h>

int _read(int fd, char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    return -1;
}

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    (void)buf;
    (void)len;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}

int _fstat(int fd, struct stat *status)
{
    (void)fd;
    (void)status;
    return -1;
}

int _isatty(int fd)
{
    (void)fd;
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

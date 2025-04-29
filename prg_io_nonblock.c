/*
 * Filename: prg_io_nonblock.c
 * Date:     2019/12/25 14:20
 * Author:   Jan Faigl
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>

#include <poll.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>    // for fcntl
#include <sys/stat.h> // for fstat

#include "prg_io_nonblock.h"

/// ----------------------------------------------------------------------------
static int io_open(const char *fname, int flag)
{
   int fd = open(fname, flag | O_NOCTTY | O_SYNC);
   if (fd != -1)
   {
      // Set fd to non block mode
      int flags = fcntl(fd, F_GETFL);
      flags &= ~O_NONBLOCK;
      if (fcntl(fd, F_SETFL, flags) < 0)
      {
         fprintf(stderr, "Error in %s(), file %s, line: %d, errno %d\n", __func__, __FILE__, __LINE__, errno);
      }
   }
   return fd;
}

/// ----------------------------------------------------------------------------
int io_open_read(const char *fname)
{
   // as defined for FIFO, open will be blocked for read only unless nonblock is specified
   return io_open(fname, O_RDONLY | O_NONBLOCK);
}

/// ----------------------------------------------------------------------------
int io_open_write(const char *fname)
{
   // Be aware that the opening named pipe for writing is blocked until the pipe is opened for reading.
   // Thus, run a program that opens the pipe or call, e.g., 'tail -f fname', where 'fname' is the filename name of the named pipe being opened for writing.
   return io_open(fname, O_WRONLY);
}

/// ----------------------------------------------------------------------------
int io_close(int fd)
{
   return close(fd);
}

/// ----------------------------------------------------------------------------
int io_putc(int fd, unsigned char c)
{
   return write(fd, &c, 1);
}

/// ----------------------------------------------------------------------------
int io_getc(int fd)
{
   char c;
   int r = read(fd, &c, 1);
   return r == 1 ? c : -1;
}

/// ----------------------------------------------------------------------------
int io_getc_timeout(int fd, int timeout_ms, unsigned char *c)
{
   struct pollfd ufdr[1];
   int r = 0;
   ufdr[0].fd = fd;
   ufdr[0].events = POLLIN | POLLRDNORM;
   int poll_result = poll(&ufdr[0], 1, timeout_ms);

   if (poll_result == 0){
      // fprintf(stderr, "DEBUG: Poll timeout (no data ready).\n");
   } else if (poll_result < 0){
      fprintf(stderr, "ERROR: Poll failed.\n");
   } else {
      if (ufdr[0].revents & (POLLIN | POLLRDNORM)){
         r = read(fd, c, 1);
         if (r != 1){
            fprintf(stderr, "ERROR: Read failed.\n");
         }
      }
   }
   return r;
}

/* end of prg_io_nonblock.c */

_Bool is_fd_valid(int fd)
{
   return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

void check_fd_info(int fd)
{
   struct stat st;
   if (fstat(fd, &st) == -1)
   {
      fprintf(stderr, "fstat failed for fd %d: %s\n", fd, strerror(errno));
   }
   else
   {
      fprintf(stderr, "fd %d is valid. Mode: %o\n", fd, st.st_mode);
   }
}

int safe_io_putc(int fd, unsigned char c)
{
   if (!is_fd_valid(fd))
   {
      fprintf(stderr, "Invalid file descriptor: %d\n", fd);
      return -1;
   }

   int result = write(fd, &c, 1);
   if (result != 1)
   {
      fprintf(stderr,
              "write(fd=%d, c=0x%02x) failed: return=%d, errno=%d (%s)\n",
              fd, c, result, errno, strerror(errno));
      check_fd_info(fd);
   }

   return result;
}

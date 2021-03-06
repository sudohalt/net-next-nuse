#define _GNU_SOURCE /* Get RTLD_NEXT */
#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/stat.h>
#include "nuse-hostcalls.h"
#include "sim-assert.h"

/* nuse-hostcalls.c */
int (*host_pthread_create)(pthread_t *, const pthread_attr_t *,
                                  void *(*) (void *), void *) = NULL;
int (*host_poll)(struct pollfd *, int, int) = NULL;
int (*host_socket) (int fd, int type, int proto) = NULL;
int (*host_close) (int fd) = NULL;
int (*host_bind)(int, const struct sockaddr *, int) = NULL;
ssize_t (*host_write) (int fd, const void *buf, size_t count) = NULL;
ssize_t (*host_read) (int fd, void *buf, size_t count) = NULL;
ssize_t (*host_writev) (int fd, const struct iovec *iovec, size_t count) = NULL;
int (*host_open) (const char *pathname, int flags, mode_t mode) = NULL;
int (*host_open64) (const char *pathname, int flags, mode_t mode) = NULL;
int (*host_ioctl)(int d, int request, ...) = NULL;

static void *
nuse_hijack_resolve_sym (const char *sym)
{
  void *resolv;
  resolv = dlsym (RTLD_NEXT, sym);
  if (!resolv)
    {
      printf ("dlsym fail %s (%s) \n", sym, dlerror ());
      sim_assert (0);
    }
  return resolv;
}

void nuse_hijack_init (void)
{
  /* hijacking functions */
  host_socket = nuse_hijack_resolve_sym ("socket");
  host_write = nuse_hijack_resolve_sym ("write");
  host_writev = nuse_hijack_resolve_sym ("writev");
  host_read = nuse_hijack_resolve_sym ("read");
  host_close = nuse_hijack_resolve_sym ("close");
  host_bind = nuse_hijack_resolve_sym ("bind");
  host_pthread_create = nuse_hijack_resolve_sym ("pthread_create");
  host_poll = nuse_hijack_resolve_sym ("poll");
  host_open = nuse_hijack_resolve_sym ("open");
  host_open64 = nuse_hijack_resolve_sym ("open64");
  host_ioctl = nuse_hijack_resolve_sym ("ioctl");

  return;
}


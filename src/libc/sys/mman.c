#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../../shim.h"

#define LINUX_MAP_SHARED     0x0001
#define LINUX_MAP_PRIVATE    0x0002
#define LINUX_MAP_FIXED      0x0010
#define LINUX_MAP_ANON       0x0020
#define LINUX_MAP_EXECUTABLE 0x1000
#define LINUX_MAP_NORESERVE  0x4000

#define KNOWN_LINUX_MMAP_FLAGS ( \
 LINUX_MAP_SHARED     |          \
 LINUX_MAP_PRIVATE    |          \
 LINUX_MAP_FIXED      |          \
 LINUX_MAP_ANON       |          \
 0x40                 |          \
 LINUX_MAP_EXECUTABLE |          \
 LINUX_MAP_NORESERVE             \
)

void* shim_mmap64_impl(void *addr, size_t len, int prot, int linux_flags, int fd, linux_off64_t offset) {

  assert((linux_flags & KNOWN_LINUX_MMAP_FLAGS) == linux_flags);

  int flags = 0;

  if (linux_flags & LINUX_MAP_SHARED) {
    flags |= MAP_SHARED;
  }

  if (linux_flags & LINUX_MAP_PRIVATE) {
    flags |= MAP_PRIVATE;
    flags |= fd == -1 ? MAP_ANON : 0; // ?
  }

  if (linux_flags & LINUX_MAP_FIXED) {
    flags |= MAP_FIXED;
  }

  if (linux_flags & LINUX_MAP_ANON) {
    flags |= MAP_ANON;
    assert(fd == -1 || fd == 0);
    fd = -1;
  }

  void* p = mmap(addr, len, prot, flags, fd, offset);
  if (p == MAP_FAILED) {
    perror(__func__);
  }

  return p;
}

void* shim_mmap_impl(void *addr, size_t len, int prot, int linux_flags, int fd, linux_off_t offset) {
  return shim_mmap64_impl(addr, len, prot, linux_flags, fd, offset);
}

SHIM_WRAP(mmap);
SHIM_WRAP(mmap64);

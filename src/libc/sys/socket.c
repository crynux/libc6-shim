#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "../../shim.h"
#include "socket.h"

static int linux_to_native_sock_level(int level) {
  switch (level) {
    case LINUX_SOL_SOCKET: return SOL_SOCKET;
    case LINUX_SOL_IP:     return IPPROTO_IP;
    case LINUX_SOL_TCP:    return IPPROTO_TCP;
    case LINUX_SOL_UDP:    return IPPROTO_UDP;
    default:
      assert(0);
  }
}

static int native_to_linux_sock_level(int level) {
  switch (level) {
    case SOL_SOCKET:  return LINUX_SOL_SOCKET;
    case IPPROTO_IP:  return LINUX_SOL_IP;
    case IPPROTO_TCP: return LINUX_SOL_TCP;
    case IPPROTO_UDP: return LINUX_SOL_UDP;
    default:
      assert(0);
  }
}

static int linux_to_native_sock_type(int linux_type) {

  assert((linux_type & KNOWN_LINUX_SOCKET_TYPES) == linux_type);

  int type = 0;

  if (linux_type & LINUX_SOCK_STREAM)   type |= SOCK_STREAM;
  if (linux_type & LINUX_SOCK_DGRAM)    type |= SOCK_DGRAM;
  if (linux_type & LINUX_SOCK_NONBLOCK) type |= SOCK_NONBLOCK;
  if (linux_type & LINUX_SOCK_CLOEXEC)  type |= SOCK_CLOEXEC;

  return type;
}

static int linux_to_native_msg_flags(int linux_flags) {

  assert((linux_flags & KNOWN_LINUX_MSG_FLAGS) == linux_flags);

  int flags = 0;

  if (linux_flags & LINUX_MSG_OOB)          flags |= MSG_OOB;
  if (linux_flags & LINUX_MSG_PEEK)         flags |= MSG_PEEK;
  if (linux_flags & LINUX_MSG_DONTROUTE)    flags |= MSG_DONTROUTE;
  if (linux_flags & LINUX_MSG_CTRUNC)       flags |= MSG_CTRUNC;
  if (linux_flags & LINUX_MSG_TRUNC)        flags |= MSG_TRUNC;
  if (linux_flags & LINUX_MSG_DONTWAIT)     flags |= MSG_DONTWAIT;
  if (linux_flags & LINUX_MSG_EOR)          flags |= MSG_EOR;
  if (linux_flags & LINUX_MSG_WAITALL)      flags |= MSG_WAITALL;
  if (linux_flags & LINUX_MSG_NOSIGNAL)     flags |= MSG_NOSIGNAL;
  if (linux_flags & LINUX_MSG_WAITFORONE)   flags |= MSG_WAITFORONE;
  if (linux_flags & LINUX_MSG_CMSG_CLOEXEC) flags |= MSG_CMSG_CLOEXEC;

  return flags;
}

static int native_to_linux_msg_flags(int flags) {

  assert((flags & KNOWN_NATIVE_MSG_FLAGS) == flags);

  int linux_flags = 0;

  if (flags & MSG_EOF) {
    assert(0);
  }

  if (flags & MSG_OOB)          linux_flags |= LINUX_MSG_OOB;
  if (flags & MSG_PEEK)         linux_flags |= LINUX_MSG_PEEK;
  if (flags & MSG_DONTROUTE)    linux_flags |= LINUX_MSG_DONTROUTE;
  if (flags & MSG_CTRUNC)       linux_flags |= LINUX_MSG_CTRUNC;
  if (flags & MSG_TRUNC)        linux_flags |= LINUX_MSG_TRUNC;
  if (flags & MSG_DONTWAIT)     linux_flags |= LINUX_MSG_DONTWAIT;
  if (flags & MSG_EOR)          linux_flags |= LINUX_MSG_EOR;
  if (flags & MSG_WAITALL)      linux_flags |= LINUX_MSG_WAITALL;
  if (flags & MSG_NOSIGNAL)     linux_flags |= LINUX_MSG_NOSIGNAL;
  if (flags & MSG_WAITFORONE)   linux_flags |= LINUX_MSG_WAITFORONE;
  if (flags & MSG_CMSG_CLOEXEC) linux_flags |= LINUX_MSG_CMSG_CLOEXEC;

  return linux_flags;
}

static void linux_to_native_sockaddr_in(struct sockaddr_in* dest, const linux_sockaddr_in* src, socklen_t addrlen) {

  assert(addrlen == sizeof(struct linux_sockaddr_in));

  dest->sin_len    = 0;
  dest->sin_family = PF_INET;
  dest->sin_port   = src->sin_port;
  dest->sin_addr   = src->sin_addr;

  memcpy(dest->sin_zero, src->sin_zero, sizeof(dest->sin_zero));
}

static void linux_to_native_sockaddr_un(struct sockaddr_un* dest, const linux_sockaddr_un* src, socklen_t addrlen) {

  assert(addrlen == sizeof(linux_sockaddr_un));

  dest->sun_len    = 0;
  dest->sun_family = PF_UNIX;

  if (src->sun_path[0] == 0 /* abstract socket address */) {
    snprintf(dest->sun_path, sizeof(dest->sun_path), "/var/run/%s", &src->sun_path[1]);
  } else {
    size_t nbytes = strlcpy(dest->sun_path, src->sun_path, sizeof(dest->sun_path));
    assert(nbytes < sizeof(dest->sun_path));
  }
}

static void native_to_linux_sockaddr_in(linux_sockaddr_in* dest, const struct sockaddr_in* src, socklen_t addrlen) {
  dest->sin_family = LINUX_PF_INET;
  dest->sin_port   = src->sin_port;
  dest->sin_addr   = src->sin_addr;
  memcpy(dest->sin_zero, src->sin_zero, sizeof(dest->sin_zero));
}

static void native_to_linux_sockaddr_un(linux_sockaddr_un* dest, const struct sockaddr_un* src, socklen_t addrlen) {
  dest->sun_family = LINUX_PF_UNIX;
  size_t nbytes = strlcpy(dest->sun_path, src->sun_path, sizeof(dest->sun_path));
  assert(nbytes < sizeof(dest->sun_path));
}

int shim_socket_impl(int domain, int type, int protocol) {
  assert(domain == LINUX_PF_UNIX || domain == LINUX_PF_INET);
  return socket(domain, linux_to_native_sock_type(type), protocol);
}

int shim_socketpair_impl(int domain, int type, int protocol, int* sv) {
  assert(domain == LINUX_PF_UNIX || domain == LINUX_PF_INET);
  return socketpair(domain, linux_to_native_sock_type(type), protocol, sv);
}

int shim_bind_impl(int s, const linux_sockaddr* linux_addr, socklen_t addrlen) {

  switch (linux_addr->sa_family) {

    case LINUX_PF_UNIX:
      {
        struct sockaddr_un addr;
        linux_to_native_sockaddr_un(&addr, (linux_sockaddr_un*)linux_addr, addrlen);

        int err = bind(s, (struct sockaddr*)&addr, sizeof(addr));
        if (err == 0) {
          // unlink(addr.sun_path); ?
        }

        return err;
      }

    case LINUX_PF_INET:
      {
        struct sockaddr_in addr;
        linux_to_native_sockaddr_in(&addr, (linux_sockaddr_in*)linux_addr, addrlen);
        return bind(s, (struct sockaddr*)&addr, sizeof(addr));
      }

    default:
      assert(0);
  }
}

int shim_connect_impl(int s, const linux_sockaddr* linux_name, socklen_t namelen) {

  switch (linux_name->sa_family) {

    case LINUX_PF_UNIX:
      {
        struct sockaddr_un addr;
        linux_to_native_sockaddr_un(&addr, (linux_sockaddr_un*)linux_name, namelen);
        LOG("%s: path = %s", __func__, addr.sun_path);
        return connect(s, (struct sockaddr*)&addr, sizeof(addr));
      }

    case LINUX_PF_INET:
      {
        struct sockaddr_in addr;
        linux_to_native_sockaddr_in(&addr, (linux_sockaddr_in*)linux_name, namelen);
        return connect(s, (struct sockaddr*)&addr, sizeof(addr));
      }

    default:
      assert(0);
  }
}

static void linux_to_native_msghdr(struct msghdr* msg, const struct linux_msghdr* linux_msg) {

  msg->msg_name    = linux_msg->msg_name;
  msg->msg_namelen = linux_msg->msg_namelen;
  msg->msg_iov     = linux_msg->msg_iov;
  msg->msg_iovlen  = linux_msg->msg_iovlen;
  msg->msg_flags   = linux_to_native_msg_flags(linux_msg->msg_flags);

  if (linux_msg->msg_controllen > 0) {

    assert(msg->msg_controllen >= linux_msg->msg_controllen);
    msg->msg_controllen = linux_msg->msg_controllen;

    memset(msg->msg_control, 0, linux_msg->msg_controllen);

    struct linux_cmsghdr* linux_cmsg = (struct linux_cmsghdr*)CMSG_FIRSTHDR(linux_msg);
    while (linux_cmsg != NULL) {
      struct cmsghdr* cmsg = (struct cmsghdr*)((uint8_t*)msg->msg_control + ((uint64_t)linux_cmsg - (uint64_t)linux_msg->msg_control));

      assert(linux_cmsg->cmsg_type == LINUX_SCM_RIGHTS);

      cmsg->cmsg_len   = linux_cmsg->cmsg_len;
      cmsg->cmsg_level = linux_to_native_sock_level(linux_cmsg->cmsg_level);
      cmsg->cmsg_type  = SCM_RIGHTS;

#ifdef __x86_64__
      memcpy((uint8_t*)cmsg + 16, (uint8_t*)linux_cmsg + 16, linux_cmsg->cmsg_len - 16);
#elif  __i386__
      memcpy((uint8_t*)cmsg + 12, (uint8_t*)linux_cmsg + 12, linux_cmsg->cmsg_len - 12);
#else
  #error
#endif

      linux_cmsg = (struct linux_cmsghdr*)CMSG_NXTHDR(linux_msg, linux_cmsg);
    }
  } else {
    msg->msg_control    = NULL;
    msg->msg_controllen = 0;
  }
}

static void native_to_linux_msghdr(struct linux_msghdr* linux_msg, const struct msghdr* msg) {

  linux_msg->msg_name    = msg->msg_name;
  linux_msg->msg_namelen = msg->msg_namelen;
  linux_msg->msg_iov     = msg->msg_iov;
  linux_msg->msg_iovlen  = msg->msg_iovlen;
  linux_msg->msg_flags   = native_to_linux_msg_flags(msg->msg_flags);

  if (msg->msg_controllen > 0) {

    assert(linux_msg->msg_controllen >= msg->msg_controllen);
    linux_msg->msg_controllen = msg->msg_controllen;

    memset(linux_msg->msg_control, 0, msg->msg_controllen);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(msg);
    while (cmsg != NULL) {
      struct linux_cmsghdr* linux_cmsg = (struct linux_cmsghdr*)((uint8_t*)linux_msg->msg_control + ((uint64_t)cmsg - (uint64_t)msg->msg_control));

      assert(cmsg->cmsg_type == SCM_RIGHTS);

      linux_cmsg->cmsg_len   = cmsg->cmsg_len;
      linux_cmsg->cmsg_level = native_to_linux_sock_level(cmsg->cmsg_level);
      linux_cmsg->cmsg_type  = LINUX_SCM_RIGHTS;

#ifdef __x86_64__
      memcpy((uint8_t*)linux_cmsg + 16, (uint8_t*)cmsg + 16, cmsg->cmsg_len - 16);
#elif  __i386__
      memcpy((uint8_t*)linux_cmsg + 12, (uint8_t*)cmsg + 12, cmsg->cmsg_len - 12);
#else
  #error
#endif

      cmsg = CMSG_NXTHDR(msg, cmsg);
    }

  } else {
    linux_msg->msg_control    = NULL;
    linux_msg->msg_controllen = 0;
  }
}

ssize_t shim_sendmsg_impl(int s, const struct linux_msghdr* linux_msg, int linux_flags) {

  struct msghdr msg;
  uint8_t buf[linux_msg->msg_controllen];

  msg.msg_control    = &buf;
  msg.msg_controllen = sizeof(buf);

  linux_to_native_msghdr(&msg, linux_msg);

  int err = sendmsg(s, &msg, linux_to_native_msg_flags(linux_flags));
  if (err == -1) {
    errno = native_to_linux_errno(errno);
  }

  return err;
}

ssize_t shim_recvmsg_impl(int s, struct linux_msghdr* linux_msg, int linux_flags) {

  struct msghdr msg;
  uint8_t buf[linux_msg->msg_controllen];

  msg.msg_name       = linux_msg->msg_name;
  msg.msg_namelen    = linux_msg->msg_namelen;
  msg.msg_iov        = linux_msg->msg_iov;
  msg.msg_iovlen     = linux_msg->msg_iovlen;
  msg.msg_control    = &buf;
  msg.msg_controllen = sizeof(buf);
  msg.msg_flags      = linux_to_native_msg_flags(linux_msg->msg_flags);

  int err = recvmsg(s, &msg, linux_to_native_msg_flags(linux_flags));
  if (err != -1) {
    native_to_linux_msghdr(linux_msg, &msg);
  } else {
    errno = native_to_linux_errno(errno);
  }

  return err;
}

ssize_t shim_recvfrom_impl(int s, void* buf, size_t len, int linux_flags, linux_sockaddr* restrict linux_from, socklen_t* restrict linux_fromlen) {

  int err;
  if (linux_from != NULL) {

    uint8_t   from[110]; // ?
    socklen_t fromlen = sizeof(from);

    err = recvfrom(s, buf, len, linux_to_native_msg_flags(linux_flags), (struct sockaddr*)&from, &fromlen);
    if (err != -1) {
      switch (((struct sockaddr*)&from)->sa_family) {
        case PF_INET: native_to_linux_sockaddr_in((linux_sockaddr_in*)linux_from, (struct sockaddr_in*)&from, fromlen); break;
        case PF_UNIX: native_to_linux_sockaddr_un((linux_sockaddr_un*)linux_from, (struct sockaddr_un*)&from, fromlen); break;
        default:
          assert(0);
      }
    }

  } else {
    err = recvfrom(s, buf, len, linux_to_native_msg_flags(linux_flags), NULL, linux_fromlen);
  }

  if (err == -1) {
    errno = native_to_linux_errno(errno);
  }

  return err;
}

ssize_t shim_sendto_impl(int s, const void* msg, size_t len, int linux_flags, const linux_sockaddr* linux_to, socklen_t tolen) {

  int err;
  switch (linux_to->sa_family) {

    case LINUX_PF_UNIX:
    {
      struct sockaddr_un to;
      linux_to_native_sockaddr_un(&to, (linux_sockaddr_un*)linux_to, tolen);
      err = sendto(s, msg, len, linux_to_native_msg_flags(linux_flags), (struct sockaddr*)&to, sizeof(to));
    }
    break;

    case LINUX_PF_INET:
    {
      struct sockaddr_in to;
      linux_to_native_sockaddr_in(&to, (linux_sockaddr_in*)linux_to, tolen);
      err = sendto(s, msg, len, linux_to_native_msg_flags(linux_flags), (struct sockaddr*)&to, sizeof(to));
    }
    break;

    default:
      assert(0);
  }

  if (err == -1) {
    errno = native_to_linux_errno(errno);
  }

  return err;
}

SHIM_WRAP(bind);
SHIM_WRAP(connect);
SHIM_WRAP(recvmsg);
SHIM_WRAP(sendmsg);
SHIM_WRAP(recvfrom);
SHIM_WRAP(sendto);
SHIM_WRAP(socket);
SHIM_WRAP(socketpair);

ssize_t shim___recv_chk_impl(int fd, void* buf, size_t len, size_t buflen, int flags) {
  assert(len <= buflen);
  return recv(fd, buf, len, flags);
}

SHIM_WRAP(__recv_chk);

static int linux_to_native_so_opt(int optname) {
  switch (optname) {
    case LINUX_SO_BROADCAST: return SO_BROADCAST;
    case LINUX_SO_SNDBUF:    return SO_SNDBUF;
    case LINUX_SO_RCVBUF:    return SO_RCVBUF;
    case LINUX_SO_KEEPALIVE: return SO_KEEPALIVE;
    default:
      assert(0);
  }
}

static int linux_to_native_tcp_opt(int optname) {
  switch (optname) {
    case LINUX_TCP_NODELAY:      return TCP_NODELAY;
    case LINUX_TCP_USER_TIMEOUT: return -1; // ?
    default:
      assert(0);
  }
}

int shim_getsockopt_impl(int s, int linux_level, int linux_optname, void* restrict optval, socklen_t* restrict optlen) {
  switch (linux_level) {
    case LINUX_SOL_SOCKET: return getsockopt(s, SOL_SOCKET,  linux_to_native_so_opt (linux_optname), optval, optlen);
    case LINUX_SOL_TCP:    return getsockopt(s, IPPROTO_TCP, linux_to_native_tcp_opt(linux_optname), optval, optlen);
    default:
      assert(0);
  }
}

int shim_setsockopt_impl(int s, int linux_level, int linux_optname, const void* optval, socklen_t optlen) {
  int err;
  switch (linux_level) {
    case LINUX_SOL_SOCKET:
      if (linux_optname == LINUX_SO_SNDBUF && optval && *((int*)optval) == 0) {
        err = 0; // ?
      } else {
        err = setsockopt(s, SOL_SOCKET,  linux_to_native_so_opt(linux_optname), optval, optlen);
      }
      break;
    case LINUX_SOL_TCP:
      err = setsockopt(s, IPPROTO_TCP, linux_to_native_tcp_opt(linux_optname), optval, optlen);
      break;
    default:
      assert(0);
  }
  return err;
}

SHIM_WRAP(getsockopt);
SHIM_WRAP(setsockopt);

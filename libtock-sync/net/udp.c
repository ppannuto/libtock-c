#include <string.h>

#include <libtock/defer.h>
#include <libtock/net/syscalls/udp_syscalls.h>

#include "syscalls/udp_syscalls.h"
#include "udp.h"

bool libtocksync_udp_exists(void) {
  return libtock_udp_driver_exists();
}

returncode_t libtocksync_udp_send(sock_handle_t* handle, void* buf, size_t len,
                                  sock_addr_t* dst_addr) {
  returncode_t ret;
  unsigned char buf_tx_cfg[2 * sizeof(sock_addr_t)];
  int bytes = sizeof(sock_addr_t);

  // The kernel requires the source half of this buffer to match the
  // currently bound address/port; the destination half is where we
  // actually want to send.
  memcpy(buf_tx_cfg, &(handle->addr), bytes);
  memcpy(buf_tx_cfg + bytes, dst_addr, bytes);

  // This buffer is only read by the kernel synchronously, inside the
  // command() call below, so it is safe for it to be stack-allocated here.
  // defer ensures it is un-allowed on every return path, since it is about
  // to go out of scope.
  ret = libtock_udp_set_readwrite_allow_cfg((void*) buf_tx_cfg, 2 * bytes);
  if (ret != RETURNCODE_SUCCESS) return ret;
  defer { libtock_udp_set_readwrite_allow_cfg(NULL, 0);
  }

  ret = libtock_udp_set_readonly_allow(buf, len);
  if (ret != RETURNCODE_SUCCESS) return ret;
  defer { libtock_udp_set_readonly_allow(NULL, 0);
  }

  ret = libtock_udp_command_send();
  if (ret != RETURNCODE_SUCCESS) return ret;

  return libtocksync_udp_yield_wait_for_send();
}

returncode_t libtocksync_udp_recv(void* buf, size_t len, size_t* received_len) {
  returncode_t ret;

  ret = libtock_udp_set_readwrite_allow_rx(buf, len);
  if (ret != RETURNCODE_SUCCESS) return ret;
  defer { libtock_udp_set_readwrite_allow_rx(NULL, 0);
  }

  ret = libtocksync_udp_yield_wait_for_recv(received_len);
  return ret;
}

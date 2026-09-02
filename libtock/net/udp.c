#include <string.h>

#include "../defer.h"
#include "syscalls/udp_syscalls.h"
#include "udp.h"

bool libtock_udp_exists(void) {
  return libtock_udp_driver_exists();
}

returncode_t libtock_udp_bind(sock_handle_t* handle, sock_addr_t* addr, unsigned char* buf_bind_cfg) {
  // Pass interface to listen on and space for kernel to write src addr
  // of received packets
  // In current design, buf_bind_cfg will still be written with addresses of external
  // senders sending addresses to the bound port even if the client application has
  // not set up a receive callback on this port. Of course, the client application
  // does not have to read these addresses or worry about them.
  returncode_t ret;

  memcpy(&(handle->addr), addr, sizeof(sock_addr_t));
  int bytes = sizeof(sock_addr_t);

  ret = libtock_udp_set_readwrite_allow_rx_cfg((void*) buf_bind_cfg, 2 * bytes);
  if (ret != RETURNCODE_SUCCESS) return ret;

  memcpy(buf_bind_cfg + bytes, &(handle->addr), bytes);

  return libtock_udp_command_bind();
}

returncode_t libtock_udp_close(__attribute__ ((unused)) sock_handle_t* handle) {
  returncode_t ret;
  // HACK!! This is not static.
  unsigned char zero_addr[2 * sizeof(sock_addr_t)] = {0};
  int bytes = sizeof(sock_addr_t);

  // call allow here to prevent any issues if close is called before bind
  // Driver 'closes' when 0 addr is bound to
  ret = libtock_udp_set_readwrite_allow_rx_cfg((void*) zero_addr, 2 * bytes);
  if (ret != RETURNCODE_SUCCESS) return ret;

  memset(zero_addr, 0, 2 * bytes);
  ret = libtock_udp_command_bind();
  if (ret != RETURNCODE_SUCCESS) return ret;

  ret = libtock_udp_set_readwrite_allow_rx_cfg(NULL, 0);
  if (ret != RETURNCODE_SUCCESS) return ret;

  // Remove the callback.
  return libtock_udp_set_upcall_frame_received(NULL, NULL);
}

static void udp_send_done_upcall(int                          status,
                                 __attribute__ ((unused)) int unused1,
                                 __attribute__ ((unused)) int unused2,
                                 void*                        opaque) {
  libtock_udp_callback_send_done cb = (libtock_udp_callback_send_done) opaque;
  cb(tock_status_to_returncode(status));
}

returncode_t libtock_udp_send(sock_handle_t* handle, void* buf, size_t len,
                              sock_addr_t* dst_addr, libtock_udp_callback_send_done cb) {
  returncode_t ret;
  unsigned char buf_tx_cfg[2 * sizeof(sock_addr_t)];

  // The kernel requires the source half of this buffer to match the
  // currently bound address/port; the destination half is where we
  // actually want to send.
  // NOTE: bind() must be called previously for this to work.
  // If bind() has not been called, command(COMMAND_SEND) will return RESERVE.
  int bytes = sizeof(sock_addr_t);
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

  // Set message buffer
  ret = libtock_udp_set_readonly_allow(buf, len);
  if (ret != RETURNCODE_SUCCESS) return ret;

  ret = libtock_udp_set_upcall_frame_transmitted(udp_send_done_upcall, cb);
  if (ret != RETURNCODE_SUCCESS) return ret;

  return libtock_udp_command_send();
}

static void udp_recv_done_upcall(int                          length,
                                 __attribute__ ((unused)) int unused1,
                                 __attribute__ ((unused)) int unused2,
                                 void*                        opaque) {
  libtock_udp_callback_recv_done cb = (libtock_udp_callback_recv_done) opaque;
  cb(RETURNCODE_SUCCESS, length);
}

returncode_t libtock_udp_recv(void* buf, size_t len, libtock_udp_callback_recv_done cb) {
  // TODO: verify that this functions returns error if this app is not
  // bound to a socket yet. Probably allow should fail..?
  returncode_t ret;

  ret = libtock_udp_set_readwrite_allow_rx((void*) buf, len);
  if (ret != RETURNCODE_SUCCESS) return ret;

  return libtock_udp_set_upcall_frame_received(udp_recv_done_upcall, cb);
}

returncode_t libtock_udp_list_ifaces(ipv6_addr_t* ifaces, size_t len) {
  if (!ifaces) return RETURNCODE_EINVAL;

  returncode_t ret = libtock_udp_set_readwrite_allow_cfg((void*)ifaces, len * sizeof(ipv6_addr_t));
  if (ret != RETURNCODE_SUCCESS) return ret;

  return libtock_udp_command_get_ifaces(len);
}

returncode_t libtock_udp_get_max_tx_len(int* length) {
  return libtock_udp_command_get_max_tx_len(length);
}

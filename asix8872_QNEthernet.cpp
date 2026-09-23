/*
  Copyright (C) 2024 Andrew Dunstan
  This file is part of teensy4_usbhost (https://github.com/A-Dunstan/teensy4_usbhost).

  teensy4_usbhost is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <asix88772_QNEthernet.h>
#include "asix88772.h"

#ifdef QNETHERNET_EXTERNAL_DRIVER_ASIX88772

#include <qnethernet/lwip_driver.h>

#pragma message("Using ASIX88772 QNEthernet Driver")

static asix88772_eth& asix88772(void) {
  DMAMEM static asix88772_eth asix_device;
  return asix_device;
}

namespace qindesign {
namespace network {
namespace driver {

FLASHMEM void get_capabilities(DriverCapabilities* const dc) {
  dc->isMACSettable                = true;
  dc->isLinkStateDetectable        = true;
  dc->isLinkSpeedDetectable        = true;
  dc->isLinkSpeedSettable          = true;
  dc->isLinkFullDuplexDetectable   = true;
  dc->isLinkFullDuplexSettable     = true;
  dc->isAutoNegotiationSettable    = true;
  dc->isLinkCrossoverDetectable    = false;
  dc->isAutoNegotiationRestartable = true;
  dc->isPHYResettable              = true;
}

bool is_unknown() {
  return false;
}

extern "C" void qnethernet_hal_get_system_mac_address(uint8_t mac[ETH_HWADDR_LEN]);

void get_system_mac(uint8_t mac[ETH_HWADDR_LEN]) {
  asix88772().get_mac(mac);
}

bool get_mac(uint8_t mac[ETH_HWADDR_LEN]) {
  return asix88772().get_mac(mac);
}

bool set_mac(const uint8_t mac[ETH_HWADDR_LEN]) {
  return asix88772().set_mac(mac);
}

bool has_hardware() {
  return true;
}

void set_chip_select_pin(const int) {}

bool init() {
  asix88772();
  return true;
}

void deinit() {
}

struct pbuf* proc_input(struct netif* const netif, const int) {
//  dprintf("PROC_INPUT\n");
  struct pbuf* p = NULL;
  asix88772_eth::read_buffer *buf;
  size_t length;
  if (asix88772().get_read(buf, length)) {
    if (length) {
      p = pbuf_alloc(PBUF_RAW, length-4+ETH_PAD_SIZE, PBUF_POOL);
      if (p) pbuf_take(p, buf->data+4-ETH_PAD_SIZE, p->tot_len);
    }

    asix88772().submit_read_buffer(*buf);
//    dprintf("proc_input buf %u bytes\n", length-4);
  }
  return p;
}

void poll(struct netif* const netif) {
//  dprintf("POLL\n");
  bool up = asix88772().loop();
  if (netif_is_link_up(netif) != up) {
    if (up) netif_set_link_up(netif);
    else netif_set_link_down(netif);
  }
}

void get_link_info(LinkInfo* const li) {
  dprintf("GET_LINK_INFO\n");
}

bool set_link(const LinkSettings* const ls) {
  dprintf("SET_LINK\n");
  return true;
}

err_t output(struct pbuf* const p) {
  pbuf_ref(p);
  auto tx_buf = pbuf_coalesce(p, PBUF_RAW_TX);
  uint16_t frame_length[2] = { tx_buf->tot_len, (uint16_t)~tx_buf->tot_len };

  int ret = pbuf_add_header(tx_buf, 4);
  if (ret == ERR_OK) {
    ret = pbuf_take(tx_buf, frame_length, 4);
    if (ret == ERR_OK)
      ret = asix88772().output_frame(tx_buf->payload, tx_buf->tot_len) ? ERR_OK:-1;
    pbuf_remove_header(tx_buf, 4);
  }

  pbuf_free(tx_buf);
  return ret;
}

//#if QNETHERNET_ENABLE_RAW_FRAME_SUPPORT
bool output_frame(const void* const frame, const size_t len) {
  bool ret = false;

  auto p = new(std::nothrow) uint8_t[len+4];
  if (p) {
    p[0] = len;
    p[1] = len >> 8;
    p[2] = ~len;
    p[3] = (~len) >> 8;
    memcpy(p+4, frame, len);
    ret = asix88772().output_frame(p, len+4);
    delete[] p;
  }

  return ret;
}
//#endif

#if !QNETHERNET_ENABLE_PROMISCUOUS_MODE
bool set_incoming_mac_address_allowed(const uint8_t mac[ETH_HWADDR_LEN], const bool allow) {
  return asix88772().filter_address(mac, allow);
}
#endif

void notify_manual_link_state(const bool) {
  dprintf("NOTIFY_MANUAL_LINK_STATE\n");
}

void restart_auto_negotiation() {
  dprintf("RESTART_AUTO_NEGOTIATION\n");
  asix88772().restart_auto_negotiation();
}

void reset_phy() {
  dprintf("RESET_PHY\n");
  asix88772().reset_phy();
}

}}}

#endif

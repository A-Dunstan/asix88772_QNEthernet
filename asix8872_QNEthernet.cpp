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

namespace qindesign {
namespace network {
namespace driver {

static asix88772_eth& asix88772();

class rx_buffer : public pbuf_custom, public asix88772_eth::read_buffer {
private:
  class rx_ref : public pbuf_custom {
  private:
    rx_buffer& ref;

    static void rx_ref_free(struct pbuf* pb) {
      delete reinterpret_cast<rx_ref*>(pb);
    }

    ~rx_ref() {
      pbuf_free(&ref.pbuf);
    }
    rx_ref(rx_buffer& _ref) : ref(_ref) {
      pbuf_ref(&ref.pbuf);
      custom_free_function = rx_ref_free;
    }
  public:
    static struct pbuf* create(rx_buffer& ref, uint16_t length) {
      struct pbuf* p = NULL;
      auto r = new rx_ref(ref);
      if (r) {
        p = pbuf_alloced_custom(PBUF_RAW, length, PBUF_ROM, r, ref.pbuf.payload, ref.pbuf.tot_len);
        if (p == NULL) delete r;
      }
      return p;
    }
  };

  static void rx_free(struct pbuf* pb) {
    reinterpret_cast<rx_buffer*>(pb)->submit();
  }

public:
  void submit() {
    asix88772().submit_read_buffer(*this);
  }

  rx_buffer() {
    custom_free_function = rx_free;
    submit();
  }

  struct pbuf* get_ref(uint16_t length) {
    return rx_ref::create(*this, length);
  }
};

static asix88772_eth& asix88772() {
  DMAMEM static asix88772_eth asix_device;
  return asix_device;
}

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
  static rx_buffer rx[asix88772_eth::max_input_buffers()] DMAMEM;
  asix88772().setPHYPower(true);
  return true;
}

void deinit() {
  asix88772().setPHYPower(false);
}

struct pbuf* proc_input(struct netif* const netif, const int) {
  static struct pbuf* head = NULL;
  struct pbuf* ret = NULL;

  struct {
    uint16_t len;
    uint16_t nlen;
    bool valid() { return (len ^ nlen) == 0xFFFF; }
  } fl = {};

  size_t length;
  asix88772_eth::read_buffer *buf;
  while (asix88772().get_read(buf, length)) {
    auto rx = static_cast<rx_buffer*>(buf);
    auto p = pbuf_alloced_custom(PBUF_RAW, length, PBUF_POOL, rx, rx->data, sizeof(rx->data));
    if (head == NULL) head = p;
    else pbuf_cat(head, p);
  }

  while (head) {
    if (pbuf_copy_partial(head, &fl, sizeof(fl), 0) == sizeof(fl)) {
      if (!fl.valid()) {
        // desynchronized, eat 2 bytes and retry
        head = pbuf_free_header(head, 2);
        continue;
      }

      uint32_t len = 4 + (fl.len & 0x7FF) + (fl.len&1);
      if (head->tot_len >= len) {
        // eat the frame length header
        head = pbuf_free_header(head, sizeof(fl));

        if (fl.len & 0xF800) {
          // some sort of error in the frame, discard it
          head = pbuf_free_header(head, len-4);
          continue;
        }
        // else process it
        break;
      }
    }

    // need more data, abort
    return ret;
  }

  if (head) {
    uint16_t l = fl.len + (fl.len&1);
    if (head->len < l ) {
      // data spans multiple read buffers, coalesce into one
      // (because lwip is full of functions that assume the entire frame is in one pbuf)
      ret = pbuf_alloc(PBUF_RAW, fl.len, PBUF_POOL);
      if (ret) pbuf_copy_partial_pbuf(ret, head, fl.len, 0);
    } else if (head->len > l) {
      // head contains the whole packet, but also contains data following
      // -> use a reference
      ret = reinterpret_cast<rx_buffer*>(head)->get_ref(fl.len);
    } else {
      // head is exactly the right size (maybe has one extra byte that will be ignored
      ret = head;
      pbuf_ref(head);
    }

    head = pbuf_free_header(head, l);
  }

  return ret;
}

void poll(struct netif* const netif) {
  auto link_up = asix88772().loop();
  if (link_up != netif_is_link_up(netif)) {
    if (link_up) netif_set_link_up(netif);
    else netif_set_link_down(netif);
  }
}

void get_link_info(LinkInfo* const li) {
  li->speed = asix88772().get100mbps() ? 100 : 10;
  li->fullNotHalfDuplex = asix88772().getFullDuplex();
  li->isAutoNegotiation = asix88772().getAutoNegotiation();
}

bool set_link(const LinkSettings* const ls) {
  if (ls->speed != 10 && ls->speed != 100)
    return false;

  asix88772().set100mbps(ls->speed == 100);
  asix88772().setFullDuplex(ls->fullNotHalfDuplex);
  asix88772().setAutoNegotiation(ls->autoNegotiation);
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

#if QNETHERNET_ENABLE_RAW_FRAME_SUPPORT
bool output_frame(const void* const frame, const size_t len) {
  bool ret = false;
  if (len <= 65535) {
    // add a 4 byte header using a buffer that is multiple of the packet size
    auto p = new(std::nothrow) uint8_t[512];
    if (p) {
      p[0] = len;
      p[1] = len >> 8;
      p[2] = ~len;
      p[3] = (~len) >> 8;
      auto front_len = std::min(len, (size_t)512-4);
      memcpy(p+4, frame, front_len);
      ret = asix88772().output_frame(p, front_len+4);
      if (ret && len > front_len)
        ret = asix88772().output_frame((const uint8_t*)frame+front_len, len-front_len);

      delete[] p;
    }
  }

  return ret;
}
#endif

#if !QNETHERNET_ENABLE_PROMISCUOUS_MODE
bool set_incoming_mac_address_allowed(const uint8_t mac[ETH_HWADDR_LEN], const bool allow) {
  return asix88772().filter_address(mac, allow);
}
#endif

void notify_manual_link_state(const bool state) {
  asix88772().setPHYPower(state);
}

void restart_auto_negotiation() {
  asix88772().restart_auto_negotiation();
}

void reset_phy() {
  asix88772().reset_phy();
}

}}}

#endif

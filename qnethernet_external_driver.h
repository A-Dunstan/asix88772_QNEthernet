#pragma once

#define MTU           1500
#define MAX_FRAME_LEN 1518  /* Does not include the 4-byte FCS (frame check sequence) */

#define QNETHERNET_EXTERNAL_DRIVER_ASIX88772

// 4 bytes for frame length + parity, 2 bytes padding
#define PBUF_LINK_ENCAPSULATION_HLEN    6

#define LWIP_SUPPORT_CUSTOM_PBUF 1

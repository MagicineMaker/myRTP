#ifndef __PACKET_H
#define __PACKET_H

#include "rtp.h"
#include "util.h"
#include <string.h>

void packet_init(rtp_packet_t *packet, uint16_t data_length, uint32_t seq_num, uint8_t flags, const char* data);
uint8_t check_flag(rtp_packet_t *packet, rtp_header_flag_t rtpFlag);
uint32_t diff(uint32_t a, uint32_t b, uint32_t w);

#endif //__PACKET_H
#include "packet.h"

void packet_init(rtp_packet_t *packet, uint16_t data_length, uint32_t seq_num, uint8_t flags, const char* data) {
    memset((char*)packet, 0, sizeof(rtp_packet_t));

    if(data && data_length)
        memcpy(packet->payload, data, data_length);

    packet->rtp.seq_num = seq_num;
    packet->rtp.length = data_length;
    packet->rtp.flags = flags;

    uint32_t checksum = compute_checksum(packet, packet->rtp.length + sizeof(rtp_header_t));
    packet->rtp.checksum = checksum;
}

uint8_t check_flag(rtp_packet_t *packet, rtp_header_flag_t rtpFlag) {
    return packet->rtp.flags & rtpFlag;
}

uint32_t diff(uint32_t a, uint32_t b, uint32_t w) {
    if(a >= b)
        return a - b;
    else
        return a + w - b;
}
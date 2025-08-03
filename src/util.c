#include "util.h"

static uint32_t crc32_for_byte(uint32_t r) {
    for (int j = 0; j < 8; ++j) r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
    return r ^ (uint32_t)0xFF000000L;
}

static void crc32(const void* data, size_t n_bytes, uint32_t* crc) {
    static uint32_t table[0x100];
    if (!*table)
        for (size_t i = 0; i < 0x100; ++i) table[i] = crc32_for_byte(i);
    for (size_t i = 0; i < n_bytes; ++i)
        *crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
}

// Computes checksum for `n_bytes` of data
//
// Hint 1: Before computing the checksum, you should set everything up
// and set the "checksum" field to 0. And when checking if a packet
// has the correct check sum, don't forget to set the "checksum" field
// back to 0 before invoking this function.
//
// Hint 2: `len + sizeof(rtp_header_t)` is the real length of a rtp
// data packet.
uint32_t compute_checksum(const void* pkt, size_t n_bytes) {
    uint32_t crc = 0;
    crc32(pkt, n_bytes, &crc);
    return crc;
}

int examine_checksum(rtp_packet_t *packet) {
    rtp_packet_t *testPacket = (rtp_packet_t*)malloc(sizeof(rtp_packet_t));
    memcpy((char*)testPacket, (char*)packet, sizeof(rtp_packet_t));
    testPacket->rtp.checksum = 0;

    uint32_t checksum = compute_checksum(testPacket, LENGTH(testPacket));
    
    free(testPacket);
    if(checksum != packet->rtp.checksum)
        return 0;
    else
        return 1;
}

int wrapped_recvfrom(int sockfd, rtp_packet_t *buffer, int tv_sec, int tv_usec, struct sockaddr_in *writeAddr, socklen_t *writeLen) {
    struct timeval timeout;
    timeout.tv_sec = tv_sec;
    timeout.tv_usec = tv_usec;
    
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(sockfd, &readSet);

    int ready = select(sockfd + 1, &readSet, NULL, NULL, &timeout);

    if(ready == 0)
        return -1;
    else if(ready > 0) {
        memset((char*)buffer, 0, sizeof(rtp_packet_t));

        int bytesRead;
        if(!writeAddr && !writeLen)
            bytesRead = recvfrom(sockfd, (char*)buffer, sizeof(rtp_packet_t), 0, NULL, NULL);
        else {
            *writeLen = sizeof(struct sockaddr_in);
            bytesRead = recvfrom(sockfd, (char*)buffer, sizeof(rtp_packet_t), 0, writeAddr, writeLen);
        }
        if(bytesRead < 0 || LENGTH(buffer) != bytesRead || !examine_checksum(buffer))
            return -1;
        else
            return bytesRead;
    }
    else
        LOG_FATAL("Error in select\n");
}

int is_timeout(struct timeval *tv, struct timeval *tv_s, long long t) {
    if(1e6 * tv->tv_sec + tv->tv_usec > 1e6 * tv_s->tv_sec + tv_s->tv_usec + t)
        return 1;
    else
        return 0;
}
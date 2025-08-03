#include "packet.h"

rtp_packet_t *sendPacket, *recvPacket;
int bytesSent, bytesRead;

struct sockaddr_in *serverAddress;
rtp_packet_t *window;
uint32_t windowSize;

uint32_t x;

FILE *fp;

int udp_clientSocket(void);
void setup_serverAddress(struct sockaddr_in *serverAddress, char *addr, port_t port);
void connect_to_server(int clientSocket, struct sockaddr_in *serverAddress, uint32_t seq_num);
void close_connection(int clientSocket, struct sockaddr_in *serverAddress, uint32_t seq_num);
void fill_in_window(uint32_t *endSeqNum, uint32_t lower, uint32_t upper, int *acked);

uint32_t go_back_N(int sockfd, struct sockaddr_in *serverAddress, uint32_t seq_num);
uint32_t selective_repeat(int sockfd, struct sockaddr_in *serverAddress, uint32_t seq_num);

int main(int argc, char **argv) {
    if (argc != 6) {
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }


    // Init
    char *addr = argv[1];
    port_t port = atoi(argv[2]);
    char *filePath = argv[3];
    windowSize = atoi(argv[4]);
    int mode = atoi(argv[5]);
    fp = fopen(filePath, "r");

    window = (rtp_packet_t*)malloc(windowSize * sizeof(rtp_packet_t));
    memset(window, 0, windowSize * sizeof(rtp_packet_t));

    sendPacket = (rtp_packet_t*)malloc(sizeof(rtp_packet_t));
    recvPacket = (rtp_packet_t*)malloc(sizeof(rtp_packet_t));
    serverAddress = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));


    // Set up
    int clientSocket = udp_clientSocket();
    setup_serverAddress(serverAddress, addr, port);


    // Connect
    x = rand();
    connect_to_server(clientSocket, serverAddress, x);


    // Send data
    uint32_t endSeqNum;
    if(mode == 0)
        endSeqNum = go_back_N(clientSocket, serverAddress, x + 1);
    else
        endSeqNum = selective_repeat(clientSocket, serverAddress, x + 1);


    // Close connection
    close_connection(clientSocket, serverAddress, endSeqNum);


    close(clientSocket);
    free(window);
    free(sendPacket);
    free(recvPacket);
    free(serverAddress);
    fclose(fp);
    LOG_DEBUG("MySender: exiting...\n");
    return 0;
}

int udp_clientSocket(void) {
    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket == -1) {
        printf("Error creating socket\n");
        return -1;
    }
    return clientSocket;
}

void setup_serverAddress(struct sockaddr_in *serverAddress, char *addr, port_t port) {
    memset(serverAddress, 0, sizeof(struct sockaddr_in));
    serverAddress->sin_port = htons(port);
    serverAddress->sin_family = AF_INET;
    inet_pton(AF_INET, addr, &serverAddress->sin_addr);
}

void connect_to_server(int clientSocket, struct sockaddr_in *serverAddress, uint32_t seq_num) {
    packet_init(sendPacket, 0, seq_num, RTP_SYN, NULL);
    int i = 0;
    while(i < MAX_RETRY) {
        bytesSent = sendto(clientSocket, (char*)sendPacket, LENGTH(sendPacket), 0,
            (struct sockaddr*)serverAddress, sizeof(struct sockaddr_in));
        if(bytesSent < 0)
            LOG_FATAL("MySender: Unable to send syn\n");

        bytesRead = wrapped_recvfrom(clientSocket, recvPacket, 0, 1e5, NULL, NULL);
        if(bytesRead > 0 && recvPacket->rtp.seq_num == seq_num + 1
            && recvPacket->rtp.flags == (RTP_SYN | RTP_ACK))
            break;

        i++;
    }
    if(i >= MAX_RETRY)
        LOG_FATAL("MySender: Timeout waiting for synack\n");

    i = 0;
    packet_init(sendPacket, 0, seq_num + 1, RTP_ACK, NULL);
    while(i < MAX_RETRY) {
        bytesSent = sendto(clientSocket, (char*)sendPacket, sizeof(rtp_header_t), 0,
            (struct sockaddr*)serverAddress, sizeof(struct sockaddr_in));
        if(bytesSent < 0)
            LOG_FATAL("MySender: Unable to send ack\n");

        bytesRead = wrapped_recvfrom(clientSocket, recvPacket, 2, 0, NULL, NULL);
        if(bytesRead < 0 || recvPacket->rtp.seq_num != seq_num + 1
            || recvPacket->rtp.flags != (RTP_SYN | RTP_ACK))
            break;

        i++;
    }
    if(i >= MAX_RETRY)
        LOG_FATAL("MySender: Max attempts to send ack\n");

    LOG_MSG("MySender: Connection built\n");
}

void fill_in_window(uint32_t *endSeqNum, uint32_t lower, uint32_t upper, int *acked) {
    char buffer[PAYLOAD_MAX];
    for(uint32_t i = lower; i < upper; ++i) {
        uint32_t pos = i % windowSize;

        memset(buffer, 0, PAYLOAD_MAX);
        int size = fread(buffer, sizeof(char), PAYLOAD_MAX, fp);
        packet_init(&window[pos], size, i, 0, buffer);

        if(acked)
            acked[pos] = 0;

        if(feof(fp)) {
            *endSeqNum = i + 1;
            return;
        }
    }
    *endSeqNum = upper;
}

void close_connection(int clientSocket, struct sockaddr_in *serverAddress, uint32_t seq_num) {
    packet_init(sendPacket, 0, seq_num, RTP_FIN, NULL);
    struct timeval tv, tv_s;
    int i = 0;
    while(i < MAX_RETRY) {
        bytesSent = sendto(clientSocket, (char*)sendPacket, LENGTH(sendPacket), 0,
            (struct sockaddr*)serverAddress, sizeof(struct sockaddr_in));
        if(bytesSent < 0)
            LOG_FATAL("MySender: Unable to send fin\n");

        gettimeofday(&tv_s, NULL);

    receive_packet:
        bytesRead = wrapped_recvfrom(clientSocket, recvPacket, 0, 1e5, NULL, NULL);
        if(bytesRead < 0 || recvPacket->rtp.seq_num != seq_num || recvPacket->rtp.flags != (RTP_FIN | RTP_ACK)) {
            gettimeofday(&tv, NULL);
            if(is_timeout(&tv, &tv_s, 1e5))
                ++i;
            else
                goto receive_packet;
        }
        else
            break;
    }
    if(i >= MAX_RETRY)
        LOG_FATAL("MySender: Timeout waiting for finack\n");

    LOG_MSG("MySender: Connection closed\n");
}

uint32_t go_back_N(int clientSocket, struct sockaddr_in *serverAddress, uint32_t seq_num) {
    uint32_t startSeqNum = seq_num, endSeqNum = startSeqNum;
    fill_in_window(&endSeqNum, seq_num, seq_num + windowSize, NULL);

    int retry = 0;
    struct timeval tv, tv_s;
    while(1) {
        for(uint32_t i = startSeqNum; i < endSeqNum; ++i) {
            uint32_t pos = i % windowSize;
            sendto(clientSocket, (char*)(&window[pos]), LENGTH((&window[pos])), 0,
                (struct sockaddr*)serverAddress, sizeof(struct sockaddr_in));
        }
        gettimeofday(&tv_s, NULL);

    receive_packet:
        bytesRead = wrapped_recvfrom(clientSocket, recvPacket, 0, 1e5, NULL, NULL);
        if(bytesRead < 0 || recvPacket->rtp.seq_num <= startSeqNum || recvPacket->rtp.seq_num > endSeqNum || recvPacket->rtp.flags != RTP_ACK) {
            gettimeofday(&tv, NULL);
            if(is_timeout(&tv, &tv_s, 1e5)) {
                if(retry < MAX_RETRY) {
                    retry++;
                    continue;
                }
                else
                    LOG_FATAL("MySender: Max attempts resending data\n");
            }
            else
                goto receive_packet;
        }

        retry = 0;
        startSeqNum = recvPacket->rtp.seq_num;
        if(feof(fp) && startSeqNum >= endSeqNum) {
            LOG_MSG("MySender: Sending completed\n");
            break;
        }
        if(!feof(fp))
            fill_in_window(&endSeqNum, endSeqNum, startSeqNum + windowSize, NULL);
    }

    return endSeqNum;
}

uint32_t selective_repeat(int clientSocket, struct sockaddr_in *serverAddress, uint32_t seq_num) {
    int *acked = (int*)malloc(windowSize * sizeof(int));
    memset((char*)acked, 0, windowSize * sizeof(int));

    uint32_t startSeqNum = seq_num, endSeqNum = startSeqNum;
    fill_in_window(&endSeqNum, seq_num, seq_num + windowSize, acked);

    int retry = 0;
    struct timeval tv, tv_s;
    while(1) {
        for(uint32_t i = startSeqNum; i < endSeqNum; ++i) {
            uint32_t pos = i % windowSize;
            if(!acked[pos]) {
                sendto(clientSocket, (char*)(&window[pos]), LENGTH((&window[pos])), 0,
                    (struct sockaddr*)serverAddress, sizeof(struct sockaddr_in));
            }
        }
        gettimeofday(&tv_s, NULL);

    receive_packet:
        bytesRead = wrapped_recvfrom(clientSocket, recvPacket, 0, 1e5, NULL, NULL);
        if(bytesRead < 0 || recvPacket->rtp.seq_num < startSeqNum || recvPacket->rtp.seq_num >= endSeqNum || recvPacket->rtp.flags != RTP_ACK) {
            gettimeofday(&tv, NULL);
            if(is_timeout(&tv, &tv_s, 1e5)) {
                if(retry < MAX_RETRY) {
                    retry++;
                    continue;
                }
                else
                    LOG_FATAL("MySender: Max attempts resending data\n");
            }
            else
                goto receive_packet;
        }
        else if(recvPacket->rtp.seq_num > startSeqNum) {
            acked[recvPacket->rtp.seq_num % windowSize] = 1;
            gettimeofday(&tv, NULL);
            if(is_timeout(&tv, &tv_s, 1e5)) {
                if(retry < MAX_RETRY) {
                    retry++;
                    continue;
                }
                else
                    LOG_FATAL("MySender: Max attempts resending data\n");
            }
            else
                goto receive_packet;
        }

        acked[startSeqNum % windowSize] = 1;
        retry = 0;

        while(startSeqNum < endSeqNum && acked[startSeqNum % windowSize])
            startSeqNum++;

        if(feof(fp) && startSeqNum >= endSeqNum) {
            LOG_MSG("MySender: Sending completed\n");
            break;
        }
        if(!feof(fp))
            fill_in_window(&endSeqNum, endSeqNum, startSeqNum + windowSize, acked);
    }

    free(acked);
    return endSeqNum;
}
#include "packet.h"

rtp_packet_t *sendPacket, *recvPacket;
int bytesSent, bytesRead;

struct sockaddr_in *listenAddress;
struct sockaddr_in *clientAddress;
socklen_t *clientAddressLen;
rtp_packet_t *window;
uint32_t windowSize;

uint32_t x;

FILE *fp;

int udp_serverSocket(struct sockaddr_in *listenAddress, port_t port);
void accept_connection(int serverSocket, struct sockaddr_in *clientAddress, socklen_t *clientAddressLen, uint32_t *seq_num);
void close_connection(int serverSocket, struct sockaddr_in *clientAddress, uint32_t seq_num);

uint32_t go_back_N(int serverSocket, struct sockaddr_in *clientAddress, uint32_t seq_num);
uint32_t selective_repeat(int serverSocket, struct sockaddr_in *clientAddress, uint32_t seq_num);

int main(int argc, char **argv) {
    if (argc != 5) {
        LOG_FATAL("Usage: ./receiver [listen port] [file path] [window size] "
                  "[mode]\n");
    }

    port_t port = atoi(argv[1]);
    char *filePath = argv[2];
    windowSize = atoi(argv[3]);
    int mode = atoi(argv[4]);

    fp = fopen(filePath, "w");

    window = (rtp_packet_t*)malloc(windowSize * sizeof(rtp_packet_t));
    memset(window, 0, windowSize * sizeof(rtp_packet_t));

    sendPacket = (rtp_packet_t*)malloc(sizeof(rtp_packet_t));
    recvPacket = (rtp_packet_t*)malloc(sizeof(rtp_packet_t));
    listenAddress = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
    clientAddress = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
    clientAddressLen = (socklen_t*)malloc(sizeof(socklen_t));


    // Set up the socket
    int serverSocket = udp_serverSocket(listenAddress, port);
    

    // Connect
    accept_connection(serverSocket, clientAddress, clientAddressLen, &x);


    // Receive data
    uint32_t endSeqNum;
    if(mode == 0)
        endSeqNum = go_back_N(serverSocket, clientAddress, x);
    else
        endSeqNum = selective_repeat(serverSocket, clientAddress, x);


    // Close connection
    close_connection(serverSocket, clientAddress, endSeqNum);


    free(window);
    free(sendPacket);
    free(recvPacket);
    free(listenAddress);
    free(clientAddress);
    free(clientAddressLen);
    close(serverSocket);
    fclose(fp);
    LOG_DEBUG("Receiver: exiting...\n");
    return 0;
}

int udp_serverSocket(struct sockaddr_in *listenAddress, port_t port) {
    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    memset(listenAddress, 0, sizeof(struct sockaddr_in));
    listenAddress->sin_family = AF_INET;
    listenAddress->sin_addr.s_addr = htonl(INADDR_ANY);
    listenAddress->sin_port = htons(port);
    bind(serverSocket, listenAddress, sizeof(struct sockaddr_in));

    return serverSocket;
}

void accept_connection(int serverSocket, struct sockaddr_in *clientAddress, socklen_t *clientAddressLen, uint32_t *seq_num) {
    struct timeval tv_s, tv;
    gettimeofday(&tv_s, NULL);

    receive_syn:
    bytesRead = wrapped_recvfrom(serverSocket, recvPacket, 5, 0, clientAddress, clientAddressLen);
    if(bytesRead < 0 || recvPacket->rtp.flags != RTP_SYN) {
        gettimeofday(&tv, NULL);
        if(is_timeout(&tv, &tv_s, 5e6))
            LOG_FATAL("MyReceiver: Timeout waiting for syn\n");
        else
            goto receive_syn;
    }

    *seq_num = recvPacket->rtp.seq_num + 1;
    packet_init(sendPacket, 0, *seq_num, RTP_SYN | RTP_ACK, NULL);

    int i = 0;
    while(i < MAX_RETRY) {
        bytesSent = sendto(serverSocket, (char*)sendPacket, LENGTH(sendPacket), 0,
            (struct sockaddr*)clientAddress, sizeof(struct sockaddr_in));
        if(bytesSent < 0)
            LOG_FATAL("MyReceiver: Unable to send synack\n");

        bytesRead = wrapped_recvfrom(serverSocket, recvPacket, 0, 1e5, NULL, NULL);
        if(bytesRead > 0 && recvPacket->rtp.seq_num == *seq_num
            && recvPacket->rtp.flags == RTP_ACK)
            break;

        i++;
    }
    if(i >= MAX_RETRY)
        LOG_FATAL("MyReceiver: Timeout waiting for ack\n");

    LOG_MSG("MyReceiver: Connection built\n");
}

void close_connection(int serverSocket, struct sockaddr_in *clientAddress, uint32_t seq_num) {
    int i = 0;
    packet_init(sendPacket, 0, seq_num, RTP_FIN | RTP_ACK, NULL);
    while(i < MAX_RETRY) {
        bytesSent = sendto(serverSocket, (char*)sendPacket, sizeof(rtp_header_t), 0,
            (struct sockaddr*)clientAddress, sizeof(struct sockaddr_in));
        if(bytesSent < 0)
            LOG_FATAL("MyReceiver: Unable to send finack\n");

        bytesRead = wrapped_recvfrom(serverSocket, recvPacket, 2, 0, NULL, NULL);
        if(bytesRead < 0 || recvPacket->rtp.seq_num != seq_num || recvPacket->rtp.flags != RTP_FIN)
            break;

        i++;
    }
    if(i >= MAX_RETRY)
        LOG_FATAL("MyReceiver: Max attempts to send finack\n");

    LOG_MSG("MyReceiver: Connection closed\n");
}

uint32_t go_back_N(int serverSocket, struct sockaddr_in *clientAddress, uint32_t seq_num) {
    uint32_t expectedSeqNum = seq_num;
    struct timeval tv_s, tv;

    while(1) {
        gettimeofday(&tv_s, NULL);
    receive_packet:
        bytesRead = wrapped_recvfrom(serverSocket, recvPacket, 0, 1e5, clientAddress, clientAddressLen);
        if(bytesRead > 0 && recvPacket->rtp.flags == RTP_FIN && recvPacket->rtp.seq_num == expectedSeqNum)
            break;
        else if(bytesRead < 0 || recvPacket->rtp.flags != 0) {
            gettimeofday(&tv, NULL);
            if(is_timeout(&tv, &tv_s, 5e6))
                LOG_FATAL("MyReceiver: Timeout waiting for more data\n");
            else 
                goto receive_packet;
        }
        else if(recvPacket->rtp.seq_num != expectedSeqNum) {
            packet_init(sendPacket, 0, expectedSeqNum, RTP_ACK, NULL);
            sendto(serverSocket, (char*)sendPacket, LENGTH(sendPacket), 0,
                (struct sockaddr*)clientAddress, sizeof(struct sockaddr_in));
            goto receive_packet;
        }

        fwrite(recvPacket->payload, sizeof(char), recvPacket->rtp.length, fp);

        expectedSeqNum++;
        packet_init(sendPacket, 0, expectedSeqNum, RTP_ACK, NULL);
        sendto(serverSocket, (char*)sendPacket, LENGTH(sendPacket), 0,
            (struct sockaddr*)clientAddress, sizeof(struct sockaddr_in));
    }

    return expectedSeqNum;
}

uint32_t selective_repeat(int serverSocket, struct sockaddr_in *clientAddress, uint32_t seq_num) {
    int *acked = (int*)malloc(windowSize * sizeof(int));
    memset((char*)acked, 0, windowSize * sizeof(int));

    uint32_t expectedSeqNum = seq_num;
    struct timeval tv_s, tv;

    while(1) {
        gettimeofday(&tv_s, NULL);
    receive_packet:
        bytesRead = wrapped_recvfrom(serverSocket, recvPacket, 5, 0, clientAddress, clientAddressLen);
        if(bytesRead > 0 && recvPacket->rtp.flags == RTP_FIN && recvPacket->rtp.seq_num == expectedSeqNum)
            break;
        else if(bytesRead < 0 || recvPacket->rtp.flags != 0 ||
            recvPacket->rtp.seq_num >= expectedSeqNum + windowSize ||
            recvPacket->rtp.seq_num < expectedSeqNum - windowSize) {
            gettimeofday(&tv, NULL);
            if(is_timeout(&tv, &tv_s, 5e6))
                LOG_FATAL("MyReceiver: Timeout waiting for more data\n");
            else
                goto receive_packet;
        }

        if(recvPacket->rtp.seq_num >= expectedSeqNum) {
            uint32_t pos = recvPacket->rtp.seq_num % windowSize;
            acked[pos] = 1;
            memcpy((char*)(&window[pos]), recvPacket, sizeof(rtp_packet_t));
        }

        if(recvPacket->rtp.seq_num == expectedSeqNum) {
            while(acked[expectedSeqNum % windowSize]) {
                uint32_t pos = expectedSeqNum % windowSize;
                fwrite(window[pos].payload, sizeof(char), window[pos].rtp.length, fp);
                acked[pos] = 0;
                expectedSeqNum++;
            }
        }

        packet_init(sendPacket, 0, recvPacket->rtp.seq_num, RTP_ACK, NULL);
        sendto(serverSocket, (char*)sendPacket, LENGTH(sendPacket), 0,
            (struct sockaddr*)clientAddress, sizeof(struct sockaddr_in));
    }

    free(acked);
    return expectedSeqNum;
}
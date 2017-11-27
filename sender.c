#include "udp_shared.h"

#include <sys/mman.h>  // mmap

static struct sockaddr_in readConnFromEnv(const char* var) {
    char* val = getenv(var);
    struct sockaddr_in ret;
    if (!val) {
        die("Environment variable %s not set", var);
    } else if (fillConnInfo(val, &ret) < 0) {
        die("Illegal connection info for %s", var);
    }
    return ret;
}

int main(int argc, const char* argv[]) {
    if (argc != 4) {
        printf("Usage: %s <ip>:<port> <input-file> <threshold>\n", argv[0]);
        printf("Environment variables:\n");
        printf("        AGENT=<agent-ip>:<agent-port>\n"
               "        RECV=<receiver-ip>:<receiver-port>\n");
        exit(1);
    }

    const char* srvConnStr = argv[1];
    const char* fnameInput = argv[2];
    const int argThres = atoi(argv[3]);

    struct sockaddr_in srcaddr, agentaddr, dstaddr;
    if (fillConnInfo(srvConnStr, &srcaddr) < 0) {
        die("Illegal connection info for sender");
    }

    agentaddr = readConnFromEnv("AGENT");
    dstaddr = readConnFromEnv("RECV");

    /* *** prepare file by mmap *** */
    FILE* fp_in = fopen(fnameInput, "rb");
    if (!fp_in) {
        die_perror("Cannot open input file");
    }
    if (fseek(fp_in, 0L, SEEK_END) < 0) {
        die_perror("Cannot get file size");
    }
    const size_t fileSize = ftell(fp_in);
    LOG_V("File size is %zu bytes", fileSize);

    const char* file = (char*) mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fileno(fp_in), 0);
    if (file == MAP_FAILED) {
        die_perror("Mapping input file");
    }
    fclose(fp_in);

    /* *** initialize socket *** */
    int sock_fd = initSocket(&srcaddr);

    struct timeval timeout = {1, 0};
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        die_perror("Failed to set timeout on socket");
    }
    LOG_D("Timeout is set to %g sec", timeout.tv_sec + timeout.tv_usec / 1e6);

    size_t pktTot = (fileSize + PACKET_DATA_SIZE - 1) / PACKET_DATA_SIZE;
    LOG_I("Ready to send %zu packets", pktTot);

    PacketStatus* pktStatus = (PacketStatus*) calloc(sizeof(PacketStatus), pktTot);

    /* *** populate packet *** */
    int winSize = 1;
    int threshold = argThres;
    packet pkt_buf = {
        .addr_src = srcaddr.sin_addr.s_addr,
        .addr_dst = dstaddr.sin_addr.s_addr,
        .port_src = srcaddr.sin_port,
        .port_dst = dstaddr.sin_port
    };
    size_t pktWinStart = 0;

    socklen_t addrlen = sizeof(agentaddr);

    while (pktWinStart < pktTot) {
        // send a window size of packets, starting from pktWinStart, capped at the file end
        // the number of emitted packets each round is recorded at pktEmit
        int pktEmit = 0;
        for (int i = 0; i < winSize; i++) {
            size_t pktId = pktWinStart + i;
            if (pktId >= pktTot) break;

            pkt_buf.type = PKT_DATA;
            pkt_buf.seq_num = pktId;
            if (pktId == pktTot - 1) {
                // the last chunk size
                pkt_buf.len = fileSize - PACKET_DATA_SIZE * pktId;
            } else {
                pkt_buf.len = PACKET_DATA_SIZE;
            }
            memcpy(&pkt_buf.data, &file[pktId * PACKET_DATA_SIZE], PACKET_DATA_SIZE);

            ssize_t ret = sendto(sock_fd, (void*) &pkt_buf, sizeof(pkt_buf), 0, (struct sockaddr*)&agentaddr, addrlen);

            if (ret < 0) {
                // TODO: resend instead, regard it as dropped
                die_perror("Failed to send out packet of seq %zu", pktId);
            }

            const char* verb = pktStatus[pktId] > PKT_STATUS_UNSENT ? "resnd" : "send";
            printfStatus(verb, "data", pktId, "winSize = %d", winSize);

            pktStatus[pktId] = PKT_STATUS_WAIT;
            pktEmit++;
        }

        // recording the last (largest) un-ack-ed packet suffices
        // 0 means unset here
        size_t firstUnAck = 0;

        // wait for those ACK or timeout, whichever comes first
        LOG_D("Waiting for %d ACKs", pktEmit);
        int timeout = 0;
        for (int i = 0; i < pktEmit; i++) {
            packet pkt_resp;
            ssize_t ret = recvfrom(sock_fd, (void*) &pkt_resp, sizeof(pkt_resp), 0, (struct sockaddr*)&agentaddr, &addrlen);
            if (ret < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    timeout = 1;
                    break;
                }
                die_perror("Failed when receiving ACK");
            }

            if (pkt_resp.type != PKT_ACK) {
                LOG_E("Expected packet of type ACK (%d), got %d", PKT_ACK, pkt_resp.type);
                exit(1);
            }

            firstUnAck = max(firstUnAck, pkt_resp.seq_num + 1);

            pktStatus[pkt_resp.seq_num] = PKT_STATUS_ACKED;
            printfStatus("recv", "ack", pkt_resp.seq_num, NULL);
        }

        // only if the whole window timeouts, find out the first un-ack-ed packet by traversal
        if (!firstUnAck) {
            firstUnAck = pktWinStart;
            while (pktStatus[firstUnAck] == PKT_STATUS_ACKED && firstUnAck < pktWinStart + pktEmit)
                firstUnAck++;
        }

        if (timeout || firstUnAck < pktWinStart + pktEmit) {
            // failed to complete this round
            pktWinStart = firstUnAck;
            LOG_W("Congestion happens, recovering from seq num %zu (timeout=%d)", pktWinStart, timeout);
            threshold = max(winSize >> 1, 1);
            winSize = 1;
            printfStatus("time", "out", -1, "threshold = %d", threshold);
        } else {
            pktWinStart += pktEmit;
            if (winSize < threshold) {
                winSize <<= 1;
            } else {
                winSize++;
            }
            LOG_V("A window of data is transferred, winSize => %d", winSize);
        }
    }

    // send ACK and receive FINACK
    printfStatus("send", "fin", -1, NULL);
    pkt_buf.type = PKT_FIN;
    ssize_t ret;
    ret = sendto(sock_fd, (void*) &pkt_buf, sizeof(pkt_buf), 0, (struct sockaddr*)&agentaddr, addrlen);
    if (ret < 0) {
        die_perror("Failed to send FIN");
    }
    printfStatus("recv", "finack", -1, NULL);
    ret = recvfrom(sock_fd, (void*) &pkt_buf, sizeof(pkt_buf), 0, (struct sockaddr*)&agentaddr, &addrlen);
    if (ret < 0) {
        die_perror("Failed to receive FINACK");
    }
    // check if it's really FINACK
    if (pkt_buf.type != PKT_FIN_ACK) {
        die("Expected packet of type FINACK (%d), got %d", PKT_FIN_ACK, pkt_buf.type);
    }

    LOG_I("Successfully completed sending!");
    munmap((void *) file, fileSize);

    close(sock_fd);
}

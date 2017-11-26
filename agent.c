#include "udp_shared.h"

static const char* PACKET_TYPE_TEXT[] = {"DATA", "FIN", "ACK", "FINACK"};
static const char* PACKET_TYPE_PRINT[] = {"data", "fin", "ack", "finack"};

static int pktSeqNum(packet* pkt) {
    return pkt->type == PKT_ACK ? pkt->seq_num : -1;
}

static double checkLossRate(const char* str) {
    errno = 0;
    char* endptr = NULL;
    double ret = strtod(str, &endptr);
    if (errno) {
        die_perror("Expected a double but found [%s]", str);
    }
    if (*endptr != '\0') {
        die("Cannot interpret [%s] as a double", str);
    }
    if (!(ret >= 0 && ret <= 1)) {
        die("Illegal value for loss rate: %lf", ret);
    }
    return ret;
}

int main(int argc, const char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <ip>:<port> <loss-rate>\n", argv[0]);
        exit(1);
    }

    const char* srvConnStr = argv[1];
    const double argLossRate = checkLossRate(argv[2]);

    struct sockaddr_in srvaddr;
    if (fillConnInfo(srvConnStr, &srvaddr) < 0) {
        die("Illegal connection info for agent");
    }

    int sock_fd = initSocket(&srvaddr);
    socklen_t addrlen = sizeof(srvaddr);

    int pktDrop = 0, pktRecv = 0;

    srand(time(NULL));

    LOG_I("Ready to forward packets with loss rate %g", argLossRate);

    while (1) {
        packet pkt_buf;
        struct sockaddr_in srcaddr;
        // XXX: will specifying NULLs work?
        ssize_t pktLen = recvfrom(sock_fd, (void*)&pkt_buf, sizeof(pkt_buf), 0, (struct sockaddr*)&srcaddr, &addrlen);

        if (pktLen < 0) {
            LOG_E("Failed when receiving data OAQ: %s", strerror(errno));
            continue;
        }

        LOG_V("Received packet: %d -> %d, type=%s",
            ntohs(pkt_buf.port_src), ntohs(pkt_buf.port_dst), PACKET_TYPE_TEXT[pkt_buf.type]);

        ssize_t result;
        struct sockaddr_in dstaddr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = pkt_buf.addr_dst,
            .sin_port = pkt_buf.port_dst
        };

        const char* type = PACKET_TYPE_PRINT[pkt_buf.type];
        printfStatus("get", type, pktSeqNum(&pkt_buf), NULL);

        switch (pkt_buf.type) {
        case PKT_DATA:
            LOG_D("Packet content preview: [%.32s]", pkt_buf.data);
            pktRecv++;
            if ((double) rand() / RAND_MAX <= argLossRate) {
                pktDrop++;
                LOG_W("Dropping packet with seq num %zu (%d/%d)", pkt_buf.seq_num, pktDrop, pktRecv);
                printfStatus("drop", type, pkt_buf.seq_num, "loss rate = %lf", (double) pktDrop / pktRecv);
                break;
            }
            // fall through if forwarding is intended
        case PKT_FIN: case PKT_ACK: case PKT_FIN_ACK:
            // seq-number only significant in ACK packets
            printfStatus("fwd", type, pktSeqNum(&pkt_buf), NULL);

            // simply transfer it
            result = sendto(sock_fd, (void*)&pkt_buf, sizeof(pkt_buf), 0, (struct sockaddr*)&dstaddr, addrlen);
            if (result < 0) {
                LOG_E("Failed to transfer data: %s", strerror(errno));
            }
            break;
        default:
            LOG_E("Unexpected packet type (%d)", pkt_buf.type);
        }
    }
}

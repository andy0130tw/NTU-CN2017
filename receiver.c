#include "udp_shared.h"

static void flushBuffer(FILE* f, char* buf, size_t sz) {
    LOG_V("Writing %zu bytes of data", sz);
    int ret = fwrite(buf, 1, sz, f);
    if (ret < 0) {
        die_perror("Cannot flush into output file");
    }
}

int main(int argc, const char* argv[]) {
    if (argc != 4) {
        printf("Usage: %s <ip>:<port> <output-name> <buf-size>\n", argv[0]);
        exit(1);
    }

    const char* srvConnStr = argv[1];
    const char* fnameOutput = argv[2];
    const int bufSize = atoi(argv[3]);

    struct sockaddr_in srvaddr;
    if (fillConnInfo(srvConnStr, &srvaddr) < 0) {
        die("Illegal connection info for receiver");
    }

    int sock_fd = initSocket(&srvaddr);
    socklen_t addrlen = sizeof(srvaddr);
    packet pkt_buf;
    char buffer[bufSize][PACKET_DATA_SIZE];  // be aware that this might overflow buffer!
    int bufUsedCnt = 0;
    size_t bufUsedLen = 0;
    size_t expectedAckId = 0;

    /* *** open output file *** */
    FILE* fp_out = fopen(fnameOutput, "wb");

    LOG_I("Ready to receive packets");

    while (1) {
        struct sockaddr_in srcaddr;
        ssize_t pktLen = recvfrom(sock_fd, (void*)&pkt_buf, sizeof(pkt_buf), 0, (struct sockaddr*) &srcaddr, &addrlen);
        if (pktLen < 0) {
            die_perror("Failed to receive packet");
        }

        packet pkt_resp = {
            .addr_src = pkt_buf.addr_dst,
            .addr_dst = pkt_buf.addr_src,
            .port_src = pkt_buf.port_dst,
            .port_dst = pkt_buf.port_src
        };

        // used in PKT_DATA
        const char* verb;

        switch (pkt_buf.type) {
        case PKT_DATA:
            pkt_resp.type = PKT_ACK;
            // LOG_D("Buffer usage: %d/%d", bufUsedCnt, bufSize);
            if (bufUsedCnt < bufSize && pkt_buf.seq_num == expectedAckId) {
                // write data, send back ACK
                verb = "recv";
                memcpy(buffer[bufUsedCnt], pkt_buf.data, pkt_buf.len);
                bufUsedCnt++;
                bufUsedLen += pkt_buf.len;
                pkt_resp.seq_num = pkt_buf.seq_num;
                expectedAckId++;
            } else {
                // drop because
                //   (1) buffer is ready to overflow
                //   (2) the seq-number is not correct, indicating a loss

                if (bufUsedCnt == bufSize) {
                    printfStatus("flush", "", -1, NULL);
                    flushBuffer(fp_out, (char*) buffer, bufUsedLen);
                    bufUsedCnt = 0;
                    bufUsedLen = 0;
                }

                if (pkt_buf.seq_num < expectedAckId) {
                    LOG_E("Sender misbehaved: Seq num is smaller then requested (%lu < %lu), refusing to continue",
                        pkt_buf.seq_num, expectedAckId);
                    exit(1);
                }

                verb = "drop";
                pkt_resp.seq_num = expectedAckId - 1;
                LOG_W("Drop packet and send ACK %zu instead", pkt_resp.seq_num);
            }
            printfStatus(verb, "data", pkt_buf.seq_num, NULL);
            break;
        case PKT_FIN:
            // finalize, send back FINACK
            pkt_resp.type = PKT_FIN_ACK;
            printfStatus("recv", "fin", -1, NULL);
            break;
        default:
            LOG_E("Unexpected packet type (%d)", pkt_buf.type);
            continue;
        }

        // send back response packet
        ssize_t respResult = sendto(sock_fd, (void*)&pkt_resp, sizeof(pkt_resp), 0, (struct sockaddr*) &srcaddr, addrlen);

        if (respResult < 0) {
            LOG_E("Failed to send ACK/FINACK: %s", strerror(errno));
            continue;
        }

        if (pkt_resp.type == PKT_ACK) {
            printfStatus("send", "ack", pkt_resp.seq_num, NULL);
        } else if (pkt_resp.type == PKT_FIN_ACK) {
            printfStatus("send", "finack", -1, NULL);
            break;
        }
    }

    flushBuffer(fp_out, (char*) buffer, bufUsedLen);

    LOG_I("Finished receiving file!");

    fclose(fp_out);
    close(sock_fd);
}

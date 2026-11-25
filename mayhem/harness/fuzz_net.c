/*
 * Network stack (kernel/net.c) fed fuzzed Ethernet frames through a mocked virtio-net
 * device: ARP, IPv4, ICMP echo, UDP demux, the DNS response parser and the TCP state
 * machine with its receive ring.
 *
 * Input: a stream of frames, each [u16 LE length][frame bytes].
 *
 * Before the frames arrive the harness puts the stack in the state it is in while a client
 * waits for replies: a DNS query outstanding (dns_recv_handler bound on its port, query id
 * 0x1234) and one TCP socket in SYN_SENT to 10.0.2.2:80 from local port 49152 with
 * send_seq 5000. tcp_connect()/dns_resolve() create that state but then spin in busy-wait
 * loops (10^8 volatile iterations) that make them unusable per input, so the state they
 * leave is set directly. All file-static state is reset per input.
 */
#include "../../kernel/net.c"
#include <stdlib.h>

static const uint8_t *frames;
static size_t frames_left;

void virtio_net_get_mac(uint8_t *mac) {
    static const uint8_t m[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    __builtin_memcpy(mac, m, 6);
}
int virtio_net_send(const void *d, uint32_t len) { (void)d; return (int)len; }
int virtio_net_has_packet(void) { return frames_left >= 2; }
int virtio_net_recv(void *buf, uint32_t maxlen) {
    if (frames_left < 2) return 0;
    size_t len = frames[0] | (frames[1] << 8);
    frames += 2; frames_left -= 2;
    if (len > frames_left) len = frames_left;
    size_t n = len < maxlen ? len : maxlen;
    __builtin_memcpy(buf, frames, n);
    frames += len; frames_left -= len;
    return (int)n;
}

static void udp_sink(uint32_t ip, uint16_t sp, uint16_t dp, const void *d, uint32_t len) {
    (void)ip; (void)sp; (void)dp;
    volatile uint8_t x = 0;
    for (uint32_t i = 0; i < len; i++) x ^= ((const uint8_t *)d)[i];
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    __builtin_memset(arp_table, 0, sizeof(arp_table));
    __builtin_memset(udp_listeners, 0, sizeof(udp_listeners));
    __builtin_memset(tcp_sockets, 0, sizeof(tcp_sockets));
    tcp_next_port = 49152;
    ping_received = 0; ping_id = 0; ping_seq = 0;
    dns_response_received = 0; dns_resolved_ip = 0;
    our_ip = NET_IP;
    net_init();

    dns_query_id = 0x1234;
    udp_bind(10053 + (dns_query_id % 100), dns_recv_handler);
    udp_bind(7777, udp_sink);

    tcp_socket_internal_t *s = &tcp_sockets[0];
    s->local_ip = our_ip;
    s->remote_ip = NET_GATEWAY;
    s->local_port = tcp_next_port++;
    s->remote_port = 80;
    s->send_seq = 5000;
    s->state = TCP_STATE_SYN_SENT;

    frames = data;
    frames_left = size;
    net_poll();

    static uint8_t buf[4096];
    tcp_is_connected(0);
    tcp_get_state(0);
    tcp_recv(0, buf, sizeof(buf));
    if (tcp_is_connected(0)) tcp_send(0, "GET / HTTP/1.0\r\n\r\n", 18);
    arp_lookup(NET_GATEWAY);
    return 0;
}

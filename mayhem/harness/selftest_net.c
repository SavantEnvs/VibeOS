#include "../../kernel/net.c"
#include "selftest.h"

static const uint8_t *rx; static size_t rx_len;
static uint8_t tx[1600]; static int tx_len;
static const uint8_t MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
void virtio_net_get_mac(uint8_t *m) { __builtin_memcpy(m, MAC, 6); }
int virtio_net_send(const void *d, uint32_t len) { if (len <= sizeof(tx)) { __builtin_memcpy(tx, d, len); tx_len = len; } return len; }
int virtio_net_has_packet(void) { return rx_len > 0; }
int virtio_net_recv(void *b, uint32_t max) { size_t n = rx_len < max ? rx_len : max; __builtin_memcpy(b, rx, n); rx_len = 0; return n; }

void selftest_net(void) {
    net_init();
    /* ARP who-has 10.0.2.15 from 10.0.2.2 */
    static const uint8_t req[42] = { 0x52,0x54,0,0x12,0x34,0x56, 0x52,0x54,0,0,2,2, 8,6,
        0,1, 8,0, 6, 4, 0,1, 0x52,0x54,0,0,2,2, 10,0,2,2, 0,0,0,0,0,0, 10,0,2,15 };
    rx = req; rx_len = sizeof(req); tx_len = 0;
    net_poll();
    st_check("arp_reply_sent", tx_len >= 42 && tx[12] == 8 && tx[13] == 6 && tx[21] == 2 &&
             !memcmp(tx + 22, MAC, 6) && tx[28] == 10 && tx[31] == 15 && tx[38] == 10 && tx[41] == 2);
    const uint8_t *m = arp_lookup(MAKE_IP(10, 0, 2, 2));
    st_check("arp_learned", m && m[5] == 2 && m[4] == 2);
    st_check("ip_to_str", !strcmp(ip_to_str(MAKE_IP(192, 168, 1, 77)), "192.168.1.77"));
    static uint8_t hdr[20] = { 0x45,0,0,0x54, 0,1,0,0, 64,1,0,0, 10,0,2,15, 10,0,2,2 };
    uint16_t c = ip_checksum(hdr, 20);
    __builtin_memcpy(hdr + 10, &c, 2);
    st_check("ip_checksum", c != 0 && ip_checksum(hdr, 20) == 0);
}

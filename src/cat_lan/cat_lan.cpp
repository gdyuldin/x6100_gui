#include "cat_lan.h"
#include "cat_lan_packets.h"
#include "cat/cat.h"
#include "cat/civ_protocol.h"
#include "cat/civ_processor.h"
#include "common/queue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cfg/settings_manager.h"
#include "lvgl/lvgl.h"

#define CONTROL_PORT     50001
#define CIV_PORT         50002
#define TX_BUF_SIZE      500
#define PURGE_MS         10000
#define POLL_TIMEOUT_MS  100
#define AUTH_USERNAME    "root"
#define AUTH_PASSWORD    "root"

enum AuthState {
    ST_LISTENING,
    ST_AYT_SENT,
    ST_LOGIN_PENDING,
    ST_AUTH_PENDING,
    ST_TOKEN_SENT,
    ST_CIV_ACTIVE
};

struct TxEntry {
    uint16_t                                    seq;
    std::vector<uint8_t>                        data;
    std::chrono::steady_clock::time_point       sent_at;
    int                                         retries;
};

static int                fd_control        = -1;
static int                fd_civ            = -1;
static int                fd_event          = -1;
static struct sockaddr_in client_ctrl;
static bool               client_ctrl_valid = false;
static struct sockaddr_in client_civ;
static bool               client_civ_valid  = false;

static AuthState          auth_state    = ST_LISTENING;
static std::thread*       thread        = nullptr;
static std::atomic<bool>  keep_running(false);

static uint32_t           my_id        = 0;
static uint32_t           remote_id    = 0;
static uint16_t           send_seq     = 1;
static uint16_t           civ_send_seq = 0;
static uint16_t           auth_seq     = 0x30;
static uint16_t           tok_request  = 0;
static uint32_t           token        = 0;

static std::map<uint16_t, TxEntry> tx_buffer;
static std::mutex                  tx_mutex;
static std::map<uint16_t, int>     rx_missing;
static std::mutex                  missing_mutex;
static TSQueue<std::vector<uint8_t>> send_queue;

static bool udp_send(int fd, const void *data, size_t len, const sockaddr_in *dst);
static bool send_control(int fd, uint16_t type, uint16_t seq, bool tracked, const sockaddr_in *dst);
static bool civ_data_send(const uint8_t *civ_data, size_t civ_len);
static bool send_tracked(int fd, const uint8_t *data, size_t len, const sockaddr_in *dst);
static void process_control_packet(const uint8_t *buf, size_t len, const sockaddr_in *src);
static void process_civ_packet(const uint8_t *buf, size_t len, const sockaddr_in *src, CivTxPacker &resp_packer);
static void send_capabilities(void);
static void send_conninfo(void);
static void handle_retransmit(int fd, const uint8_t *buf, size_t len, const sockaddr_in *dst);
static void check_retransmit(void);
static void purge_buffer(void);
static void on_fg_freq_change_cb(Subject *s, void *user_data);
static void on_mode_change_cb(Subject *s, void *user_data);
static void on_vfo_change_cb(Subject *s, void *user_data);
static void handle_login_packet(const uint8_t *buf, size_t len);
static void handle_token_packet(const uint8_t *buf, size_t len);
static void handle_conninfo_packet(const uint8_t *buf, size_t len);

static uint32_t make_my_id(uint16_t port) {
    return (uint32_t)port;
}

static bool udp_send(int fd, const void *data, size_t len, const sockaddr_in *dst) {
    ssize_t ret = sendto(fd, data, len, 0, (const sockaddr *)dst, sizeof(*dst));
    return ret == (ssize_t)len;
}

static bool send_control(int fd, uint16_t type, uint16_t seq, bool tracked, const sockaddr_in *dst) {
    control_packet_t pkt;
    std::memset(&pkt, 0, sizeof(pkt));
    pkt.len    = sizeof(pkt);
    pkt.type   = type;
    pkt.seq    = seq;
    pkt.sentid = my_id;
    pkt.rcvdid = remote_id;
    if (tracked) {
        return send_tracked(fd, (const uint8_t *)&pkt, sizeof(pkt), dst);
    }
    return udp_send(fd, &pkt, sizeof(pkt), dst);
}

static bool send_tracked(int fd, const uint8_t *data, size_t len, const sockaddr_in *dst) {
    if (send_seq == 0) send_seq = 1;
    uint16_t seq = send_seq++;
    uint8_t *mutable_data = const_cast<uint8_t *>(data);
    mutable_data[6] = seq & 0xFF;
    mutable_data[7] = (seq >> 8) & 0xFF;
    TxEntry entry;
    entry.seq = seq;
    entry.data.assign(data, data + len);
    entry.sent_at = std::chrono::steady_clock::now();
    entry.retries = 0;
    {
        std::lock_guard<std::mutex> lock(tx_mutex);
        if (tx_buffer.size() >= TX_BUF_SIZE) {
            tx_buffer.erase(tx_buffer.begin());
        }
        tx_buffer[seq] = std::move(entry);
    }
    return udp_send(fd, data, len, dst);
}

static bool civ_data_send(const uint8_t *civ_data, size_t civ_len) {
    if (!client_civ_valid) return false;
    std::vector<uint8_t> buf(sizeof(ping_packet_t) + civ_len);
    ping_packet_t *pkt = (ping_packet_t *)buf.data();
    pkt->len     = (uint32_t)(sizeof(ping_packet_t) + civ_len);
    pkt->type    = 0;
    pkt->sentid  = my_id;
    pkt->rcvdid  = remote_id;
    pkt->reply   = 0xC1;
    pkt->datalen = (uint16_t)civ_len;
    pkt->sendseq = htons(civ_send_seq++);
    if (civ_len > 0) {
        std::memcpy(buf.data() + sizeof(ping_packet_t), civ_data, civ_len);
    }
    return send_tracked(fd_civ, buf.data(), buf.size(), &client_civ);
}

// ---- Control packet processing -------------------------------------------

static void handle_retransmit(int fd, const uint8_t *buf, size_t len, const sockaddr_in *dst) {
    const control_packet_t *in = (const control_packet_t *)buf;
    if (len == CONTROL_SIZE) {
        std::lock_guard<std::mutex> lock(tx_mutex);
        auto it = tx_buffer.find(in->seq);
        if (it != tx_buffer.end()) {
            it->second.retries++;
            udp_send(fd, it->second.data.data(), it->second.data.size(), dst);
        }
    } else {
        const uint8_t *p = buf + CONTROL_SIZE;
        const uint8_t *end = buf + len;
        std::lock_guard<std::mutex> lock(tx_mutex);
        while (p + 4 <= end) {
            uint16_t rseq = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            auto it = tx_buffer.find(rseq);
            if (it != tx_buffer.end()) {
                it->second.retries++;
                udp_send(fd, it->second.data.data(), it->second.data.size(), dst);
            }
            p += 4;
        }
    }
}

static void process_control_packet(const uint8_t *buf, size_t len, const sockaddr_in *src) {
    if (len < sizeof(control_packet_t)) return;
    const control_packet_t *in = (const control_packet_t *)buf;

    if (in->type == 0x01) {
        handle_retransmit(fd_control, buf, len, src);
        return;
    }

    // Ping request
    if (len == PING_SIZE) {
        const ping_packet_t *pin = (const ping_packet_t *)buf;
        if (pin->type == 0x07 && pin->reply == 0x00) {
            ping_packet_t resp;
            std::memset(&resp, 0, sizeof(resp));
            resp.len    = sizeof(resp);
            resp.type   = 0x07;
            resp.seq    = pin->seq;
            resp.sentid = my_id;
            resp.rcvdid = remote_id;
            resp.reply  = 0x01;
            resp.time   = pin->time;
            udp_send(fd_control, &resp, sizeof(resp), &client_ctrl);
        }
        return;
    }

    switch (auth_state) {
    case ST_LISTENING:
        if (in->type == 0x03) {
            LV_LOG_INFO("LAN: are-you-there received");
            client_ctrl        = *src;
            client_ctrl_valid  = true;
            remote_id          = in->sentid;
            send_control(fd_control, 0x04, 0, false, &client_ctrl);
            send_control(fd_control, 0x06, 0x01, false, &client_ctrl);
            auth_state = ST_AYT_SENT;
        }
        break;

    case ST_AYT_SENT:
        if (in->type == 0x06) {
            LV_LOG_INFO("LAN: I-am-ready received");
            remote_id = in->sentid;
            auth_state = ST_LOGIN_PENDING;
        } else {
            send_control(fd_control, 0x04, 0, false, &client_ctrl);
            send_control(fd_control, 0x06, 0x01, false, &client_ctrl);
        }
        break;

    default:
        if (in->type == 0x03) {
            send_control(fd_control, 0x04, 0, false, &client_ctrl);
        }
        break;
    }
}

// ---- Login / Token / ConnInfo handlers -----------------------------------

static void handle_login_packet(const uint8_t *buf, size_t len) {
    if (len < sizeof(login_packet_t)) return;
    const login_packet_t *in = (const login_packet_t *)buf;

    uint8_t expected_user[16], expected_pass[16];
    int ulen, plen;
    passcode_encode(AUTH_USERNAME, (int)std::strlen(AUTH_USERNAME), expected_user, &ulen);
    passcode_encode(AUTH_PASSWORD, (int)std::strlen(AUTH_PASSWORD), expected_pass, &plen);

    bool user_ok = (ulen <= 16 && std::memcmp(in->username, expected_user, (size_t)ulen) == 0);
    bool pass_ok = (plen <= 16 && std::memcmp(in->password, expected_pass, (size_t)plen) == 0);

    if (!user_ok || !pass_ok) {
        LV_LOG_WARN("LAN: invalid credentials");
    }

    remote_id   = in->sentid;
    tok_request = in->tokrequest;
    token       = 0;
    auth_seq    = in->innerseq + 1;

    login_response_packet_t resp;
    std::memset(&resp, 0, sizeof(resp));
    resp.len          = sizeof(resp);
    resp.sentid       = my_id;
    resp.rcvdid       = remote_id;
    resp.payloadsize  = htonl((uint32_t)(sizeof(resp) - 0x10));
    resp.requesttype  = 0x00;
    resp.requestreply = 0x02;
    resp.innerseq     = htons(auth_seq++);
    resp.tokrequest   = tok_request;
    resp.error        = 0;
    std::strncpy(resp.connection, "WFVIEW", sizeof(resp.connection) - 1);

    send_tracked(fd_control, (const uint8_t *)&resp, sizeof(resp), &client_ctrl);
    auth_state = ST_AUTH_PENDING;
    LV_LOG_INFO("LAN: login response sent");
}

static void handle_token_packet(const uint8_t *buf, size_t len) {
    if (len < sizeof(token_packet_t)) return;
    const token_packet_t *in = (const token_packet_t *)buf;

    remote_id = in->sentid;
    token     = in->token;
    token++;
    auth_seq = in->innerseq + 1;

    token_packet_t resp;
    std::memset(&resp, 0, sizeof(resp));
    resp.len          = sizeof(resp);
    resp.sentid       = my_id;
    resp.rcvdid       = remote_id;
    resp.payloadsize  = htonl((uint32_t)(sizeof(resp) - 0x10));
    resp.requestreply = 0x02;
    resp.requesttype  = 0x05;
    resp.innerseq     = htons(auth_seq++);
    resp.tokrequest   = tok_request;
    resp.token        = token;
    resp.resetcap     = htons((uint16_t)0x0798);
    resp.response     = 0;

    send_tracked(fd_control, (const uint8_t *)&resp, sizeof(resp), &client_ctrl);
    auth_state = ST_TOKEN_SENT;
    send_capabilities();
    send_conninfo();
    LV_LOG_INFO("LAN: token confirmed, caps+conninfo sent");
}

static void send_capabilities(void) {
    uint8_t buf[CAPABILITIES_SIZE + RADIO_CAP_SIZE];
    std::memset(buf, 0, sizeof(buf));

    capabilities_packet_t *caps = (capabilities_packet_t *)buf;
    caps->len          = sizeof(buf);
    caps->sentid       = my_id;
    caps->rcvdid       = remote_id;
    caps->payloadsize  = htonl((uint32_t)(sizeof(buf) - 0x10));
    caps->requesttype  = 0x00;
    caps->requestreply = 0x02;
    caps->innerseq     = htons(auth_seq++);
    caps->tokrequest   = tok_request;
    caps->token        = token;
    caps->numradios    = htons(1);

    radio_cap_packet_t *rad = (radio_cap_packet_t *)(buf + CAPABILITIES_SIZE);
    rad->commoncap = 0x8010;
    uint8_t mac[6] = {0x00, 0x1E, 0xC0, 0xFF, 0xEE, 0x01};
    std::memcpy(rad->macaddress, mac, 6);
    std::strncpy(rad->name, "X6100", sizeof(rad->name) - 1);
    rad->civ = 0xE0;
    rad->baudrate = htonl(19200U);

    send_tracked(fd_control, buf, sizeof(buf), &client_ctrl);
}

static void send_conninfo(void) {
    conninfo_packet_t pkt;
    std::memset(&pkt, 0, sizeof(pkt));
    pkt.len          = sizeof(pkt);
    pkt.sentid       = my_id;
    pkt.rcvdid       = remote_id;
    pkt.payloadsize  = htonl((uint32_t)(sizeof(pkt) - 0x10));
    pkt.requesttype  = 0x03;
    pkt.requestreply = 0x02;
    pkt.innerseq     = htons(auth_seq++);
    pkt.tokrequest   = tok_request;
    pkt.token        = token;
    pkt.commoncap    = 0x8010;
    uint8_t mac[6] = {0x00, 0x1E, 0xC0, 0xFF, 0xEE, 0x01};
    std::memcpy(pkt.macaddress, mac, 6);
    std::strncpy(pkt.name, "X6100", sizeof(pkt.name) - 1);
    pkt.busy    = 0;
    pkt.ipaddress = 0;

    send_tracked(fd_control, (const uint8_t *)&pkt, sizeof(pkt), &client_ctrl);
}

static void handle_conninfo_packet(const uint8_t *buf, size_t len) {
    (void)buf;
    (void)len;
    LV_LOG_INFO("LAN: stream request received");

    status_packet_t resp;
    std::memset(&resp, 0, sizeof(resp));
    resp.len          = sizeof(resp);
    resp.sentid       = my_id;
    resp.rcvdid       = remote_id;
    resp.payloadsize  = htonl((uint32_t)(sizeof(resp) - 0x10));
    resp.requesttype  = 0x03;
    resp.requestreply = 0x02;
    resp.innerseq     = htons(auth_seq++);
    resp.tokrequest   = tok_request;
    resp.token        = token;
    resp.error        = 0;
    resp.disc         = 0;
    resp.civport      = htons(CIV_PORT);
    resp.audioport    = 0;
    resp.commoncap    = 0x8010;

    send_tracked(fd_control, (const uint8_t *)&resp, sizeof(resp), &client_ctrl);
    auth_state = ST_CIV_ACTIVE;
    LV_LOG_INFO("LAN: CIV stream active on port %d", CIV_PORT);
}

// ---- CI-V port processing ------------------------------------------------

static void process_civ_packet(const uint8_t *buf, size_t len, const sockaddr_in *src, CivTxPacker &resp_packer) {
    if (len < sizeof(control_packet_t)) return;
    const control_packet_t *hdr = (const control_packet_t *)buf;

    if (hdr->type == 0x01) {
        handle_retransmit(fd_civ, buf, len, src);
        return;
    }

    switch (len) {
    case CONTROL_SIZE:
        if (hdr->type == 0x03) {
            client_civ       = *src;
            client_civ_valid = true;
            remote_id        = hdr->sentid;
            send_control(fd_civ, 0x04, 0, false, &client_civ);
            send_control(fd_civ, 0x06, 0x01, false, &client_civ);
            LV_LOG_INFO("LAN/CIV: handshake");
        } else if (hdr->type == 0x06) {
            remote_id = hdr->sentid;
            LV_LOG_INFO("LAN/CIV: ready");
        }
        break;

    case PING_SIZE: {
        // Ping on CIV port
        const ping_packet_t *pin = (const ping_packet_t *)buf;
        if (pin->type == 0x07 && pin->reply == 0x00) {
            ping_packet_t resp;
            std::memset(&resp, 0, sizeof(resp));
            resp.len    = sizeof(resp);
            resp.type   = 0x07;
            resp.seq    = pin->seq;
            resp.sentid = my_id;
            resp.rcvdid = remote_id;
            resp.reply  = 0x01;
            resp.time   = pin->time;
            udp_send(fd_civ, &resp, sizeof(resp), &client_civ);
        }
        break;
    }

    case OPENCLOSE_SIZE: {
        const openclose_packet_t *oc = (const openclose_packet_t *)buf;
        if (oc->magic == 0x04) {
            LV_LOG_INFO("LAN/CIV: open");
            openclose_packet_t resp;
            std::memset(&resp, 0, sizeof(resp));
            resp.len     = sizeof(resp);
            resp.sentid  = my_id;
            resp.rcvdid  = remote_id;
            resp.data    = oc->data;
            resp.sendseq = htons(civ_send_seq++);
            resp.magic   = 0x04;
            send_tracked(fd_civ, (const uint8_t *)&resp, sizeof(resp), &client_civ);
        }
        break;
    }

    default:
        if (len > PING_SIZE) {
            const ping_packet_t *in = (const ping_packet_t *)buf;
            if (in->type != 0x01 && in->reply == (uint8_t)0xC1) {
                uint16_t civ_dlen = in->datalen;
                size_t payload_offset = PING_SIZE;
                if (payload_offset + civ_dlen <= len && civ_dlen >= FRAME_ADD_LEN + 2) {
                    const uint8_t *civ_payload = buf + payload_offset;
                    CivPacketView req(civ_payload, civ_dlen);
                    auto resp = process_civ_message(req, resp_packer);
                    if (!resp.empty()) {
                        civ_data_send(reinterpret_cast<const uint8_t *>(resp.data()), resp.size());
                    }
                }
            }
        }
        break;
    }
}

// ---- Retransmit / maintenance --------------------------------------------

static void check_retransmit(void) {
    if (rx_missing.empty()) return;
    if (rx_missing.size() > MAX_MISSING) {
        std::lock_guard<std::mutex> lock(missing_mutex);
        rx_missing.clear();
        return;
    }

    std::vector<uint16_t> to_request;
    {
        std::lock_guard<std::mutex> lock(missing_mutex);
        for (auto it = rx_missing.begin(); it != rx_missing.end(); ) {
            if (it->second < 4) {
                it->second++;
                to_request.push_back(it->first);
                ++it;
            } else {
                it = rx_missing.erase(it);
            }
        }
    }

    if (to_request.empty()) return;

    std::vector<uint8_t> pkt(CONTROL_SIZE);
    control_packet_t *hdr = (control_packet_t *)pkt.data();
    hdr->len    = CONTROL_SIZE;
    hdr->type   = 0x01;
    hdr->sentid = my_id;
    hdr->rcvdid = remote_id;

    if (to_request.size() == 1) {
        hdr->seq = to_request[0];
        if (client_ctrl_valid)
            udp_send(fd_control, pkt.data(), pkt.size(), &client_ctrl);
    } else {
        pkt.reserve(CONTROL_SIZE + to_request.size() * 4);
        for (uint16_t s : to_request) {
            pkt.push_back((uint8_t)(s & 0xFF));
            pkt.push_back((uint8_t)(s >> 8));
            pkt.push_back((uint8_t)(s & 0xFF));
            pkt.push_back((uint8_t)(s >> 8));
        }
        hdr = (control_packet_t *)pkt.data();
        hdr->len = (uint32_t)pkt.size();
        hdr->seq = 0;
        if (client_ctrl_valid)
            udp_send(fd_control, pkt.data(), pkt.size(), &client_ctrl);
    }
}

static void purge_buffer(void) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(tx_mutex);
    for (auto it = tx_buffer.begin(); it != tx_buffer.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.sent_at).count();
        if (age > PURGE_MS) {
            it = tx_buffer.erase(it);
        } else {
            ++it;
        }
    }
}

// ---- CIV notification helper + callbacks ----------------------------------

static void push_civ_notify_lan(std::string_view resp) {
    send_queue.push(std::vector<uint8_t>(resp.begin(), resp.end()));
    if (fd_event >= 0) {
        uint64_t u = 1;
        ssize_t r = write(fd_event, &u, sizeof(u));
        (void)r;
    }
}

static void on_fg_freq_change_cb(Subject *s, void *user_data) {
    if (auth_state < ST_CIV_ACTIVE) return;
    uint8_t buf[16];
    CivTxPacker packer{buf, 0, LOCAL_ADDRESS};
    push_civ_notify_lan(pack_fg_freq_notify_00(cfg_sm.cp_fg_freq.get(), packer));
}

static void on_mode_change_cb(Subject *s, void *user_data) {
    if (auth_state < ST_CIV_ACTIVE) return;
    uint8_t buf[16];
    CivTxPacker packer{buf, 0, LOCAL_ADDRESS};
    push_civ_notify_lan(pack_mode_notify_01(
        static_cast<x6100_mode_t>(cfg_sm.cp_cur_mode.get()), packer));
}

static void on_vfo_change_cb(Subject *s, void *user_data) {
    if (auth_state < ST_CIV_ACTIVE) return;
    uint8_t buf[16];
    CivTxPacker packer{buf, 0, LOCAL_ADDRESS};
    push_civ_notify_lan(pack_vfo_notify_07(
        static_cast<x6100_vfo_t>(cfg_sm.p_band_current_vfo.get()), packer));
}

// ---- Thread ---------------------------------------------------------------

static void cat_lan_thread() {
    uint8_t recv_buf[65536];

    uint8_t tx_buf[64];
    CivTxPacker resp_packer{tx_buf, LOCAL_ADDRESS, 0};

    while (keep_running) {
        struct pollfd fds[3];
        std::memset(fds, 0, sizeof(fds));

        fds[0].fd     = fd_control;
        fds[0].events = POLLIN;
        fds[1].fd     = fd_civ;
        fds[1].events = POLLIN;
        fds[2].fd     = fd_event;
        fds[2].events = POLLIN;

        int ret = poll(fds, 3, POLL_TIMEOUT_MS);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Control port
        if (fds[0].revents & POLLIN) {
            sockaddr_in src;
            socklen_t src_len = sizeof(src);
            ssize_t n = recvfrom(fd_control, recv_buf, sizeof(recv_buf), 0,
                                 (sockaddr *)&src, &src_len);
            if (n > 0) {
                const control_packet_t *hdr = (const control_packet_t *)recv_buf;
                if (hdr->type == 0 && (size_t)n == LOGIN_SIZE) {
                    handle_login_packet(recv_buf, (size_t)n);
                } else if (hdr->type == 0 && (size_t)n == TOKEN_SIZE) {
                    handle_token_packet(recv_buf, (size_t)n);
                } else if (hdr->type == 0 && (size_t)n == CONNINFO_SIZE) {
                    handle_conninfo_packet(recv_buf, (size_t)n);
                } else {
                    process_control_packet(recv_buf, (size_t)n, &src);
                }
            }
        }

        // CIV port
        if (fds[1].revents & POLLIN) {
            sockaddr_in src;
            socklen_t src_len = sizeof(src);
            ssize_t n = recvfrom(fd_civ, recv_buf, sizeof(recv_buf), 0,
                                 (sockaddr *)&src, &src_len);
            if (n > 0) {
                process_civ_packet(recv_buf, (size_t)n, &src, resp_packer);
            }
        }

        // Event fd (queue trigger)
        if (fds[2].revents & POLLIN) {
            uint64_t u;
            ssize_t r = read(fd_event, &u, sizeof(u));
            (void)r;

            std::vector<uint8_t> data;
            while (send_queue.try_pop(data)) {
                if (!data.empty() && client_civ_valid) {
                    civ_data_send(data.data(), data.size());
                }
            }
        }

        // Periodic maintenance
        check_retransmit();
        purge_buffer();
    }
}

// ---- Public API -----------------------------------------------------------

int cat_lan_init(void) {
    if (fd_control >= 0) {
        LV_LOG_WARN("LAN CAT already initialized");
        return 0;
    }

    fd_control = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_control < 0) { perror("cat_lan: socket control"); return -1; }

    int opt = 1;
    setsockopt(fd_control, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CONTROL_PORT);

    if (bind(fd_control, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("cat_lan: bind control");
        close(fd_control); fd_control = -1; return -1;
    }
    fcntl(fd_control, F_SETFL, O_NONBLOCK);

    fd_civ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_civ < 0) { perror("cat_lan: socket civ"); close(fd_control); fd_control = -1; return -1; }

    setsockopt(fd_civ, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CIV_PORT);

    if (bind(fd_civ, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("cat_lan: bind civ");
        close(fd_civ); fd_civ = -1;
        close(fd_control); fd_control = -1;
        return -1;
    }
    fcntl(fd_civ, F_SETFL, O_NONBLOCK);

    fd_event = eventfd(0, EFD_NONBLOCK);
    if (fd_event < 0) {
        perror("cat_lan: eventfd");
        close(fd_civ); fd_civ = -1;
        close(fd_control); fd_control = -1;
        return -1;
    }

    my_id      = make_my_id(CONTROL_PORT);
    auth_state = ST_LISTENING;

    LV_LOG_INFO("LAN CAT listening on port %d (control) and %d (CIV)",
                CONTROL_PORT, CIV_PORT);

    cfg_sm.cp_fg_freq.subscribe(on_fg_freq_change_cb, nullptr);
    cfg_sm.cp_cur_mode.subscribe(on_mode_change_cb, nullptr);
    cfg_sm.p_band_current_vfo.subscribe(on_vfo_change_cb, nullptr);

    keep_running = true;
    thread = new std::thread(cat_lan_thread);

    return 0;
}

void cat_lan_destruct(void) {
    if (thread) {
        keep_running = false;
        thread->join();
        delete thread;
        thread = nullptr;
    }
    if (fd_control >= 0) { close(fd_control); fd_control = -1; }
    if (fd_civ >= 0)     { close(fd_civ);     fd_civ     = -1; }
    if (fd_event >= 0)   { close(fd_event);   fd_event   = -1; }

    client_ctrl_valid = false;
    client_civ_valid  = false;
    auth_state        = ST_LISTENING;

    LV_LOG_INFO("LAN CAT shut down");
}

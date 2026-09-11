/*
 * X6100 CAT I/O — UART + Bluetooth + event queue dispatcher.
 *
 * Frame parsing, command handler dispatch, and BCD conversion live in
 * cat_frame.cpp; this file owns the poll loop, socket management, and
 * frequency-change echo.
 */

#include "cat.h"
#include "cat_internal.h"

#include <mutex>
#include <thread>
#include <vector>
#include <cmath>
#include <chrono>
#include <string>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#include "lvgl/lvgl.h"

#include "../cfg/settings_manager.h"
#include "../common/queue.h"
#include "../scheduler.h"

extern "C" {
    #include "../events.h"
    #include "../meter.h"
    #include "../radio.h"
    #include "../spectrum.h"
    #include "../waterfall.h"
    #include "../tx_info.h"

    #include <aether_radio/x6100_control/low/gpio.h>
    #include <fcntl.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <sys/poll.h>
    #include <termios.h>
    #include <unistd.h>
}

static int fd_wire = -1;
static int fd_bt = -1;
static int fd_queue_event = -1;

static TSQueue<std::vector<char>> send_queue;
static std::thread* thread = nullptr;
static std::atomic<bool> keep_running(false);

static void on_fg_freq_change(Subject *s, void *user_data);

struct FeedResult {
    int status;
    std::optional<Frame> frame;
};

class Connection {
    int        *fd;
    char       buf[1024];
    const char header[2] = {FRAME_PRE, FRAME_PRE};
    size_t     start=0;
    size_t     end=0;

  protected:
    bool write_buf(const char *buf, size_t len) {
        ssize_t l;
        while (len) {
            l = write(*fd, buf, len);
            if (l < 0) {
                perror("Error during writing message");
                return false;
            }
            len -= l;
        }
        return true;
    }

  public:
    Connection(int *fd) : fd(fd) {};

    FeedResult feed() {
        int res = read(*fd, buf + end, sizeof(buf) - end);
        if (res < 0) {
            return {res, std::nullopt};
        }
        end += res;
        char *frame_start = (char *)memmem(buf, end, header, sizeof(header));
        if (frame_start == NULL) {
            buf[0] = buf[end - 1];
            end    = 1;
            return {0, std::nullopt};
        }
        if (frame_start != buf) {
            end = end + buf - frame_start;
            memmove(buf, frame_start, end);
            start = 0;
        }
        if (end >= FRAME_ADD_LEN) {
            char *end_pos = (char *)memchr(buf + FRAME_ADD_LEN, FRAME_END, end - FRAME_ADD_LEN);
            if (end_pos) {
                size_t frame_len = end_pos - buf + 1;
                start            = frame_len;
                end              = start;
                return {res, Frame{buf, frame_len}};
            }
        }
        return {0, std::nullopt};
    }

    bool send(const char * data, size_t len) {
        return write_buf(data, len);
    }
    bool send(const Frame &frame) {
        auto data = frame.dump();
        return write_buf(data.data(), data.size());
    }
};

void to_bcd(uint8_t bcd_data[], uint64_t data, uint8_t len);

static void cat_thread() {
    Connection conn_wire{&fd_wire};
    Connection conn_bt{&fd_bt};

    Connection *conn = &conn_wire;

    // Setup BT socket
    int fd_bt_sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    struct sockaddr_rc addr = { 0 };
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = 1;
    // mask 00:00:00:00:00:00
    for(int i=0; i<6; i++) addr.rc_bdaddr.b[i] = 0;

    bind(fd_bt_sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd_bt_sock, 1);
    fcntl(fd_bt_sock, F_SETFL, O_NONBLOCK);

    struct pollfd fds[4];

    while (keep_running) {
        // Setup polls
        fds[0].fd = fd_wire;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        fds[1].fd = fd_bt;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        fds[2].fd = fd_queue_event;
        fds[2].events = POLLIN;
        fds[2].revents = 0;

        fds[3].fd = fd_bt_sock;
        fds[3].events = POLLIN;
        fds[3].revents = 0;

        int ret = poll(fds, 4, -1);
        if (ret < 0) {
            perror("poll error");
            break;
        }

        // New BT connection
        if (fds[3].revents & POLLIN) {
            struct sockaddr_rc rem_addr = { 0 };
            socklen_t opt = sizeof(rem_addr);

            int new_fd = accept(fd_bt_sock, (struct sockaddr *)&rem_addr, &opt);
            if (new_fd >= 0) {
                if (fd_bt >= 0) close(fd_bt);
                fd_bt = new_fd;
                fcntl(fd_bt, F_SETFL, O_NONBLOCK);
                char bt_addr[18] = {0};
                ba2str(&rem_addr.rc_bdaddr, bt_addr);
                LV_LOG_USER("New Bluetooth connection %s", bt_addr);
                conn = &conn_bt;
            }
        }

        // Wire data
        if (fds[0].revents & POLLIN) {
            auto res = conn_wire.feed();
            if (res.frame) {
                conn_wire.send(res.frame->process());
                conn = &conn_wire;
            }
        }

        // BT data
        if (fd_bt >= 0 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            auto res = conn_bt.feed();
            if (res.frame) {
                conn_bt.send(res.frame->process());
                conn = &conn_bt;
            }
            else if (res.status <= 0) {
                LV_LOG_USER("Bluetooth disconnected");
                close(fd_bt);
                fd_bt = -1;
                conn = &conn_wire;
            }
        }

        // Queue
        if (fds[2].revents & POLLIN) {
            uint64_t u;
            ssize_t ret;
            ret = read(fd_queue_event, &u, sizeof(uint64_t)); // Reset trigger

            auto data = send_queue.pop();
            bool ok = conn->send(data.data(), data.size());
            if (!ok && conn == &conn_bt) {
                LV_LOG_ERROR("Bluetooth write error, switch to wire");
                conn = &conn_wire;
                conn->send(data.data(), data.size());
            }
        }
    }
}

void cat_init() {
    /* UART */
    x6100_gpio_set(x6100_pin_usb, 1); /* USB -> CAT */

    fd_wire = open("/dev/ttyS2", O_RDWR | O_NONBLOCK | O_NOCTTY);

    fd_bt = -1;

    fd_queue_event = eventfd(0, EFD_NONBLOCK);

    if (fd_wire > 0) {
        struct termios attr;

        tcgetattr(fd_wire, &attr);

        cfsetispeed(&attr, B19200);
        cfsetospeed(&attr, B19200);
        cfmakeraw(&attr);

        if (tcsetattr(fd_wire, 0, &attr) < 0) {
            close(fd_wire);
            LV_LOG_ERROR("UART set speed");
            return;
        }
    } else {
        LV_LOG_ERROR("UART open");
        return;
    }

    cat_frame_set_sm(nullptr);

    cfg_sm.cp_fg_freq.subscribe(on_fg_freq_change, &cfg_sm.cp_fg_freq);

    /* * */
    if (!thread) {
        keep_running = true;
        thread = new std::thread(cat_thread);
    }
}

void cat_destruct() {
    keep_running = false;
    thread->join();
    close(fd_wire);
    close(fd_queue_event);
    if (fd_bt >= 0) close(fd_bt);
}

static void on_fg_freq_change(Subject *s, void *user_data) {
    auto *p = static_cast<ComputedParameter<int32_t>*>(user_data);
    int32_t new_freq = p->get();

    uint8_t bcd[5];
    to_bcd(bcd, new_freq, 10);

    std::vector<char> buf(5 + 1 + FRAME_ADD_LEN);
    buf[0] = FRAME_PRE;
    buf[1] = FRAME_PRE;
    buf[2] = 0;
    buf[3] = LOCAL_ADDRESS;
    buf[4] = C_SND_FREQ;
    std::copy(bcd, bcd + 5, buf.begin() + 5);
    buf.back() = FRAME_END;

    send_queue.push(std::move(buf));

    // Notify thread
    if (fd_queue_event >= 0) {
        uint64_t u = 1;
        ssize_t ret;
        ret = write(fd_queue_event, &u, sizeof(uint64_t));
    }
}

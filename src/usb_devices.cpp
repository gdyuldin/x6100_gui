#include "usb_devices.h"

#include "scheduler.h"
#include "globals.h"

#include <libudev.h>
#include <pthread.h>
#include <cstring>

extern "C" {
    #include <sys/select.h>
    #include "lvgl/lvgl.h"
    #include "pubsub_ids.h"
}

static struct udev *udev;
static struct udev_monitor *mon;
static int fd;

static pthread_t usb_pthread;
static bool usb_pthread_started = false;

static void notify_device_added(void *) {
    lv_msg_send(MSG_USB_DEVICE_CHANGED, (void *)USB_DEV_ADDED);
}
static void notify_device_removed(void *) {
    lv_msg_send(MSG_USB_DEVICE_CHANGED, (void *)USB_DEV_REMOVED);
}
static void notify_input_changed(void *) {
    lv_msg_send(MSG_INPUT_DEVICE_CHANGED, NULL);
}

static void *wait_new_device(void *) {
    while (app_is_running) {
        fd_set fds;
        struct timeval tv;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(fd, &fds)) {
            struct udev_device *dev = udev_monitor_receive_device(mon);
            if (dev) {
                const char *action    = udev_device_get_action(dev);
                const char *subsystem = udev_device_get_subsystem(dev);
                if (subsystem && strcmp(subsystem, "input") == 0) {
                    /* A Bluetooth keyboard has no USB event of its own */
                    if (action && (strcmp(action, "add") == 0 || strcmp(action, "remove") == 0))
                        scheduler_put_noargs(notify_input_changed);
                } else if (action && strcmp(action, "add") == 0) {
                    scheduler_put_noargs(notify_device_added);
                } else if (action && strcmp(action, "remove") == 0) {
                    scheduler_put_noargs(notify_device_removed);
                }
                udev_device_unref(dev);
            }
        }
    }

    return NULL;
}

void usb_devices_monitor_init() {
    udev = udev_new();
    if (!udev) {
        LV_LOG_ERROR("Cannot create udev context.");
        return;
    }

    mon = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon) {
        LV_LOG_ERROR("Cannot create udev monitor.");
        udev_unref(udev);
        udev = NULL;
        return;
    }

    udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", NULL);
    udev_monitor_filter_add_match_subsystem_devtype(mon, "input", NULL);
    udev_monitor_enable_receiving(mon);
    fd = udev_monitor_get_fd(mon);

    int rc = pthread_create(&usb_pthread, NULL, wait_new_device, NULL);
    if (rc != 0) {
        LV_LOG_ERROR("Can't start usb devices thread: %d", rc);
        udev_monitor_unref(mon);
        mon = NULL;
        udev_unref(udev);
        udev = NULL;
        return;
    }
    usb_pthread_started = true;
}

void usb_devices_monitor_shutdown() {
    if (!usb_pthread_started) {
        return;
    }

    pthread_join(usb_pthread, NULL);
    usb_pthread_started = false;

    if (mon) {
        udev_monitor_unref(mon);
        mon = NULL;
    }
    if (udev) {
        udev_unref(udev);
        udev = NULL;
    }
}

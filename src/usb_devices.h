#pragma once



#ifdef __cplusplus
extern "C" {
#endif

enum usb_devices_event_t {
    USB_DEV_ADDED,
    USB_DEV_REMOVED,
};

void usb_devices_monitor_init();
void usb_devices_monitor_shutdown();

#ifdef __cplusplus
}
#endif

/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "bt.h"

#include <string>
#include <map>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include <gio/gio.h>

#include "globals.h"
#include "cfg/cfg_api.h"
#include "pubsub_ids.h"

#include "lvgl/lvgl.h"

#define BT_AGENT_PATH "/org/bluez/x6100_agent"
#define BT_ADAPTER_PATH "/org/bluez/hci0"
#define BT_AGENT_CAPABILITY "DisplayYesNo"

/* Pair/Connect can block while the user answers an agent request; the D-Bus
 * default (25 s) is too short for typing a passkey on the GUI keyboard. */
#define BT_INTERACTIVE_TIMEOUT_MS 120000

static GDBusConnection     *conn = NULL;
static GMainLoop           *loop = NULL;
static lv_timer_t          *loop_timer = NULL;
static guint                agent_reg_id = 0;
static Subscription         sub_wifi_enabled;

static GDBusMethodInvocation *pending_invocation = NULL;
static bt_passkey_kind_t      pending_kind = BT_PASSKEY_NONE;
static char                   pending_bdaddr[18];
static char                   pending_name[248];
static char                   pending_pin[16];

static bt_passkey_cb_t        passkey_cb = NULL;
static bool                   scanning = false;
static bool                   adapter_present = false;
static bool                   agent_registered = false;
static bool                   want_power_on = false;
static const char            *current_op = NULL;

static std::map<std::string, bt_dev_info_t> dev_map;

static void loop_iterations_cb(lv_timer_t *);
static void interfaces_added_cb(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *,
                                gpointer);
static void interfaces_removed_cb(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *,
                                  gpointer);
static void device_props_changed_cb(GDBusConnection *, const char *, const char *, const char *, const char *,
                                    GVariant *, gpointer);
static void battery_props_changed_cb(GDBusConnection *, const char *, const char *, const char *, const char *,
                                     GVariant *, gpointer);
static void adapter_props_changed_cb(GDBusConnection *, const char *, const char *, const char *, const char *,
                                     GVariant *, gpointer);
static void get_managed_objects_cb(GObject *, GAsyncResult *, gpointer);
static void op_done_cb(GObject *, GAsyncResult *, gpointer);
static void agent_registered_cb(GObject *, GAsyncResult *, gpointer);
static void request_default_agent();
static void ensure_agent();
static void power_set_cb(GObject *, GAsyncResult *, gpointer);
static void apply_power();
static void agent_method_call_cb(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *,
                                 GDBusMethodInvocation *, gpointer);
static void on_wifi_enabled_changed(Subject *, void *);

static void notify_changed();
static void parse_device(GVariant *props);
static void parse_battery(const char *dev_path, GVariant *props);
static void resolve_device(const char *dev_path, char *bdaddr_out, size_t bdaddr_sz, char *name_out, size_t name_sz);
static void path_from_bdaddr(const char *bdaddr, char *out, size_t sz);
static bt_dev_class_t classify_icon(const char *icon);
static void cod_to_text(uint32_t cod, char *out, size_t sz);

/* ---- BlueZ Agent1 interface info ---- */

static const GDBusArgInfo agent_io_device_arg  = { -1, (gchar *)"device", (gchar *)"o", NULL };
static const GDBusArgInfo agent_io_passkey_arg = { -1, (gchar *)"passkey", (gchar *)"u", NULL };
static const GDBusArgInfo agent_io_uuid_arg    = { -1, (gchar *)"uuid", (gchar *)"s", NULL };
static const GDBusArgInfo agent_io_pin_arg     = { -1, (gchar *)"pincode", (gchar *)"s", NULL };

static const GDBusArgInfo *const agent_release_in[]  = { NULL };
static const GDBusMethodInfo    agent_release_method = {
    .ref_count = -1,
    .name      = (gchar *)"Release",
    .in_args   = (GDBusArgInfo **)agent_release_in,
};

static const GDBusArgInfo *const agent_pin_in[]   = { &agent_io_device_arg, NULL };
static const GDBusArgInfo *const agent_pin_out[]  = { &agent_io_pin_arg, NULL };
static const GDBusMethodInfo    agent_pin_method  = {
    .ref_count = -1,
    .name      = (gchar *)"RequestPinCode",
    .in_args   = (GDBusArgInfo **)agent_pin_in,
    .out_args  = (GDBusArgInfo **)agent_pin_out,
};

static const GDBusArgInfo *const agent_passkey_in[]   = { &agent_io_device_arg, NULL };
static const GDBusArgInfo *const agent_passkey_out[]  = { &agent_io_passkey_arg, NULL };
static const GDBusMethodInfo    agent_passkey_method = {
    .ref_count = -1,
    .name      = (gchar *)"RequestPasskey",
    .in_args   = (GDBusArgInfo **)agent_passkey_in,
    .out_args  = (GDBusArgInfo **)agent_passkey_out,
};

static const GDBusArgInfo *const agent_confirm_in[] = { &agent_io_device_arg, &agent_io_passkey_arg, NULL };
static const GDBusMethodInfo    agent_confirm_method = {
    .ref_count = -1,
    .name      = (gchar *)"RequestConfirmation",
    .in_args   = (GDBusArgInfo **)agent_confirm_in,
};

static const GDBusArgInfo *const agent_auth_in[] = { &agent_io_device_arg, NULL };
static const GDBusMethodInfo    agent_auth_method = {
    .ref_count = -1,
    .name      = (gchar *)"RequestAuthorization",
    .in_args   = (GDBusArgInfo **)agent_auth_in,
};

static const GDBusArgInfo *const agent_service_in[] = { &agent_io_device_arg, &agent_io_uuid_arg, NULL };
static const GDBusMethodInfo    agent_service_method = {
    .ref_count = -1,
    .name      = (gchar *)"AuthorizeService",
    .in_args   = (GDBusArgInfo **)agent_service_in,
};

static const GDBusMethodInfo agent_cancel_method = { .ref_count = -1, .name = (gchar *)"Cancel" };

static const GDBusMethodInfo *const agent_methods[] = {
    &agent_release_method,
    &agent_pin_method,
    &agent_passkey_method,
    &agent_confirm_method,
    &agent_auth_method,
    &agent_service_method,
    &agent_cancel_method,
    NULL,
};

static GDBusInterfaceInfo agent_iface = {
    .ref_count = -1,
    .name      = (gchar *)"org.bluez.Agent1",
    .methods   = (GDBusMethodInfo **)agent_methods,
};

static const GDBusInterfaceVTable agent_vtable = {
    .method_call = agent_method_call_cb,
};

/* ---- lifecycle ---- */

void bt_power_setup() {
    loop = g_main_loop_new(NULL, FALSE);
    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!conn) {
        LV_LOG_ERROR("BT: cannot connect to system bus");
        return;
    }

    agent_reg_id = g_dbus_connection_register_object(conn, BT_AGENT_PATH, &agent_iface, &agent_vtable, NULL, NULL, NULL);
    LV_LOG_USER("BT: agent object registered path=%s id=%u cap=%s", BT_AGENT_PATH, agent_reg_id, BT_AGENT_CAPABILITY);
    ensure_agent();

    g_dbus_connection_signal_subscribe(conn, "org.bluez", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                                       NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, interfaces_added_cb, NULL, NULL);
    g_dbus_connection_signal_subscribe(conn, "org.bluez", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved",
                                       NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, interfaces_removed_cb, NULL, NULL);
    g_dbus_connection_signal_subscribe(conn, "org.bluez", "org.freedesktop.DBus.Properties", "PropertiesChanged", NULL,
                                       "org.bluez.Device1", G_DBUS_SIGNAL_FLAGS_NONE, device_props_changed_cb, NULL,
                                       NULL);
    g_dbus_connection_signal_subscribe(conn, "org.bluez", "org.freedesktop.DBus.Properties", "PropertiesChanged", NULL,
                                       "org.bluez.Battery1", G_DBUS_SIGNAL_FLAGS_NONE, battery_props_changed_cb, NULL,
                                       NULL);
    g_dbus_connection_signal_subscribe(conn, "org.bluez", "org.freedesktop.DBus.Properties", "PropertiesChanged", NULL,
                                       "org.bluez.Adapter1", G_DBUS_SIGNAL_FLAGS_NONE, adapter_props_changed_cb, NULL,
                                       NULL);

    g_dbus_connection_call(conn, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", NULL,
                           G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                           get_managed_objects_cb, NULL);

    loop_timer = lv_timer_create(loop_iterations_cb, 30, NULL);

    sub_wifi_enabled = Subscription(cfg.network.wifi_enabled()->subscribe(on_wifi_enabled_changed));
    on_wifi_enabled_changed(NULL, NULL);
}

void bt_cleanup() {
    sub_wifi_enabled.reset();
    if (loop_timer) {
        lv_timer_del(loop_timer);
        loop_timer = NULL;
    }
    if (conn) {
        if (agent_reg_id) {
            g_dbus_connection_unregister_object(conn, agent_reg_id);
            agent_reg_id = 0;
        }
        g_object_unref(conn);
        conn = NULL;
    }
    if (loop) {
        g_main_loop_unref(loop);
        loop = NULL;
    }
}

/* ---- status / power ---- */

bt_status_t bt_get_status() {
    if (!cfg.network.wifi_enabled()->get()) {
        return BT_OFF;
    }
    for (const auto &kv : dev_map) {
        if (kv.second.connected) {
            return BT_CONNECTED;
        }
    }
    return BT_ON;
}

void bt_power_on() {
    want_power_on = true;
    apply_power();
}

void bt_power_off() {
    want_power_on = false;
    apply_power();
}

static void apply_power() {
    LV_LOG_USER("BT apply_power want=%d adapter_present=%d conn=%p", want_power_on, adapter_present, (void *)conn);
    if (!conn || !adapter_present) {
        return;
    }
    g_dbus_connection_call(conn, "org.bluez", BT_ADAPTER_PATH, "org.freedesktop.DBus.Properties", "Set",
                           g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered",
                                         g_variant_new_boolean(want_power_on ? TRUE : FALSE)),
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, power_set_cb, NULL);
}

static void on_wifi_enabled_changed(Subject *s, void *user_data) {
    want_power_on = cfg.network.wifi_enabled()->get();
    LV_LOG_USER("BT wifi_enabled changed -> want_power_on=%d", want_power_on);
    apply_power();
    notify_changed();
}

/* ---- discovery ---- */

void bt_start_discovery() {
    if (!conn || scanning || current_op) {
        LV_LOG_USER("BT StartDiscovery skipped (conn=%p scanning=%d op=%s)", (void *)conn, scanning,
                    current_op ? current_op : "");
        return;
    }
    LV_LOG_USER("BT StartDiscovery");
    g_dbus_connection_call(conn, "org.bluez", BT_ADAPTER_PATH, "org.bluez.Adapter1", "StartDiscovery", NULL, NULL,
                           G_DBUS_CALL_FLAGS_NONE, -1, NULL, op_done_cb, (gpointer)"StartDiscovery");
}

void bt_stop_discovery() {
    if (!conn || !scanning) {
        LV_LOG_USER("BT StopDiscovery skipped (conn=%p scanning=%d)", (void *)conn, scanning);
        return;
    }
    LV_LOG_USER("BT StopDiscovery");
    g_dbus_connection_call(conn, "org.bluez", BT_ADAPTER_PATH, "org.bluez.Adapter1", "StopDiscovery", NULL, NULL,
                           G_DBUS_CALL_FLAGS_NONE, -1, NULL, op_done_cb, (gpointer)"StopDiscovery");
}

bool bt_is_scanning() {
    return scanning;
}

bool bt_is_busy() {
    return current_op != NULL;
}

/* ---- device operations ---- */

void bt_connect(const char *bdaddr) {
    if (!conn) {
        return;
    }
    if (current_op) {
        LV_LOG_USER("BT Connect ignored, %s pending", current_op);
        return;
    }
    if (scanning) {
        bt_stop_discovery();
    }
    char path[64];
    path_from_bdaddr(bdaddr, path, sizeof(path));
    LV_LOG_USER("BT Connect %s path=%s", bdaddr, path);
    current_op = "Connect";
    g_dbus_connection_call(conn, "org.bluez", path, "org.bluez.Device1", "Connect", NULL, NULL,
                           G_DBUS_CALL_FLAGS_NONE, BT_INTERACTIVE_TIMEOUT_MS, NULL, op_done_cb, (gpointer)"Connect");
}

void bt_disconnect(const char *bdaddr) {
    if (!conn) {
        return;
    }
    if (current_op) {
        LV_LOG_USER("BT Disconnect ignored, %s pending", current_op);
        return;
    }
    char path[64];
    path_from_bdaddr(bdaddr, path, sizeof(path));
    LV_LOG_USER("BT Disconnect %s path=%s", bdaddr, path);
    current_op = "Disconnect";
    g_dbus_connection_call(conn, "org.bluez", path, "org.bluez.Device1", "Disconnect", NULL, NULL,
                           G_DBUS_CALL_FLAGS_NONE, -1, NULL, op_done_cb, (gpointer)"Disconnect");
}

void bt_pair(const char *bdaddr) {
    if (!conn) {
        return;
    }
    if (current_op) {
        LV_LOG_USER("BT Pair ignored, %s pending", current_op);
        return;
    }
    if (scanning) {
        bt_stop_discovery();
    }
    char path[64];
    path_from_bdaddr(bdaddr, path, sizeof(path));
    LV_LOG_USER("BT Pair %s path=%s", bdaddr, path);
    current_op = "Pair";
    g_dbus_connection_call(conn, "org.bluez", path, "org.bluez.Device1", "Pair", NULL, NULL,
                           G_DBUS_CALL_FLAGS_NONE, BT_INTERACTIVE_TIMEOUT_MS, NULL, op_done_cb, (gpointer)"Pair");
}

void bt_remove(const char *bdaddr) {
    if (!conn) {
        return;
    }
    if (current_op) {
        LV_LOG_USER("BT RemoveDevice ignored, %s pending", current_op);
        return;
    }
    char path[64];
    path_from_bdaddr(bdaddr, path, sizeof(path));
    LV_LOG_USER("BT RemoveDevice %s path=%s", bdaddr, path);
    current_op = "RemoveDevice";
    g_dbus_connection_call(conn, "org.bluez", BT_ADAPTER_PATH, "org.bluez.Adapter1", "RemoveDevice",
                           g_variant_new("(o)", path), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, op_done_cb,
                           (gpointer)"RemoveDevice");
}

/* ---- device list ---- */

bt_dev_arr_t bt_get_devices() {
    bt_dev_arr_t arr;
    arr.count = 0;
    arr.dev_arr = NULL;

    if (dev_map.empty()) {
        return arr;
    }

    arr.count = (uint8_t)dev_map.size();
    arr.dev_arr = (bt_dev_info_t *)malloc(sizeof(bt_dev_info_t) * arr.count);
    uint8_t i = 0;
    for (const auto &kv : dev_map) {
        arr.dev_arr[i++] = kv.second;
    }
    return arr;
}

void bt_devices_info_delete(bt_dev_arr_t arr) {
    if (arr.dev_arr) {
        free(arr.dev_arr);
    }
}

/* ---- passkey agent ---- */

void bt_set_passkey_cb(bt_passkey_cb_t cb) {
    passkey_cb = cb;
}

void bt_agent_respond(const char *bdaddr, bool accept, const char *pin) {
    LV_LOG_USER("BT agent respond bdaddr=%s accept=%d pin=%s pending=%p", bdaddr ? bdaddr : "", accept,
                pin ? pin : "", (void *)pending_invocation);
    if (!pending_invocation) {
        return;
    }
    if (!accept) {
        g_dbus_method_invocation_return_error(pending_invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                              "User rejected pairing");
    } else {
        switch (pending_kind) {
        case BT_PASSKEY_PIN:
            g_dbus_method_invocation_return_value(pending_invocation, g_variant_new("(s)", pin ? pin : ""));
            break;
        case BT_PASSKEY_ENTRY:
            g_dbus_method_invocation_return_value(pending_invocation,
                                                  g_variant_new("(u)", (guint32)atoi(pin ? pin : "0")));
            break;
        default:
            g_dbus_method_invocation_return_value(pending_invocation, NULL);
            break;
        }
    }
    pending_invocation = NULL;
    pending_kind       = BT_PASSKEY_NONE;
}

static void set_pending(GDBusMethodInvocation *invocation, bt_passkey_kind_t kind, const char *dev_path,
                        const char *pin_hint) {
    pending_invocation = invocation;
    pending_kind       = kind;
    resolve_device(dev_path, pending_bdaddr, sizeof(pending_bdaddr), pending_name, sizeof(pending_name));
    if (pin_hint) {
        snprintf(pending_pin, sizeof(pending_pin), "%s", pin_hint);
    } else {
        pending_pin[0] = '\0';
    }
    LV_LOG_USER("BT agent pending kind=%d dev=%s bdaddr=%s name=%s pin=%s cb=%p", kind, dev_path, pending_bdaddr,
                pending_name, pending_pin, (void *)passkey_cb);
    if (passkey_cb) {
        passkey_cb(pending_bdaddr, pending_name, kind, pending_pin);
    }
}

static void agent_method_call_cb(GDBusConnection *connection, const char *sender, const char *object_path,
                                 const char *interface_name, const char *method_name, GVariant *parameters,
                                 GDBusMethodInvocation *invocation, gpointer user_data) {
    LV_LOG_USER("BT agent method call: %s", method_name);
    if (g_strcmp0(method_name, "Release") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Cancel") == 0) {
        pending_invocation = NULL;
        pending_kind       = BT_PASSKEY_NONE;
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "RequestPinCode") == 0) {
        const char *dev_path;
        g_variant_get(parameters, "(&o)", &dev_path);
        set_pending(invocation, BT_PASSKEY_PIN, dev_path, NULL);
    } else if (g_strcmp0(method_name, "RequestPasskey") == 0) {
        const char *dev_path;
        g_variant_get(parameters, "(&o)", &dev_path);
        set_pending(invocation, BT_PASSKEY_ENTRY, dev_path, NULL);
    } else if (g_strcmp0(method_name, "RequestConfirmation") == 0) {
        const char *dev_path;
        guint32     passkey;
        g_variant_get(parameters, "(&ou)", &dev_path, &passkey);
        LV_LOG_USER("BT agent auto-confirm %s passkey=%06u", dev_path, passkey);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "RequestAuthorization") == 0) {
        const char *dev_path;
        g_variant_get(parameters, "(&o)", &dev_path);
        LV_LOG_USER("BT agent auto-authorize %s", dev_path);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "AuthorizeService") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method '%s'", method_name);
    }
}

/* ---- helpers ---- */

static void path_from_bdaddr(const char *bdaddr, char *out, size_t sz) {
    snprintf(out, sz, "/org/bluez/hci0/dev_%s", bdaddr);
    for (char *p = out; *p; p++) {
        if (*p == ':') {
            *p = '_';
        }
    }
}

static void resolve_device(const char *dev_path, char *bdaddr_out, size_t bdaddr_sz, char *name_out, size_t name_sz) {
    bdaddr_out[0] = '\0';
    name_out[0]   = '\0';

    const char *slash = strrchr(dev_path, '/');
    if (!slash) {
        return;
    }
    const char *addr_start = slash + 1;
    if (strncmp(addr_start, "dev_", 4) != 0) {
        return;
    }
    addr_start += 4;
    size_t j = 0;
    for (size_t i = 0; addr_start[i] && j < bdaddr_sz - 1; i++) {
        if (addr_start[i] == '_') {
            bdaddr_out[j++] = ':';
        } else {
            bdaddr_out[j++] = addr_start[i];
        }
    }
    bdaddr_out[j] = '\0';

    auto it = dev_map.find(bdaddr_out);
    if (it != dev_map.end()) {
        snprintf(name_out, name_sz, "%s", it->second.name);
    } else {
        snprintf(name_out, name_sz, "Unknown");
    }
}

static bt_dev_class_t classify_icon(const char *icon) {
    if (!icon) {
        return BT_DEV_UNKNOWN;
    }
    if (g_str_has_prefix(icon, "audio")) {
        return BT_DEV_AUDIO;
    }
    if (g_str_has_prefix(icon, "input")) {
        return BT_DEV_KEYBOARD;
    }
    return BT_DEV_UNKNOWN;
}

static void cod_to_text(uint32_t cod, char *out, size_t sz) {
    uint32_t    major = (cod >> 8) & 0x1F;
    uint32_t    minor = (cod >> 2) & 0x3F;
    const char *name  = "Unknown";
    const char *sub   = NULL;

    switch (major) {
    case 0x00:
        name = "Miscellaneous";
        break;
    case 0x01:
        name = "Computer";
        switch (minor) {
        case 0x01: sub = "Desktop workstation"; break;
        case 0x02: sub = "Server"; break;
        case 0x03: sub = "Laptop"; break;
        case 0x04: sub = "Handheld PC"; break;
        case 0x05: sub = "Palm-size PC"; break;
        case 0x06: sub = "Wearable computer"; break;
        case 0x07: sub = "Tablet"; break;
        }
        break;
    case 0x02:
        name = "Phone";
        switch (minor) {
        case 0x01: sub = "Cellular"; break;
        case 0x02: sub = "Cordless"; break;
        case 0x03: sub = "Smartphone"; break;
        case 0x04: sub = "Wired modem"; break;
        case 0x05: sub = "ISDN access"; break;
        }
        break;
    case 0x03:
        name = "Network access point";
        break;
    case 0x04:
        name = "Audio/Video";
        switch (minor) {
        case 0x01: sub = "Wearable headset"; break;
        case 0x02: sub = "Hands-free"; break;
        case 0x04: sub = "Microphone"; break;
        case 0x05: sub = "Loudspeaker"; break;
        case 0x06: sub = "Headphones"; break;
        case 0x07: sub = "Portable audio"; break;
        case 0x08: sub = "Car audio"; break;
        case 0x09: sub = "Set-top box"; break;
        case 0x0A: sub = "HiFi audio"; break;
        case 0x0B: sub = "VCR"; break;
        case 0x0C: sub = "Video camera"; break;
        case 0x0D: sub = "Camcorder"; break;
        case 0x0E: sub = "Video monitor"; break;
        case 0x0F: sub = "Video display and loudspeaker"; break;
        case 0x10: sub = "Video conferencing"; break;
        case 0x12: sub = "Gaming/Toy"; break;
        }
        break;
    case 0x05:
        name = "Peripheral";
        if (minor & 0x20) {
            sub = "Pointing device";
        } else if (minor & 0x10) {
            sub = "Keyboard";
        } else if ((minor & 0x0C) == 0x04) {
            sub = "Joystick";
        } else if ((minor & 0x0C) == 0x08) {
            sub = "Gamepad";
        } else if ((minor & 0x0C) == 0x0C) {
            sub = "Remote control";
        } else if ((minor & 0x03) == 0x01) {
            sub = "Sensing device";
        } else if ((minor & 0x03) == 0x02) {
            sub = "Digitizer tablet";
        } else if ((minor & 0x03) == 0x03) {
            sub = "Card reader";
        }
        break;
    case 0x06:
        name = "Imaging";
        break;
    case 0x07:
        name = "Wearable";
        break;
    case 0x08:
        name = "Toy";
        break;
    case 0x09:
        name = "Health";
        break;
    case 0x1F:
        name = "Uncategorized";
        break;
    default:
        name = "Reserved";
        break;
    }

    if (sub) {
        snprintf(out, sz, "%s, %s", name, sub);
    } else {
        snprintf(out, sz, "%s", name);
    }
}

void bt_device_class_text(const bt_dev_info_t *info, char *out, size_t sz) {
    if (info->cod) {
        cod_to_text(info->cod, out, sz);
        return;
    }
    switch (info->dev_class) {
    case BT_DEV_AUDIO:
        snprintf(out, sz, "Audio/Video");
        break;
    case BT_DEV_KEYBOARD:
        snprintf(out, sz, "Peripheral, Keyboard");
        break;
    default:
        snprintf(out, sz, "Unknown");
        break;
    }
}

static void parse_device(GVariant *props) {
    bt_dev_info_t info;
    memset(&info, 0, sizeof(info));

    GVariantDict dict;
    g_variant_dict_init(&dict, props);

    GVariant *v = g_variant_dict_lookup_value(&dict, "Address", G_VARIANT_TYPE_STRING);
    if (v) {
        snprintf(info.bdaddr, sizeof(info.bdaddr), "%s", g_variant_get_string(v, NULL));
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Alias", G_VARIANT_TYPE_STRING);
    if (!v) {
        v = g_variant_dict_lookup_value(&dict, "Name", G_VARIANT_TYPE_STRING);
    }
    if (v) {
        snprintf(info.name, sizeof(info.name), "%s", g_variant_get_string(v, NULL));
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Paired", G_VARIANT_TYPE_BOOLEAN);
    if (v) {
        info.paired = g_variant_get_boolean(v);
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Connected", G_VARIANT_TYPE_BOOLEAN);
    if (v) {
        info.connected = g_variant_get_boolean(v);
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "RSSI", G_VARIANT_TYPE_INT16);
    if (v) {
        gint16 rssi = g_variant_get_int16(v);
        if (rssi < -128) {
            info.rssi = 0;
        } else if (rssi > 0) {
            info.rssi = 100;
        } else {
            info.rssi = (uint8_t)(rssi + 128);
        }
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Icon", G_VARIANT_TYPE_STRING);
    if (v) {
        info.dev_class = classify_icon(g_variant_get_string(v, NULL));
        g_variant_unref(v);
    } else {
        info.dev_class = BT_DEV_UNKNOWN;
    }
    v = g_variant_dict_lookup_value(&dict, "Class", G_VARIANT_TYPE_UINT32);
    if (v) {
        info.cod = g_variant_get_uint32(v);
        g_variant_unref(v);
    }
    info.known = info.paired;

    g_variant_dict_clear(&dict);

    if (info.bdaddr[0] != '\0') {
        auto it = dev_map.find(info.bdaddr);
        if (it != dev_map.end()) {
            info.has_battery = it->second.has_battery;
            info.battery     = it->second.battery;
        }
        dev_map[info.bdaddr] = info;
    }
}

static void parse_battery(const char *dev_path, GVariant *props) {
    char bdaddr[18];
    char name[248];
    resolve_device(dev_path, bdaddr, sizeof(bdaddr), name, sizeof(name));
    if (bdaddr[0] == '\0') {
        return;
    }

    auto it = dev_map.find(bdaddr);
    if (it == dev_map.end()) {
        return;
    }

    GVariantDict dict;
    g_variant_dict_init(&dict, props);

    GVariant *v = g_variant_dict_lookup_value(&dict, "Percentage", G_VARIANT_TYPE_BYTE);
    if (v) {
        it->second.has_battery = true;
        it->second.battery     = g_variant_get_byte(v);
        g_variant_unref(v);
    }

    g_variant_dict_clear(&dict);
}

static void update_device_props(const char *path, GVariant *props) {
    char bdaddr[18];
    char name[248];
    resolve_device(path, bdaddr, sizeof(bdaddr), name, sizeof(name));
    if (bdaddr[0] == '\0') {
        return;
    }

    auto it = dev_map.find(bdaddr);
    if (it == dev_map.end()) {
        return;
    }
    bt_dev_info_t &info = it->second;

    GVariantDict dict;
    g_variant_dict_init(&dict, props);

    GVariant *v = g_variant_dict_lookup_value(&dict, "Alias", G_VARIANT_TYPE_STRING);
    if (!v) {
        v = g_variant_dict_lookup_value(&dict, "Name", G_VARIANT_TYPE_STRING);
    }
    if (v) {
        snprintf(info.name, sizeof(info.name), "%s", g_variant_get_string(v, NULL));
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Paired", G_VARIANT_TYPE_BOOLEAN);
    if (v) {
        info.paired = g_variant_get_boolean(v);
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Connected", G_VARIANT_TYPE_BOOLEAN);
    if (v) {
        info.connected = g_variant_get_boolean(v);
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "RSSI", G_VARIANT_TYPE_INT16);
    if (v) {
        gint16 rssi = g_variant_get_int16(v);
        if (rssi < -128) {
            info.rssi = 0;
        } else if (rssi > 0) {
            info.rssi = 100;
        } else {
            info.rssi = (uint8_t)(rssi + 128);
        }
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Icon", G_VARIANT_TYPE_STRING);
    if (v) {
        info.dev_class = classify_icon(g_variant_get_string(v, NULL));
        g_variant_unref(v);
    }
    v = g_variant_dict_lookup_value(&dict, "Class", G_VARIANT_TYPE_UINT32);
    if (v) {
        info.cod = g_variant_get_uint32(v);
        g_variant_unref(v);
    }
    info.known = info.paired;

    g_variant_dict_clear(&dict);
}

/* ---- signal handlers ---- */

static void interfaces_added_cb(GDBusConnection *connection, const char *sender_name, const char *object_path,
                                const char *interface_name, const char *signal_name, GVariant *parameters,
                                gpointer user_data) {
    const char *path;
    GVariant   *ifaces;
    g_variant_get(parameters, "(&o@a{sa{sv}})", &path, &ifaces);

    if (g_strcmp0(path, BT_ADAPTER_PATH) == 0) {
        adapter_present = true;
        LV_LOG_USER("BT adapter added: %s", path);
        ensure_agent();
        apply_power();
    }
    if (g_str_has_prefix(path, "/org/bluez/hci0/dev_")) {
        GVariant *props;
        if (g_variant_lookup(ifaces, "org.bluez.Device1", "@a{sv}", &props)) {
            parse_device(props);
            g_variant_unref(props);
        }
        if (g_variant_lookup(ifaces, "org.bluez.Battery1", "@a{sv}", &props)) {
            parse_battery(path, props);
            g_variant_unref(props);
        }
    }
    g_variant_unref(ifaces);
    notify_changed();
}

static void interfaces_removed_cb(GDBusConnection *connection, const char *sender_name, const char *object_path,
                                  const char *interface_name, const char *signal_name, GVariant *parameters,
                                  gpointer user_data) {
    const char *path;
    GVariant   *ifaces;
    g_variant_get(parameters, "(&o@as)", &path, &ifaces);

    if (g_strcmp0(path, BT_ADAPTER_PATH) == 0) {
        adapter_present = false;
        agent_registered = false;
        LV_LOG_USER("BT adapter removed: %s", path);
    } else if (g_str_has_prefix(path, "/org/bluez/hci0/dev_")) {
        char bdaddr[18];
        char name[248];
        resolve_device(path, bdaddr, sizeof(bdaddr), name, sizeof(name));
        auto it = dev_map.find(bdaddr);

        GVariantIter iter;
        const char  *iface;
        g_variant_iter_init(&iter, ifaces);
        while (g_variant_iter_next(&iter, "&s", &iface)) {
            if (g_strcmp0(iface, "org.bluez.Device1") == 0) {
                if (it != dev_map.end()) {
                    dev_map.erase(it);
                    it = dev_map.end();
                }
            } else if (g_strcmp0(iface, "org.bluez.Battery1") == 0) {
                if (it != dev_map.end()) {
                    it->second.has_battery = false;
                    it->second.battery     = 0;
                }
            }
        }
    }
    g_variant_unref(ifaces);
    notify_changed();
}

static void device_props_changed_cb(GDBusConnection *connection, const char *sender_name, const char *object_path,
                                    const char *interface_name, const char *signal_name, GVariant *parameters,
                                    gpointer user_data) {
    const char *iface;
    GVariant   *changed;
    GVariant   *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, &invalidated);

    if (g_strcmp0(iface, "org.bluez.Device1") == 0) {
        update_device_props(object_path, changed);
        notify_changed();
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

static void battery_props_changed_cb(GDBusConnection *connection, const char *sender_name, const char *object_path,
                                     const char *interface_name, const char *signal_name, GVariant *parameters,
                                     gpointer user_data) {
    const char *iface;
    GVariant   *changed;
    GVariant   *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, &invalidated);

    if (g_strcmp0(iface, "org.bluez.Battery1") == 0) {
        parse_battery(object_path, changed);
        notify_changed();
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

static void adapter_props_changed_cb(GDBusConnection *connection, const char *sender_name, const char *object_path,
                                     const char *interface_name, const char *signal_name, GVariant *parameters,
                                     gpointer user_data) {
    const char *iface;
    GVariant   *changed;
    GVariant   *invalidated;
    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, &invalidated);

    if (g_strcmp0(iface, "org.bluez.Adapter1") == 0) {
        GVariantDict dict;
        g_variant_dict_init(&dict, changed);
        GVariant *v = g_variant_dict_lookup_value(&dict, "Discovering", G_VARIANT_TYPE_BOOLEAN);
        if (v) {
            scanning = g_variant_get_boolean(v);
            LV_LOG_USER("BT Discovering -> %d", scanning);
            g_variant_unref(v);
            notify_changed();
        }
        g_variant_dict_clear(&dict);
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

static void get_managed_objects_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
    GError   *error = NULL;
    GVariant *objects = g_dbus_connection_call_finish(conn, result, &error);
    if (error) {
        LV_LOG_ERROR("BT GetManagedObjects: %s", error->message);
        g_error_free(error);
        return;
    }
    if (objects) {
        bool         saw_adapter = false;
        GVariant    *managed = g_variant_get_child_value(objects, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, managed);
        const char *path;
        GVariant   *ifaces;
        while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &ifaces)) {
            if (g_strcmp0(path, BT_ADAPTER_PATH) == 0) {
                GVariant *tmp;
                if (g_variant_lookup(ifaces, "org.bluez.Adapter1", "@a{sv}", &tmp)) {
                    g_variant_unref(tmp);
                    saw_adapter = true;
                }
            }
            if (g_str_has_prefix(path, "/org/bluez/hci0/dev_")) {
                GVariant *props;
                if (g_variant_lookup(ifaces, "org.bluez.Device1", "@a{sv}", &props)) {
                    parse_device(props);
                    g_variant_unref(props);
                }
                if (g_variant_lookup(ifaces, "org.bluez.Battery1", "@a{sv}", &props)) {
                    parse_battery(path, props);
                    g_variant_unref(props);
                }
            }
            g_variant_unref(ifaces);
        }
        g_variant_unref(managed);
        g_variant_unref(objects);
        adapter_present = saw_adapter;
        LV_LOG_USER("BT GetManagedObjects done adapter_present=%d devices=%zu", adapter_present, dev_map.size());
        if (adapter_present) {
            ensure_agent();
        }
        apply_power();
        notify_changed();
    }
}

static void request_default_agent() {
    LV_LOG_USER("BT RequestDefaultAgent");
    g_dbus_connection_call(conn, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
                           g_variant_new("(o)", BT_AGENT_PATH), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, op_done_cb,
                           (gpointer)"RequestDefaultAgent");
}

static void ensure_agent() {
    if (!conn) {
        return;
    }
    if (agent_registered) {
        request_default_agent();
        return;
    }
    LV_LOG_USER("BT RegisterAgent request");
    g_dbus_connection_call(conn, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
                           g_variant_new("(os)", BT_AGENT_PATH, BT_AGENT_CAPABILITY), NULL,
                           G_DBUS_CALL_FLAGS_NONE, -1, NULL, agent_registered_cb, NULL);
}

static void agent_registered_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
    GError   *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(conn, result, &error);
    if (error) {
        bool already = error->message && strstr(error->message, "AlreadyExists");
        LV_LOG_USER("BT RegisterAgent error: %s", error->message ? error->message : "");
        g_error_free(error);
        if (!already) {
            return;
        }
    }
    if (reply) {
        g_variant_unref(reply);
    }
    agent_registered = true;
    LV_LOG_USER("BT agent registered, requesting default agent");
    request_default_agent();
}

static void op_done_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
    const char *op    = user_data ? (const char *)user_data : "call";
    GError     *error = NULL;
    GVariant   *reply = g_dbus_connection_call_finish(conn, result, &error);
    if (error) {
        LV_LOG_ERROR("BlueZ %s failed: %s", op, error->message);
        g_error_free(error);
    } else {
        LV_LOG_USER("BlueZ %s ok", op);
    }
    if (reply) {
        g_variant_unref(reply);
    }
    if (current_op && g_strcmp0(current_op, op) == 0) {
        current_op = NULL;
    }
    notify_changed();
}

static void power_set_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
    GError   *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(conn, result, &error);
    if (error) {
        const char *msg = error->message ? error->message : "";
        if (strstr(msg, "UnknownObject") || strstr(msg, "org.bluez.Error.Failed")) {
            LV_LOG_INFO("BT power transition: %s", msg);
        } else {
            LV_LOG_ERROR("BT power: %s", msg);
        }
        g_error_free(error);
    }
    if (reply) {
        g_variant_unref(reply);
    }
}

static void loop_iterations_cb(lv_timer_t *t) {
    if (!app_is_running) {
        lv_timer_del(t);
    } else {
        g_main_context_iteration(g_main_loop_get_context(loop), FALSE);
    }
}

static void notify_changed() {
    lv_msg_send(MSG_BT_STATE_CHANGED, NULL);
}

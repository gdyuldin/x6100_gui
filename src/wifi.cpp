/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */


#include "wifi.h"

extern "C" {

#include "msg.h"
#include "params/params.h"
#include "pubsub_ids.h"

#include <aether_radio/x6100_control/low/gpio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

}

#include <string>
#include <map>
#include <vector>
#include <array>

#define WLAN_IFACE "wlan0"
#define EMPTY_SSID_STR "--"

static GMainLoop *loop;
static NMClient  *client = NULL;
static NMDevice  *device = NULL;

static lv_timer_t *loop_timer;
static lv_timer_t *scan_timer = NULL;

// static wifi_ap_change_cb ap_add_cb=NULL;
// static wifi_ap_change_cb ap_del_cb=NULL;

static wifi_status_t status = WIFI_OFF;
static bool          scanning = false;
static uint64_t      last_scan;

static void loop_iterations_cb(lv_timer_t *);
static void update_scan_status_cb(lv_timer_t *t);

static void setup_nm_client();
static void setup_wifi_device();
static void fill_access_point_info(GBytes *active_ssid, NMAccessPoint *ap, wifi_ap_info_t *ap_info);

static void set_status(wifi_status_t val);

static void device_added_sig_cb(NMClient *client, GObject *device, gpointer user_data);
static void device_state_changed_sig_cb(NMDevice *device, guint new_state, guint old_state, guint reason,
                                        gpointer user_data);
// static void access_point_added_sig_cb(NMDeviceWifi *device, GObject *ap, gpointer user_data);
// static void access_point_removed_sig_cb(NMDeviceWifi *device, GObject *ap, gpointer user_data);
static void active_con_state_changed_sig_cb(NMActiveConnection *active_connection, guint state, guint reason,
                                            gpointer user_data);

static void device_disconnection_cb(GObject *client, GAsyncResult *result, gpointer user_data);
static void scan_request_finishing_cb(GObject *device, GAsyncResult *result, gpointer user_data);
static void connection_adding_cb(GObject *client, GAsyncResult *result, gpointer user_data);
static void connection_adding_and_activating_cb(GObject *client, GAsyncResult *result, gpointer user_data);
static void connection_modify_cb(GObject *connection, GAsyncResult *result, gpointer user_data);
static void connection_delete_cb(GObject *connection, GAsyncResult *result, gpointer user_data);
static void connection_activating_cb(GObject *client, GAsyncResult *result, gpointer user_data);

void wifi_power_setup() {
    set_status(WIFI_DISCONNECTED);
    loop = g_main_loop_new(NULL, FALSE);
    setup_nm_client();
    if (client != NULL) {
        device = nm_client_get_device_by_iface(client, WLAN_IFACE);
        if (device) {
            setup_wifi_device();
        }
    }
    loop_timer = lv_timer_create(loop_iterations_cb, 30, NULL);

    if (params.wifi_enabled.x)
        wifi_power_on();
    else
        wifi_power_off();
}

void wifi_power_on() {
    LV_LOG_USER("Power on wifi/bt");
    params_bool_set(&params.wifi_enabled, true);
    x6100_gpio_set(x6100_pin_wifi, 0);
    if (!device) {
        set_status(WIFI_STARTING);
    }
    lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
}

void wifi_power_off() {
    LV_LOG_USER("Power off wifi/bt");
    set_status(WIFI_OFF);
    if (device) {
        device = NULL;
    }
    params_bool_set(&params.wifi_enabled, false);
    x6100_gpio_set(x6100_pin_wifi, 1);
    if (scan_timer) {
        lv_timer_del(scan_timer);
        scan_timer = NULL;
    }
    scanning = false;
}

// void wifi_set_change_ap_callbacks(wifi_ap_change_cb add_cb, wifi_ap_change_cb del_cb) {
//     ap_add_cb = add_cb;
//     ap_del_cb = del_cb;
// }

// void wifi_clear_change_ap_callbacks() {
//     ap_add_cb = NULL;
//     ap_del_cb = NULL;
// }

wifi_status_t wifi_get_status() {
    return status;
}

void wifi_start_scan() {
    if (device != NULL) {
        last_scan = nm_device_wifi_get_last_scan(NM_DEVICE_WIFI(device));
        nm_device_wifi_request_scan_async(NM_DEVICE_WIFI(device), NULL, scan_request_finishing_cb, loop);
    }
}

bool wifi_scanning() {
    return scanning;
}

wifi_ap_arr_t wifi_get_available_access_points() {
    const GPtrArray *aps;
    NMAccessPoint   *active_ap = NULL;
    GBytes          *active_ssid = NULL;
    uint             i;

    wifi_ap_arr_t aps_info;

    aps_info.count = 0;
    aps_info.ap_arr = NULL;
    aps_info.is_connected = false;

    if (device != NULL) {
        /* Get active AP */
        if (nm_device_get_state(device) == NM_DEVICE_STATE_ACTIVATED) {
            if ((active_ap = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device)))) {
                active_ssid = nm_access_point_get_ssid(active_ap);
            }
        }

        aps = nm_device_wifi_get_access_points(NM_DEVICE_WIFI(device));

        // map ssid -> [strength, ap_vec_id]
        std::map<std::string, std::array<uint, 2>> ssid_strength_map{};
        std::vector<wifi_ap_info_t>                ap_info_vec;

        for (i = 0; i < aps->len; i++) {
            wifi_ap_info_t ap_info;
            NMAccessPoint *ap = NM_ACCESS_POINT(g_ptr_array_index(aps, i));
            fill_access_point_info(active_ssid, ap, &ap_info);

            std::string ssid = ap_info.ssid;

            auto search = ssid_strength_map.find(ssid);
            if ((search != ssid_strength_map.end()) && (ssid.compare(EMPTY_SSID_STR) !=0 )) {
                if (search->second[0] < ap_info.strength) {
                    ap_info_vec[ssid_strength_map[ssid][1]] = ap_info;
                    ssid_strength_map[ssid] = {ap_info.strength, i};
                }

            } else {
                ssid_strength_map[ssid] = {
                    {ap_info.strength, ap_info_vec.size()}
                };
                ap_info_vec.push_back(ap_info);
            }
        }

        aps_info.count = ssid_strength_map.size();
        aps_info.ap_arr = (wifi_ap_info_t *)malloc(sizeof(wifi_ap_info_t) * aps_info.count);
        for (i = 0; i < aps_info.count; i++) {
            aps_info.ap_arr[i] = ap_info_vec[i];
            aps_info.is_connected |= aps_info.ap_arr[i].is_connected;
        }
    }
    return aps_info;
}

void wifi_aps_info_delete(wifi_ap_arr_t aps_info) {
    if (aps_info.ap_arr != NULL) {
        free(aps_info.ap_arr);
    }
}

void wifi_add_connection(const char *ssid, const char *password) {
    NMConnection              *connection;
    NMSettingConnection       *s_con;
    NMSettingWireless         *s_wireless;
    NMSettingWirelessSecurity *s_wsec;
    NMSettingIP4Config        *s_ip4;
    GBytes                    *g_ssid;
    char                      *uuid;

    /* Create a new connection object */
    connection = nm_simple_connection_new();

    /* Build up the 'connection' Setting */
    s_con = (NMSettingConnection *)nm_setting_connection_new();
    uuid = nm_utils_uuid_generate();
    // clang-format off
    g_object_set(G_OBJECT(s_con),
				 NM_SETTING_CONNECTION_UUID, uuid,
				 NM_SETTING_CONNECTION_ID, ssid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
				 NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
                 NM_SETTING_CONNECTION_AUTOCONNECT_RETRIES, 1,
				 NM_SETTING_CONNECTION_INTERFACE_NAME, WLAN_IFACE,
				 NULL);
    // clang-format on
    g_free(uuid);
    nm_connection_add_setting(connection, NM_SETTING(s_con));

    /* Build up the 'wireless' Setting */
    // nm_utils_ap_mode_security_valid
    g_ssid = g_bytes_new(ssid, strlen(ssid));
    s_wireless = (NMSettingWireless *)nm_setting_wireless_new();
    // clang-format off
    g_object_set(G_OBJECT(s_wireless),
				 NM_SETTING_WIRELESS_SSID, g_ssid,
				 NM_SETTING_WIRELESS_MODE, NM_SETTING_WIRELESS_MODE_INFRA,
				 NULL);
    // clang-format on
    nm_connection_add_setting(connection, NM_SETTING(s_wireless));

    /* Wifi security */
    if (password) {
        s_wsec = (NMSettingWirelessSecurity *)nm_setting_wireless_security_new();
        // clang-format off
        g_object_set(G_OBJECT(s_wsec),
                    NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open",
                    NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
                    NM_SETTING_WIRELESS_SECURITY_PSK, password,
                    NULL);
        // clang-format on
        nm_connection_add_setting(connection, NM_SETTING(s_wsec));
    }

    /* Build up the 'ipv4' Setting */
    s_ip4 = (NMSettingIP4Config *)nm_setting_ip4_config_new();
    // clang-format off
    g_object_set(G_OBJECT(s_ip4),
			     NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO,
				 NULL);
    // clang-format on
    nm_connection_add_setting(connection, NM_SETTING(s_ip4));

    // /* Build up the 'ipv6' Setting */
    // s_ip6 = (NMSettingIP6Config *) nm_setting_ip6_config_new ();
    // g_object_set (G_OBJECT (s_ip6),
    //               NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_,
    //               NULL);
    // nm_connection_add_setting (connection, NM_SETTING (s_ip4));

    LV_LOG_USER("Adding connection");
    // nm_client_add_connection_async(client, connection, TRUE, NULL, connection_adding_cb, NULL);
    nm_client_add_and_activate_connection_async(client, connection, device, NULL, NULL,
                                                connection_adding_and_activating_cb, NULL);
    g_object_unref(connection);
}

void wifi_update_connection(const char *id, const char *password) {
    NMRemoteConnection        *rem_con = NULL;
    NMConnection              *new_connection;
    NMSettingWirelessSecurity *s_wsec;
    gboolean                   temporary = FALSE;

    rem_con = nm_client_get_connection_by_id(client, id);
    if (rem_con) {
        new_connection = nm_simple_connection_new_clone(NM_CONNECTION(rem_con));
        nm_connection_remove_setting(new_connection, NM_TYPE_SETTING_WIRELESS_SECURITY);
        if (password) {
            s_wsec = (NMSettingWirelessSecurity *)nm_setting_wireless_security_new();
            // clang-format off
            g_object_set(s_wsec,
                        NM_SETTING_WIRELESS_SECURITY_AUTH_ALG, "open",
                        NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
                        NM_SETTING_WIRELESS_SECURITY_PSK, password,
                        NULL);
            // clang-format on
            nm_connection_add_setting(new_connection, NM_SETTING(s_wsec));
        }
        nm_connection_replace_settings_from_connection(NM_CONNECTION(rem_con), new_connection);
        nm_remote_connection_commit_changes_async(rem_con, !temporary, NULL, connection_modify_cb, NULL);
        g_object_unref(new_connection);
    } else {
        LV_LOG_ERROR("Connection %s not found", id);
    }
}

void wifi_delete_connection(const char *id) {
    NMRemoteConnection *rem_con;

    rem_con = nm_client_get_connection_by_id(client, id);
    nm_remote_connection_delete_async(rem_con, NULL, connection_delete_cb, NULL);
}

void wifi_connect(const char *id) {
    NMRemoteConnection  *rem_con = NULL;
    NMSettingConnection *s_con;
    NMConnection        *connection = NULL;
    gboolean             temporary = FALSE;
    const char          *uuid;
    const char          *password;

    if (device != NULL) {
        rem_con = nm_client_get_connection_by_id(client, id);
        if (rem_con != NULL) {
            nm_client_activate_connection_async(client, NM_CONNECTION(rem_con), device, NULL, NULL,
                                                connection_activating_cb, loop);
        } else {
            LV_LOG_WARN("Connection with id=%s is not found", id);
        }
    }
}

void wifi_disconnect() {
    if (device != NULL) {
        nm_device_disconnect_async(device, NULL, device_disconnection_cb, NULL);
    }
}

bool wifi_get_ipaddr(char **ip_addr, char **gateway) {
    NMIPConfig *ip_cfg;
    if (device) {
        ip_cfg = nm_device_get_ip4_config(device);
        if (ip_cfg) {
            const char * gw = nm_ip_config_get_gateway(ip_cfg);
            if(gw) {
                strcpy(*gateway, gw);
                GPtrArray *addresses = nm_ip_config_get_addresses(ip_cfg);
                if (addresses->len > 0) {
                    NMIPAddress *address = (NMIPAddress *)g_ptr_array_index(addresses, 0);
                    strcpy(*ip_addr, nm_ip_address_get_address(address));
                }
                return true;
            }
        }
    }
    return false;
}

static void loop_iterations_cb(lv_timer_t *t) {
    g_main_context_iteration(g_main_loop_get_context(loop), FALSE);
}

static void update_scan_status_cb(lv_timer_t *t) {
    uint64_t val;

    if (device) {
        val = nm_device_wifi_get_last_scan(NM_DEVICE_WIFI(device));
        if (val != last_scan) {
            LV_LOG_USER("Scan is finished");
            scanning = false;
            last_scan = val;
            lv_timer_del(scan_timer);
            scan_timer = NULL;
            lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
        }
    }
}

static void setup_nm_client() {
    GError *error = NULL;

    /* Get NMClient object */
    client = nm_client_new(NULL, &error);
    if (!client) {
        LV_LOG_ERROR("Could not create NMClient: %s.", error->message);
        g_error_free(error);
    }
    g_signal_connect(client, "device-added", G_CALLBACK(device_added_sig_cb), NULL);
}

static void setup_wifi_device() {
    NMActiveConnection *active_con;

    LV_LOG_USER("Setup wlan0 device");
    g_signal_connect(device, "state-changed", G_CALLBACK(device_state_changed_sig_cb), NULL);
    // g_signal_connect(device, "access-point-added", G_CALLBACK(access_point_added_sig_cb), NULL);
    // g_signal_connect(device, "access-point-removed", G_CALLBACK(access_point_removed_sig_cb), NULL);
    active_con = nm_device_get_active_connection(device);
    if (active_con) {
        g_signal_connect(active_con, "state-changed", G_CALLBACK(active_con_state_changed_sig_cb), NULL);
        set_status(WIFI_CONNECTED);
        // g_object_unref(active_con);
    } else {
        set_status(WIFI_DISCONNECTED);
    }
}

static void device_added_sig_cb(NMClient *client, GObject *dev, gpointer user_data) {
    NMDevice           *nm_dev;
    NMActiveConnection *active_con;
    nm_dev = NM_DEVICE(dev);
    if (strcmp(nm_device_get_iface(nm_dev), WLAN_IFACE) == 0) {
        device = nm_dev;
        setup_wifi_device();
    }
}

static void fill_access_point_info(GBytes *active_ssid, NMAccessPoint *ap, wifi_ap_info_t *ap_info) {
    NMRemoteConnection *rem_con;
    guint32             flags, wpa_flags, rsn_flags;
    guint8              strength;
    GBytes             *ssid;
    const char         *hwaddr;
    NM80211Mode         mode;
    char               *ssid_str, *wpa_flags_str, *rsn_flags_str;

    /* Get AP properties */
    flags = nm_access_point_get_flags(ap);
    wpa_flags = nm_access_point_get_wpa_flags(ap);
    rsn_flags = nm_access_point_get_rsn_flags(ap);
    ssid = nm_access_point_get_ssid(ap);
    hwaddr = nm_access_point_get_bssid(ap);
    mode = nm_access_point_get_mode(ap);
    strength = nm_access_point_get_strength(ap);

    /* Convert to strings */
    if (ssid)
        ssid_str = nm_utils_ssid_to_utf8((const guint8 *)g_bytes_get_data(ssid, NULL), g_bytes_get_size(ssid));
    else
        ssid_str = g_strdup(EMPTY_SSID_STR);
    strcpy(ap_info->bssid, hwaddr);
    strcpy(ap_info->ssid, ssid_str);
    ap_info->strength = strength;
    ap_info->mode = mode;
    ap_info->is_connected = active_ssid && (g_bytes_compare(ssid, active_ssid) == 0);

    rem_con = nm_client_get_connection_by_id(client, ssid_str);
    ap_info->known = rem_con != NULL;

    /* Set password validator */
    if (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK) {
        // WPA2
        ap_info->password_validator = nm_utils_wpa_psk_valid;
    } else if (flags == NM_802_11_AP_FLAGS_NONE) {
        // Open
        ap_info->password_validator = NULL;
    }

    // printf("%s\t%#010x\t%#010x\t%#010x\n", ssid_str, flags, wpa_flags, rsn_flags);

    g_free(ssid_str);
}

static void set_status(wifi_status_t val) {
    wifi_status_t prev_val = status;
    status = val;
    if (val != prev_val) {
        lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
    }
}

static void scan_request_finishing_cb(GObject *device, GAsyncResult *result, gpointer user_data) {
    GError *error = NULL;
    bool    res;

    res = nm_device_wifi_request_scan_finish(NM_DEVICE_WIFI(device), result, &error);

    if (!res) {
        LV_LOG_ERROR("Error during starting scan: %s", error->message);
        g_error_free(error);
    } else {
        LV_LOG_USER("Scan is started");
        scanning = true;
        scan_timer = lv_timer_create(update_scan_status_cb, 500, NULL);
    }
    lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
    g_main_loop_quit(loop);
}

static void connection_adding_cb(GObject *client, GAsyncResult *result, gpointer user_data) {
    NMRemoteConnection *remote;
    GError             *error = NULL;

    /* NM responded to our request; either handle the resulting error or
     * print out the object path of the connection we just added.
     */
    remote = nm_client_add_connection_finish(NM_CLIENT(client), result, &error);

    if (error) {
        LV_LOG_ERROR("Error adding connection: %s (code: %zu, domain: %zu)", error->message, error->code,
                     error->domain);
        // stort password - (code: 7, domain: 53)
        g_error_free(error);
    } else {
        LV_LOG_USER("Added: %s\n", nm_connection_get_path(NM_CONNECTION(remote)));
        g_object_unref(remote);
    }

    /* Tell the mainloop we're done and we can quit now */
    g_main_loop_quit(loop);
}

static void connection_adding_and_activating_cb(GObject *client, GAsyncResult *result, gpointer user_data) {
    NMActiveConnection *active_con;
    GError             *error = NULL;

    active_con = nm_client_add_and_activate_connection_finish(NM_CLIENT(client), result, &error);

    if (error) {
        LV_LOG_ERROR("Error adding and activating connection: %s (code: %zu, domain: %zu)", error->message, error->code,
                     error->domain);
        // stort password - (code: 7, domain: 53)
        g_error_free(error);
    } else {
        LV_LOG_USER("Activated: %s\n", nm_active_connection_get_id(active_con));
        set_status(WIFI_CONNECTING);
        g_signal_connect(active_con, "state-changed", G_CALLBACK(active_con_state_changed_sig_cb), NULL);
        g_object_unref(active_con);
    }

    /* Tell the mainloop we're done and we can quit now */
    g_main_loop_quit(loop);
}

static void connection_modify_cb(GObject *connection, GAsyncResult *result, gpointer user_data) {
    GError *error = NULL;

    if (!nm_remote_connection_commit_changes_finish(NM_REMOTE_CONNECTION(connection), result, &error)) {
        LV_LOG_ERROR(("Error: Failed to modify connection '%s': %s"), nm_connection_get_id(NM_CONNECTION(connection)),
                     error->message);
    } else {
        LV_LOG_USER(("Connection '%s' (%s) successfully modified.\n"), nm_connection_get_id(NM_CONNECTION(connection)),
                    nm_connection_get_uuid(NM_CONNECTION(connection)));
    }
    g_main_loop_quit(loop);
}

static void connection_delete_cb(GObject *connection, GAsyncResult *result, gpointer user_data) {
    GError *error = NULL;

    if (!nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(connection), result, &error)) {
        LV_LOG_ERROR(("Error: Failed to delete connection '%s': %s"), nm_connection_get_id(NM_CONNECTION(connection)),
                     error->message);
    } else {
        LV_LOG_USER(("Connection '%s' (%s) successfully deleted.\n"), nm_connection_get_id(NM_CONNECTION(connection)),
                    nm_connection_get_uuid(NM_CONNECTION(connection)));
    }
    g_main_loop_quit(loop);
}

static void device_state_changed_sig_cb(NMDevice *device, guint new_state, guint old_state, guint reason,
                                        gpointer user_data) {
    LV_LOG_INFO("Old state: %zu, new state: %zu, reason: %zu", old_state, new_state, reason);
    switch (new_state) {
    case NM_DEVICE_STATE_FAILED:
        if (reason == NM_DEVICE_STATE_REASON_NO_SECRETS) {
            LV_LOG_WARN("Wrong password");
            msg_update_text_fmt("Wrong WiFi password");
        }
        break;
    case NM_DEVICE_STATE_ACTIVATED:
        set_status(WIFI_CONNECTED);
        break;
    case NM_DEVICE_STATE_DISCONNECTED:
        set_status(WIFI_DISCONNECTED);
        break;
    }
}

// static void access_point_added_sig_cb(NMDeviceWifi *device, GObject *ap, gpointer user_data) {
//     wifi_ap_info_t ap_info;
//     NMAccessPoint *active_ap = NULL;
//     GBytes        *active_ssid = NULL;

//     if ((active_ap = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device)))) {
//         active_ssid = nm_access_point_get_ssid(active_ap);
//     }
//     printf("Access point added\n");
//     if (ap_add_cb) {
//         fill_access_point_info(active_ssid, NM_ACCESS_POINT(ap), &ap_info);
//         ap_add_cb(&ap_info);
//         if (active_ap) {
//             g_object_unref(active_ap);
//             g_object_unref(active_ssid);
//         }
//     }
// }
// static void access_point_removed_sig_cb(NMDeviceWifi *device, GObject *ap, gpointer user_data) {
//     wifi_ap_info_t ap_info;
//     NMAccessPoint *active_ap = NULL;
//     GBytes        *active_ssid = NULL;

//     if ((active_ap = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device)))) {
//         active_ssid = nm_access_point_get_ssid(active_ap);
//     }
//     printf("Access point removed\n");
//     if (ap_del_cb) {
//         fill_access_point_info(active_ssid, NM_ACCESS_POINT(ap), &ap_info);
//         ap_del_cb(&ap_info);
//         if (active_ap) {
//             g_object_unref(active_ap);
//             g_object_unref(active_ssid);
//         }
//     }
// }

static void active_con_state_changed_sig_cb(NMActiveConnection *active_connection, guint state, guint reason,
                                            gpointer user_data) {
    LV_LOG_INFO("Active con state change -  state: %zu, reason: %zu", state, reason);
    lv_msg_send(MSG_WIFI_STATE_CHANGED, NULL);
}

static void connection_activating_cb(GObject *client, GAsyncResult *result, gpointer user_data) {
    NMActiveConnection *active_con;
    GError             *error = NULL;

    active_con = nm_client_activate_connection_finish(NM_CLIENT(client), result, &error);

    if (error) {
        LV_LOG_ERROR("Error activating connection: %s (code: %zu, domain: %zu)", error->message, error->code,
                     error->domain);
        g_error_free(error);
    } else {
        LV_LOG_USER("Activated: %s\n", nm_active_connection_get_id(active_con));
        set_status(WIFI_CONNECTING);
        g_signal_connect(active_con, "state-changed", G_CALLBACK(active_con_state_changed_sig_cb), NULL);
        g_object_unref(active_con);
    }

    g_main_loop_quit(loop);
}

static void device_disconnection_cb(GObject *device, GAsyncResult *result, gpointer user_data) {
    GError *error = NULL;
    bool    res;

    res = nm_device_disconnect_finish(NM_DEVICE(device), result, &error);

    if (!res) {
        LV_LOG_ERROR("Error disconnecting: %s", error->message);
        g_error_free(error);
    } else {
        LV_LOG_USER("%s disconnected", nm_device_get_iface(NM_DEVICE(device)));
        set_status(WIFI_DISCONNECTED);
    }
    g_main_loop_quit(loop);
}

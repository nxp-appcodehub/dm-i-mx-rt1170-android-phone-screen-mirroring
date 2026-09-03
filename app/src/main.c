/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/socket.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/wifi_mgmt.h>

#include "screen.h"
#include "decode.h"
#include "stats.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#define MY_PORT          5000
#define MAX_CLIENT_QUEUE 2

#define WIFI_AP_SSID "screen-mirror-wifi"
#define WIFI_AP_PSK  "nxpdemo2025"
#define MACSTR       "%02X:%02X:%02X:%02X:%02X:%02X"
#define NET_EVENT_WIFI_MASK                                                                        \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                        \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |                      \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

enum app_state {
	APP_WAIT_FOR_CLIENT,
	APP_RUNNING,
	APP_ERROR,
};

K_EVENT_DEFINE(application_event);

int main(void)
{
	struct sockaddr_in addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int ret, sock, video_sock;
	struct net_if *iface;
	enum app_state cur_state, next_state;

	iface = net_if_get_default();

#if defined(CONFIG_USB_DEVICE_NETWORK_ECM)
	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Cannot enable USB (%d)", ret);
		return ret;
	}

	/* Looking for the netusb to configure to override the default iface */
	STRUCT_SECTION_FOREACH(net_if, i_iface) {
		if (strncmp(net_if_get_device(i_iface)->name, "eth_netusb", 10) == 0) {
			iface = i_iface;
			break;
		}
	}
#elif defined(CONFIG_WIFI)
	STRUCT_SECTION_FOREACH(net_if, i_iface) {
		if (strncmp(net_if_get_device(i_iface)->name, "ua", 2) == 0) {
			iface = i_iface;
			break;
		}
	}
	static struct wifi_connect_req_params ap_config;

	if (!iface) {
		LOG_INF("AP: is not initialized");
		return -EIO;
	}

	LOG_INF("Turning on AP Mode");
	ap_config.ssid = (const uint8_t *)WIFI_AP_SSID;
	ap_config.ssid_length = strlen(WIFI_AP_SSID);
	ap_config.psk = (const uint8_t *)WIFI_AP_PSK;
	ap_config.psk_length = strlen(WIFI_AP_PSK);
	ap_config.channel = WIFI_CHANNEL_ANY;
	ap_config.band = WIFI_FREQ_BAND_5_GHZ;
	ap_config.bandwidth = WIFI_FREQ_BANDWIDTH_40MHZ;

	if (strlen(WIFI_AP_PSK) == 0) {
		ap_config.security = WIFI_SECURITY_TYPE_NONE;
	} else {

		ap_config.security = WIFI_SECURITY_TYPE_PSK;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_config,
		       sizeof(struct wifi_connect_req_params));
#endif

	LOG_INF("Protocol %s is selected", iface->if_dev->dev->name);

	/* Prepare Network */
	(void)memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(MY_PORT);

	sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Failed to create TCP socket: %d", errno);
		return 0;
	}

	ret = zsock_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind TCP socket: %d", errno);
		zsock_close(sock);
		return 0;
	}

	ret = zsock_listen(sock, MAX_CLIENT_QUEUE);
	if (ret < 0) {
		LOG_ERR("Failed to listen on TCP socket: %d", errno);
		zsock_close(sock);
		return 0;
	}

	const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		LOG_ERR("%s: display device not ready.", display_dev->name);
		return 0;
	}

	int err = display_setup(display_dev);
	if (err) {
		LOG_ERR("Unable to set up display");
		return err;
	}

	control_init();

#if defined(CONFIG_STATS_MONITOR)
	/* Start the background FPS and CPU load monitor thread. */
	stats_monitor_start();
#endif /* CONFIG_STATS_MONITOR */

	cur_state = APP_WAIT_FOR_CLIENT;
	next_state = APP_WAIT_FOR_CLIENT;

	while (1) {
		switch (cur_state) {
		case APP_WAIT_FOR_CLIENT:
			if (net_if_oper_state(iface) != NET_IF_OPER_UP) {
				k_msleep(1000);
				break;
			}

			printk("TCP: Waiting for client...\n");
			video_sock = zsock_accept(sock, (struct sockaddr *)&client_addr,
						  &client_addr_len);
			if (video_sock < 0) {
				printk("Failed to accept: %d\n", errno);
				next_state = APP_ERROR;
			} else {
				printk("TCP: Accepted connection\n");
				client_addr.sin_port = htons(MY_PORT);
				decode_start(&video_sock);

				/* connect_control_socket() may return before the
				 * control connection is established if the video
				 * decode path tore down while it was waiting. In
				 * that case tear everything back down and go back
				 * to waiting for a fresh client instead of moving
				 * to APP_RUNNING with a half-connected session.
				 */
				if (connect_control_socket(sock, &client_addr) != 0) {
					printk("TCP: Control socket not established, "
					       "resetting session\n");
					decode_stop();
					disconnect_control_socket();
					/* Clear any pending events so the next
					 * session starts from a clean slate and
					 * does not tear down immediately on a
					 * stale EVENT_SOCKET_THREAD_STOP bit.
					 */
					k_event_clear(&application_event,
						      EVENT_SOCKET_THREAD_STOP | EVENT_TOUCH_ERROR);
					next_state = APP_WAIT_FOR_CLIENT;

				} else {
					next_state = APP_RUNNING;
				}
			}

			break;
		case APP_RUNNING:
			k_event_wait(&application_event,
				     EVENT_SOCKET_THREAD_STOP | EVENT_TOUCH_ERROR, true, K_FOREVER);
			decode_stop();
			disconnect_control_socket();
			next_state = APP_WAIT_FOR_CLIENT;
			break;
		case APP_ERROR:
			return -EIO;
		}
		cur_state = next_state;
	}

	return 0;
}

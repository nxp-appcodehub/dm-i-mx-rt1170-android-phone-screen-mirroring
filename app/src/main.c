/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>

#include "screen.h"
#include "decode.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#define MY_PORT          5000
#define MAX_CLIENT_QUEUE 2

static struct in_addr server_addr = {{{192, 0, 2, 1}}};
static struct in_addr base_addr = {{{192, 0, 2, 2}}};
static struct in_addr netmask = {{{255, 255, 255, 0}}};

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

	/* Set up DHCPv4 server */
	iface = net_if_get_default();
	(void)net_if_ipv4_addr_add(iface, &server_addr, NET_ADDR_MANUAL, 0);
	(void)net_if_ipv4_set_netmask_by_addr(iface, &server_addr, &netmask);

	net_dhcpv4_server_start(iface, &base_addr);

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

	cur_state = APP_WAIT_FOR_CLIENT;
	next_state = APP_WAIT_FOR_CLIENT;

	bool dhcpv4_server_running = false;

	while (1) {
		switch (net_if_oper_state(iface)) {
		case NET_IF_OPER_UP:
			if (!dhcpv4_server_running) {
				net_dhcpv4_server_start(iface, &base_addr);
				dhcpv4_server_running = true;
			}
			break;
		default:
			if (dhcpv4_server_running) {
				net_dhcpv4_server_stop(iface);
				dhcpv4_server_running = false;
			}
			break;
		}

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
				connect_control_socket(sock, &client_addr);
				next_state = APP_RUNNING;
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

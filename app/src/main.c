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

#include <zephyr/drivers/display.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#define MY_PORT          5000
#define MAX_CLIENT_QUEUE 1

#define SCR_WIDTH  DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCR_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define SCR_BUF_SZ (SCR_WIDTH * SCR_HEIGHT * 4)

static struct in_addr server_addr = {{{192, 0, 2, 1}}};
static struct in_addr base_addr = {{{192, 0, 2, 2}}};
static struct in_addr netmask = {{{255, 255, 255, 0}}};

static inline int display_setup(const struct device *const display_dev)
{
	struct display_capabilities capabilities;
	int ret = 0;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found", display_dev->name);
		return -ENODEV;
	}

	printk("\nDisplay device: %s\n", display_dev->name);

	display_get_capabilities(display_dev, &capabilities);

	printk("- Capabilities:\n");
	printk("  x_resolution = %u, y_resolution = %u, supported_pixel_formats = %u\n"
	       "  current_pixel_format = %u, current_orientation = %u\n\n",
	       capabilities.x_resolution, capabilities.y_resolution,
	       capabilities.supported_pixel_formats, capabilities.current_pixel_format,
	       capabilities.current_orientation);

	/* Received buffer from gstreamer is in BGRx format */
	ret = display_set_pixel_format(display_dev, PIXEL_FORMAT_ARGB_8888);
	if (ret) {
		LOG_ERR("Unable to set display format");
		return ret;
	}

	return display_blanking_off(display_dev);
}

static inline void display_frame(const struct device *const display_dev,
				 const unsigned char *const buf)
{
	struct display_buffer_descriptor buf_desc;

	buf_desc.buf_size = SCR_BUF_SZ;
	buf_desc.height = SCR_HEIGHT;
	buf_desc.width = SCR_WIDTH;
	buf_desc.pitch = buf_desc.width;

	display_write(display_dev, 0, 0, &buf_desc, buf);
}

static ssize_t receive_all(int sock, void *buf, size_t len)
{
	while (len) {
		ssize_t out_len = recv(sock, buf, len, 0);
		/* Just for testing to see received packet size */
		if (len == SCR_BUF_SZ) {
			LOG_INF("out_len = %d, len = %d", out_len, len);
		}
		if (out_len < 0) {
			return out_len;
		}
		buf = (char *)buf + out_len;
		len -= out_len;
	}

	return 0;
}

int main(void)
{
	unsigned char buffer[SCR_BUF_SZ];
	struct sockaddr_in addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int i, ret, sock, client;
	struct net_if *iface;

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

	/* Connection loop */
	do {
		printk("TCP: Waiting for client...\n");

		client = accept(sock, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client < 0) {
			printk("Failed to accept: %d\n", errno);
			return 0;
		}

		printk("TCP: Accepted connection\n");

		/* Display loop */
		i = 0;
		do {
			printk("\nReceiving frame %d\n", i++);

			/* Receive video buffer from client */
			ret = receive_all(client, buffer, SCR_BUF_SZ);
			if (ret && ret != -EAGAIN) {
				/* client disconnected */
				printk("\nTCP: Client disconnected %d\n", ret);
				close(client);
			}

			display_frame(display_dev, buffer);
		} while (!ret);
	} while (1);
}

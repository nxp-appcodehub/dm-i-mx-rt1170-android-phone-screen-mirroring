#ifndef SCRMIRROR_SCREEN
#define SCRMIRROR_SCREEN

#include <zephyr/net/socket.h>

#define SCR_WIDTH  DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCR_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define SCR_BUF_SZ (SCR_WIDTH * SCR_HEIGHT * 2)
#define SCR_FORMAT PIXEL_FORMAT_RGB_565

#define EVENT_SOCKET_THREAD_STOP (1 << 1)

int display_setup(const struct device *const display_dev);

#endif

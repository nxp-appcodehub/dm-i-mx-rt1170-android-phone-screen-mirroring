#ifndef SCRMIRROR_SCREEN
#define SCRMIRROR_SCREEN

#define SCR_WIDTH  DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCR_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)
#define SCR_BUF_SZ (SCR_WIDTH * SCR_HEIGHT * 2)
#define SCR_FORMAT PIXEL_FORMAT_RGB_565

#endif

/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "decode.h"
#include "JPEGDEC.h"
#include "screen.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL

LOG_MODULE_REGISTER(decode);

/* Thread management structure */
struct decode_threads {
	struct k_thread parse_decode_thread;
	struct k_thread socket_recv_thread;
	K_KERNEL_STACK_MEMBER(parse_decode_stack, 32768);
	K_KERNEL_STACK_MEMBER(socket_recv_stack, 2048);
};

/* Socket receive context */
struct socket_recv_ctx {
	int control;
	int *socket_fd;
};

/* Decode context */
struct decode_ctx {
	const struct device *display_dev;
};

/* JPEG segment with memory slab pointer */
struct jpeg_frame {
	void *slab_ptr;
	size_t offset;
	size_t size;
	bool is_last_frame;
};

K_MSGQ_DEFINE(jpeg_frames, sizeof(struct jpeg_frame), CONFIG_JPEG_SLAB_COUNT, 4);
K_MEM_SLAB_DEFINE(jpeg_slab, KB(CONFIG_JPEG_SLAB_SIZE), CONFIG_JPEG_SLAB_COUNT, 4);
static struct decode_threads threads;
extern struct k_event application_event;
static struct socket_recv_ctx recv_ctx;
static struct decode_ctx decode_ctx;
static uint8_t scr_buf[CONFIG_MCUX_ELCDIF_FB_SIZE];

/* A JPEG image starts with a SOI marker and ends with an EOI marker */
static const uint8_t jpeg_soi[] = {0xFF, 0xD8};
static const uint8_t jpeg_eoi[] = {0xFF, 0xD9};

static void *find_pattern(const void *haystack, size_t haystacklen, const void *needle,
			  size_t needlelen)
{
	if (!haystack || !needle || haystacklen < needlelen || needlelen == 0) {
		return NULL;
	}

	const uint8_t *h = (const uint8_t *)haystack;
	const uint8_t *n = (const uint8_t *)needle;

	for (size_t i = 0; i <= haystacklen - needlelen; i++) {
		if (memcmp(&h[i], n, needlelen) == 0) {
			return (void *)&h[i];
		}
	}

	return NULL;
}

static int jpeg_decode(uint8_t *jpeg_buffer, size_t size, uint8_t *output_buf,
		       struct display_buffer_descriptor *buf_desc)
{
	int rc;
	JPEGIMAGE jpg;

	rc = JPEG_openRAM(&jpg, jpeg_buffer, size, NULL);
	if (rc == 0) {
		LOG_WRN("JPEG open failed: %d", JPEG_getLastError(&jpg));
		return -1;
	}

	buf_desc->width = JPEG_getWidth(&jpg);
	buf_desc->height = JPEG_getHeight(&jpg);
	buf_desc->pitch = buf_desc->width;
	buf_desc->buf_size = buf_desc->width * buf_desc->height * JPEG_getBpp(&jpg) / 8;

	JPEG_setFramebuffer(&jpg, output_buf);
	JPEG_setPixelType(&jpg, RGB565_LITTLE_ENDIAN);

	rc = JPEG_decode(&jpg, 0, 0, 0);
	if (rc == 0) {
		printk("JPEG decode failed: %d\n", rc);
		return -1;
	}

	return 0;
}

static void socket_recv_task(void *p1, void *p2, void *p3)
{
	struct socket_recv_ctx *ctx = p1;
	struct jpeg_frame end_frame = {NULL, 0};
	uint8_t *current_slab = NULL;
	size_t bytes_in_slab = 0; /* valid bytes held by the slab */
	size_t scan_offset = 0;   /* first byte of the slab not scanned yet */
	size_t frame_offset = 0;  /* offset of the SOI of the frame being received */
	bool in_frame = false;    /* a SOI was seen, its EOI is still missing */
	bool slab_has_frames = false;
	ssize_t recv_len;
	int ret;

	LOG_INF("Socket receiving started");

	/* Allocate first slab */
	ret = k_mem_slab_alloc(&jpeg_slab, (void **)&current_slab, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to allocate initial slab");
		goto exit;
	}

	while (ctx->control == THREAD_RUN) {
		struct jpeg_frame frame = {.slab_ptr = current_slab};
		size_t scan, keep_from;

		recv_len = zsock_recv(*ctx->socket_fd, current_slab + bytes_in_slab,
				      KB(CONFIG_JPEG_SLAB_SIZE) - bytes_in_slab, 0);
		if (recv_len <= 0) {
			LOG_WRN("Socket receive ended or error: %zd", recv_len);
			break;
		}

		bytes_in_slab += recv_len;

		/* Scanning resumes one byte before the freshly received data so that a
		 * marker split between two zsock_recv() calls is still matched.
		 */
		scan = scan_offset;

		/* Extract every complete image held by the slab. Both markers have to
		 * be matched: the stream may start in the middle of an image, and a
		 * 0xFFD9 byte pair may also show up inside a marker segment payload.
		 * Cutting on EOI alone would hand the decoder data with no SOI, which
		 * is not a valid JPEG image.
		 */
		while (true) {
			uint8_t *marker;

			if (!in_frame) {
				marker = find_pattern(current_slab + scan, bytes_in_slab - scan,
						      jpeg_soi, ARRAY_SIZE(jpeg_soi));
				if (marker == NULL) {
					break;
				}

				frame_offset = (size_t)(marker - current_slab);
				scan = frame_offset + ARRAY_SIZE(jpeg_soi);
				in_frame = true;
			}

			marker = find_pattern(current_slab + scan, bytes_in_slab - scan, jpeg_eoi,
					      ARRAY_SIZE(jpeg_eoi));
			if (marker == NULL) {
				break;
			}

			scan = (size_t)(marker - current_slab) + ARRAY_SIZE(jpeg_eoi);
			in_frame = false;

			frame.offset = frame_offset;
			frame.size = scan - frame_offset;
			frame.is_last_frame = false;
			k_msgq_put(&jpeg_frames, &frame, K_FOREVER);
			slab_has_frames = true;
		}

		/* Keep the image being received. Outside of an image only the last
		 * byte is worth keeping: it may be the 0xFF of a split marker.
		 */
		keep_from = in_frame ? frame_offset : bytes_in_slab - 1;

		if (slab_has_frames) {
			uint8_t *next_slab = NULL;

			/* The slab now belongs to the decode thread: hand it over and
			 * carry the incomplete tail into a fresh one.
			 */
			ret = k_mem_slab_alloc(&jpeg_slab, (void **)&next_slab, K_FOREVER);
			if (ret != 0) {
				LOG_ERR("Failed to allocate slab");
				frame.offset = 0;
				frame.size = 0;
				frame.is_last_frame = true;
				k_msgq_put(&jpeg_frames, &frame, K_FOREVER);
				current_slab = NULL;
				break;
			}

			bytes_in_slab -= keep_from;
			memcpy(next_slab, current_slab + keep_from, bytes_in_slab);

			frame.offset = 0;
			frame.size = 0;
			frame.is_last_frame = true;
			k_msgq_put(&jpeg_frames, &frame, K_FOREVER);

			current_slab = next_slab;
			slab_has_frames = false;
		} else if (keep_from > 0) {
			/* Nothing was queued from this slab, it is still ours: drop the
			 * consumed bytes in place.
			 */
			bytes_in_slab -= keep_from;
			memmove(current_slab, current_slab + keep_from, bytes_in_slab);
		}

		if (in_frame) {
			frame_offset = 0;
		}
		scan_offset = bytes_in_slab - 1;

		/* An image bigger than a whole slab can never be completed. Drop it
		 * and resynchronise on the next SOI, otherwise the next zsock_recv()
		 * would be called with no room left and report the socket as closed.
		 */
		if (bytes_in_slab >= KB(CONFIG_JPEG_SLAB_SIZE)) {
			LOG_WRN("Dropping a JPEG frame larger than %d KB", CONFIG_JPEG_SLAB_SIZE);
			in_frame = false;
			bytes_in_slab = 0;
			scan_offset = 0;
		}
	}

exit:
	/* Cleanup on exit */
	if (current_slab != NULL) {
		k_mem_slab_free(&jpeg_slab, current_slab);
	}

	k_msgq_put(&jpeg_frames, &end_frame, K_FOREVER);
	k_event_post(&application_event, EVENT_SOCKET_THREAD_STOP);
	LOG_INF("Socket receiving thread terminated");
}

static void decode_display_task(void *p1, void *p2, void *p3)
{
	struct decode_ctx *ctx = p1;
	struct jpeg_frame frame;
	struct display_buffer_descriptor buf_desc;

	LOG_INF("Decoding thread started");
	while (k_msgq_get(&jpeg_frames, &frame, K_FOREVER) == 0) {
		if (frame.slab_ptr == NULL) {
			break;
		}

		/* Decode from the correct offset in the slab */
		if (frame.size > 0) {
			uint8_t *jpeg_data = (uint8_t *)frame.slab_ptr + frame.offset;
			if (jpeg_decode(jpeg_data, frame.size, scr_buf, &buf_desc) == 0) {
				display_write(ctx->display_dev, 0, 0, &buf_desc, scr_buf);
			}
			LOG_DBG("decoded frame at offset %zu, size %zu bytes", frame.offset,
				frame.size);
		}

		/* Free the memory slab after decoding */
		if (frame.is_last_frame) {
			k_mem_slab_free(&jpeg_slab, frame.slab_ptr);
			LOG_DBG("Freed slab");
		}
	}

	LOG_INF("Decoding thread terminated");
}

void decode_start(int *socket)
{

	k_tid_t tid;

	/* Initialize contexts */
	recv_ctx.control = THREAD_RUN;
	recv_ctx.socket_fd = socket;
	decode_ctx.display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(decode_ctx.display_dev)) {
		LOG_ERR("Display device not ready");
		return;
	}

	/* Create socket recv thread */
	tid = k_thread_create(&threads.socket_recv_thread, threads.socket_recv_stack,
			      K_THREAD_STACK_SIZEOF(threads.socket_recv_stack), socket_recv_task,
			      &recv_ctx, NULL, NULL, K_PRIO_PREEMPT(CONFIG_RECV_THREAD_PRIO), 0,
			      K_NO_WAIT);
	k_thread_name_set(tid, "socket_recv_task");

	/* Create decode and display thread */
	tid = k_thread_create(&threads.parse_decode_thread, threads.parse_decode_stack,
			      K_THREAD_STACK_SIZEOF(threads.parse_decode_stack),
			      decode_display_task, &decode_ctx, NULL, NULL,
			      K_PRIO_PREEMPT(CONFIG_DECODE_THREAD_PRIO), 0, K_NO_WAIT);
	k_thread_name_set(tid, "parse_decode_display_task");
}

void decode_stop()
{
	struct jpeg_frame frame;

	recv_ctx.control = THREAD_STOP;
	zsock_shutdown(*recv_ctx.socket_fd, ZSOCK_SHUT_RDWR);
	k_thread_join(&threads.socket_recv_thread, K_FOREVER);
	k_thread_join(&threads.parse_decode_thread, K_FOREVER);

	zsock_close(*recv_ctx.socket_fd);

	/* Purge message queue and free any remaining slabs */
	while (k_msgq_get(&jpeg_frames, &frame, K_NO_WAIT) == 0) {
		if (frame.slab_ptr != NULL) {
			k_mem_slab_free(&jpeg_slab, frame.slab_ptr);
		}
	}

	k_msgq_purge(&jpeg_frames);
}

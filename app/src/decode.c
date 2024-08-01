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
	uint8_t *search_start, *eoi_ptr, *current_slab;
	size_t search_len, remain_data, recv_len, bytes_in_slab = 0;
	int ret;

	LOG_INF("Socket receiving started");

	/* Allocate first slab */
	ret = k_mem_slab_alloc(&jpeg_slab, (void **)&current_slab, K_FOREVER);
	if (ret != 0) {
		LOG_ERR("Failed to allocate initial slab");
		goto exit;
	}

	while (ctx->control == THREAD_RUN) {
		recv_len = zsock_recv(*ctx->socket_fd, current_slab + bytes_in_slab,
				      KB(CONFIG_JPEG_SLAB_SIZE) - bytes_in_slab, 0);
		if (recv_len <= 0) {
			LOG_WRN("Socket receive ended or error: %d", recv_len);
			break;
		}

		if (bytes_in_slab > 0) {
			search_start = current_slab + bytes_in_slab - 1;
			search_len = recv_len + 1;
		} else {
			search_start = current_slab;
			search_len = recv_len;
		}

		bytes_in_slab += recv_len;

		/* Find the first JPEG frame */
		eoi_ptr = find_pattern(search_start, search_len, "\xFF\xD9", 2);
		if (eoi_ptr != NULL) {
			struct jpeg_frame frame = {
				.slab_ptr = current_slab,
			};

			/* Found the first occurent of jpeg marker of the first frame*/
			search_start = current_slab;

			/* Process all complete JPEG frames in the slab */
			do {
				frame.offset = search_start - current_slab;
				frame.size = (size_t)(eoi_ptr - search_start) + 2;
				frame.is_last_frame = false;
				k_msgq_put(&jpeg_frames, &frame, K_FOREVER);

				search_start = eoi_ptr + 2;
				search_len = bytes_in_slab - (search_start - current_slab);
				eoi_ptr = find_pattern(search_start, search_len, "\xFF\xD9", 2);
			} while (eoi_ptr != NULL);

			/* After this, the remain jpeg data is partial in the slab, so we need to
			 * copy if any */
			remain_data = bytes_in_slab - (search_start - current_slab);
			if (remain_data > 0) {
				uint8_t *next_slab = NULL;

				/* Allocate a next new slab and copy remaining data */
				k_mem_slab_alloc(&jpeg_slab, (void **)&next_slab, K_FOREVER);
				memcpy(next_slab, search_start, remain_data);
				frame.offset = search_start - current_slab;

				/* Switch to a new slab */
				current_slab = next_slab;
				bytes_in_slab = remain_data;
			} else {
				/* Allocate a fresh new slab */
				k_mem_slab_alloc(&jpeg_slab, (void **)&current_slab, K_FOREVER);
				frame.offset = 0;
				bytes_in_slab = 0;
			}
			frame.size = 0;
			frame.is_last_frame = true;
			k_msgq_put(&jpeg_frames, &frame, K_FOREVER);
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
	k_thread_join(&threads.socket_recv_thread, K_FOREVER);
	k_thread_join(&threads.parse_decode_thread, K_FOREVER);
	zsock_shutdown(*recv_ctx.socket_fd, ZSOCK_SHUT_RDWR);
	zsock_close(*recv_ctx.socket_fd);

	/* Purge message queue and free any remaining slabs */
	while (k_msgq_get(&jpeg_frames, &frame, K_NO_WAIT) == 0) {
		if (frame.slab_ptr != NULL) {
			k_mem_slab_free(&jpeg_slab, frame.slab_ptr);
		}
	}

	k_msgq_purge(&jpeg_frames);
}

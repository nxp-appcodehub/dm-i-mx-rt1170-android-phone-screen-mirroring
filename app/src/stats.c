/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_CPU_LOAD)
#include <zephyr/sys/cpu_load.h>
#endif

#include "stats.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
LOG_MODULE_REGISTER(stats);

#if defined(CONFIG_STATS_MONITOR)

/* Running count of frames displayed since the last report window. */
static atomic_t frame_count;

/* Monitor thread resources. */
#define STATS_STACK_SIZE 1024
static K_THREAD_STACK_DEFINE(stats_stack, STATS_STACK_SIZE);
static struct k_thread stats_thread;
static bool stats_started;

void stats_frame_done(void)
{
	atomic_inc(&frame_count);
}

static void stats_monitor_task(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const int64_t interval_ms = (int64_t)CONFIG_STATS_MONITOR_INTERVAL * MSEC_PER_SEC;
	int64_t window_start = k_uptime_get();

	LOG_INF("Statistics monitor thread started");

	while (1) {
		k_msleep((int32_t)interval_ms);

		int64_t now = k_uptime_get();
		int64_t elapsed = now - window_start;

		if (elapsed <= 0) {
			elapsed = interval_ms;
		}

		/* Atomically read and reset the frame counter for this window. */
		uint32_t frames = (uint32_t)atomic_set(&frame_count, 0);

		/* FPS scaled by 10 to keep one decimal place without floating point. */
		uint32_t fps_x10 = (uint32_t)((frames * 10000LL) / elapsed);

#if defined(CONFIG_CPU_LOAD)
		/* cpu_load_get() returns per mille (0..1000); reset the window. */
		int load = cpu_load_get(true);

		if (load >= 0) {
			LOG_INF("FPS: %u.%u | CPU: %d.%d%% (%u frames in %lld ms)", fps_x10 / 10U,
				fps_x10 % 10U, load / 10, load % 10, frames, elapsed);
		} else {
			LOG_INF("FPS: %u.%u | CPU: n/a (%u frames in %lld ms)", fps_x10 / 10U,
				fps_x10 % 10U, frames, elapsed);
		}
#else
		LOG_INF("FPS: %u.%u (%u frames in %lld ms)", fps_x10 / 10U, fps_x10 % 10U, frames,
			elapsed);
#endif /* CONFIG_CPU_LOAD */

		window_start = now;
	}
}

void stats_monitor_start(void)
{
	if (stats_started) {
		return;
	}

	k_tid_t tid = k_thread_create(
		&stats_thread, stats_stack, K_THREAD_STACK_SIZEOF(stats_stack), stats_monitor_task,
		NULL, NULL, NULL, K_PRIO_PREEMPT(CONFIG_STATS_MONITOR_THREAD_PRIO), 0, K_NO_WAIT);
	k_thread_name_set(tid, "stats_monitor");
	stats_started = true;
}

#endif

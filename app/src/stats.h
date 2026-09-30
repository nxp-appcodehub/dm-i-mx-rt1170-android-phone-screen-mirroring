/*
 * Copyright 2024-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCRMIRROR_STATS
#define SCRMIRROR_STATS

#include <zephyr/kernel.h>

/*
 * Count one successfully decoded and displayed frame. Safe to call from the
 * decode thread; the value is consumed by the statistics monitor thread.
 */
void stats_frame_done(void);

/*
 * Start the background statistics monitor thread. The thread periodically logs
 * the measured frames-per-second (FPS) and, when CONFIG_CPU_LOAD is enabled,
 * the current CPU load. Safe to call once at startup; subsequent calls are
 * no-ops.
 */
void stats_monitor_start(void);

#endif /* SCRMIRROR_STATS */

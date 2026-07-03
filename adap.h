/**
 * @file adap.h
 * @brief Adaptive tuning engine and per-GM profile manager for linuxptp.
 * @note Copyright (C) 2026 Kuldip Dwivedi <kuldip.dwivedi@happiestminds.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef HAVE_ADAP_H
#define HAVE_ADAP_H

#include <stdint.h>
#include <sys/queue.h>

#include "ds.h"
#include "servo.h"
#include "tmv.h"

struct clock;
struct config;

/** Opaque type for the adaptive tuning engine. */
struct adap;

/**
 * Tuning presets / modes for the adaptive engine.
 */
enum adap_tuning_mode {
	ADAP_MODE_CONSERVATIVE,	/* noisy/jittery networks */
	ADAP_MODE_BALANCED,		/* default moderate */
	ADAP_MODE_AGGRESSIVE,		/* clean deterministic networks */
};

/**
 * A set of tuning parameters that can be applied to the servo/clock.
 */
struct adap_params {
	int    num_offset_values;	/* 1-100 */
	int    offset_threshold;	/* ns per update, >= 0 */
	double kp;			/* proportional gain */
	double ki;			/* integral gain */
	double interval;		/* PI update interval (seconds) */
	int    filter_length;		/* tsproc filter window 1-256 */
	int    freq_est_interval;	/* clock freq est window 1-4096 */
	double step_threshold_ns;	/* step detection threshold */
	double first_step_threshold_ns;	/* first step threshold */
	int    max_frequency_ppb;	/* max frequency adjustment */
};

/**
 * Per-GM profile entry.
 */
struct adap_gm_profile {
	LIST_ENTRY(adap_gm_profile) list;
	struct ClockIdentity gm_id;
	struct adap_params   params;
	char                 label[64];
};

/**
 * Network condition metrics computed over a rolling window.
 */
struct adap_metrics {
	/* Computed metrics */
	double offset_jitter_ns;	/* stddev of recent offsets */
	double offset_mean_ns;		/* mean offset */
	double delay_variance_ns;	/* variation in path delay */
	int    packet_loss_count;	/* missing sync messages */
	int    sample_count;		/* samples in current window */

	/* Servo state tracking */
	enum servo_state current_state;
	enum servo_state prev_state;
	int  state_stable_count;	/* consecutive samples in LOCKED_STABLE */

	/* Timing */
	uint64_t last_update_ts;	/* monotonic clock at last eval */
	double   sampling_interval_s;
};

/**
 * Create the adaptive tuning engine.
 * @param cfg  Pointer to the configuration database.
 * @return     A pointer to a new adap instance on success, NULL otherwise.
 */
struct adap *adap_create(struct config *cfg);

/**
 * Destroy the adaptive tuning engine.
 * @param a  Pointer to an adap instance obtained via adap_create().
 */
void adap_destroy(struct adap *a);

/**
 * Feed a synchronization sample into the adaptive engine for metric
 * computation.
 * @param a       Pointer to an adap instance.
 * @param offset  The estimated clock offset in nanoseconds.
 * @param delay   The estimated path delay in nanoseconds.
 * @param state   The current servo state.
 * @param local_ts  The local time stamp of the sample in nanoseconds.
 */
void adap_feed_sample(struct adap *a, int64_t offset, int64_t delay,
		      enum servo_state state, uint64_t local_ts);

/**
 * Evaluate network conditions and apply tuning if needed.
 * Called periodically (e.g., from clock_poll or clock_sync_interval).
 * @param a  Pointer to an adap instance.
 * @param c  Pointer to the clock instance (for applying params).
 */
void adap_evaluate(struct adap *a, struct clock *c);

/**
 * Notify the adaptive engine that the Grandmaster has changed.
 * The engine will look up the GM profile and apply it.
 * @param a      Pointer to an adap instance.
 * @param c      Pointer to the clock instance.
 * @param gm_id  The ClockIdentity of the new Grandmaster.
 */
void adap_on_gm_change(struct adap *a, struct clock *c,
		       struct ClockIdentity gm_id);

/**
 * Reset the adaptive engine's metrics window (e.g., on GM change).
 * @param a  Pointer to an adap instance.
 */
void adap_reset_metrics(struct adap *a);

/**
 * Get the current tuning mode.
 * @param a  Pointer to an adap instance.
 * @return   The current tuning mode.
 */
enum adap_tuning_mode adap_get_mode(struct adap *a);

/**
 * Set the tuning mode manually (overrides auto-detection).
 * @param a     Pointer to an adap instance.
 * @param mode  The tuning mode to set.
 */
void adap_set_mode(struct adap *a, enum adap_tuning_mode mode);

/**
 * Enable or disable the adaptive engine.
 * @param a       Pointer to an adap instance.
 * @param enabled 1 to enable, 0 to disable.
 */
void adap_set_enabled(struct adap *a, int enabled);

/**
 * Check if the adaptive engine is enabled.
 * @param a  Pointer to an adap instance.
 * @return   1 if enabled, 0 otherwise.
 */
int adap_get_enabled(struct adap *a);

/**
 * Add or update a per-GM profile.
 * @param a       Pointer to an adap instance.
 * @param gm_id   The ClockIdentity of the GM.
 * @param params  The tuning parameters for this GM.
 * @param label   A human-readable label (optional, can be NULL).
 * @return        0 on success, -1 on failure.
 */
int adap_set_gm_profile(struct adap *a, struct ClockIdentity gm_id,
			struct adap_params *params, const char *label);

/**
 * Remove a per-GM profile.
 * @param a      Pointer to an adap instance.
 * @param gm_id  The ClockIdentity of the GM to remove.
 * @return       0 on success, -1 if not found.
 */
int adap_remove_gm_profile(struct adap *a, struct ClockIdentity gm_id);

/**
 * Look up a per-GM profile by GM identity.
 * @param a      Pointer to an adap instance.
 * @param gm_id  The ClockIdentity to look up.
 * @return       Pointer to the profile if found, NULL otherwise.
 */
struct adap_gm_profile *adap_find_gm_profile(struct adap *a,
					     struct ClockIdentity gm_id);

/**
 * Get the default tuning parameters (based on current mode).
 * @param a      Pointer to an adap instance.
 * @param params  Output: the default parameters.
 */
void adap_get_default_params(struct adap *a, struct adap_params *params);

/**
 * Apply a set of tuning parameters to the clock/servo.
 * @param c       Pointer to the clock instance.
 * @param params  The parameters to apply.
 */
void adap_apply_params(struct clock *c, struct adap_params *params);

/**
 * Get the current metrics from the adaptive engine.
 * @param a       Pointer to an adap instance.
 * @param metrics Output: the current metrics.
 */
void adap_get_metrics(struct adap *a, struct adap_metrics *metrics);

#endif /* HAVE_ADAP_H */
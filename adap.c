/**
 * @file adap.c
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
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "adap.h"
#include "clock.h"
#include "config.h"
#include "missing.h"
#include "pi.h"
#include "port.h"
#include "print.h"
#include "servo.h"
#include "tsproc.h"
#include "util.h"

/*
 * Default tuning parameters per mode.
 */
#define ADAP_DEFAULT_SAMPLE_WINDOW  10
#define ADAP_DEFAULT_EVAL_INTERVAL  1.0  /* seconds */
#define ADAP_DEFAULT_STEP_THRESHOLD_NS 20000.0

/* Conservative mode (noisy/jittery networks) */
static const struct adap_params default_conservative = {
	.num_offset_values      = 8,
	.offset_threshold       = 200000,
	.kp                     = 0.5,
	.ki                     = 0.2,
	.interval               = 1.0,
	.filter_length          = 16,
	.freq_est_interval      = 512,
	.step_threshold_ns      = ADAP_DEFAULT_STEP_THRESHOLD_NS,
	.first_step_threshold_ns = ADAP_DEFAULT_STEP_THRESHOLD_NS,
	.max_frequency_ppb      = 900000000,
};

/* Balanced mode (default moderate) */
static const struct adap_params default_balanced = {
	.num_offset_values      = 5,
	.offset_threshold       = 100000,
	.kp                     = 0.7,
	.ki                     = 0.3,
	.interval               = 1.0,
	.filter_length          = 10,
	.freq_est_interval      = 256,
	.step_threshold_ns      = ADAP_DEFAULT_STEP_THRESHOLD_NS,
	.first_step_threshold_ns = ADAP_DEFAULT_STEP_THRESHOLD_NS,
	.max_frequency_ppb      = 900000000,
};

/* Aggressive mode (clean deterministic networks) */
static const struct adap_params default_aggressive = {
	.num_offset_values      = 4,
	.offset_threshold       = 50000,
	.kp                     = 1.0,
	.ki                     = 0.5,
	.interval               = 1.0,
	.filter_length          = 6,
	.freq_est_interval      = 128,
	.step_threshold_ns      = ADAP_DEFAULT_STEP_THRESHOLD_NS,
	.first_step_threshold_ns = ADAP_DEFAULT_STEP_THRESHOLD_NS,
	.max_frequency_ppb      = 900000000,
};

/*
 * Thresholds for network condition classification.
 */
#define ADAP_JITTER_LOW_NS      100.0   /* below this = stable */
#define ADAP_JITTER_HIGH_NS     500.0   /* above this = noisy */
#define ADAP_LOSS_THRESHOLD     5       /* lost syncs in window */
#define ADAP_STABLE_WATERMARK   20      /* consecutive LOCKED_STABLE samples */

/*
 * Internal structure for the adaptive engine.
 */
struct adap {
	/* Configuration */
	int             enabled;
	enum adap_tuning_mode mode;
	int             manual_mode;  /* 1 if mode was set manually */

	/* Sample window for metrics */
	int64_t        *offset_samples;
	int64_t        *delay_samples;
	enum servo_state *state_samples;
	int             sample_window;
	int             sample_count;
	int             sample_index;

	/* Packet loss tracking */
	int64_t         last_sample_ts;
	double          expected_sample_interval_ns;
	int             expected_samples;
	int             missed_samples;

	/* Computed metrics */
	struct adap_metrics metrics;

	/* Per-GM profile table */
	LIST_HEAD(gm_profiles_head, adap_gm_profile) gm_profiles;

	/* Evaluation timing */
	uint64_t        last_eval_ts;
	double          eval_interval_s;
};

/* Helper: compute mean of an int64_t array */
static double compute_mean(const int64_t *samples, int count)
{
	double sum = 0.0;
	int i;

	if (count <= 0)
		return 0.0;

	for (i = 0; i < count; i++)
		sum += (double)samples[i];

	return sum / count;
}

/* Helper: compute standard deviation of an int64_t array */
static double compute_stddev(const int64_t *samples, int count, double mean)
{
	double sum_sq = 0.0;
	int i;

	if (count <= 1)
		return 0.0;

	for (i = 0; i < count; i++) {
		double diff = (double)samples[i] - mean;
		sum_sq += diff * diff;
	}

	return sqrt(sum_sq / (count - 1));
}

/* Helper: get monotonic timestamp in nanoseconds */
static uint64_t get_monotonic_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void adap_load_gm_profiles(struct adap *a, const char *path)
{
	struct ClockIdentity gm_id;
	struct adap_params params;
	char line[512], gm[64], label[64];
	FILE *fp;
	int lineno = 0;

	if (!a || !path || !path[0])
		return;

	fp = fopen(path, "r");
	if (!fp) {
		pr_warning("ADAP: failed to open GM profile file %s: %s",
			   path, strerror(errno));
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *p = line;
		int n;

		lineno++;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == '\n' || *p == '\0')
			continue;

		memset(&params, 0, sizeof(params));
		n = sscanf(p, " %63[^,],%63[^,],%d,%d,%lf,%lf,%lf,%d,%d,%lf,%lf,%d",
			   gm, label,
			   &params.num_offset_values,
			   &params.offset_threshold,
			   &params.kp,
			   &params.ki,
			   &params.interval,
			   &params.filter_length,
			   &params.freq_est_interval,
			   &params.step_threshold_ns,
			   &params.first_step_threshold_ns,
			   &params.max_frequency_ppb);
		if (n != 12 || str2cid(gm, &gm_id)) {
			pr_warning("ADAP: ignoring invalid GM profile %s:%d",
				   path, lineno);
			continue;
		}
		if (adap_set_gm_profile(a, gm_id, &params, label)) {
			pr_warning("ADAP: failed to add GM profile %s:%d",
				   path, lineno);
		}
	}

	fclose(fp);
}

struct adap *adap_create(struct config *cfg)
{
	struct adap *a;
	int window;

	a = calloc(1, sizeof(*a));
	if (!a)
		return NULL;

	window = config_get_int(cfg, NULL, "adap_sample_window");
	if (window < 1)
		window = ADAP_DEFAULT_SAMPLE_WINDOW;

	a->sample_window = window;
	a->sample_count = 0;
	a->sample_index = 0;
	a->enabled = config_get_int(cfg, NULL, "adap_tuning_enabled");
	a->eval_interval_s = config_get_double(cfg, NULL, "adap_eval_interval");
	if (a->eval_interval_s <= 0.0)
		a->eval_interval_s = ADAP_DEFAULT_EVAL_INTERVAL;

	/* Parse initial mode from config. Only "auto" enables mode switching. */
	{
		const char *mode_str = config_get_string(cfg, NULL, "adap_tuning_mode");
		if (!strcmp(mode_str, "conservative")) {
			a->mode = ADAP_MODE_CONSERVATIVE;
			a->manual_mode = 1;
		} else if (!strcmp(mode_str, "aggressive")) {
			a->mode = ADAP_MODE_AGGRESSIVE;
			a->manual_mode = 1;
		} else if (!strcmp(mode_str, "balanced")) {
			a->mode = ADAP_MODE_BALANCED;
			a->manual_mode = 1;
		} else {
			a->mode = ADAP_MODE_BALANCED;
			a->manual_mode = 0;
		}
	}

	a->last_eval_ts = 0;
	a->last_sample_ts = 0;
	a->expected_sample_interval_ns = 0.0;
	a->expected_samples = 0;
	a->missed_samples = 0;

	/* Allocate sample buffers */
	a->offset_samples = calloc(window, sizeof(int64_t));
	a->delay_samples = calloc(window, sizeof(int64_t));
	a->state_samples = calloc(window, sizeof(enum servo_state));
	if (!a->offset_samples || !a->delay_samples || !a->state_samples) {
		free(a->offset_samples);
		free(a->delay_samples);
		free(a->state_samples);
		free(a);
		return NULL;
	}

	LIST_INIT(&a->gm_profiles);

	adap_load_gm_profiles(a,
			      config_get_string(cfg, NULL,
						"adap_gm_profile_file"));

	/* Initialize metrics */
	memset(&a->metrics, 0, sizeof(a->metrics));
	a->metrics.sampling_interval_s = a->eval_interval_s;

	pr_info("ADAP: created adaptive engine, mode=%s window=%d enabled=%d auto=%d",
		a->mode == ADAP_MODE_CONSERVATIVE ? "conservative" :
		a->mode == ADAP_MODE_AGGRESSIVE ? "aggressive" : "balanced",
		window, a->enabled, !a->manual_mode);

	return a;
}

void adap_destroy(struct adap *a)
{
	struct adap_gm_profile *profile, *tmp;

	if (!a)
		return;

	/* Free all GM profiles */
	LIST_FOREACH_SAFE(profile, &a->gm_profiles, list, tmp) {
		LIST_REMOVE(profile, list);
		free(profile);
	}

	free(a->offset_samples);
	free(a->delay_samples);
	free(a->state_samples);
	free(a);
}

void adap_feed_sample(struct adap *a, int64_t offset, int64_t delay,
		      enum servo_state state, uint64_t local_ts)
{
	if (!a || !a->enabled)
		return;

	/* Track packet loss */
	if (a->last_sample_ts != 0) {
		uint64_t elapsed = local_ts - a->last_sample_ts;

		if (a->expected_sample_interval_ns > 0.0 &&
		    elapsed > (uint64_t)(a->expected_sample_interval_ns * 2.5)) {
			int lost = (int)(elapsed /
					 a->expected_sample_interval_ns) - 1;
			a->missed_samples += lost;
			pr_debug("ADAP: detected %d lost sync(s)", lost);
		} else if (elapsed > 0) {
			if (a->expected_sample_interval_ns == 0.0) {
				a->expected_sample_interval_ns = (double) elapsed;
			} else {
				a->expected_sample_interval_ns =
					0.8 * a->expected_sample_interval_ns +
					0.2 * (double) elapsed;
			}
		}
	}
	a->last_sample_ts = local_ts;

	/* Store sample in circular buffer */
	a->offset_samples[a->sample_index] = offset;
	a->delay_samples[a->sample_index] = delay;
	a->state_samples[a->sample_index] = state;
	a->sample_index = (a->sample_index + 1) % a->sample_window;

	if (a->sample_count < a->sample_window)
		a->sample_count++;

	/* Update servo state tracking */
	a->metrics.prev_state = a->metrics.current_state;
	a->metrics.current_state = state;

	if (state == SERVO_LOCKED_STABLE &&
	    a->metrics.prev_state == SERVO_LOCKED_STABLE) {
		a->metrics.state_stable_count++;
	} else if (state != SERVO_LOCKED_STABLE) {
		a->metrics.state_stable_count = 0;
	}
}

void adap_reset_metrics(struct adap *a)
{
	if (!a)
		return;

	a->sample_count = 0;
	a->sample_index = 0;
	a->missed_samples = 0;
	a->expected_samples = 0;
	a->last_sample_ts = 0;
	a->expected_sample_interval_ns = 0.0;
	a->last_eval_ts = 0;
	memset(&a->metrics, 0, sizeof(a->metrics));
	a->metrics.sampling_interval_s = a->eval_interval_s;

	pr_debug("ADAP: metrics reset");
}

static void adap_compute_metrics(struct adap *a)
{
	double mean_offset;
	int count;

	if (!a || a->sample_count < 2) {
		return;
	}

	count = a->sample_count;
	mean_offset = compute_mean(a->offset_samples, count);

	a->metrics.offset_mean_ns = mean_offset;
	a->metrics.offset_jitter_ns = compute_stddev(a->offset_samples,
						     count, mean_offset);
	a->metrics.delay_variance_ns = compute_stddev(a->delay_samples,
						      count,
						      compute_mean(a->delay_samples,
								   count));
	a->metrics.packet_loss_count = a->missed_samples;
	a->metrics.sample_count = count;
}

static enum adap_tuning_mode adap_decide_mode(struct adap *a)
{
	double jitter = a->metrics.offset_jitter_ns;
	int loss = a->metrics.packet_loss_count;
	int stable = a->metrics.state_stable_count;

	/* Lossy network → conservative */
	if (loss >= ADAP_LOSS_THRESHOLD) {
		pr_debug("ADAP: loss=%d >= threshold=%d → CONSERVATIVE",
			 loss, ADAP_LOSS_THRESHOLD);
		return ADAP_MODE_CONSERVATIVE;
	}

	/* Very stable for a long time → aggressive */
	if (jitter < ADAP_JITTER_LOW_NS &&
	    stable >= ADAP_STABLE_WATERMARK) {
		pr_debug("ADAP: jitter=%.1f < low=%.1f stable=%d → AGGRESSIVE",
			 jitter, ADAP_JITTER_LOW_NS, stable);
		return ADAP_MODE_AGGRESSIVE;
	}

	/* Noisy → conservative */
	if (jitter > ADAP_JITTER_HIGH_NS) {
		pr_debug("ADAP: jitter=%.1f > high=%.1f → CONSERVATIVE",
			 jitter, ADAP_JITTER_HIGH_NS);
		return ADAP_MODE_CONSERVATIVE;
	}

	/* Default → balanced */
	return ADAP_MODE_BALANCED;
}

static const struct adap_params *adap_get_mode_params(enum adap_tuning_mode mode)
{
	switch (mode) {
	case ADAP_MODE_CONSERVATIVE:
		return &default_conservative;
	case ADAP_MODE_AGGRESSIVE:
		return &default_aggressive;
	case ADAP_MODE_BALANCED:
	default:
		return &default_balanced;
	}
}

static int adap_params_valid(const struct adap_params *p)
{
	if (!p)
		return 0;
	if (p->num_offset_values < 1 || p->num_offset_values > 100)
		return 0;
	if (p->offset_threshold < 0)
		return 0;
	if (p->kp < 0.0 || p->kp > 10.0 || !isfinite(p->kp))
		return 0;
	if (p->ki < 0.0 || p->ki > 10.0 || !isfinite(p->ki))
		return 0;
	if (p->interval <= 0.0 || p->interval > 100.0 ||
	    !isfinite(p->interval))
		return 0;
	if (p->filter_length < 1 || p->filter_length > 256)
		return 0;
	if (p->freq_est_interval < 1 || p->freq_est_interval > 4096)
		return 0;
	if (p->step_threshold_ns < 0.0 || p->step_threshold_ns > 1e9 ||
	    !isfinite(p->step_threshold_ns))
		return 0;
	if (p->first_step_threshold_ns < 0.0 ||
	    p->first_step_threshold_ns > 1e9 ||
	    !isfinite(p->first_step_threshold_ns))
		return 0;
	if (p->max_frequency_ppb < 0 || p->max_frequency_ppb > 1000000000)
		return 0;

	return 1;
}

void adap_get_default_params(struct adap *a, struct adap_params *params)
{
	const struct adap_params *p;

	if (!a || !params)
		return;

	p = adap_get_mode_params(a->mode);
	memcpy(params, p, sizeof(*params));
}

void adap_apply_params(struct clock *c, struct adap_params *params)
{
	struct servo *sv;
	struct tsproc *tsp;

	if (!c || !params)
		return;
	if (!adap_params_valid(params)) {
		pr_warning("ADAP: refusing invalid tuning parameters");
		return;
	}

	sv = clock_servo(c);
	if (!sv)
		return;

	/* Apply servo parameters */
	servo_set_num_offset_values(sv, params->num_offset_values);
	servo_set_offset_threshold(sv, params->offset_threshold);
	servo_set_step_threshold(sv, params->step_threshold_ns);
	servo_set_first_step_threshold(sv, params->first_step_threshold_ns);
	servo_set_max_frequency(sv, params->max_frequency_ppb);

	/* Apply PI constants */
	if (clock_servo_type(c) == CLOCK_SERVO_PI) {
		pi_servo_set_constants(sv, params->kp, params->ki,
				       params->interval);
	} else {
		pr_debug("ADAP: skipping PI constants for non-PI servo");
	}

	/* Apply tsproc filter length */
	tsp = clock_get_tsproc(c);
	if (tsp) {
		tsproc_set_filter_length(tsp, params->filter_length);
	}

	/* Apply clock frequency estimation interval */
	clock_set_freq_est_interval(c, params->freq_est_interval);

	pr_info("ADAP: applied params: numOff=%d offThr=%d kp=%.3f ki=%.3f "
		"fltLen=%d freqEst=%d stepThr=%.0f maxFreq=%d",
		params->num_offset_values, params->offset_threshold,
		params->kp, params->ki,
		params->filter_length, params->freq_est_interval,
		params->step_threshold_ns, params->max_frequency_ppb);
}

void adap_evaluate(struct adap *a, struct clock *c)
{
	uint64_t now;
	double elapsed;
	enum adap_tuning_mode new_mode;

	if (!a || !a->enabled || !c)
		return;

	now = get_monotonic_ns();
	if (a->last_eval_ts == 0) {
		a->last_eval_ts = now;
		return;
	}

	elapsed = (double)(now - a->last_eval_ts) / 1e9;
	if (elapsed < a->eval_interval_s)
		return;

	a->last_eval_ts = now;

	/* Compute metrics from accumulated samples */
	adap_compute_metrics(a);

	/* If we don't have enough samples yet, skip evaluation */
	if (a->sample_count < 2) {
		pr_debug("ADAP: skipping eval, only %d samples", a->sample_count);
		return;
	}

	/* Log current metrics */
	pr_debug("ADAP: metrics jitter=%.1fns mean=%.1fns delay_var=%.1fns "
		 "loss=%d stable=%d state=%d",
		 a->metrics.offset_jitter_ns,
		 a->metrics.offset_mean_ns,
		 a->metrics.delay_variance_ns,
		 a->metrics.packet_loss_count,
		 a->metrics.state_stable_count,
		 a->metrics.current_state);

	/* Decide on new mode (only if not manually overridden) */
	if (!a->manual_mode) {
		new_mode = adap_decide_mode(a);

		if (new_mode != a->mode) {
			const struct adap_params *params;

			pr_info("ADAP: switching mode %d -> %d "
				"(jitter=%.1f loss=%d stable=%d)",
				a->mode, new_mode,
				a->metrics.offset_jitter_ns,
				a->metrics.packet_loss_count,
				a->metrics.state_stable_count);

			a->mode = new_mode;
			params = adap_get_mode_params(new_mode);
			adap_apply_params(c, (struct adap_params *)params);
		}
	}

	/* Reset loss counter after evaluation */
	a->missed_samples = 0;
}

void adap_on_gm_change(struct adap *a, struct clock *c,
		       struct ClockIdentity gm_id)
{
	struct adap_gm_profile *profile;
	const struct adap_params *params;
	const char *id_str;

	if (!a || !c)
		return;

	id_str = cid2str(&gm_id);

	/* Look for a per-GM profile */
	profile = adap_find_gm_profile(a, gm_id);
	if (profile) {
		pr_info("ADAP: GM changed to %s, applying profile '%s'",
			id_str, profile->label);
		adap_apply_params(c, &profile->params);
	} else {
		pr_info("ADAP: GM changed to %s, using default %s mode",
			id_str,
			a->mode == ADAP_MODE_CONSERVATIVE ? "conservative" :
			a->mode == ADAP_MODE_AGGRESSIVE ? "aggressive" :
			"balanced");
		params = adap_get_mode_params(a->mode);
		adap_apply_params(c, (struct adap_params *)params);
	}

	/* Reset metrics for the new GM */
	adap_reset_metrics(a);
}

enum adap_tuning_mode adap_get_mode(struct adap *a)
{
	if (!a)
		return ADAP_MODE_BALANCED;

	return a->mode;
}

void adap_set_mode(struct adap *a, enum adap_tuning_mode mode)
{
	if (!a)
		return;

	a->mode = mode;
	a->manual_mode = 1;

	pr_info("ADAP: mode manually set to %s",
		mode == ADAP_MODE_CONSERVATIVE ? "conservative" :
		mode == ADAP_MODE_AGGRESSIVE ? "aggressive" : "balanced");
}

void adap_set_enabled(struct adap *a, int enabled)
{
	if (!a)
		return;

	a->enabled = enabled;
	pr_info("ADAP: %s", enabled ? "enabled" : "disabled");
}

int adap_get_enabled(struct adap *a)
{
	return a ? a->enabled : 0;
}

int adap_set_gm_profile(struct adap *a, struct ClockIdentity gm_id,
			struct adap_params *params, const char *label)
{
	struct adap_gm_profile *profile;

	if (!a || !params)
		return -1;
	if (!adap_params_valid(params)) {
		pr_warning("ADAP: rejected invalid GM profile for %s",
			   cid2str(&gm_id));
		return -1;
	}

	/* Check if profile already exists */
	profile = adap_find_gm_profile(a, gm_id);
	if (profile) {
		/* Update existing */
		memcpy(&profile->params, params, sizeof(*params));
		if (label)
			snprintf(profile->label, sizeof(profile->label), "%s", label);
		pr_info("ADAP: updated GM profile for %s", cid2str(&gm_id));
		return 0;
	}

	/* Create new profile */
	profile = calloc(1, sizeof(*profile));
	if (!profile)
		return -1;

	profile->gm_id = gm_id;
	memcpy(&profile->params, params, sizeof(*params));
	if (label)
		snprintf(profile->label, sizeof(profile->label), "%s", label);
	else
		snprintf(profile->label, sizeof(profile->label), "unnamed");

	LIST_INSERT_HEAD(&a->gm_profiles, profile, list);

	pr_info("ADAP: added GM profile '%s' for %s",
		profile->label, cid2str(&gm_id));

	return 0;
}

int adap_remove_gm_profile(struct adap *a, struct ClockIdentity gm_id)
{
	struct adap_gm_profile *profile;

	if (!a)
		return -1;

	profile = adap_find_gm_profile(a, gm_id);
	if (!profile)
		return -1;

	LIST_REMOVE(profile, list);
	pr_info("ADAP: removed GM profile for %s", cid2str(&gm_id));
	free(profile);

	return 0;
}

struct adap_gm_profile *adap_find_gm_profile(struct adap *a,
					     struct ClockIdentity gm_id)
{
	struct adap_gm_profile *profile;

	if (!a)
		return NULL;

	LIST_FOREACH(profile, &a->gm_profiles, list) {
		if (cid_eq(&profile->gm_id, &gm_id))
			return profile;
	}

	return NULL;
}

void adap_get_metrics(struct adap *a, struct adap_metrics *metrics)
{
	if (!a || !metrics)
		return;

	adap_compute_metrics(a);
	memcpy(metrics, &a->metrics, sizeof(*metrics));
}

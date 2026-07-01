/**
 * @file print.h
 * @brief Logging support functions
 * @note Copyright (C) 2011 Richard Cochran <richardcochran@gmail.com>
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
#ifndef HAVE_PRINT_H
#define HAVE_PRINT_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
/**
 * Logging levels.
 */
#define LOG_EMERG  (-1)
#define LOG_ERR     0
#define LOG_WARNING 1
#define LOG_NOTICE  2
#define LOG_INFO    3
#define LOG_DEBUG   4
#define LOG_DEBUG1  5
#define LOG_DEBUG2  6

#define PRINT_LEVEL_MIN LOG_EMERG
#define PRINT_LEVEL_MAX LOG_DEBUG2

void print_set_progname(const char *name);
void print_set_tag(const char *tag);
void print_set_syslog(int value);
void print_set_level(int level);
void print_set_verbose(int value);
extern void print(int level, const char *format, ...)
	__attribute__ ((__format__ (__printf__, 2, 3)));

/*
 * Better check print log level before execution of print itself.
 * Otherwise all arguments are evaluated and slow down the system.
 * e.g.   in 'unicast_service.c' unicast_service_clients()
 *                            pid2str() is the killer
 *           pr_debug("%s wants 0x%x", pid2str(&client->portIdentity),
 *                    client->message_types);
 */

extern int print_level;

static inline int print_get_level(void)
{
	return print_level;
}

#define PRINT_CL(l, x...) /* PRINT Check Level */	\
do {							\
	if (print_get_level() >= l)			\
		print(l, x);				\
} while (0)

#define pr_emerg(x...)   PRINT_CL(LOG_EMERG, x)
#define pr_alert(x...)   PRINT_CL(LOG_ALERT, x)
#define pr_crit(x...)    PRINT_CL(LOG_CRIT, x)
#define pr_err(x...)     PRINT_CL(LOG_ERR, x)
#define pr_warning(x...) PRINT_CL(LOG_WARNING, x)
#define pr_notice(x...)  PRINT_CL(LOG_NOTICE, x)
#define pr_info(x...)    PRINT_CL(LOG_INFO, x)
#define pr_debug(x...)   PRINT_CL(LOG_DEBUG, x)

#define pl_info(lvl, x...) \
	do { \
		if (lvl >= print_level) \
			print(LOG_INFO, "ptp4l " x); \
	} while (0)

#define pl_warning(lvl, x...) \
	do { \
		if (lvl >= print_level) \
			print(LOG_WARNING, "ptp4l " x); \
	} while (0)

#define pl_err(lvl, x...) \
	do { \
		if (lvl >= print_level) \
			print(LOG_ERR, "ptp4l " x); \
	} while (0)

/**
 * Tuning audit log - dedicated channel for recording tuning parameter changes.
 * These are logged at LOG_NOTICE level with a consistent format for
 * parsing by monitoring tools.
 *
 * Format: TUNE: <parameter_name> <old_value> -> <new_value>
 *
 * Examples:
 *   TUNE: numOffsetValues 10 -> 5
 *   TUNE: kp 0.700000 -> 0.500000
 *   TUNE: port[eth0] filter_length 8 -> 12
 */
#define pr_tune(param, old_val, new_val, fmt_spec) \
	do { \
		print(LOG_NOTICE, "ptp4l", \
		      "TUNE: " param " " fmt_spec " -> " fmt_spec, \
		      (old_val), (new_val)); \
	} while (0)

/* For string-based parameters */
#define pr_tune_str(param, old_val, new_val) \
	do { \
		print(LOG_NOTICE, "ptp4l", \
		      "TUNE: " param " %s -> %s", \
		      (old_val), (new_val)); \
	} while (0)

#endif

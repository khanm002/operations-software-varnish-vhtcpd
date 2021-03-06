/* Copyright © 2013-2017 Brandon L Black <bblack@wikimedia.org>
 *
 * This file is part of vhtcpd.
 *
 * vhtcpd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vhtcpd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vhtcpd.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stats.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ev.h>

#include "libdmn/dmn.h"

#define FILE_INTERVAL 30
#define LOG_INTERVAL 900

stats_t stats;
static unsigned num_purgers;
static ev_tstamp start_time;
static ev_timer* file_timer;
static ev_timer* log_timer;
static char* outfn;
static char* outfn_tmp;

static void log_stats(ev_tstamp now) {
    dmn_log_info("start: %" PRIu64
                 " uptime: %" PRIu64
                 " purgers: %u"
                 " recvd: %" PRIu64
                 " bad: %" PRIu64
                 " filtered: %" PRIu64,
                 (uint64_t)start_time,
                 (uint64_t)(now - start_time),
                 num_purgers,
                 stats.recvd,
                 stats.bad,
                 stats.filtered
    );

    for (unsigned i = 0; i < num_purgers; i++) {
        dmn_log_info("Purger%u: "
                     " input: %" PRIu64
                     " failed: %" PRIu64
                     " q_size: %" PRIu64
                     " q_mem: %" PRIu64
                     " q_max_size: %" PRIu64
                     " q_max_mem: %" PRIu64,
                     i,
                     stats.purgers[i].input,
                     stats.purgers[i].failed,
                     stats.purgers[i].q_size,
                     stats.purgers[i].q_mem,
                     stats.purgers[i].q_max_size,
                     stats.purgers[i].q_max_mem
        );
    }
}

static void write_stats_file(ev_tstamp now) {
    FILE* outfile = fopen(outfn_tmp, "w");
    if(!outfile) {
        dmn_log_err("Failed to open stats tmpfile '%s' for writing: %s", outfn_tmp, dmn_logf_errno());
        return;
    }

    int fpf_rv = fprintf(outfile,
                 "start:%" PRIu64
                 " uptime:%" PRIu64
                 " purgers:%u"
                 " recvd:%" PRIu64
                 " bad:%" PRIu64
                 " filtered:%" PRIu64
                 "\n",
                 (uint64_t)start_time,
                 (uint64_t)(now - start_time),
                 num_purgers,
                 stats.recvd,
                 stats.bad,
                 stats.filtered
    );
    if(fpf_rv < 0) {
        dmn_log_err("Failed to write data to stats tmpfile '%s': %s", outfn_tmp, dmn_logf_errno());
        fclose(outfile);
        return;
    }

    for (unsigned i = 0; i < num_purgers; i++) {
        fprintf(outfile,
                "Purger%u:"
                " input:%" PRIu64
                " failed:%" PRIu64
                " q_size:%" PRIu64
                " q_mem:%" PRIu64
                " q_max_size:%" PRIu64
                " q_max_mem:%" PRIu64
                "\n",
                i,
                stats.purgers[i].input,
                stats.purgers[i].failed,
                stats.purgers[i].q_size,
                stats.purgers[i].q_mem,
                stats.purgers[i].q_max_size,
                stats.purgers[i].q_max_mem
        );

        if(fpf_rv < 0) {
            dmn_log_err("Failed to write data to stats tmpfile '%s': %s", outfn_tmp, dmn_logf_errno());
            fclose(outfile);
            return;
        }
    }

    if(fclose(outfile)) {
        dmn_log_err("Failed to close stats tmpfile '%s': %s", outfn_tmp, dmn_logf_errno());
        return;
    }

    if(rename(outfn_tmp, outfn))
        dmn_log_err("Failed to rename stats file from '%s' to '%s': %s", outfn_tmp, outfn, dmn_logf_errno());
}

static void log_timer_cb(struct ev_loop* loop, ev_timer* w, int revents) {
    dmn_assert(loop); dmn_assert(w); dmn_assert(revents == EV_TIMER);
    ev_tstamp now = ev_now(loop);
    log_stats(now);
}

static void file_timer_cb(struct ev_loop* loop, ev_timer* w, int revents) {
    dmn_assert(loop); dmn_assert(w); dmn_assert(revents == EV_TIMER);
    ev_tstamp now = ev_now(loop);
    write_stats_file(now);
}

void stats_init(struct ev_loop* loop, const char* statsfile, const unsigned num_purgers_in) {
    dmn_assert(num_purgers_in);
    num_purgers = num_purgers_in;
    memset(&stats, 0, sizeof(stats_t));
    stats.purgers = calloc(num_purgers, sizeof(*stats.purgers));
    start_time = ev_time();
    const unsigned ofn_len = strlen(statsfile);
    outfn = malloc(ofn_len + 1);
    outfn_tmp = malloc(ofn_len + 4 + 1);
    memcpy(outfn, statsfile, ofn_len + 1);
    memcpy(outfn_tmp, statsfile, ofn_len);
    memcpy(&outfn_tmp[ofn_len], ".tmp\0", 4 + 1);
    log_timer = malloc(sizeof(ev_timer));
    ev_timer_init(log_timer, log_timer_cb, LOG_INTERVAL, LOG_INTERVAL);
    ev_set_priority(log_timer, -2);
    ev_timer_start(loop, log_timer);
    file_timer = malloc(sizeof(ev_timer));
    ev_timer_init(file_timer, file_timer_cb, FILE_INTERVAL, FILE_INTERVAL);
    ev_set_priority(file_timer, -2);
    ev_timer_start(loop, file_timer);
}

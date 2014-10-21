/*
 * Copyright 2014 Con Kolivas <kernel@kolivas.org>
 * Copyright 2014 Zvi Shteingart - Spondoolies-tech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef SPONDA_HFILE
#define SPONDA_HFILE

#include "miner.h"
#include "driver-spondoolies-sp30-p.h"



typedef enum adapter_state {
	ADAPTER_STATE_INIT,
	ADAPTER_STATE_OPERATIONAL,
} ADAPTER_STATE;

typedef enum spond_work_state {
	SPONDWORK_STATE_EMPTY,
	SPONDWORK_STATE_IN_BUSY,
} SPONDWORK_STATE;

#define MAX_JOBS_PENDING_IN_MINERGATE_SP30 30
#define MAX_NROLES 60 


typedef struct {
	struct work      *cgminer_work;
	SPONDWORK_STATE  state;
	uint32_t         merkle_root;
	time_t           start_time;
	int              job_id;
} spond_driver_work_sp30;



struct spond_adapter {
	pthread_mutex_t lock;
	ADAPTER_STATE adapter_state;
	void *cgpu;

	// Statistics
	int wins;
	int good;
	int empty;
	int bad;
	int overflow;
	// state
	int works_in_driver;
	int works_in_minergate_and_pending_tx;
	int works_pending_tx;
	int socket_fd;
	int reset_mg_queue;  // 3=reset, 2=fast send 1 job, 1=fast send 10 jobs, 0=nada
	int current_job_id;
	int parse_resp;
	minergate_req_packet_sp30* mp_next_req;
	minergate_rsp_packet_sp30* mp_last_rsp;
	spond_driver_work_sp30 my_jobs[MAX_JOBS_PENDING_IN_MINERGATE_SP30];

	// Temperature statistics
	int temp_rate;
	int front_temp;
	int rear_temp_top;
	int rear_temp_bot;

	// Last second we polled stats
	time_t last_stats;
};

// returns non-zero if needs to change ASICs.
int spond_one_sec_timer_scaling(struct spond_adapter *a, int t);
int spond_do_scaling(struct spond_adapter *a);

extern void one_sec_spondoolies_watchdog(int uptime);

#define REQUEST_PERIOD (100000)  //  times per second - in usec
#define REQUEST_SIZE   10      //  jobs per request

#endif

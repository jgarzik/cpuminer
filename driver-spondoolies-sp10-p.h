/*
 * Copyright 2014-2015 Con Kolivas <kernel@kolivas.org>
 * Copyright 2014 Zvi (Zvisha) Shteingart - Spondoolies-tech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 * Note that changing this SW will void your miners guaranty
 */

#ifndef ____MINERGATE_LIB_H___
#define ____MINERGATE_LIB_H___

#include "config.h"

//#include "squid.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <netinet/in.h>
//#include "queue.h"
//#include "spond_debug.h"

#include "miner.h"
#include "util.h"

#ifndef passert
#define passert assert
#endif

#define MINERGATE_PROTOCOL_VERSION 6
#define MINERGATE_SOCKET_FILE "/tmp/connection_pipe"

typedef enum {
	//MINERGATE_DATA_ID_CONNECT = 1,
	MINERGATE_DATA_ID_DO_JOB_REQ = 2,
	MINERGATE_DATA_ID_DO_JOB_RSP = 3, 

} MINERGATE_DATA_ID;

typedef struct {
	uint32_t work_id_in_sw;
	uint32_t difficulty;
	uint32_t timestamp;
	uint32_t mrkle_root;
	uint32_t midstate[8];
	uint8_t leading_zeroes;
	uint8_t ntime_limit;
	uint8_t ntime_offset;
	uint8_t resr1;
} minergate_do_job_req;

#define MAX_REQUESTS 100
#define MAX_RESPONDS 300
#define MINERGATE_TOTAL_QUEUE 300

typedef struct {
	uint32_t work_id_in_sw;
	uint32_t mrkle_root;     // to validate
	uint32_t winner_nonce[2];
	uint8_t  ntime_offset;
	uint8_t  res;            // 0 = done, 1 = overflow, 2 = dropped bist
	uint8_t  resrv1;
	uint8_t  resrv2;
} minergate_do_job_rsp;


typedef struct {
	uint8_t requester_id;
	uint8_t request_id;
	uint8_t protocol_version;
	uint8_t mask; // 0x01 = first request, 0x2 = drop old work
	uint16_t magic; // 0xcafe
	uint16_t req_count;
	minergate_do_job_req req[MAX_REQUESTS]; // array of requests
} minergate_req_packet;

typedef struct {
	uint8_t requester_id;
	uint8_t request_id;
	uint8_t protocol_version;
	uint8_t gh_div_10_rate; // == 
	uint16_t magic; // 0xcafe
	uint16_t rsp_count;
	minergate_do_job_rsp rsp[MAX_RESPONDS]; // array of responce
} minergate_rsp_packet;

minergate_req_packet *allocate_minergate_packet_req(uint8_t requester_id, uint8_t request_id);
minergate_rsp_packet *allocate_minergate_packet_rsp(uint8_t requester_id, uint8_t request_id);

#endif

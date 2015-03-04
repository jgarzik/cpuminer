/*
 * Copyright 2015 Con Kolivas <kernel@kolivas.org>
 * Copyright 2014 Zvi (Zvisha) Shteingart - Spondoolies-tech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 * Note that changing this SW will void your miners guaranty
 */


#ifndef ____MINERGATE_LIB30_H___
#define ____MINERGATE_LIB30_H___

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <netinet/in.h>

#include "miner.h"
#include "util.h"

#define MINERGATE_PROTOCOL_VERSION_SP30 30
#define MINERGATE_SOCKET_FILE_SP30 "/tmp/connection_pipe_sp30"

typedef enum {
  MINERGATE_DATA_ID_DO_JOB_REQ_SP30 = 5,
  MINERGATE_DATA_ID_DO_JOB_RSP_SP30 = 6,
} MINERGATE_DATA_ID_SP30;

typedef struct {
  uint32_t work_id_in_sw;
  uint32_t difficulty;
  uint32_t timestamp;
  uint32_t mrkle_root;
  uint32_t midstate[8];
  uint8_t leading_zeroes;
  uint8_t ntime_limit; // max ntime - should be 60
  uint8_t resr2;
  uint8_t resr1;
} minergate_do_job_req_sp30;

#define MAX_REQUESTS_SP30 30
#define MAX_RESPONDS_SP30 60
#define MINERGATE_ADAPTER_QUEUE_SP30 40

typedef struct {
  uint32_t work_id_in_sw;
  uint32_t mrkle_root; // to validate
  uint32_t winner_nonce;  
  uint8_t  ntime_offset;
  uint8_t res; // 0 = done, 1 = overflow, 2 = dropped bist
  uint8_t job_complete; 
  uint8_t resrv2; 
} minergate_do_job_rsp_sp30;

typedef struct {
  uint8_t requester_id;
  uint8_t request_id;
  uint8_t protocol_version;
	uint8_t mask; // 0x01 = first request, 0x2 = drop old work
  uint16_t magic;   // 0xcaf4
  uint16_t req_count;
  minergate_do_job_req_sp30 req[MAX_REQUESTS_SP30]; // array of requests
} minergate_req_packet_sp30;

typedef struct {
  uint8_t requester_id;
  uint8_t request_id;
  uint8_t protocol_version;
  uint8_t gh_div_50_rate;
  uint16_t magic;   // 0xcaf4
  uint16_t rsp_count;
  minergate_do_job_rsp_sp30 rsp[MAX_RESPONDS_SP30]; // array of responces
} minergate_rsp_packet_sp30;

minergate_req_packet_sp30* allocate_minergate_packet_req_sp30(uint8_t requester_id,uint8_t request_id);
minergate_rsp_packet_sp30* allocate_minergate_packet_rsp_sp30(uint8_t requester_id,uint8_t request_id);

#endif

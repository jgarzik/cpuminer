/*
 * Copyright 2014 Con Kolivas <kernel@kolivas.org>
 * Copyright 2014 Zvi (Zvisha) Shteingart - Spondoolies-tech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 * Note that changing this SW will void your miners guaranty
 */

/*
	This file holds functions needed for minergate packet parsing/creation
	by Zvisha Shteingart
*/

#include "driver-spondoolies-sp10-p.h"
#include "assert.h"
//#include "spond_debug.h"

minergate_req_packet *allocate_minergate_packet_req(uint8_t requester_id, uint8_t request_id)
{
	minergate_req_packet *p  = (minergate_req_packet*)malloc(sizeof(minergate_req_packet));
	p->requester_id = requester_id;
	p->req_count = 0;
	p->protocol_version = MINERGATE_PROTOCOL_VERSION;
	p->request_id = request_id;
	p->magic = 0xcaf4;
	p->mask |= 0x01; // first packet
	return p;
}

minergate_rsp_packet *allocate_minergate_packet_rsp(uint8_t requester_id, uint8_t request_id)
{
	minergate_rsp_packet *p  = (minergate_rsp_packet*)malloc(sizeof(minergate_rsp_packet));
	p->requester_id = requester_id;
	p->rsp_count = 0;
	p->protocol_version = MINERGATE_PROTOCOL_VERSION;
	p->request_id = request_id;
	p->magic = 0xcaf4;
	p->gh_div_10_rate = 0;
	return p;
}

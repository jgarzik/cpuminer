/*
 * Copyright 2014 Zvi (Zvisha) Shteingart - Spondoolies-tech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 This file holds functions needed for minergate packet parsing/creation
*/

#include "driver-spondoolies-sp30-p.h"
#include "assert.h"
//#include "spond_debug.h"

#ifndef passert
#define passert assert
#endif

minergate_req_packet_sp30 *allocate_minergate_packet_req_sp30(uint8_t requester_id,
                                                        uint8_t request_id) {
  minergate_req_packet_sp30 *p =
      (minergate_req_packet_sp30 *)malloc(sizeof(minergate_req_packet_sp30));
  p->requester_id = requester_id;
  p->req_count = 0;
  p->protocol_version = MINERGATE_PROTOCOL_VERSION_SP30;
  p->request_id = request_id;
  p->magic = 0xcaf4;
  p->mask = 0;
  return p;
}

minergate_rsp_packet_sp30 *allocate_minergate_packet_rsp_sp30(uint8_t requester_id,
                                                    uint8_t request_id) {

  minergate_rsp_packet_sp30 *p =
      (minergate_rsp_packet_sp30 *)malloc(sizeof(minergate_rsp_packet_sp30));
  p->requester_id = requester_id;
  p->rsp_count = 0;
  p->protocol_version = MINERGATE_PROTOCOL_VERSION_SP30;
  p->request_id = request_id;
  p->magic = 0xcaf4;
  p->gh_div_50_rate= 0;
  return p;
}

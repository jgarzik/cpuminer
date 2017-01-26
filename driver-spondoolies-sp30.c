/*
 * Copyright 2014 Con Kolivas <kernel@kolivas.org>
 * Copyright 2014 Zvi (Zvisha) Shteingart - Spondoolies-tech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 This driver communicates the job requests via Unix socket to the minergate
 process, that is responsible for controlling the Spondoolies Dawson SP10 miner.

 The jobs sent each with unique ID and returned asynchronously in one of the next
 transactions. REQUEST_PERIOD and REQUEST_SIZE define the communication rate with minergate.
*/

#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "config.h"

#include "compat.h"
#include "miner.h"
#include "driver-spondoolies-sp30-p.h"
#include "driver-spondoolies-sp30.h"

#ifdef WORDS_BIGENDIAN
#  define swap32tobe(out, in, sz)  ((out == in) ? (void)0 : memmove(out, in, sz))
#  define LOCAL_swap32be(type, var, sz)  ;
#  define swap32tole(out, in, sz)  swap32yes(out, in, sz)
#  define LOCAL_swap32le(type, var, sz)  LOCAL_swap32(type, var, sz)
#else
#  define swap32tobe(out, in, sz)  swap32yes(out, in, sz)
#  define LOCAL_swap32be(type, var, sz)  LOCAL_swap32(type, var, sz)
#  define swap32tole(out, in, sz)  ((out == in) ? (void)0 : memmove(out, in, sz))
#  define LOCAL_swap32le(type, var, sz)  ;
#endif

static inline void swap32yes(void *out, const void *in, size_t sz)
{
	size_t swapcounter;

	for (swapcounter = 0; swapcounter < sz; ++swapcounter)
		(((uint32_t*)out)[swapcounter]) = swab32(((uint32_t*)in)[swapcounter]);
}

static void send_minergate_pkt(const minergate_req_packet_sp30* mp_req, minergate_rsp_packet_sp30* mp_rsp,
			int  socket_fd)
{
	int nbytes, nwrote, nread;

	nbytes = sizeof(minergate_req_packet_sp30);
	nwrote = write(socket_fd, (const void *)mp_req, nbytes);
	if (unlikely(nwrote != nbytes))
		_quit(-1);
	nbytes = sizeof(minergate_rsp_packet_sp30);
	nread = read(socket_fd, (void *)mp_rsp, nbytes);
	if (unlikely(nread != nbytes))
		_quit(-1);
	assert(mp_rsp->magic == 0xcaf4);
}

static bool spondoolies_prepare_sp30(struct thr_info *thr)
{
	struct cgpu_info *spondoolies_sp30 = thr->cgpu;
	struct timeval now;

	assert(spondoolies_sp30);
	cgtime(&now);
	/* FIXME: Vladik */
#if NEED_FIX
	get_datestamp(spondoolies_sp30->init, &now);
#endif
	return true;
}

static int init_socket(void)
{
	int socket_fd;
	struct sockaddr_un address;

	socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		applog(LOG_ERR, "SP30: socket error: %s", strerror(errno));
		return 0;
	}

	/* start with a clean address structure */
	memset(&address, 0, sizeof(struct sockaddr_un));

	address.sun_family = AF_UNIX;
	sprintf(address.sun_path, MINERGATE_SOCKET_FILE_SP30);

	if (connect(socket_fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un))) {
		applog(LOG_ERR, "SP30: socket connect error: %s", strerror(errno));
		return 0;
	}

	return socket_fd;
}

static bool spondoolies_flush_queue(struct spond_adapter* a, bool flush_queue)
{
	if (!a->parse_resp) {
		static int i = 0;

		if (i++ % 10 == 0 && a->works_in_minergate_and_pending_tx + a->works_pending_tx != a->works_in_driver)
			applog(LOG_DEBUG, "SP30: %d + %d != %d", a->works_in_minergate_and_pending_tx, a->works_pending_tx,a->works_in_driver);
		assert(a->works_in_minergate_and_pending_tx + a->works_pending_tx == a->works_in_driver);
		send_minergate_pkt(a->mp_next_req,  a->mp_last_rsp, a->socket_fd);
		if (flush_queue) {
			applog(LOG_DEBUG, "SP30: FLUSH!");
			a->mp_next_req->mask |= 0x02;
		} else {
			a->mp_next_req->mask &= ~0x02;
		}

		a->mp_next_req->req_count = 0;
		a->parse_resp = 1;
		a->works_in_minergate_and_pending_tx += a->works_pending_tx;
		a->works_pending_tx = 0;
	}
	return true;
}

static void spondoolies_detect_sp30(__maybe_unused bool hotplug)
{
	struct cgpu_info *cgpu = calloc(1, sizeof(*cgpu));
	struct device_drv *drv = &sp30_drv;
	struct spond_adapter *a;

#if NEED_FIX
	nDevs = 1;
#endif

	assert(cgpu);
	cgpu->drv = drv;
	cgpu->deven = DEV_ENABLED;
	cgpu->threads = 1;
	cgpu->device_data = calloc(sizeof(struct spond_adapter), 1);
	if (unlikely(!(cgpu->device_data)))
		quit(1, "Failed to calloc cgpu_info data");
	a = cgpu->device_data;
	a->cgpu = (void *)cgpu;
	a->adapter_state = ADAPTER_STATE_OPERATIONAL;
	a->mp_next_req = allocate_minergate_packet_req_sp30(0xca, 0xfe);
	a->mp_last_rsp = allocate_minergate_packet_rsp_sp30(0xca, 0xfe);

	pthread_mutex_init(&a->lock, NULL);
	a->socket_fd = init_socket();
	if (a->socket_fd < 1) {
		applog(LOG_ERR, "SP30: Failed to connect to minergate server");
		_quit(-1);
	}

	assert(add_cgpu(cgpu));
	// Clean MG socket
	spondoolies_flush_queue(a, true);
	spondoolies_flush_queue(a, true);
	spondoolies_flush_queue(a, true);
	applog(LOG_DEBUG, "SP30: SPOND spondoolies_detect_sp30 done");
}

static struct api_data *spondoolies_api_stats_sp30(struct cgpu_info *cgpu)
{
	struct spond_adapter *a = cgpu->device_data;
	struct api_data *root = NULL;

	root = api_add_int(root, "ASICs total rate", &a->temp_rate, false);
	root = api_add_int(root, "Temperature front", &a->front_temp, false);
	root = api_add_int(root, "Temperature rear top", &a->rear_temp_top, false);
	root = api_add_int(root, "Temperature rear bot", &a->rear_temp_bot, false);

  

	return root;
}

#if 0
static unsigned char get_leading_zeroes(const unsigned char *target)
{
	unsigned char leading = 0;
	int first_non_zero_chr;
	uint8_t m;

	for (first_non_zero_chr = 31; first_non_zero_chr >= 0; first_non_zero_chr--) {
		if (target[first_non_zero_chr] == 0)
			leading += 8;
		else
			break;
	}

	// j = first non-zero
	m = target[first_non_zero_chr];
	while ((m & 0x80) == 0) {
		leading++;
		m = m << 1;
	}
	return leading;
}
#endif

static void spondoolies_shutdown_sp30(__maybe_unused struct thr_info *thr)
{
}

static void fill_minergate_request(minergate_do_job_req_sp30* work, struct work *cg_work, int max_offset)
{
	uint32_t x[64 / 4];
	uint64_t wd;

	memset(work, 0, sizeof(minergate_do_job_req_sp30));
	//work->
	LOCAL_swap32le(unsigned char, cg_work->midstate, 32 / 4)
	LOCAL_swap32le(unsigned char, cg_work->data + 64, 64 / 4)
	swap32yes(x, cg_work->data + 64, 64 / 4);
	memcpy(work->midstate, cg_work->midstate, 32);
	work->mrkle_root = ntohl(x[0]);
	work->timestamp  = ntohl(x[1]);
	work->difficulty = ntohl(x[2]);
	//work->leading_zeroes = get_leading_zeroes(cg_work->target);
	// Is there no better way to get leading zeroes?
	work->leading_zeroes = 31;
	wd = round(cg_work->device_diff);
	while (wd) {
		work->leading_zeroes++;
		wd = wd >> 1;
	}
	//printf("%d %d\n",work->leading_zeroes, (int)round(cg_work->work_difficulty));
	work->work_id_in_sw = cg_work->subid;
	work->ntime_limit = max_offset;
	//printf("ID:%d, TS:%x\n",work->work_id_in_sw,work->timestamp);
	//work->ntime_offset = ntime_offset;
}

// returns true if queue full.
static struct timeval last_force_queue;

unsigned long usec_stamp(void)
{
	static unsigned long long int first_usec = 0;
	struct timeval tv;
	unsigned long long int curr_usec;

	cgtime(&tv);
	curr_usec = tv.tv_sec * 1000000 + tv.tv_usec;
	if (first_usec == 0) {
		first_usec = curr_usec;
		curr_usec = 0;
	} else
		curr_usec -= first_usec;
	return curr_usec;
}

static bool spondoolies_queue_full_sp30(struct cgpu_info *cgpu)
{
	struct spond_adapter* a = cgpu->device_data;
#if 0
	static int bla = 0;

	if (!((bla++)%500)) {
		printf("FAKE TEST FLUSH T:%d!\n",usec_stamp());
		a->reset_mg_queue = 3;
	}
#endif
	// Only once every 1/10 second do work.
	bool ret = false, do_sleep = false;
	int next_job_id;
	struct timeval tv;
	struct work *work;
	unsigned int usec;

	mutex_lock(&a->lock);
	assert(a->works_pending_tx <= REQUEST_SIZE);

	gettimeofday(&tv, NULL);

	usec = (tv.tv_sec-last_force_queue.tv_sec) * 1000000;
	usec += (tv.tv_usec-last_force_queue.tv_usec);

	if ((usec >= REQUEST_PERIOD) ||
		(a->reset_mg_queue == 3) || // push flush
		((a->reset_mg_queue == 2)) || // Fast pull
		((a->reset_mg_queue == 1) && (a->works_pending_tx == REQUEST_SIZE))) { // Fast push after flush
			spondoolies_flush_queue(a, (a->reset_mg_queue == 3));
			if (a->reset_mg_queue) {
				//printf("FLUSH(%d) %d T:%d\n",a->reset_mg_queue , a->works_pending_tx, usec_stamp());
				if (a->works_pending_tx || (a->reset_mg_queue == 3)) {
					a->reset_mg_queue--;
				}
			}
			last_force_queue = tv;
	}

	// see if we have enough jobs
	if (a->works_pending_tx == REQUEST_SIZE) {
		ret = true;
		goto return_unlock;
	}

	// see if can take 1 more job.
	// Must be smaller to prevent overflow.
	assert(MAX_JOBS_PENDING_IN_MINERGATE_SP30 < MINERGATE_ADAPTER_QUEUE_SP30);
	next_job_id = (a->current_job_id + 1) % MAX_JOBS_PENDING_IN_MINERGATE_SP30;
	if (a->my_jobs[next_job_id].cgminer_work) {
		ret = true;
		goto return_unlock;
	}
	work = get_queued(cgpu);
	if (unlikely(!work)) {
		do_sleep = true;
		goto return_unlock;
	}

	work->thr = cgpu->thr[0];
	work->thr_id = cgpu->thr[0]->id;
	assert(work->thr);

	a->current_job_id = next_job_id;
	work->subid = a->current_job_id;
	// Get pointer for the request
	a->my_jobs[a->current_job_id].cgminer_work = work;
	a->my_jobs[a->current_job_id].state = SPONDWORK_STATE_IN_BUSY;
	//printf("Push: %d\n", a->current_job_id);

	int max_ntime_roll = (work->drv_rolllimit < MAX_NROLES) ? work->drv_rolllimit : MAX_NROLES;
	minergate_do_job_req_sp30* pkt_job =  &a->mp_next_req->req[a->works_pending_tx];
	fill_minergate_request(pkt_job, work, max_ntime_roll);
	a->works_in_driver++;
	a->works_pending_tx++;
	a->mp_next_req->req_count++;
	a->my_jobs[a->current_job_id].merkle_root = pkt_job->mrkle_root;

return_unlock:
	//printf("D:P.TX:%d inD:%d\n", a->works_pending_tx, a->works_in_driver);
	mutex_unlock(&a->lock);

	if (do_sleep)
		cgsleep_ms(10);

	return ret;
}

static void spond_poll_stats(struct cgpu_info *spond, struct spond_adapter *a)
{
	FILE *fp = fopen("/var/run/mg_rate_temp", "r");

	if (!fp) {
		applog(LOG_DEBUG, "SP30: SPOND unable to open mg_rate_temp");
		a->temp_rate = a->front_temp = a->rear_temp_top = a->rear_temp_bot = 0;
	} else {
  	int ret = fscanf(fp, "%d %d %d %d", &a->temp_rate,  &a->front_temp , &a->rear_temp_top , &a->rear_temp_bot);


	if (ret != 4)
		a->temp_rate = a->front_temp = a->rear_temp_top = a->rear_temp_bot = 0;
		fclose(fp);
	}
	applog(LOG_DEBUG, "SP30: SPOND poll_stats rate: %d front: %d rear(T/B): %d/%d",
	a->temp_rate, a->front_temp , a->rear_temp_top, a->rear_temp_bot);
	/* Use the rear temperature as the dev temperature for now */
	spond->temp = (a->rear_temp_top + a->rear_temp_bot)/2;
}

// Return completed work to submit_nonce() and work_completed()
// struct timeval last_force_queue = {0};
static int64_t spond_scanhash_sp30(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct spond_adapter *a = cgpu->device_data;
	int64_t ghashes = 0;
	cgtimer_t cgt;
	time_t now_t;

	cgsleep_prepare_r(&cgt);
	now_t = time(NULL);
	/* Poll stats only once per second */
	if (now_t != a->last_stats) {
		a->last_stats = now_t;
		spond_poll_stats(cgpu, a);
	}

	if (a->parse_resp) {
		int array_size, i;

		mutex_lock(&a->lock);
		//ghashes = (a->mp_last_rsp->gh_div_50_rate);
		//ghashes = ghashes  * 50000 * REQUEST_PERIOD;
		array_size = a->mp_last_rsp->rsp_count;
		for (i = 0; i < array_size; i++) { // walk the jobs
			int job_id;

			minergate_do_job_rsp_sp30* work = a->mp_last_rsp->rsp + i;
			job_id = work->work_id_in_sw;
			if ((a->my_jobs[job_id].cgminer_work)) {
				if (a->my_jobs[job_id].merkle_root == work->mrkle_root) {
					assert(a->my_jobs[job_id].state == SPONDWORK_STATE_IN_BUSY);

					if (work->winner_nonce) {
						struct work *cg_work = a->my_jobs[job_id].cgminer_work;
						bool ok;

						ok = submit_noffset_nonce(cg_work->thr, cg_work, work->winner_nonce, work->ntime_offset);
						if (ok)
							ghashes += 0xffffffffull * cg_work->device_diff;
						/*printf("WIn on %d (+%d), none=%x = %d\n",
						*              work->work_id_in_sw, work->ntime_offset, htole32(work->winner_nonce), ok);*/
						a->wins++;
					}

					//printf("%d ntime_clones = %d\n",job_id,a->my_jobs[job_id].ntime_clones);

					//printf("Done with %d\n", job_id);
					if (work->job_complete) {
						//printf("Complete %d\n", job_id);
						work_completed(a->cgpu, a->my_jobs[job_id].cgminer_work);
						a->good++;
						a->my_jobs[job_id].cgminer_work = NULL;
						a->my_jobs[job_id].state = SPONDWORK_STATE_EMPTY;
						a->works_in_minergate_and_pending_tx--;
						a->works_in_driver--;
					}
				} else {
					a->bad++;
					applog(LOG_DEBUG, "SP30: Dropping minergate old job id=%d mrkl=%x my-mrkl=%x",
					job_id, a->my_jobs[job_id].merkle_root, work->mrkle_root);
				}
			} else {
				a->empty++;
				applog(LOG_DEBUG, "SP30: No cgminer job (id:%d res:%d)!",job_id, work->res);
			}
		}
		mutex_unlock(&a->lock);

		a->parse_resp = 0;
	}
	cgsleep_ms_r(&cgt, 40);

	return ghashes;
}

// Remove all work from queue
static void spond_flush_work_sp30(struct cgpu_info *cgpu)
{
	struct spond_adapter *a = cgpu->device_data;

	//printf("GOT FLUSH!%d\n");
	mutex_lock(&a->lock);
	a->reset_mg_queue = 3;
	mutex_unlock(&a->lock);
}

struct device_drv sp30_drv = {
	.drv_id = DRIVER_sp30,
	.dname = "Sp30",
	.name = "S30",
	.min_diff = 16,
	.max_diff = 1024.0, // Limit max diff to get some nonces back regardless
	.drv_detect = spondoolies_detect_sp30,
	.get_api_stats = spondoolies_api_stats_sp30,
	.thread_prepare = spondoolies_prepare_sp30,
	.thread_shutdown = spondoolies_shutdown_sp30,
	.hash_work = hash_queued_work,
	.queue_full = spondoolies_queue_full_sp30,
	.scanwork = spond_scanhash_sp30,
	.flush_work = spond_flush_work_sp30,
};

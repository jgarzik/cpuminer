/*
 * Copyright 2011-2014 Con Kolivas
 * Copyright 2011-2012 Luke Dashjr
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef HAVE_CURSES
#include <curses.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#include <limits.h>

#ifdef USE_USBUTILS
#include <semaphore.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

#ifndef WIN32
#include <sys/resource.h>
#else
#include <windows.h>
#endif
#include <ccan/opt/opt.h>
#include <jansson.h>
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#else
char *curly = ":D";
#endif
#include <libgen.h>
#include <sha2.h>

#include "compat.h"
#include "miner.h"
#include "bench_block.h"
#ifdef USE_USBUTILS
#include "usbutils.h"
#endif

#if defined(unix) || defined(__APPLE__)
	#include <errno.h>
	#include <fcntl.h>
	#include <sys/wait.h>
#endif

#ifdef USE_AVALON
#include "driver-avalon.h"
#endif

#ifdef USE_AVALON2
#include "driver-avalon2.h"
#endif

#ifdef USE_AVALON4
#include "driver-avalon4.h"
#endif

#ifdef USE_BFLSC
#include "driver-bflsc.h"
#endif

#ifdef USE_SP10
#include "driver-spondoolies-sp10.h"
#endif

#ifdef USE_SP30
#include "driver-spondoolies-sp30.h"
#endif

#ifdef USE_BLOCK_ERUPTER
#include "driver-blockerupter.h"
#endif

#ifdef USE_BITFURY
#include "driver-bitfury.h"
#endif

#ifdef USE_COINTERRA
#include "driver-cointerra.h"
#endif

#ifdef USE_HASHFAST
#include "driver-hashfast.h"
#endif

#if defined(USE_ANT_S1) || defined(USE_ANT_S2)
#include "driver-bitmain.h"
#endif

#if defined(USE_BITFORCE) || defined(USE_ICARUS) || defined(USE_AVALON) || defined(USE_AVALON2) || defined(USE_MODMINER)
#	define USE_FPGA
#endif

struct strategies strategies[] = {
	{ "Failover" },
	{ "Round Robin" },
	{ "Rotate" },
	{ "Load Balance" },
	{ "Balance" },
};

static char packagename[256];

bool opt_work_update;
bool opt_protocol;
static struct benchfile_layout {
	int length;
	char *name;
} benchfile_data[] = {
	{ 1,	"Version" },
	{ 64,	"MerkleRoot" },
	{ 64,	"PrevHash" },
	{ 8,	"DifficultyBits" },
	{ 10,	"NonceTime" } // 10 digits
};
enum benchwork {
	BENCHWORK_VERSION = 0,
	BENCHWORK_MERKLEROOT,
	BENCHWORK_PREVHASH,
	BENCHWORK_DIFFBITS,
	BENCHWORK_NONCETIME,
	BENCHWORK_COUNT
};

#ifdef HAVE_LIBCURL
static char *opt_btc_address;
static char *opt_btc_sig;
#endif
static char *opt_benchfile;
static bool opt_benchfile_display;
static FILE *benchfile_in;
static int benchfile_line;
static int benchfile_work;
static bool opt_benchmark;
bool have_longpoll;
bool want_per_device_stats;
bool use_syslog;
bool opt_quiet;
bool opt_realquiet;
bool opt_loginput;
bool opt_compact;
const int opt_cutofftemp = 95;
int opt_log_interval = 5;
int opt_queue = 1;
static int max_queue = 1;
int opt_scantime = -1;
int opt_expiry = 120;
static const bool opt_time = true;
unsigned long long global_hashrate;
unsigned long global_quota_gcd = 1;
time_t last_getwork;

#if defined(USE_USBUTILS)
int nDevs;
#endif
bool opt_restart = true;
bool opt_nogpu;

struct list_head scan_devices;
static bool opt_display_devs;
int total_devices;
int zombie_devs;
static int most_devices;
struct cgpu_info **devices;
int mining_threads;
int num_processors;
#ifdef HAVE_CURSES
bool use_curses = true;
#else
bool use_curses;
#endif
static bool opt_widescreen;
static bool alt_status;
static bool switch_status;
static bool opt_submit_stale = true;
static int opt_shares;
bool opt_fail_only;
static bool opt_fix_protocol;
bool opt_lowmem;
bool opt_autofan;
bool opt_autoengine;
bool opt_noadl;
char *opt_api_allow = NULL;
char *opt_api_groups;
char *opt_api_description = PACKAGE_STRING;
int opt_api_port = 4028;
char *opt_api_host = API_LISTEN_ADDR;
bool opt_api_listen;
bool opt_api_mcast;
char *opt_api_mcast_addr = API_MCAST_ADDR;
char *opt_api_mcast_code = API_MCAST_CODE;
char *opt_api_mcast_des = "";
int opt_api_mcast_port = 4028;
bool opt_api_network;
bool opt_delaynet;
bool opt_disable_pool;
static bool no_work;
#ifdef USE_ICARUS
char *opt_icarus_options = NULL;
char *opt_icarus_timing = NULL;
float opt_anu_freq = 250;
float opt_au3_freq = 225;
int opt_au3_volt = 750;
float opt_rock_freq = 270;
#endif
bool opt_worktime;
#ifdef USE_AVALON
char *opt_avalon_options;
char *opt_bitburner_fury_options;
static char *opt_set_avalon_fan;
static char *opt_set_avalon_freq;
#endif
#ifdef USE_AVALON2
static char *opt_set_avalon2_freq;
static char *opt_set_avalon2_fan;
static char *opt_set_avalon2_voltage;
#endif
#ifdef USE_AVALON4
static char *opt_set_avalon4_fan;
static char *opt_set_avalon4_voltage;
static char *opt_set_avalon4_freq;
#endif
#ifdef USE_BLOCKERUPTER
int opt_bet_clk = 0;
#endif
#ifdef USE_HASHRATIO
#include "driver-hashratio.h"
#endif
#ifdef USE_KLONDIKE
char *opt_klondike_options = NULL;
#endif
#ifdef USE_DRILLBIT
char *opt_drillbit_options = NULL;
char *opt_drillbit_auto = NULL;
#endif
char *opt_bab_options = NULL;
#ifdef USE_BITMINE_A1
char *opt_bitmine_a1_options = NULL;
#endif
#if defined(USE_ANT_S1) || defined(USE_ANT_S2)
char *opt_bitmain_options;
static char *opt_set_bitmain_fan;
static char *opt_set_bitmain_freq;
#endif
#ifdef USE_ANT_S2
char *opt_bitmain_dev;
#endif
#ifdef USE_HASHFAST
static char *opt_set_hfa_fan;
#endif
static char *opt_set_null;
#ifdef USE_MINION
int opt_minion_chipreport;
char *opt_minion_cores;
bool opt_minion_extra;
char *opt_minion_freq;
int opt_minion_freqchange = 1000;
int opt_minion_freqpercent = 70;
bool opt_minion_idlecount;
int opt_minion_ledcount;
int opt_minion_ledlimit = 98;
bool opt_minion_noautofreq;
bool opt_minion_overheat;
int opt_minion_spidelay;
char *opt_minion_spireset;
int opt_minion_spisleep = 200;
int opt_minion_spiusec;
char *opt_minion_temp;
#endif

#ifdef USE_USBUTILS
char *opt_usb_select = NULL;
int opt_usbdump = -1;
bool opt_usb_list_all;
cgsem_t usb_resource_sem;
static pthread_t usb_poll_thread;
static bool usb_polling;
#endif

char *opt_kernel_path;
char *cgminer_path;

#if defined(USE_BITFORCE)
bool opt_bfl_noncerange;
#endif
#define QUIET	(opt_quiet || opt_realquiet)

struct thr_info *control_thr;
struct thr_info **mining_thr;
static int gwsched_thr_id;
static int watchpool_thr_id;
static int watchdog_thr_id;
#ifdef HAVE_CURSES
static int input_thr_id;
#endif
int gpur_thr_id;
static int api_thr_id;
#ifdef USE_USBUTILS
static int usbres_thr_id;
static int hotplug_thr_id;
#endif
static int total_control_threads;
bool hotplug_mode;
static int new_devices;
static int new_threads;
int hotplug_time = 5;

#if LOCK_TRACKING
pthread_mutex_t lockstat_lock;
#endif

pthread_mutex_t hash_lock;
static pthread_mutex_t *stgd_lock;
pthread_mutex_t console_lock;
cglock_t ch_lock;
static pthread_rwlock_t blk_lock;
static pthread_mutex_t sshare_lock;

pthread_rwlock_t netacc_lock;
pthread_rwlock_t mining_thr_lock;
pthread_rwlock_t devices_lock;

static pthread_mutex_t lp_lock;
static pthread_cond_t lp_cond;

pthread_mutex_t restart_lock;
pthread_cond_t restart_cond;

pthread_cond_t gws_cond;

double rolling1, rolling5, rolling15;
double total_rolling;
double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;
static struct timeval restart_tv_start, update_tv_start;

cglock_t control_lock;
pthread_mutex_t stats_lock;

int hw_errors;
int64_t total_accepted, total_rejected, total_diff1;
int64_t total_getworks, total_stale, total_discarded;
double total_diff_accepted, total_diff_rejected, total_diff_stale;
static int staged_rollable;
unsigned int new_blocks;
static unsigned int work_block;
unsigned int found_blocks;

unsigned int local_work;
unsigned int total_go, total_ro;

struct pool **pools;
static struct pool *currentpool = NULL;

int total_pools, enabled_pools;
enum pool_strategy pool_strategy = POOL_FAILOVER;
int opt_rotate_period;
static int total_urls, total_users, total_passes, total_userpasses;

static
#ifndef HAVE_CURSES
const
#endif
bool curses_active;

/* Protected by ch_lock */
char current_hash[68];
static char prev_block[12];
static char current_block[32];

static char datestamp[40];
static char blocktime[32];
struct timeval block_timeval;
static char best_share[8] = "0";
double current_diff = 0xFFFFFFFFFFFFFFFFULL;
static char block_diff[8];
uint64_t best_diff = 0;

struct block {
	char hash[68];
	UT_hash_handle hh;
	int block_no;
};

static struct block *blocks = NULL;


int swork_id;

/* For creating a hash database of stratum shares submitted that have not had
 * a response yet */
struct stratum_share {
	UT_hash_handle hh;
	bool block;
	struct work *work;
	int id;
	time_t sshare_time;
	time_t sshare_sent;
};

static struct stratum_share *stratum_shares = NULL;

char *opt_socks_proxy = NULL;
int opt_suggest_diff;
static const char def_conf[] = "cgminer.conf";
static char *default_config;
static bool config_loaded;
static int include_count;
#define JSON_INCLUDE_CONF "include"
#define JSON_LOAD_ERROR "JSON decode of file '%s' failed\n %s"
#define JSON_LOAD_ERROR_LEN strlen(JSON_LOAD_ERROR)
#define JSON_MAX_DEPTH 10
#define JSON_MAX_DEPTH_ERR "Too many levels of JSON includes (limit 10) or a loop"
#define JSON_WEB_ERROR "WEB config err"

#if defined(unix) || defined(__APPLE__)
	static char *opt_stderr_cmd = NULL;
	static int forkpid;
#endif // defined(unix)

struct sigaction termhandler, inthandler;

struct thread_q *getq;

static uint32_t total_work;
struct work *staged_work = NULL;

struct schedtime {
	bool enable;
	struct tm tm;
};

struct schedtime schedstart;
struct schedtime schedstop;
bool sched_paused;

static bool time_before(struct tm *tm1, struct tm *tm2)
{
	if (tm1->tm_hour < tm2->tm_hour)
		return true;
	if (tm1->tm_hour == tm2->tm_hour && tm1->tm_min < tm2->tm_min)
		return true;
	return false;
}

static bool should_run(void)
{
	struct timeval tv;
	struct tm *tm;

	if (!schedstart.enable && !schedstop.enable)
		return true;

	cgtime(&tv);
	const time_t tmp_time = tv.tv_sec;
	tm = localtime(&tmp_time);
	if (schedstart.enable) {
		if (!schedstop.enable) {
			if (time_before(tm, &schedstart.tm))
				return false;

			/* This is a once off event with no stop time set */
			schedstart.enable = false;
			return true;
		}
		if (time_before(&schedstart.tm, &schedstop.tm)) {
			if (time_before(tm, &schedstop.tm) && !time_before(tm, &schedstart.tm))
				return true;
			return false;
		} /* Times are reversed */
		if (time_before(tm, &schedstart.tm)) {
			if (time_before(tm, &schedstop.tm))
				return true;
			return false;
		}
		return true;
	}
	/* only schedstop.enable == true */
	if (!time_before(tm, &schedstop.tm))
		return false;
	return true;
}

void get_datestamp(char *f, size_t fsiz, struct timeval *tv)
{
	struct tm *tm;

	const time_t tmp_time = tv->tv_sec;
	tm = localtime(&tmp_time);
	snprintf(f, fsiz, "[%d-%02d-%02d %02d:%02d:%02d]",
		tm->tm_year + 1900,
		tm->tm_mon + 1,
		tm->tm_mday,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static void get_timestamp(char *f, size_t fsiz, struct timeval *tv)
{
	struct tm *tm;

	const time_t tmp_time = tv->tv_sec;
	tm = localtime(&tmp_time);
	snprintf(f, fsiz, "[%02d:%02d:%02d]",
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static char exit_buf[512];

static void applog_and_exit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(exit_buf, sizeof(exit_buf), fmt, ap);
	va_end(ap);
	_applog(LOG_ERR, exit_buf, true);
	exit(1);
}

static pthread_mutex_t sharelog_lock;
static FILE *sharelog_file = NULL;

static struct thr_info *__get_thread(int thr_id)
{
	return mining_thr[thr_id];
}

struct thr_info *get_thread(int thr_id)
{
	struct thr_info *thr;

	rd_lock(&mining_thr_lock);
	thr = __get_thread(thr_id);
	rd_unlock(&mining_thr_lock);

	return thr;
}

static struct cgpu_info *get_thr_cgpu(int thr_id)
{
	struct thr_info *thr = get_thread(thr_id);

	return thr->cgpu;
}

struct cgpu_info *get_devices(int id)
{
	struct cgpu_info *cgpu;

	rd_lock(&devices_lock);
	cgpu = devices[id];
	rd_unlock(&devices_lock);

	return cgpu;
}

static void sharelog(const char*disposition, const struct work*work)
{
	char *target, *hash, *data;
	struct cgpu_info *cgpu;
	unsigned long int t;
	struct pool *pool;
	int thr_id, rv;
	char s[1024];
	size_t ret;

	if (!sharelog_file)
		return;

	thr_id = work->thr_id;
	cgpu = get_thr_cgpu(thr_id);
	pool = work->pool;
	t = (unsigned long int)(work->tv_work_found.tv_sec);
	target = bin2hex(work->target, sizeof(work->target));
	hash = bin2hex(work->hash, sizeof(work->hash));
	data = bin2hex(work->data, sizeof(work->data));

	// timestamp,disposition,target,pool,dev,thr,sharehash,sharedata
	rv = snprintf(s, sizeof(s), "%lu,%s,%s,%s,%s%u,%u,%s,%s\n", t, disposition, target, pool->rpc_url, cgpu->drv->name, cgpu->device_id, thr_id, hash, data);
	free(target);
	free(hash);
	free(data);
	if (rv >= (int)(sizeof(s)))
		s[sizeof(s) - 1] = '\0';
	else if (rv < 0) {
		applog(LOG_ERR, "sharelog printf error");
		return;
	}

	mutex_lock(&sharelog_lock);
	ret = fwrite(s, rv, 1, sharelog_file);
	fflush(sharelog_file);
	mutex_unlock(&sharelog_lock);

	if (ret != 1)
		applog(LOG_ERR, "sharelog fwrite error");
}

static char *getwork_req = "{\"method\": \"getwork\", \"params\": [], \"id\":0}\n";

static char *gbt_req = "{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": [{\"capabilities\": [\"coinbasetxn\", \"workid\", \"coinbase/append\"]}]}\n";

static char *gbt_solo_req = "{\"id\": 0, \"method\": \"getblocktemplate\"}\n";

/* Adjust all the pools' quota to the greatest common denominator after a pool
 * has been added or the quotas changed. */
void adjust_quota_gcd(void)
{
	unsigned long gcd, lowest_quota = ~0UL, quota;
	struct pool *pool;
	int i;

	for (i = 0; i < total_pools; i++) {
		pool = pools[i];
		quota = pool->quota;
		if (!quota)
			continue;
		if (quota < lowest_quota)
			lowest_quota = quota;
	}

	if (likely(lowest_quota < ~0UL)) {
		gcd = lowest_quota;
		for (i = 0; i < total_pools; i++) {
			pool = pools[i];
			quota = pool->quota;
			if (!quota)
				continue;
			while (quota % gcd)
				gcd--;
		}
	} else
		gcd = 1;

	for (i = 0; i < total_pools; i++) {
		pool = pools[i];
		pool->quota_used *= global_quota_gcd;
		pool->quota_used /= gcd;
		pool->quota_gcd = pool->quota / gcd;
	}

	global_quota_gcd = gcd;
	applog(LOG_DEBUG, "Global quota greatest common denominator set to %lu", gcd);
}

/* Return value is ignored if not called from add_pool_details */
struct pool *add_pool(void)
{
	struct pool *pool;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool)
		quit(1, "Failed to malloc pool in add_pool");
	pool->pool_no = pool->prio = total_pools;
	pools = realloc(pools, sizeof(struct pool *) * (total_pools + 2));
	pools[total_pools++] = pool;
	mutex_init(&pool->pool_lock);
	if (unlikely(pthread_cond_init(&pool->cr_cond, NULL)))
		quit(1, "Failed to pthread_cond_init in add_pool");
	cglock_init(&pool->data_lock);
	mutex_init(&pool->stratum_lock);
	cglock_init(&pool->gbt_lock);
	INIT_LIST_HEAD(&pool->curlring);

	/* Make sure the pool doesn't think we've been idle since time 0 */
	pool->tv_idle.tv_sec = ~0UL;

	pool->rpc_req = getwork_req;
	pool->rpc_proxy = NULL;
	pool->quota = 1;
	adjust_quota_gcd();

	return pool;
}

/* Pool variant of test and set */
static bool pool_tset(struct pool *pool, bool *var)
{
	bool ret;

	mutex_lock(&pool->pool_lock);
	ret = *var;
	*var = true;
	mutex_unlock(&pool->pool_lock);

	return ret;
}

bool pool_tclear(struct pool *pool, bool *var)
{
	bool ret;

	mutex_lock(&pool->pool_lock);
	ret = *var;
	*var = false;
	mutex_unlock(&pool->pool_lock);

	return ret;
}

struct pool *current_pool(void)
{
	struct pool *pool;

	cg_rlock(&control_lock);
	pool = currentpool;
	cg_runlock(&control_lock);

	return pool;
}

char *set_int_range(const char *arg, int *i, int min, int max)
{
	char *err = opt_set_intval(arg, i);

	if (err)
		return err;

	if (*i < min || *i > max)
		return "Value out of range";

	return NULL;
}

static char *set_int_0_to_9999(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 9999);
}

static char *set_int_1_to_65535(const char *arg, int *i)
{
	return set_int_range(arg, i, 1, 65535);
}

static char *set_int_0_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 10);
}

static char *set_int_0_to_100(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 100);
}

static char *set_int_0_to_255(const char *arg, int *i)
{
        return set_int_range(arg, i, 0, 255);
}

static char *set_int_0_to_200(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 200);
}

static char *set_int_32_to_63(const char *arg, int *i)
{
	return set_int_range(arg, i, 32, 63);
}

static char *set_int_22_to_55(const char *arg, int *i)
{
	return set_int_range(arg, i, 22, 55);
}

static char *set_int_42_to_65(const char *arg, int *i)
{
	return set_int_range(arg, i, 42, 62);
}

static char *set_int_1_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 1, 10);
}

static char __maybe_unused *set_int_0_to_4(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 4);
}

#ifdef USE_FPGA_SERIAL
static char *opt_add_serial;
static char *add_serial(char *arg)
{
	string_elist_add(arg, &scan_devices);
	return NULL;
}
#endif

void get_intrange(char *arg, int *val1, int *val2)
{
	if (sscanf(arg, "%d-%d", val1, val2) == 1)
		*val2 = *val1;
}

static char *set_balance(enum pool_strategy *strategy)
{
	*strategy = POOL_BALANCE;
	return NULL;
}

static char *set_loadbalance(enum pool_strategy *strategy)
{
	*strategy = POOL_LOADBALANCE;
	return NULL;
}

static char *set_rotate(const char *arg, char __maybe_unused *i)
{
	pool_strategy = POOL_ROTATE;
	return set_int_range(arg, &opt_rotate_period, 0, 9999);
}

static char *set_rr(enum pool_strategy *strategy)
{
	*strategy = POOL_ROUNDROBIN;
	return NULL;
}

/* Detect that url is for a stratum protocol either via the presence of
 * stratum+tcp or by detecting a stratum server response */
bool detect_stratum(struct pool *pool, char *url)
{
	if (!extract_sockaddr(url, &pool->sockaddr_url, &pool->stratum_port))
		return false;

	if (!strncasecmp(url, "stratum+tcp://", 14)) {
		pool->rpc_url = strdup(url);
		pool->has_stratum = true;
		pool->stratum_url = pool->sockaddr_url;
		return true;
	}

	return false;
}

static struct pool *add_url(void)
{
	total_urls++;
	if (total_urls > total_pools)
		add_pool();
	return pools[total_urls - 1];
}

static void setup_url(struct pool *pool, char *arg)
{
	arg = get_proxy(arg, pool);

	if (detect_stratum(pool, arg))
		return;

	opt_set_charp(arg, &pool->rpc_url);
	if (strncmp(arg, "http://", 7) &&
	    strncmp(arg, "https://", 8)) {
		char *httpinput;

		httpinput = malloc(256);
		if (!httpinput)
			quit(1, "Failed to malloc httpinput");
		strcpy(httpinput, "stratum+tcp://");
		strncat(httpinput, arg, 242);
		detect_stratum(pool, httpinput);
	}
}

static char *set_url(char *arg)
{
	struct pool *pool = add_url();

	setup_url(pool, arg);
	return NULL;
}

static char *set_quota(char *arg)
{
	char *semicolon = strchr(arg, ';'), *url;
	int len, qlen, quota;
	struct pool *pool;

	if (!semicolon)
		return "No semicolon separated quota;URL pair found";
	len = strlen(arg);
	*semicolon = '\0';
	qlen = strlen(arg);
	if (!qlen)
		return "No parameter for quota found";
	len -= qlen + 1;
	if (len < 1)
		return "No parameter for URL found";
	quota = atoi(arg);
	if (quota < 0)
		return "Invalid negative parameter for quota set";
	url = arg + qlen + 1;
	pool = add_url();
	setup_url(pool, url);
	pool->quota = quota;
	applog(LOG_INFO, "Setting pool %d to quota %d", pool->pool_no, pool->quota);
	adjust_quota_gcd();

	return NULL;
}

static char *set_user(const char *arg)
{
	struct pool *pool;

	if (total_userpasses)
		return "Use only user + pass or userpass, but not both";
	total_users++;
	if (total_users > total_pools)
		add_pool();

	pool = pools[total_users - 1];
	opt_set_charp(arg, &pool->rpc_user);

	return NULL;
}

static char *set_pass(const char *arg)
{
	struct pool *pool;

	if (total_userpasses)
		return "Use only user + pass or userpass, but not both";
	total_passes++;
	if (total_passes > total_pools)
		add_pool();

	pool = pools[total_passes - 1];
	opt_set_charp(arg, &pool->rpc_pass);

	return NULL;
}

static char *set_userpass(const char *arg)
{
	struct pool *pool;
	char *updup;

	if (total_users || total_passes)
		return "Use only user + pass or userpass, but not both";
	total_userpasses++;
	if (total_userpasses > total_pools)
		add_pool();

	pool = pools[total_userpasses - 1];
	updup = strdup(arg);
	opt_set_charp(arg, &pool->rpc_userpass);
	pool->rpc_user = strtok(updup, ":");
	if (!pool->rpc_user)
		return "Failed to find : delimited user info";
	pool->rpc_pass = strtok(NULL, ":");
	if (!pool->rpc_pass)
		pool->rpc_pass = strdup("");

	return NULL;
}

static char *enable_debug(bool *flag)
{
	*flag = true;
	/* Turn on verbose output, too. */
	opt_log_output = true;
	return NULL;
}

static char *opt_set_sched_start;
static char *opt_set_sched_stop;

static char *set_schedtime(const char *arg, struct schedtime *st)
{
	if (sscanf(arg, "%d:%d", &st->tm.tm_hour, &st->tm.tm_min) != 2)
		return "Invalid time set, should be HH:MM";
	if (st->tm.tm_hour > 23 || st->tm.tm_min > 59 || st->tm.tm_hour < 0 || st->tm.tm_min < 0)
		return "Invalid time set.";
	st->enable = true;
	return NULL;
}

static char *set_sched_start(const char *arg)
{
	return set_schedtime(arg, &schedstart);
}

static char *set_sched_stop(const char *arg)
{
	return set_schedtime(arg, &schedstop);
}

static char *opt_set_sharelog;
static char* set_sharelog(char *arg)
{
	char *r = "";
	long int i = strtol(arg, &r, 10);

	if ((!*r) && i >= 0 && i <= INT_MAX) {
		sharelog_file = fdopen((int)i, "a");
		if (!sharelog_file)
			applog(LOG_ERR, "Failed to open fd %u for share log", (unsigned int)i);
	} else if (!strcmp(arg, "-")) {
		sharelog_file = stdout;
		if (!sharelog_file)
			applog(LOG_ERR, "Standard output missing for share log");
	} else {
		sharelog_file = fopen(arg, "a");
		if (!sharelog_file)
			applog(LOG_ERR, "Failed to open %s for share log", arg);
	}

	return NULL;
}

static char *temp_cutoff_str = NULL;
static char __maybe_unused *opt_set_temp_cutoff;

char *set_temp_cutoff(char *arg)
{
	int val;

	if (!(arg && arg[0]))
		return "Invalid parameters for set temp cutoff";
	val = atoi(arg);
	if (val < 0 || val > 200)
		return "Invalid value passed to set temp cutoff";
	temp_cutoff_str = arg;

	return NULL;
}

static void load_temp_cutoffs()
{
	int i, val = 0, device = 0;
	char *nextptr;

	if (temp_cutoff_str) {
		for (device = 0, nextptr = strtok(temp_cutoff_str, ","); nextptr; ++device, nextptr = strtok(NULL, ",")) {
			if (device >= total_devices)
				quit(1, "Too many values passed to set temp cutoff");
			val = atoi(nextptr);
			if (val < 0 || val > 200)
				quit(1, "Invalid value passed to set temp cutoff");

			rd_lock(&devices_lock);
			devices[device]->cutofftemp = val;
			rd_unlock(&devices_lock);
		}
	} else {
		rd_lock(&devices_lock);
		for (i = device; i < total_devices; ++i) {
			if (!devices[i]->cutofftemp)
				devices[i]->cutofftemp = opt_cutofftemp;
		}
		rd_unlock(&devices_lock);

		return;
	}
	if (device <= 1) {
		rd_lock(&devices_lock);
		for (i = device; i < total_devices; ++i)
			devices[i]->cutofftemp = val;
		rd_unlock(&devices_lock);
	}
}

static char *set_float_125_to_500(const char *arg, float *i)
{
	char *err = opt_set_floatval(arg, i);

	if (err)
		return err;

	if (*i < 125 || *i > 500)
		return "Value out of range";

	return NULL;
}

static char *set_float_100_to_250(const char *arg, float *i)
{
	char *err = opt_set_floatval(arg, i);

	if (err)
		return err;

	if (*i < 100 || *i > 250)
		return "Value out of range";

	return NULL;
}

static char *set_null(const char __maybe_unused *arg)
{
	return NULL;
}

/* These options are available from config file or commandline */
static struct opt_table opt_config_table[] = {
#ifdef USE_ICARUS
	OPT_WITH_ARG("--anu-freq",
		     set_float_125_to_500, &opt_show_floatval, &opt_anu_freq,
		     "Set AntminerU1/2 frequency in MHz, range 125-500"),
#endif
	OPT_WITH_ARG("--api-allow",
		     opt_set_charp, NULL, &opt_api_allow,
		     "Allow API access only to the given list of [G:]IP[/Prefix] addresses[/subnets]"),
	OPT_WITH_ARG("--api-description",
		     opt_set_charp, NULL, &opt_api_description,
		     "Description placed in the API status header, default: cgminer version"),
	OPT_WITH_ARG("--api-groups",
		     opt_set_charp, NULL, &opt_api_groups,
		     "API one letter groups G:cmd:cmd[,P:cmd:*...] defining the cmds a groups can use"),
	OPT_WITHOUT_ARG("--api-listen",
			opt_set_bool, &opt_api_listen,
			"Enable API, default: disabled"),
	OPT_WITHOUT_ARG("--api-mcast",
			opt_set_bool, &opt_api_mcast,
			"Enable API Multicast listener, default: disabled"),
	OPT_WITH_ARG("--api-mcast-addr",
		     opt_set_charp, NULL, &opt_api_mcast_addr,
		     "API Multicast listen address"),
	OPT_WITH_ARG("--api-mcast-code",
		     opt_set_charp, NULL, &opt_api_mcast_code,
		     "Code expected in the API Multicast message, don't use '-'"),
	OPT_WITH_ARG("--api-mcast-des",
		     opt_set_charp, NULL, &opt_api_mcast_des,
		     "Description appended to the API Multicast reply, default: ''"),
	OPT_WITH_ARG("--api-mcast-port",
		     set_int_1_to_65535, opt_show_intval, &opt_api_mcast_port,
		     "API Multicast listen port"),
	OPT_WITHOUT_ARG("--api-network",
			opt_set_bool, &opt_api_network,
			"Allow API (if enabled) to listen on/for any address, default: only 127.0.0.1"),
	OPT_WITH_ARG("--api-port",
		     set_int_1_to_65535, opt_show_intval, &opt_api_port,
		     "Port number of miner API"),
	OPT_WITH_ARG("--api-host",
		     opt_set_charp, NULL, &opt_api_host,
		     "Specify API listen address, default: 0.0.0.0"),
#ifdef USE_ICARUS
	OPT_WITH_ARG("--au3-freq",
		     set_float_100_to_250, &opt_show_floatval, &opt_au3_freq,
		     "Set AntminerU3 frequency in MHz, range 100-250"),
	OPT_WITH_ARG("--au3-volt",
		     set_int_0_to_9999, &opt_show_intval, &opt_au3_volt,
		     "Set AntminerU3 voltage in mv, range 725-850, 0 to not set"),
#endif
#ifdef USE_AVALON
	OPT_WITHOUT_ARG("--avalon-auto",
			opt_set_bool, &opt_avalon_auto,
			"Adjust avalon overclock frequency dynamically for best hashrate"),
	OPT_WITH_ARG("--avalon-cutoff",
		     set_int_0_to_100, opt_show_intval, &opt_avalon_overheat,
		     "Set avalon overheat cut off temperature"),
	OPT_WITH_CBARG("--avalon-fan",
		     set_avalon_fan, NULL, &opt_set_avalon_fan,
		     "Set fanspeed percentage for avalon, single value or range (default: 20-100)"),
	OPT_WITH_CBARG("--avalon-freq",
		     set_avalon_freq, NULL, &opt_set_avalon_freq,
		     "Set frequency range for avalon-auto, single value or range"),
	OPT_WITH_ARG("--avalon-options",
		     opt_set_charp, NULL, &opt_avalon_options,
		     "Set avalon options baud:miners:asic:timeout:freq:tech"),
	OPT_WITH_ARG("--avalon-temp",
		     set_int_0_to_100, opt_show_intval, &opt_avalon_temp,
		     "Set avalon target temperature"),
#endif
#ifdef USE_AVALON2
	OPT_WITH_CBARG("--avalon2-freq",
		     set_avalon2_freq, NULL, &opt_set_avalon2_freq,
		     "Set frequency range for Avalon2, single value or range, step: 25"),
	OPT_WITH_CBARG("--avalon2-voltage",
		     set_avalon2_voltage, NULL, &opt_set_avalon2_voltage,
		     "Set Avalon2 core voltage, in millivolts, step: 125"),
	OPT_WITH_CBARG("--avalon2-fan",
		     set_avalon2_fan, NULL, &opt_set_avalon2_fan,
		     "Set Avalon2 target fan speed"),
	OPT_WITH_ARG("--avalon2-cutoff",
		     set_int_0_to_100, opt_show_intval, &opt_avalon2_overheat,
		     "Set Avalon2 overheat cut off temperature"),
	OPT_WITHOUT_ARG("--avalon2-fixed-speed",
		     set_avalon2_fixed_speed, &opt_avalon2_fan_fixed,
		     "Set Avalon2 fan to fixed speed"),
	OPT_WITH_ARG("--avalon2-polling-delay",
		     set_int_1_to_65535, opt_show_intval, &opt_avalon2_polling_delay,
		     "Set Avalon2 polling delay value (ms)"),
#endif
#ifdef USE_AVALON4
	OPT_WITHOUT_ARG("--avalon4-automatic-voltage",
			opt_set_bool, &opt_avalon4_autov,
			"Automatic adjust voltage base on module DH"),
	OPT_WITH_CBARG("--avalon4-voltage",
		     set_avalon4_voltage, NULL, &opt_set_avalon4_voltage,
		     "Set Avalon4 core voltage, in millivolts, step: 125"),
	OPT_WITH_CBARG("--avalon4-freq",
		     set_avalon4_freq, NULL, &opt_set_avalon4_freq,
		     "Set frequency for Avalon4, 1 to 3 values, example: 445:385:370"),
	OPT_WITH_CBARG("--avalon4-fan",
		     set_avalon4_fan, NULL, &opt_set_avalon4_fan,
		     "Set Avalon4 target fan speed range"),
	OPT_WITH_ARG("--avalon4-temp",
		     set_int_22_to_55, opt_show_intval, &opt_avalon4_temp_target,
		     "Set Avalon4 target temperature"),
	OPT_WITH_ARG("--avalon4-cutoff",
		     set_int_42_to_65, opt_show_intval, &opt_avalon4_overheat,
		     "Set Avalon4 overheat cut off temperature"),
	OPT_WITH_ARG("--avalon4-polling-delay",
		     set_int_1_to_65535, opt_show_intval, &opt_avalon4_polling_delay,
		     "Set Avalon4 polling delay value (ms)"),
	OPT_WITH_ARG("--avalon4-ntime-offset",
		     opt_set_intval, opt_show_intval, &opt_avalon4_ntime_offset,
		     "Set Avalon4 MM ntime rolling max offset"),
	OPT_WITH_ARG("--avalon4-aucspeed",
		     opt_set_intval, opt_show_intval, &opt_avalon4_aucspeed,
		     "Set Avalon4 AUC IIC bus speed"),
	OPT_WITH_ARG("--avalon4-aucxdelay",
		     opt_set_intval, opt_show_intval, &opt_avalon4_aucxdelay,
		     "Set Avalon4 AUC IIC xfer read delay, 4800 ~= 1ms"),
#endif
#ifdef USE_BAB
	OPT_WITH_ARG("--bab-options",
		     opt_set_charp, NULL, &opt_bab_options,
		     "Set bab options max:def:min:up:down:hz:delay:trf"),
#endif
	OPT_WITHOUT_ARG("--balance",
		     set_balance, &pool_strategy,
		     "Change multipool strategy from failover to even share balance"),
	OPT_WITH_ARG("--benchfile",
			opt_set_charp, NULL, &opt_benchfile,
			"Run cgminer in benchmark mode using a work file - produces no shares"),
	OPT_WITHOUT_ARG("--benchfile-display",
			opt_set_bool, &opt_benchfile_display,
			"Display each benchfile nonce found"),
	OPT_WITHOUT_ARG("--benchmark",
			opt_set_bool, &opt_benchmark,
			"Run cgminer in benchmark mode - produces no shares"),
#if defined(USE_BITFORCE)
	OPT_WITHOUT_ARG("--bfl-range",
			opt_set_bool, &opt_bfl_noncerange,
			"Use nonce range on bitforce devices if supported"),
#endif
#ifdef USE_BFLSC
	OPT_WITH_ARG("--bflsc-overheat",
		     set_int_0_to_200, opt_show_intval, &opt_bflsc_overheat,
		     "Set overheat temperature where BFLSC devices throttle, 0 to disable"),
#endif
#ifdef USE_AVALON
	OPT_WITH_ARG("--bitburner-voltage",
		     opt_set_intval, NULL, &opt_bitburner_core_voltage,
		     "Set BitBurner (Avalon) core voltage, in millivolts"),
	OPT_WITH_ARG("--bitburner-fury-voltage",
		     opt_set_intval, NULL, &opt_bitburner_fury_core_voltage,
		     "Set BitBurner Fury core voltage, in millivolts"),
	OPT_WITH_ARG("--bitburner-fury-options",
		     opt_set_charp, NULL, &opt_bitburner_fury_options,
		     "Override avalon-options for BitBurner Fury boards baud:miners:asic:timeout:freq"),
#endif
#if defined(USE_ANT_S1) || defined(USE_ANT_S2)
	OPT_WITHOUT_ARG("--bitmain-auto",
			opt_set_bool, &opt_bitmain_auto,
			"Adjust bitmain overclock frequency dynamically for best hashrate"),
	OPT_WITH_ARG("--bitmain-cutoff",
		     set_int_0_to_100, opt_show_intval, &opt_bitmain_overheat,
		     "Set bitmain overheat cut off temperature"),
	OPT_WITH_CBARG("--bitmain-fan",
		     set_bitmain_fan, NULL, &opt_set_bitmain_fan,
		     "Set fanspeed percentage for bitmain, single value or range (default: 20-100)"),
	OPT_WITH_CBARG("--bitmain-freq",
		     set_bitmain_freq, NULL, &opt_set_bitmain_freq,
		     "Set frequency range for bitmain-auto, single value or range"),
	OPT_WITHOUT_ARG("--bitmain-hwerror",
			opt_set_bool, &opt_bitmain_hwerror,
			"Set bitmain device detect hardware error"),
	OPT_WITH_ARG("--bitmain-options",
		     opt_set_charp, NULL, &opt_bitmain_options,
		     "Set bitmain options baud:miners:asic:timeout:freq"),
	OPT_WITH_ARG("--bitmain-temp",
		     set_int_0_to_100, opt_show_intval, &opt_bitmain_temp,
		     "Set bitmain target temperature"),
#endif
#ifdef USE_ANT_S2
	OPT_WITH_ARG("--bitmain-dev",
		     opt_set_charp, NULL, &opt_bitmain_dev,
		     "Set bitmain device"),
	OPT_WITHOUT_ARG("--bitmainbeeper",
			opt_set_bool, &opt_bitmain_beeper,
			"Set bitmain beeper ringing"),
	OPT_WITHOUT_ARG("--bitmain-checkall",
			opt_set_bool, &opt_bitmain_checkall,
			opt_hidden),
	OPT_WITHOUT_ARG("--bitmain-checkn2diff",
			opt_set_bool, &opt_bitmain_checkn2diff,
			opt_hidden),
	OPT_WITHOUT_ARG("--bitmaintempoverctrl",
			opt_set_bool, &opt_bitmain_tempoverctrl,
			"Set bitmain stop runing when temprerature is over 80 degree Celsius"),
#endif
#ifdef USE_BITMINE_A1
	OPT_WITH_ARG("--bitmine-a1-options",
		     opt_set_charp, NULL, &opt_bitmine_a1_options,
		     "Bitmine A1 options ref_clk_khz:sys_clk_khz:spi_clk_khz:override_chip_num"),
#endif
#ifdef USE_BITFURY
	OPT_WITH_ARG("--bxf-bits",
		     set_int_32_to_63, opt_show_intval, &opt_bxf_bits,
		     "Set max BXF/HXF bits for overclocking"),
	OPT_WITH_ARG("--bxf-debug",
		     set_int_0_to_4, opt_show_intval, &opt_bxf_debug,
		    "BXF: Debug all USB I/O, > is to the board(s), < is from the board(s)"),
	OPT_WITH_ARG("--bxf-temp-target",
		     set_int_0_to_200, opt_show_intval, &opt_bxf_temp_target,
		     "Set target temperature for BXF/HXF devices"),
	OPT_WITH_ARG("--bxm-bits",
		     set_int_0_to_100, opt_show_intval, &opt_bxm_bits,
		     "Set BXM bits for overclocking"),
#endif
#ifdef USE_BLOCKERUPTER
        OPT_WITH_ARG("--bet-clk",
                     opt_set_intval, opt_show_intval, &opt_bet_clk,
                     "Set Block Erupter clock"),
#endif
#ifdef HAVE_LIBCURL
	OPT_WITH_ARG("--btc-address",
		     opt_set_charp, NULL, &opt_btc_address,
		     "Set bitcoin target address when solo mining to bitcoind (mandatory)"),
	OPT_WITH_ARG("--btc-sig",
		     opt_set_charp, NULL, &opt_btc_sig,
		     "Set signature to add to coinbase when solo mining (optional)"),
#endif
#ifdef HAVE_CURSES
	OPT_WITHOUT_ARG("--compact",
			opt_set_bool, &opt_compact,
			"Use compact display without per device statistics"),
#endif
#ifdef USE_COINTERRA
	OPT_WITH_ARG("--cta-load",
		set_int_0_to_255, opt_show_intval, &opt_cta_load,
		"Set load for CTA devices, 0-255 range"),
	OPT_WITH_ARG("--ps-load",
		set_int_0_to_100, opt_show_intval, &opt_ps_load,
		"Set power supply load for CTA devices, 0-100 range"),
#endif
	OPT_WITHOUT_ARG("--debug|-D",
		     enable_debug, &opt_debug,
		     "Enable debug output"),
	OPT_WITHOUT_ARG("--disable-rejecting",
			opt_set_bool, &opt_disable_pool,
			"Automatically disable pools that continually reject shares"),
#ifdef USE_DRILLBIT
        OPT_WITH_ARG("--drillbit-options",
		     opt_set_charp, NULL, &opt_drillbit_options,
		     "Set drillbit options <int|ext>:clock[:clock_divider][:voltage]"),
        OPT_WITH_ARG("--drillbit-auto",
		     opt_set_charp, NULL, &opt_drillbit_auto,
		     "Enable drillbit automatic tuning <every>:[<gooderr>:<baderr>:<maxerr>]"),
#endif
	OPT_WITH_ARG("--expiry|-E",
		     set_int_0_to_9999, opt_show_intval, &opt_expiry,
		     "Upper bound on how many seconds after getting work we consider a share from it stale"),
	OPT_WITHOUT_ARG("--failover-only",
			opt_set_bool, &opt_fail_only,
			"Don't leak work to backup pools when primary pool is lagging"),
	OPT_WITHOUT_ARG("--fix-protocol",
			opt_set_bool, &opt_fix_protocol,
			"Do not redirect to a different getwork protocol (eg. stratum)"),
#ifdef USE_HASHFAST
	OPT_WITHOUT_ARG("--hfa-dfu-boot",
			opt_set_bool, &opt_hfa_dfu_boot,
			opt_hidden),
	OPT_WITH_ARG("--hfa-hash-clock",
		     set_int_0_to_9999, opt_show_intval, &opt_hfa_hash_clock,
		     "Set hashfast clock speed"),
	OPT_WITH_ARG("--hfa-fail-drop",
		     set_int_0_to_100, opt_show_intval, &opt_hfa_fail_drop,
		     "Set how many MHz to drop clockspeed each failure on an overlocked hashfast device"),
	OPT_WITH_CBARG("--hfa-fan",
		     set_hfa_fan, NULL, &opt_set_hfa_fan,
		     "Set fanspeed percentage for hashfast, single value or range (default: 10-85)"),
	OPT_WITH_ARG("--hfa-name",
		     opt_set_charp, NULL, &opt_hfa_name,
		     "Set a unique name for a single hashfast device specified with --usb or the first device found"),
	OPT_WITHOUT_ARG("--hfa-noshed",
			opt_set_bool, &opt_hfa_noshed,
			"Disable hashfast dynamic core disabling feature"),
	OPT_WITH_ARG("--hfa-ntime-roll",
		     opt_set_intval, NULL, &opt_hfa_ntime_roll,
		     opt_hidden),
	OPT_WITH_ARG("--hfa-options",
		     opt_set_charp, NULL, &opt_hfa_options,
		     "Set hashfast options name:clock (comma separated)"),
	OPT_WITHOUT_ARG("--hfa-pll-bypass",
			opt_set_bool, &opt_hfa_pll_bypass,
			opt_hidden),
	OPT_WITH_ARG("--hfa-temp-overheat",
		     set_int_0_to_200, opt_show_intval, &opt_hfa_overheat,
		     "Set the hashfast overheat throttling temperature"),
	OPT_WITH_ARG("--hfa-temp-target",
		     set_int_0_to_200, opt_show_intval, &opt_hfa_target,
		     "Set the hashfast target temperature (0 to disable)"),
#endif
#ifdef USE_HASHRATIO
	OPT_WITH_CBARG("--hro-freq",
		       set_hashratio_freq, NULL, &opt_hashratio_freq,
		       "Set the hashratio clock frequency"),
#endif
	OPT_WITH_ARG("--hotplug",
		     set_int_0_to_9999, NULL, &hotplug_time,
#ifdef USE_USBUTILS
		     "Seconds between hotplug checks (0 means never check)"
#else
		     opt_hidden
#endif
		    ),
#ifdef USE_ICARUS
	OPT_WITH_ARG("--icarus-options",
		     opt_set_charp, NULL, &opt_icarus_options,
		     opt_hidden),
	OPT_WITH_ARG("--icarus-timing",
		     opt_set_charp, NULL, &opt_icarus_timing,
		     opt_hidden),
#endif
#if defined(HAVE_MODMINER)
	OPT_WITH_ARG("--kernel-path|-K",
		     opt_set_charp, opt_show_charp, &opt_kernel_path,
	             "Specify a path to where bitstream files are"),
#endif
#ifdef USE_KLONDIKE
	OPT_WITH_ARG("--klondike-options",
		     opt_set_charp, NULL, &opt_klondike_options,
		     "Set klondike options clock:temptarget"),
#endif
	OPT_WITHOUT_ARG("--load-balance",
		     set_loadbalance, &pool_strategy,
		     "Change multipool strategy from failover to quota based balance"),
	OPT_WITH_ARG("--log|-l",
		     set_int_0_to_9999, opt_show_intval, &opt_log_interval,
		     "Interval in seconds between log output"),
	OPT_WITHOUT_ARG("--lowmem",
			opt_set_bool, &opt_lowmem,
			"Minimise caching of shares for low memory applications"),
#ifdef USE_MINION
	OPT_WITH_ARG("--minion-chipreport",
		     set_int_0_to_100, opt_show_intval, &opt_minion_chipreport,
		     "Seconds to report chip 5min hashrate, range 0-100 (default: 0=disabled)"),
	OPT_WITH_ARG("--minion-cores",
		     opt_set_charp, NULL, &opt_minion_cores,
		     opt_hidden),
	OPT_WITHOUT_ARG("--minion-extra",
		     opt_set_bool, &opt_minion_extra,
		     opt_hidden),
	OPT_WITH_ARG("--minion-freq",
		     opt_set_charp, NULL, &opt_minion_freq,
		     "Set minion chip frequencies in MHz, single value or comma list, range 100-1400 (default: 1200)"),
	OPT_WITH_ARG("--minion-freqchange",
		     set_int_0_to_9999, opt_show_intval, &opt_minion_freqchange,
		     "Millisecond total time to do frequency changes (default: 1000)"),
	OPT_WITH_ARG("--minion-freqpercent",
		     set_int_0_to_100, opt_show_intval, &opt_minion_freqpercent,
		     "Percentage to use when starting up a chip (default: 70%)"),
	OPT_WITHOUT_ARG("--minion-idlecount",
		     opt_set_bool, &opt_minion_idlecount,
		     "Report when IdleCount is >0 or changes"),
	OPT_WITH_ARG("--minion-ledcount",
		     set_int_0_to_100, opt_show_intval, &opt_minion_ledcount,
		     "Turn off led when more than this many chips below the ledlimit (default: 0)"),
	OPT_WITH_ARG("--minion-ledlimit",
		     set_int_0_to_200, opt_show_intval, &opt_minion_ledlimit,
		     "Turn off led when chips GHs are below this (default: 90)"),
	OPT_WITHOUT_ARG("--minion-noautofreq",
		     opt_set_bool, &opt_minion_noautofreq,
		     "Disable automatic frequency adjustment"),
	OPT_WITHOUT_ARG("--minion-overheat",
		     opt_set_bool, &opt_minion_overheat,
		     "Enable directly halting any chip when the status exceeds 100C"),
	OPT_WITH_ARG("--minion-spidelay",
		     set_int_0_to_9999, opt_show_intval, &opt_minion_spidelay,
		     "Add a delay in microseconds after each SPI I/O"),
	OPT_WITH_ARG("--minion-spireset",
		     opt_set_charp, NULL, &opt_minion_spireset,
		     "SPI regular reset: iNNN for I/O count or sNNN for seconds - 0 means none"),
	OPT_WITH_ARG("--minion-spisleep",
		     set_int_0_to_9999, opt_show_intval, &opt_minion_spisleep,
		     "Sleep time in milliseconds when doing an SPI reset"),
	OPT_WITH_ARG("--minion-spiusec",
		     set_int_0_to_9999, NULL, &opt_minion_spiusec,
		     opt_hidden),
	OPT_WITH_ARG("--minion-temp",
		     opt_set_charp, NULL, &opt_minion_temp,
		     "Set minion chip temperature threshold, single value or comma list, range 120-160 (default: 135C)"),
#endif
#if defined(unix) || defined(__APPLE__)
	OPT_WITH_ARG("--monitor|-m",
		     opt_set_charp, NULL, &opt_stderr_cmd,
		     "Use custom pipe cmd for output messages"),
#endif // defined(unix)
#ifdef USE_BITFURY
	OPT_WITH_ARG("--nfu-bits",
		     set_int_32_to_63, opt_show_intval, &opt_nfu_bits,
		     "Set nanofury bits for overclocking, range 32-63"),
#endif
	OPT_WITHOUT_ARG("--net-delay",
			opt_set_bool, &opt_delaynet,
			"Impose small delays in networking to not overload slow routers"),
	OPT_WITHOUT_ARG("--no-pool-disable",
			opt_set_invbool, &opt_disable_pool,
			opt_hidden),
	OPT_WITHOUT_ARG("--no-submit-stale",
			opt_set_invbool, &opt_submit_stale,
		        "Don't submit shares if they are detected as stale"),
#ifdef USE_BITFURY
	OPT_WITH_ARG("--osm-led-mode",
		     set_int_0_to_4, opt_show_intval, &opt_osm_led_mode,
		     "Set LED mode for OneStringMiner devices"),
#endif
	OPT_WITH_ARG("--pass|-p",
		     set_pass, NULL, &opt_set_null,
		     "Password for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--per-device-stats",
			opt_set_bool, &want_per_device_stats,
			"Force verbose mode and output per-device statistics"),
	OPT_WITH_ARG("--pools",
			opt_set_bool, NULL, &opt_set_null, opt_hidden),
	OPT_WITHOUT_ARG("--protocol-dump|-P",
			opt_set_bool, &opt_protocol,
			"Verbose dump of protocol-level activities"),
	OPT_WITH_ARG("--queue|-Q",
		     set_int_0_to_9999, opt_show_intval, &opt_queue,
		     "Maximum number of work items to have queued"),
	OPT_WITHOUT_ARG("--quiet|-q",
			opt_set_bool, &opt_quiet,
			"Disable logging output, display status and errors"),
	OPT_WITH_ARG("--quota|-U",
		     set_quota, NULL, &opt_set_null,
		     "quota;URL combination for server with load-balance strategy quotas"),
	OPT_WITHOUT_ARG("--real-quiet",
			opt_set_bool, &opt_realquiet,
			"Disable all output"),
	OPT_WITH_ARG("--retries",
		     set_null, NULL, &opt_set_null,
		     opt_hidden),
	OPT_WITH_ARG("--retry-pause",
		     set_null, NULL, &opt_set_null,
		     opt_hidden),
#ifdef USE_ICARUS
	OPT_WITH_ARG("--rock-freq",
		     set_float_125_to_500, &opt_show_floatval, &opt_rock_freq,
		     "Set RockMiner frequency in MHz, range 125-500"),
#endif
	OPT_WITH_ARG("--rotate",
		     set_rotate, NULL, &opt_set_null,
		     "Change multipool strategy from failover to regularly rotate at N minutes"),
	OPT_WITHOUT_ARG("--round-robin",
		     set_rr, &pool_strategy,
		     "Change multipool strategy from failover to round robin on failure"),
#ifdef USE_FPGA_SERIAL
	OPT_WITH_CBARG("--scan-serial|-S",
		     add_serial, NULL, &opt_add_serial,
		     "Serial port to probe for Serial FPGA Mining device"),
#endif
	OPT_WITH_ARG("--scan-time|-s",
		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
		     "Upper bound on time spent scanning current work, in seconds"),
	OPT_WITH_CBARG("--sched-start",
		     set_sched_start, NULL, &opt_set_sched_start,
		     "Set a time of day in HH:MM to start mining (a once off without a stop time)"),
	OPT_WITH_CBARG("--sched-stop",
		     set_sched_stop, NULL, &opt_set_sched_stop,
		     "Set a time of day in HH:MM to stop mining (will quit without a start time)"),
	OPT_WITH_CBARG("--sharelog",
		     set_sharelog, NULL, &opt_set_sharelog,
		     "Append share log to file"),
	OPT_WITH_ARG("--shares",
		     opt_set_intval, NULL, &opt_shares,
		     "Quit after mining N shares (default: unlimited)"),
	OPT_WITH_ARG("--socks-proxy",
		     opt_set_charp, NULL, &opt_socks_proxy,
		     "Set socks4 proxy (host:port)"),
	OPT_WITH_ARG("--suggest-diff",
		     opt_set_intval, NULL, &opt_suggest_diff,
		     "Suggest miner difficulty for pool to user (default: none)"),
#ifdef HAVE_SYSLOG_H
	OPT_WITHOUT_ARG("--syslog",
			opt_set_bool, &use_syslog,
			"Use system log for output messages (default: standard error)"),
#endif
#if defined(USE_BITFORCE) || defined(USE_MODMINER) || defined(USE_BFLSC)
	OPT_WITH_CBARG("--temp-cutoff",
		     set_temp_cutoff, opt_show_intval, &opt_set_temp_cutoff,
		     "Temperature where a device will be automatically disabled, one value or comma separated list"),
#endif
	OPT_WITHOUT_ARG("--text-only|-T",
			opt_set_invbool, &use_curses,
#ifdef HAVE_CURSES
			"Disable ncurses formatted screen output"
#else
			opt_hidden
#endif
	),
	OPT_WITH_ARG("--url|-o",
		     set_url, NULL, &opt_set_null,
		     "URL for bitcoin JSON-RPC server"),
#ifdef USE_USBUTILS
	OPT_WITH_ARG("--usb",
		     opt_set_charp, NULL, &opt_usb_select,
		     "USB device selection"),
	OPT_WITH_ARG("--usb-dump",
		     set_int_0_to_10, opt_show_intval, &opt_usbdump,
		     opt_hidden),
	OPT_WITHOUT_ARG("--usb-list-all",
			opt_set_bool, &opt_usb_list_all,
			opt_hidden),
#endif
	OPT_WITH_ARG("--user|-u",
		     set_user, NULL, &opt_set_null,
		     "Username for bitcoin JSON-RPC server"),
	OPT_WITH_ARG("--userpass|-O",
		     set_userpass, NULL, &opt_set_null,
		     "Username:Password pair for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--verbose",
			opt_set_bool, &opt_log_output,
			"Log verbose output to stderr as well as status output"),
	OPT_WITHOUT_ARG("--widescreen",
			opt_set_bool, &opt_widescreen,
			"Use extra wide display without toggling"),
	OPT_WITHOUT_ARG("--worktime",
			opt_set_bool, &opt_worktime,
			"Display extra work time debug information"),
	OPT_ENDTABLE
};

static char *load_config(const char *arg, void __maybe_unused *unused);

static int fileconf_load;

static char *parse_config(json_t *config, bool fileconf)
{
	static char err_buf[200];
	struct opt_table *opt;
	const char *str;
	json_t *val;

	if (fileconf && !fileconf_load)
		fileconf_load = 1;

	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name;

		/* We don't handle subtables. */
		assert(!(opt->type & OPT_SUBTABLE));

		if (!opt->names)
			continue;

		/* Pull apart the option name(s). */
		name = strdup(opt->names);
		for (p = strtok(name, "|"); p; p = strtok(NULL, "|")) {
			char *err = NULL;

			/* Ignore short options. */
			if (p[1] != '-')
				continue;

			val = json_object_get(config, p+2);
			if (!val)
				continue;

			if ((opt->type & (OPT_HASARG | OPT_PROCESSARG)) && json_is_string(val)) {
				str = json_string_value(val);
				err = opt->cb_arg(str, opt->u.arg);
				if (opt->type == OPT_PROCESSARG)
					opt_set_charp(str, opt->u.arg);
			} else if ((opt->type & (OPT_HASARG | OPT_PROCESSARG)) && json_is_array(val)) {
				json_t *arr_val;
				size_t index;

				json_array_foreach(val, index, arr_val) {
					if (json_is_string(arr_val)) {
						str = json_string_value(arr_val);
						err = opt->cb_arg(str, opt->u.arg);
						if (opt->type == OPT_PROCESSARG)
							opt_set_charp(str, opt->u.arg);
					} else if (json_is_object(arr_val))
						err = parse_config(arr_val, false);
					if (err)
						break;
				}
			} else if ((opt->type & OPT_NOARG) && json_is_true(val))
				err = opt->cb(opt->u.arg);
			else
				err = "Invalid value";

			if (err) {
				/* Allow invalid values to be in configuration
				 * file, just skipping over them provided the
				 * JSON is still valid after that. */
				if (fileconf) {
					applog(LOG_ERR, "Invalid config option %s: %s", p, err);
					fileconf_load = -1;
				} else {
					snprintf(err_buf, sizeof(err_buf), "Parsing JSON option %s: %s",
						p, err);
					return err_buf;
				}
			}
		}
		free(name);
	}

	val = json_object_get(config, JSON_INCLUDE_CONF);
	if (val && json_is_string(val))
		return load_config(json_string_value(val), NULL);

	return NULL;
}

char *cnfbuf = NULL;

#ifdef HAVE_LIBCURL
char conf_web1[] = "http://";
char conf_web2[] = "https://";

static char *load_web_config(const char *arg)
{
	json_t *val = json_web_config(arg);

	if (!val || !json_is_object(val))
		return JSON_WEB_ERROR;

	if (!cnfbuf)
		cnfbuf = strdup(arg);

	config_loaded = true;

	return parse_config(val, true);
}
#endif

static char *load_config(const char *arg, void __maybe_unused *unused)
{
	json_error_t err;
	json_t *config;
	char *json_error;
	size_t siz;

#ifdef HAVE_LIBCURL
	if (strncasecmp(arg, conf_web1, sizeof(conf_web1)-1) == 0 ||
	    strncasecmp(arg, conf_web2, sizeof(conf_web2)-1) == 0)
		return load_web_config(arg);
#endif

	if (!cnfbuf)
		cnfbuf = strdup(arg);

	if (++include_count > JSON_MAX_DEPTH)
		return JSON_MAX_DEPTH_ERR;

	config = json_load_file(arg, 0, &err);
	if (!json_is_object(config)) {
		siz = JSON_LOAD_ERROR_LEN + strlen(arg) + strlen(err.text);
		json_error = malloc(siz);
		if (!json_error)
			quit(1, "Malloc failure in json error");

		snprintf(json_error, siz, JSON_LOAD_ERROR, arg, err.text);
		return json_error;
	}

	config_loaded = true;

	/* Parse the config now, so we can override it.  That can keep pointers
	 * so don't free config object. */
	return parse_config(config, true);
}

static char *set_default_config(const char *arg)
{
	opt_set_charp(arg, &default_config);

	return NULL;
}

void default_save_file(char *filename);

static void load_default_config(void)
{
	cnfbuf = malloc(PATH_MAX);

	default_save_file(cnfbuf);

	if (!access(cnfbuf, R_OK))
		load_config(cnfbuf, NULL);
	else {
		free(cnfbuf);
		cnfbuf = NULL;
	}
}

extern const char *opt_argv0;

static char *opt_verusage_and_exit(const char *extra)
{
	printf("%s\nBuilt with "
#ifdef USE_ANT_S1
		"ant.S1 "
#endif
#ifdef USE_ANT_S2
		"ant.S2 "
#endif
#ifdef USE_AVALON
		"avalon "
#endif
#ifdef USE_AVALON2
		"avalon2 "
#endif
#ifdef USE_AVALON4
		"avalon4 "
#endif
#ifdef USE_BFLSC
		"bflsc "
#endif
#ifdef USE_BITFORCE
		"bitforce "
#endif
#ifdef USE_BITFURY
		"bitfury "
#endif
#ifdef USE_COINTERRA
		"cointerra "
#endif
#ifdef USE_DRILLBIT
                "drillbit "
#endif
#ifdef USE_HASHFAST
		"hashfast "
#endif
#ifdef USE_ICARUS
		"icarus "
#endif
#ifdef USE_KLONDIKE
		"klondike "
#endif
#ifdef USE_KNC
		"KnC "
#endif
#ifdef USE_BAB
		"BaB "
#endif
#ifdef USE_MINION
		"minion "
#endif
#ifdef USE_MODMINER
		"modminer "
#endif
#ifdef USE_BITMINE_A1
		"Bitmine.A1 "
#endif
#ifdef USE_SP10
		"spondoolies "
#endif
#ifdef USE_SP30
        "sp30 "
#endif

		"mining support.\n"
		, packagename);
	printf("%s", opt_usage(opt_argv0, extra));
	fflush(stdout);
	exit(0);
}

#if defined(USE_USBUTILS)
char *display_devs(int *ndevs)
{
	*ndevs = 0;
	usb_all(0);
	exit(*ndevs);
}
#endif

/* These options are available from commandline only */
static struct opt_table opt_cmdline_table[] = {
	OPT_WITH_ARG("--config|-c",
		     load_config, NULL, &opt_set_null,
		     "Load a JSON-format configuration file\n"
		     "See example.conf for an example configuration."),
	OPT_WITH_ARG("--default-config",
		     set_default_config, NULL, &opt_set_null,
		     "Specify the filename of the default config file\n"
		     "Loaded at start and used when saving without a name."),
	OPT_WITHOUT_ARG("--help|-h",
			opt_verusage_and_exit, NULL,
			"Print this message"),
#if defined(USE_USBUTILS)
	OPT_WITHOUT_ARG("--ndevs|-n",
			display_devs, &nDevs,
			"Display all USB devices and exit"),
#endif
	OPT_WITHOUT_ARG("--version|-V",
			opt_version_and_exit, packagename,
			"Display version and exit"),
	OPT_ENDTABLE
};

#ifdef HAVE_LIBCURL
static bool jobj_binary(const json_t *obj, const char *key,
			void *buf, size_t buflen, bool required)
{
	const char *hexstr;
	json_t *tmp;

	tmp = json_object_get(obj, key);
	if (unlikely(!tmp)) {
		if (unlikely(required))
			applog(LOG_ERR, "JSON key '%s' not found", key);
		return false;
	}
	hexstr = json_string_value(tmp);
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "JSON key '%s' is not a string", key);
		return false;
	}
	if (!hex2bin(buf, hexstr, buflen))
		return false;

	return true;
}
#endif

static void calc_midstate(struct work *work)
{
	unsigned char data[64];
	uint32_t *data32 = (uint32_t *)data;
	sha256_ctx ctx;

	flip64(data32, work->data);
	sha256_init(&ctx);
	sha256_update(&ctx, data, 64);
	memcpy(work->midstate, ctx.h, 32);
	endian_flip32(work->midstate, work->midstate);
}

/* Returns the current value of total_work and increments it */
static int total_work_inc(void)
{
	int ret;

	cg_wlock(&control_lock);
	ret = total_work++;
	cg_wunlock(&control_lock);

	return ret;
}

static struct work *make_work(void)
{
	struct work *work = calloc(1, sizeof(struct work));

	if (unlikely(!work))
		quit(1, "Failed to calloc work in make_work");

	work->id = total_work_inc();

	return work;
}

/* This is the central place all work that is about to be retired should be
 * cleaned to remove any dynamically allocated arrays within the struct */
void clean_work(struct work *work)
{
	free(work->job_id);
	free(work->ntime);
	free(work->coinbase);
	free(work->nonce1);
	memset(work, 0, sizeof(struct work));
}

/* All dynamically allocated work structs should be freed here to not leak any
 * ram from arrays allocated within the work struct */
void _free_work(struct work *work)
{
	clean_work(work);
	free(work);
}

static void gen_hash(unsigned char *data, unsigned char *hash, int len);
static void calc_diff(struct work *work, double known);
char *workpadding = "000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000";

#ifdef HAVE_LIBCURL
/* Process transactions with GBT by storing the binary value of the first
 * transaction, and the hashes of the remaining transactions since these
 * remain constant with an altered coinbase when generating work. Must be
 * entered under gbt_lock */
static void gbt_merkle_bins(struct pool *pool, json_t *transaction_arr);

static void __build_gbt_txns(struct pool *pool, json_t *res_val)
{
	json_t *txn_array;

	txn_array = json_object_get(res_val, "transactions");
	gbt_merkle_bins(pool, txn_array);
}

static void __gbt_merkleroot(struct pool *pool, unsigned char *merkle_root)
{
	unsigned char merkle_sha[64];
	int i;

	gen_hash(pool->coinbase, merkle_root, pool->coinbase_len);
	memcpy(merkle_sha, merkle_root, 32);
	for (i = 0; i < pool->merkles; i++) {
		memcpy(merkle_sha + 32, pool->merklebin + i * 32, 32);
		gen_hash(merkle_sha, merkle_root, 64);
		memcpy(merkle_sha, merkle_root, 32);
	}
}

static bool work_decode(struct pool *pool, struct work *work, json_t *val);

static void update_gbt(struct pool *pool)
{
	int rolltime;
	json_t *val;
	CURL *curl;

	curl = curl_easy_init();
	if (unlikely(!curl))
		quit (1, "CURL initialisation failed in update_gbt");

	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass,
			    pool->rpc_req, true, false, &rolltime, pool, false);

	if (val) {
		struct work *work = make_work();
		bool rc = work_decode(pool, work, val);

		total_getworks++;
		pool->getwork_requested++;
		if (rc) {
			applog(LOG_DEBUG, "Successfully retrieved and updated GBT from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			if (pool == current_pool())
				opt_work_update = true;
		} else {
			applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher GBT from pool %u %s",
			       pool->pool_no, pool->rpc_url);
		}
		json_decref(val);
		free_work(work);
	} else {
		applog(LOG_DEBUG, "FAILED to update GBT from pool %u %s",
		       pool->pool_no, pool->rpc_url);
	}
	curl_easy_cleanup(curl);
}

static void gen_gbt_work(struct pool *pool, struct work *work)
{
	unsigned char merkleroot[32];
	struct timeval now;
	uint64_t nonce2le;

	cgtime(&now);
	if (now.tv_sec - pool->tv_lastwork.tv_sec > 60)
		update_gbt(pool);

	cg_wlock(&pool->gbt_lock);
	nonce2le = htole64(pool->nonce2);
	memcpy(pool->coinbase + pool->nonce2_offset, &nonce2le, pool->n2size);
	pool->nonce2++;
	cg_dwlock(&pool->gbt_lock);
	__gbt_merkleroot(pool, merkleroot);

	memcpy(work->data, &pool->gbt_version, 4);
	memcpy(work->data + 4, pool->previousblockhash, 32);
	memcpy(work->data + 4 + 32 + 32, &pool->curtime, 4);
	memcpy(work->data + 4 + 32 + 32 + 4, &pool->gbt_bits, 4);

	memcpy(work->target, pool->gbt_target, 32);

	work->coinbase = bin2hex(pool->coinbase, pool->coinbase_len);

	/* For encoding the block data on submission */
	work->gbt_txns = pool->gbt_txns + 1;

	if (pool->gbt_workid)
		work->job_id = strdup(pool->gbt_workid);
	cg_runlock(&pool->gbt_lock);

	flip32(work->data + 4 + 32, merkleroot);
	memset(work->data + 4 + 32 + 32 + 4 + 4, 0, 4); /* nonce */

	hex2bin(work->data + 4 + 32 + 32 + 4 + 4 + 4, workpadding, 48);

	if (opt_debug) {
		char *header = bin2hex(work->data, 128);

		applog(LOG_DEBUG, "Generated GBT header %s", header);
		applog(LOG_DEBUG, "Work coinbase %s", work->coinbase);
		free(header);
	}

	calc_midstate(work);
	local_work++;
	work->pool = pool;
	work->gbt = true;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_GBT;
	work->work_block = work_block;
	/* Nominally allow a driver to ntime roll 60 seconds */
	work->drv_rolllimit = 60;
	calc_diff(work, 0);
	cgtime(&work->tv_staged);
}

static bool gbt_decode(struct pool *pool, json_t *res_val)
{
	const char *previousblockhash;
	const char *target;
	const char *coinbasetxn;
	const char *longpollid;
	unsigned char hash_swap[32];
	int expires;
	int version;
	int curtime;
	bool submitold;
	const char *bits;
	const char *workid;
	int cbt_len, orig_len;
	uint8_t *extra_len;
	size_t cal_len;

	previousblockhash = json_string_value(json_object_get(res_val, "previousblockhash"));
	target = json_string_value(json_object_get(res_val, "target"));
	coinbasetxn = json_string_value(json_object_get(json_object_get(res_val, "coinbasetxn"), "data"));
	longpollid = json_string_value(json_object_get(res_val, "longpollid"));
	expires = json_integer_value(json_object_get(res_val, "expires"));
	version = json_integer_value(json_object_get(res_val, "version"));
	curtime = json_integer_value(json_object_get(res_val, "curtime"));
	submitold = json_is_true(json_object_get(res_val, "submitold"));
	bits = json_string_value(json_object_get(res_val, "bits"));
	workid = json_string_value(json_object_get(res_val, "workid"));

	if (!previousblockhash || !target || !coinbasetxn || !longpollid ||
	    !expires || !version || !curtime || !bits) {
		applog(LOG_ERR, "JSON failed to decode GBT");
		return false;
	}

	applog(LOG_DEBUG, "previousblockhash: %s", previousblockhash);
	applog(LOG_DEBUG, "target: %s", target);
	applog(LOG_DEBUG, "coinbasetxn: %s", coinbasetxn);
	applog(LOG_DEBUG, "longpollid: %s", longpollid);
	applog(LOG_DEBUG, "expires: %d", expires);
	applog(LOG_DEBUG, "version: %d", version);
	applog(LOG_DEBUG, "curtime: %d", curtime);
	applog(LOG_DEBUG, "submitold: %s", submitold ? "true" : "false");
	applog(LOG_DEBUG, "bits: %s", bits);
	if (workid)
		applog(LOG_DEBUG, "workid: %s", workid);

	cg_wlock(&pool->gbt_lock);
	free(pool->coinbasetxn);
	pool->coinbasetxn = strdup(coinbasetxn);
	cbt_len = strlen(pool->coinbasetxn) / 2;
	/* We add 8 bytes of extra data corresponding to nonce2 */
	pool->n2size = 8;
	pool->coinbase_len = cbt_len + pool->n2size;
	cal_len = pool->coinbase_len + 1;
	align_len(&cal_len);
	free(pool->coinbase);
	pool->coinbase = calloc(cal_len, 1);
	if (unlikely(!pool->coinbase))
		quit(1, "Failed to calloc pool coinbase in gbt_decode");
	hex2bin(pool->coinbase, pool->coinbasetxn, 42);
	extra_len = (uint8_t *)(pool->coinbase + 41);
	orig_len = *extra_len;
	hex2bin(pool->coinbase + 42, pool->coinbasetxn + 84, orig_len);
	*extra_len += pool->n2size;
	hex2bin(pool->coinbase + 42 + *extra_len, pool->coinbasetxn + 84 + (orig_len * 2),
		cbt_len - orig_len - 42);
	pool->nonce2_offset = orig_len + 42;

	free(pool->longpollid);
	pool->longpollid = strdup(longpollid);
	free(pool->gbt_workid);
	if (workid)
		pool->gbt_workid = strdup(workid);
	else
		pool->gbt_workid = NULL;

	hex2bin(hash_swap, previousblockhash, 32);
	swap256(pool->previousblockhash, hash_swap);

	hex2bin(hash_swap, target, 32);
	swab256(pool->gbt_target, hash_swap);

	pool->gbt_expires = expires;
	pool->gbt_version = htobe32(version);
	pool->curtime = htobe32(curtime);
	pool->submit_old = submitold;

	hex2bin((unsigned char *)&pool->gbt_bits, bits, 4);

	__build_gbt_txns(pool, res_val);
	cg_wunlock(&pool->gbt_lock);

	return true;
}

static bool getwork_decode(json_t *res_val, struct work *work)
{
	if (unlikely(!jobj_binary(res_val, "data", work->data, sizeof(work->data), true))) {
		applog(LOG_ERR, "JSON inval data");
		return false;
	}

	if (!jobj_binary(res_val, "midstate", work->midstate, sizeof(work->midstate), false)) {
		// Calculate it ourselves
		applog(LOG_DEBUG, "Calculating midstate locally");
		calc_midstate(work);
	}

	if (unlikely(!jobj_binary(res_val, "target", work->target, sizeof(work->target), true))) {
		applog(LOG_ERR, "JSON inval target");
		return false;
	}
	return true;
}

/* Returns whether the pool supports local work generation or not. */
static bool pool_localgen(struct pool *pool)
{
	return (pool->has_stratum || pool->has_gbt || pool->gbt_solo);
}

static void gbt_merkle_bins(struct pool *pool, json_t *transaction_arr)
{
	unsigned char *hashbin;
	json_t *arr_val;
	int i, j, binleft, binlen;

	free(pool->txn_data);
	pool->txn_data = NULL;
	pool->transactions = 0;
	pool->merkles = 0;
	pool->transactions = json_array_size(transaction_arr);
	binlen = pool->transactions * 32 + 32;
	hashbin = alloca(binlen + 32);
	memset(hashbin, 0, 32);
	binleft = binlen / 32;
	if (pool->transactions) {
		int len = 0, ofs = 0;
		const char *txn;

		for (i = 0; i < pool->transactions; i++) {
			arr_val = json_array_get(transaction_arr, i);
			txn = json_string_value(json_object_get(arr_val, "data"));
			if (!txn) {
				applog(LOG_ERR, "Pool %d json_string_value fail - cannot find transaction data",
					pool->pool_no);
				return;
			}
			len += strlen(txn);
		}

		pool->txn_data = malloc(len + 1);
		if (unlikely(!pool->txn_data))
			quit(1, "Failed to calloc txn_data in gbt_merkle_bins");
		pool->txn_data[len] = '\0';

		for (i = 0; i < pool->transactions; i++) {
			unsigned char binswap[32];
			const char *hash;

			arr_val = json_array_get(transaction_arr, i);
			hash = json_string_value(json_object_get(arr_val, "hash"));
			txn = json_string_value(json_object_get(arr_val, "data"));
			len = strlen(txn);
			memcpy(pool->txn_data + ofs, txn, len);
			ofs += len;
			if (!hash) {
				unsigned char *txn_bin;
				int txn_len;

				txn_len = len / 2;
				txn_bin = malloc(txn_len);
				if (!txn_bin)
					quit(1, "Failed to malloc txn_bin in gbt_merkle_bins");
				hex2bin(txn_bin, txn, txn_len);
				/* This is needed for pooled mining since only
				 * transaction data and not hashes are sent */
				gen_hash(txn_bin, hashbin + 32 + 32 * i, txn_len);
				continue;
			}
			if (!hex2bin(binswap, hash, 32)) {
				applog(LOG_ERR, "Failed to hex2bin hash in gbt_merkle_bins");
				return;
			}
			swab256(hashbin + 32 + 32 * i, binswap);
		}
	}
	if (binleft > 1) {
		while (42) {
			if (binleft == 1)
				break;
			memcpy(pool->merklebin + (pool->merkles * 32), hashbin + 32, 32);
			pool->merkles++;
			if (binleft % 2) {
				memcpy(hashbin + binlen, hashbin + binlen - 32, 32);
				binlen += 32;
				binleft++;
			}
			for (i = 32, j = 64; j < binlen; i += 32, j += 64) {
				gen_hash(hashbin + j, hashbin + i, 64);
			}
			binleft /= 2;
			binlen = binleft * 32;
		}
	}
	if (opt_debug) {
		char hashhex[68];

		for (i = 0; i < pool->merkles; i++) {
			__bin2hex(hashhex, pool->merklebin + i * 32, 32);
			applog(LOG_DEBUG, "MH%d %s",i, hashhex);
		}
	}
	applog(LOG_INFO, "Stored %d transactions from pool %d", pool->transactions,
		pool->pool_no);
}

static double diff_from_target(void *target);

static const char scriptsig_header[] = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff";
static unsigned char scriptsig_header_bin[41];

static bool gbt_solo_decode(struct pool *pool, json_t *res_val)
{
	json_t *transaction_arr, *coinbase_aux;
	const char *previousblockhash;
	unsigned char hash_swap[32];
	struct timeval now;
	const char *target;
	uint64_t coinbasevalue;
	const char *flags;
	const char *bits;
	char header[228];
	int ofs = 0, len;
	uint64_t *u64;
	uint32_t *u32;
	int version;
	int curtime;
	int height;

	previousblockhash = json_string_value(json_object_get(res_val, "previousblockhash"));
	target = json_string_value(json_object_get(res_val, "target"));
	transaction_arr = json_object_get(res_val, "transactions");
	version = json_integer_value(json_object_get(res_val, "version"));
	curtime = json_integer_value(json_object_get(res_val, "curtime"));
	bits = json_string_value(json_object_get(res_val, "bits"));
	height = json_integer_value(json_object_get(res_val, "height"));
	coinbasevalue = json_integer_value(json_object_get(res_val, "coinbasevalue"));
	coinbase_aux = json_object_get(res_val, "coinbaseaux");
	flags = json_string_value(json_object_get(coinbase_aux, "flags"));

	if (!previousblockhash || !target || !version || !curtime || !bits || !coinbase_aux || !flags) {
		applog(LOG_ERR, "Pool %d JSON failed to decode GBT", pool->pool_no);
		return false;
	}

	applog(LOG_DEBUG, "previousblockhash: %s", previousblockhash);
	applog(LOG_DEBUG, "target: %s", target);
	applog(LOG_DEBUG, "version: %d", version);
	applog(LOG_DEBUG, "curtime: %d", curtime);
	applog(LOG_DEBUG, "bits: %s", bits);
	applog(LOG_DEBUG, "height: %d", height);
	applog(LOG_DEBUG, "flags: %s", flags);

	cg_wlock(&pool->gbt_lock);
	hex2bin(hash_swap, previousblockhash, 32);
	swap256(pool->previousblockhash, hash_swap);
	__bin2hex(pool->prev_hash, pool->previousblockhash, 32);

	hex2bin(hash_swap, target, 32);
	swab256(pool->gbt_target, hash_swap);
	pool->sdiff = diff_from_target(pool->gbt_target);

	pool->gbt_version = htobe32(version);
	pool->curtime = htobe32(curtime);
	snprintf(pool->ntime, 9, "%08x", curtime);
	snprintf(pool->bbversion, 9, "%08x", version);
	snprintf(pool->nbit, 9, "%s", bits);
	pool->nValue = coinbasevalue;
	hex2bin((unsigned char *)&pool->gbt_bits, bits, 4);
	gbt_merkle_bins(pool, transaction_arr);
	pool->height = height;

	memset(pool->scriptsig_base, 0, 42);
	ofs++; // Leave room for template length

	/* Put block height at start of template. */
	ofs += ser_number(pool->scriptsig_base + ofs, height); // max 5

	/* Followed by flags */
	len = strlen(flags) / 2;
	pool->scriptsig_base[ofs++] = len;
	hex2bin(pool->scriptsig_base + ofs, flags, len);
	ofs += len;

	/* Followed by timestamp */
	cgtime(&now);
	pool->scriptsig_base[ofs++] = 0xfe; // Encode seconds as u32
	u32 = (uint32_t *)&pool->scriptsig_base[ofs];
	*u32 = htole32(now.tv_sec);
	ofs += 4; // sizeof uint32_t
	pool->scriptsig_base[ofs++] = 0xfe; // Encode usecs as u32
	u32 = (uint32_t *)&pool->scriptsig_base[ofs];
	*u32 = htole32(now.tv_usec);
	ofs += 4; // sizeof uint32_t

	memcpy(pool->scriptsig_base + ofs, "\x09\x63\x67\x6d\x69\x6e\x65\x72\x34\x32", 10);
	ofs += 10;

	/* Followed by extranonce size, fixed at 8 */
	pool->scriptsig_base[ofs++] = 8;
	pool->nonce2_offset = 41 + ofs;
	ofs += 8;

	if (opt_btc_sig) {
		len = strlen(opt_btc_sig);
		if (len > 32)
			len = 32;
		pool->scriptsig_base[ofs++] = len;
		memcpy(pool->scriptsig_base + ofs, opt_btc_sig, len);
		ofs += len;
	}

	pool->scriptsig_base[0] = ofs++; // Template length
	pool->n1_len = ofs;

	len = 	41 // prefix
		+ ofs // Template length
		+ 4 // txin sequence no
		+ 1 // transactions
		+ 8 // value
		+ 1 + 25 // txout
		+ 4; // lock
	free(pool->coinbase);
	pool->coinbase = calloc(len, 1);
	if (unlikely(!pool->coinbase))
		quit(1, "Failed to calloc coinbase in gbt_solo_decode");

	memcpy(pool->coinbase + 41, pool->scriptsig_base, ofs);
	memcpy(pool->coinbase + 41 + ofs, "\xff\xff\xff\xff", 4);
	pool->coinbase[41 + ofs + 4] = 1;
	u64 = (uint64_t *)&(pool->coinbase[41 + ofs + 4 + 1]);
	*u64 = htole64(coinbasevalue);

	pool->nonce2 = 0;
	pool->n2size = 4;
	pool->coinbase_len = 41 + ofs + 4 + 1 + 8 + 1 + 25 + 4;
	cg_wunlock(&pool->gbt_lock);

	snprintf(header, 225, "%s%s%s%s%s%s%s",
		 pool->bbversion,
		 pool->prev_hash,
		 "0000000000000000000000000000000000000000000000000000000000000000",
		 pool->ntime,
		 pool->nbit,
		 "00000000", /* nonce */
		 workpadding);
	if (unlikely(!hex2bin(pool->header_bin, header, 112)))
		quit(1, "Failed to hex2bin header in gbt_solo_decode");

	return true;
}

static bool work_decode(struct pool *pool, struct work *work, json_t *val)
{
	json_t *res_val = json_object_get(val, "result");
	bool ret = false;

	cgtime(&pool->tv_lastwork);
	if (!res_val || json_is_null(res_val)) {
		applog(LOG_ERR, "JSON Failed to decode result");
		goto out;
	}

	if (pool->gbt_solo) {
		if (unlikely(!gbt_solo_decode(pool, res_val)))
			goto out;
		ret = true;
		goto out;
	} else if (pool->has_gbt) {
		if (unlikely(!gbt_decode(pool, res_val)))
			goto out;
		work->gbt = true;
		ret = true;
		goto out;
	} else if (unlikely(!getwork_decode(res_val, work)))
		goto out;

	memset(work->hash, 0, sizeof(work->hash));

	cgtime(&work->tv_staged);

	ret = true;

out:
	return ret;
}
#else /* HAVE_LIBCURL */
/* Always true with stratum */
#define pool_localgen(pool) (true)
#define json_rpc_call(curl, url, userpass, rpc_req, probe, longpoll, rolltime, pool, share) (NULL)
#define work_decode(pool, work, val) (false)
#define gen_gbt_work(pool, work) {}
#endif /* HAVE_LIBCURL */

int dev_from_id(int thr_id)
{
	struct cgpu_info *cgpu = get_thr_cgpu(thr_id);

	return cgpu->device_id;
}

/* Create an exponentially decaying average over the opt_log_interval */
void decay_time(double *f, double fadd, double fsecs, double interval)
{
	double ftotal, fprop;

	if (fsecs <= 0)
		return;
	fprop = 1.0 - 1 / (exp(fsecs / interval));
	ftotal = 1.0 + fprop;
	*f += (fadd / fsecs * fprop);
	*f /= ftotal;
}

static int __total_staged(void)
{
	return HASH_COUNT(staged_work);
}

static int total_staged(void)
{
	int ret;

	mutex_lock(stgd_lock);
	ret = __total_staged();
	mutex_unlock(stgd_lock);

	return ret;
}

#ifdef HAVE_CURSES
WINDOW *mainwin, *statuswin, *logwin;
#endif
double total_secs = 1.0;
static char statusline[256];
/* logstart is where the log window should start */
static int devcursor, logstart, logcursor;
#ifdef HAVE_CURSES
/* statusy is where the status window goes up to in cases where it won't fit at startup */
static int statusy;
#endif

#ifdef HAVE_CURSES
static inline void unlock_curses(void)
{
	mutex_unlock(&console_lock);
}

static inline void lock_curses(void)
{
	mutex_lock(&console_lock);
}

static bool curses_active_locked(void)
{
	bool ret;

	lock_curses();
	ret = curses_active;
	if (!ret)
		unlock_curses();
	return ret;
}
#endif

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void suffix_string(uint64_t val, char *buf, size_t bufsiz, int sigdigits)
{
	const double  dkilo = 1000.0;
	const uint64_t kilo = 1000ull;
	const uint64_t mega = 1000000ull;
	const uint64_t giga = 1000000000ull;
	const uint64_t tera = 1000000000000ull;
	const uint64_t peta = 1000000000000000ull;
	const uint64_t exa  = 1000000000000000000ull;
	char suffix[2] = "";
	bool decimal = true;
	double dval;

	if (val >= exa) {
		val /= peta;
		dval = (double)val / dkilo;
		strcpy(suffix, "E");
	} else if (val >= peta) {
		val /= tera;
		dval = (double)val / dkilo;
		strcpy(suffix, "P");
	} else if (val >= tera) {
		val /= giga;
		dval = (double)val / dkilo;
		strcpy(suffix, "T");
	} else if (val >= giga) {
		val /= mega;
		dval = (double)val / dkilo;
		strcpy(suffix, "G");
	} else if (val >= mega) {
		val /= kilo;
		dval = (double)val / dkilo;
		strcpy(suffix, "M");
	} else if (val >= kilo) {
		dval = (double)val / dkilo;
		strcpy(suffix, "K");
	} else {
		dval = val;
		decimal = false;
	}

	if (!sigdigits) {
		if (decimal)
			snprintf(buf, bufsiz, "%.3g%s", dval, suffix);
		else
			snprintf(buf, bufsiz, "%d%s", (unsigned int)dval, suffix);
	} else {
		/* Always show sigdigits + 1, padded on right with zeroes
		 * followed by suffix */
		int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

		snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
	}
}

double cgpu_runtime(struct cgpu_info *cgpu)
{
	struct timeval now;
	double dev_runtime;

	if (cgpu->dev_start_tv.tv_sec == 0)
		dev_runtime = total_secs;
	else {
		cgtime(&now);
		dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
	}

	if (dev_runtime < 1.0)
		dev_runtime = 1.0;
	return dev_runtime;
}

double tsince_restart(void)
{
	struct timeval now;

	cgtime(&now);
	return tdiff(&now, &restart_tv_start);
}

double tsince_update(void)
{
	struct timeval now;

	cgtime(&now);
	return tdiff(&now, &update_tv_start);
}

static void get_statline(char *buf, size_t bufsiz, struct cgpu_info *cgpu)
{
	char displayed_hashes[16], displayed_rolling[16];
	double dev_runtime, wu;
	uint64_t dh64, dr64;

	dev_runtime = cgpu_runtime(cgpu);

	wu = cgpu->diff1 / dev_runtime * 60.0;

	dh64 = (double)cgpu->total_mhashes / dev_runtime * 1000000ull;
	dr64 = (double)cgpu->rolling * 1000000ull;
	suffix_string(dh64, displayed_hashes, sizeof(displayed_hashes), 4);
	suffix_string(dr64, displayed_rolling, sizeof(displayed_rolling), 4);

	snprintf(buf, bufsiz, "%s %d ", cgpu->drv->name, cgpu->device_id);
	cgpu->drv->get_statline_before(buf, bufsiz, cgpu);
	tailsprintf(buf, bufsiz, "(%ds):%s (avg):%sh/s | A:%.0f R:%.0f HW:%d WU:%.1f/m",
		opt_log_interval,
		displayed_rolling,
		displayed_hashes,
		cgpu->diff_accepted,
		cgpu->diff_rejected,
		cgpu->hw_errors,
		wu);
	cgpu->drv->get_statline(buf, bufsiz, cgpu);
}

static bool shared_strategy(void)
{
	return (pool_strategy == POOL_LOADBALANCE || pool_strategy == POOL_BALANCE);
}

#ifdef HAVE_CURSES
#define CURBUFSIZ 256
#define cg_mvwprintw(win, y, x, fmt, ...) do { \
	char tmp42[CURBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	mvwprintw(win, y, x, "%s", tmp42); \
} while (0)
#define cg_wprintw(win, fmt, ...) do { \
	char tmp42[CURBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	wprintw(win, "%s", tmp42); \
} while (0)

/* Must be called with curses mutex lock held and curses_active */
static void curses_print_status(void)
{
	struct pool *pool = current_pool();
	int linewidth = opt_widescreen ? 100 : 80;

	wattron(statuswin, A_BOLD);
	cg_mvwprintw(statuswin, 0, 0, " " PACKAGE " version " VERSION " - Started: %s", datestamp);
	wattroff(statuswin, A_BOLD);
	mvwhline(statuswin, 1, 0, '-', linewidth);
	cg_mvwprintw(statuswin, 2, 0, " %s", statusline);
	wclrtoeol(statuswin);
	if (opt_widescreen) {
		cg_mvwprintw(statuswin, 3, 0, " A:%.0f  R:%.0f  HW:%d  WU:%.1f/m |"
			     " ST: %d  SS: %"PRId64"  NB: %d  LW: %d  GF: %d  RF: %d",
			     total_diff_accepted, total_diff_rejected, hw_errors,
			     total_diff1 / total_secs * 60,
			     total_staged(), total_stale, new_blocks, local_work, total_go, total_ro);
	} else if (alt_status) {
		cg_mvwprintw(statuswin, 3, 0, " ST: %d  SS: %"PRId64"  NB: %d  LW: %d  GF: %d  RF: %d",
			     total_staged(), total_stale, new_blocks, local_work, total_go, total_ro);
	} else {
		cg_mvwprintw(statuswin, 3, 0, " A:%.0f  R:%.0f  HW:%d  WU:%.1f/m",
			     total_diff_accepted, total_diff_rejected, hw_errors,
			     total_diff1 / total_secs * 60);
	}
	wclrtoeol(statuswin);
	if (shared_strategy() && total_pools > 1) {
		cg_mvwprintw(statuswin, 4, 0, " Connected to multiple pools with%s block change notify",
			have_longpoll ? "": "out");
	} else if (pool->has_stratum) {
		cg_mvwprintw(statuswin, 4, 0, " Connected to %s diff %s with stratum as user %s",
			pool->sockaddr_url, pool->diff, pool->rpc_user);
	} else {
		cg_mvwprintw(statuswin, 4, 0, " Connected to %s diff %s with%s %s as user %s",
			pool->sockaddr_url, pool->diff, have_longpoll ? "": "out",
			pool->has_gbt ? "GBT" : "LP", pool->rpc_user);
	}
	wclrtoeol(statuswin);
	cg_mvwprintw(statuswin, 5, 0, " Block: %s...  Diff:%s  Started: %s  Best share: %s   ",
		     prev_block, block_diff, blocktime, best_share);
	mvwhline(statuswin, 6, 0, '-', linewidth);
	mvwhline(statuswin, statusy - 1, 0, '-', linewidth);
#ifdef USE_USBUTILS
	cg_mvwprintw(statuswin, devcursor - 1, 1, "[U]SB management [P]ool management [S]ettings [D]isplay options [Q]uit");
#else
	cg_mvwprintw(statuswin, devcursor - 1, 1, "[P]ool management [S]ettings [D]isplay options [Q]uit");
#endif
}

static void adj_width(int var, int *length)
{
	if ((int)(log10(var) + 1) > *length)
		(*length)++;
}

static void adj_fwidth(float var, int *length)
{
	if ((int)(log10(var) + 1) > *length)
		(*length)++;
}

#define STATBEFORELEN 23
const char blanks[] = "                                        ";

static void curses_print_devstatus(struct cgpu_info *cgpu, int devno, int count)
{
	static int devno_width = 1, dawidth = 1, drwidth = 1, hwwidth = 1, wuwidth = 1;
	char logline[256], unique_id[12];
	struct timeval now;
	double dev_runtime, wu;
	unsigned int devstatlen;

	if (opt_compact)
		return;

	if (devcursor + count > LINES - 2)
		return;

	if (count >= most_devices)
		return;

	if (cgpu->dev_start_tv.tv_sec == 0)
		dev_runtime = total_secs;
	else {
		cgtime(&now);
		dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
	}

	if (dev_runtime < 1.0)
		dev_runtime = 1.0;

	cgpu->utility = cgpu->accepted / dev_runtime * 60;
	wu = cgpu->diff1 / dev_runtime * 60;

	wmove(statuswin,devcursor + count, 0);
	adj_width(devno, &devno_width);
	if (cgpu->unique_id) {
		unique_id[8] = '\0';
		memcpy(unique_id, blanks, 8);
		strncpy(unique_id, cgpu->unique_id, 8);
	} else
		sprintf(unique_id, "%-8d", cgpu->device_id);
	cg_wprintw(statuswin, " %*d: %s %-8s: ", devno_width, devno, cgpu->drv->name,
		   unique_id);
	logline[0] = '\0';
	cgpu->drv->get_statline_before(logline, sizeof(logline), cgpu);
	devstatlen = strlen(logline);
	if (devstatlen < STATBEFORELEN)
		strncat(logline, blanks, STATBEFORELEN - devstatlen);
	cg_wprintw(statuswin, "%s | ", logline);


#ifdef USE_USBUTILS
	if (cgpu->usbinfo.nodev)
		cg_wprintw(statuswin, "ZOMBIE");
	else
#endif
	if (cgpu->status == LIFE_DEAD)
		cg_wprintw(statuswin, "DEAD  ");
	else if (cgpu->status == LIFE_SICK)
		cg_wprintw(statuswin, "SICK  ");
	else if (cgpu->deven == DEV_DISABLED)
		cg_wprintw(statuswin, "OFF   ");
	else if (cgpu->deven == DEV_RECOVER)
		cg_wprintw(statuswin, "REST  ");
	else if (opt_widescreen) {
		char displayed_hashes[16], displayed_rolling[16];
		uint64_t d64;

		d64 = (double)cgpu->total_mhashes / dev_runtime * 1000000ull;
		suffix_string(d64, displayed_hashes, sizeof(displayed_hashes), 4);
		d64 = (double)cgpu->rolling * 1000000ull;
		suffix_string(d64, displayed_rolling, sizeof(displayed_rolling), 4);
		adj_width(wu, &wuwidth);
		adj_fwidth(cgpu->diff_accepted, &dawidth);
		adj_fwidth(cgpu->diff_rejected, &drwidth);
		adj_width(cgpu->hw_errors, &hwwidth);
		cg_wprintw(statuswin, "%6s / %6sh/s WU:%*.1f/m "
				"A:%*.0f R:%*.0f HW:%*d",
				displayed_rolling,
				displayed_hashes, wuwidth + 2, wu,
				dawidth, cgpu->diff_accepted,
				drwidth, cgpu->diff_rejected,
				hwwidth, cgpu->hw_errors);
	} else if (!alt_status) {
		char displayed_hashes[16], displayed_rolling[16];
		uint64_t d64;

		d64 = (double)cgpu->total_mhashes / dev_runtime * 1000000ull;
		suffix_string(d64, displayed_hashes, sizeof(displayed_hashes), 4);
		d64 = (double)cgpu->rolling * 1000000ull;
		suffix_string(d64, displayed_rolling, sizeof(displayed_rolling), 4);
		adj_width(wu, &wuwidth);
		cg_wprintw(statuswin, "%6s / %6sh/s WU:%*.1f/m", displayed_rolling,
			   displayed_hashes, wuwidth + 2, wu);
	} else {
		adj_fwidth(cgpu->diff_accepted, &dawidth);
		adj_fwidth(cgpu->diff_rejected, &drwidth);
		adj_width(cgpu->hw_errors, &hwwidth);
		cg_wprintw(statuswin, "A:%*.0f R:%*.0f HW:%*d",
				dawidth, cgpu->diff_accepted,
				drwidth, cgpu->diff_rejected,
				hwwidth, cgpu->hw_errors);
	}

	logline[0] = '\0';
	cgpu->drv->get_statline(logline, sizeof(logline), cgpu);
	cg_wprintw(statuswin, "%s", logline);

	wclrtoeol(statuswin);
}
#endif

#ifdef HAVE_CURSES
/* Check for window resize. Called with curses mutex locked */
static inline void change_logwinsize(void)
{
	int x, y, logx, logy;

	getmaxyx(mainwin, y, x);
	if (x < 80 || y < 25)
		return;

	if (y > statusy + 2 && statusy < logstart) {
		if (y - 2 < logstart)
			statusy = y - 2;
		else
			statusy = logstart;
		logcursor = statusy + 1;
		mvwin(logwin, logcursor, 0);
		wresize(statuswin, statusy, x);
	}

	y -= logcursor;
	getmaxyx(logwin, logy, logx);
	/* Detect screen size change */
	if (x != logx || y != logy)
		wresize(logwin, y, x);
}

static void check_winsizes(void)
{
	if (!use_curses)
		return;
	if (curses_active_locked()) {
		int y, x;

		erase();
		x = getmaxx(statuswin);
		if (logstart > LINES - 2)
			statusy = LINES - 2;
		else
			statusy = logstart;
		logcursor = statusy;
		wresize(statuswin, statusy, x);
		getmaxyx(mainwin, y, x);
		y -= logcursor;
		wresize(logwin, y, x);
		mvwin(logwin, logcursor, 0);
		unlock_curses();
	}
}

static void disable_curses_windows(void);
static void enable_curses_windows(void);

static void switch_logsize(bool __maybe_unused newdevs)
{
	if (curses_active_locked()) {
#ifdef WIN32
		if (newdevs)
			disable_curses_windows();
#endif
		if (opt_compact) {
			logstart = devcursor + 1;
			logcursor = logstart + 1;
		} else {
			logstart = devcursor + most_devices + 1;
			logcursor = logstart + 1;
		}
#ifdef WIN32
		if (newdevs)
			enable_curses_windows();
#endif
		unlock_curses();
		check_winsizes();
	}
}

/* For mandatory printing when mutex is already locked */
void _wlog(const char *str)
{
	wprintw(logwin, "%s", str);
}

/* Mandatory printing */
void _wlogprint(const char *str)
{
	if (curses_active_locked()) {
		wprintw(logwin, "%s", str);
		unlock_curses();
	}
}
#endif

#ifdef HAVE_CURSES
bool log_curses_only(int prio, const char *datetime, const char *str)
{
	bool high_prio;

	high_prio = (prio == LOG_WARNING || prio == LOG_ERR);

	if (curses_active_locked()) {
		if (!opt_loginput || high_prio) {
			wprintw(logwin, "%s%s\n", datetime, str);
			if (high_prio) {
				touchwin(logwin);
				wrefresh(logwin);
			}
		}
		unlock_curses();
		return true;
	}
	return false;
}

void clear_logwin(void)
{
	if (curses_active_locked()) {
		erase();
		wclear(logwin);
		unlock_curses();
	}
}

void logwin_update(void)
{
	if (curses_active_locked()) {
		touchwin(logwin);
		wrefresh(logwin);
		unlock_curses();
	}
}
#endif

static void enable_pool(struct pool *pool)
{
	if (pool->enabled != POOL_ENABLED) {
		enabled_pools++;
		pool->enabled = POOL_ENABLED;
	}
}

#ifdef HAVE_CURSES
static void disable_pool(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED)
		enabled_pools--;
	pool->enabled = POOL_DISABLED;
}
#endif

static void reject_pool(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED)
		enabled_pools--;
	pool->enabled = POOL_REJECTING;
}

static void restart_threads(void);

/* Theoretically threads could race when modifying accepted and
 * rejected values but the chance of two submits completing at the
 * same time is zero so there is no point adding extra locking */
static void
share_result(json_t *val, json_t *res, json_t *err, const struct work *work,
	     char *hashshow, bool resubmit, char *worktime)
{
	struct pool *pool = work->pool;
	struct cgpu_info *cgpu;

	cgpu = get_thr_cgpu(work->thr_id);

	if (json_is_true(res) || (work->gbt && json_is_null(res))) {
		mutex_lock(&stats_lock);
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		cgpu->diff_accepted += work->work_difficulty;
		total_diff_accepted += work->work_difficulty;
		pool->diff_accepted += work->work_difficulty;
		mutex_unlock(&stats_lock);

		pool->seq_rejects = 0;
		cgpu->last_share_pool = pool->pool_no;
		cgpu->last_share_pool_time = time(NULL);
		cgpu->last_share_diff = work->work_difficulty;
		pool->last_share_time = cgpu->last_share_pool_time;
		pool->last_share_diff = work->work_difficulty;
		applog(LOG_DEBUG, "PROOF OF WORK RESULT: true (yay!!!)");
		if (!QUIET) {
			if (total_pools > 1)
				applog(LOG_NOTICE, "Accepted %s %s %d pool %d %s%s",
				       hashshow, cgpu->drv->name, cgpu->device_id, work->pool->pool_no, resubmit ? "(resubmit)" : "", worktime);
			else
				applog(LOG_NOTICE, "Accepted %s %s %d %s%s",
				       hashshow, cgpu->drv->name, cgpu->device_id, resubmit ? "(resubmit)" : "", worktime);
		}
		sharelog("accept", work);
		if (opt_shares && total_diff_accepted >= opt_shares) {
			applog(LOG_WARNING, "Successfully mined %d accepted shares as requested and exiting.", opt_shares);
			kill_work();
			return;
		}

		/* Detect if a pool that has been temporarily disabled for
		 * continually rejecting shares has started accepting shares.
		 * This will only happen with the work returned from a
		 * longpoll */
		if (unlikely(pool->enabled == POOL_REJECTING)) {
			applog(LOG_WARNING, "Rejecting pool %d now accepting shares, re-enabling!", pool->pool_no);
			enable_pool(pool);
			switch_pools(NULL);
		}
		/* If we know we found the block we know better than anyone
		 * that new work is needed. */
		if (unlikely(work->block))
			restart_threads();
	} else {
		mutex_lock(&stats_lock);
		cgpu->rejected++;
		total_rejected++;
		pool->rejected++;
		cgpu->diff_rejected += work->work_difficulty;
		total_diff_rejected += work->work_difficulty;
		pool->diff_rejected += work->work_difficulty;
		pool->seq_rejects++;
		mutex_unlock(&stats_lock);

		applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
		if (!QUIET) {
			char where[20];
			char disposition[36] = "reject";
			char reason[32];

			strcpy(reason, "");
			if (total_pools > 1)
				snprintf(where, sizeof(where), "pool %d", work->pool->pool_no);
			else
				strcpy(where, "");

			if (!work->gbt)
				res = json_object_get(val, "reject-reason");
			if (res) {
				const char *reasontmp = json_string_value(res);

				size_t reasonLen = strlen(reasontmp);
				if (reasonLen > 28)
					reasonLen = 28;
				reason[0] = ' '; reason[1] = '(';
				memcpy(2 + reason, reasontmp, reasonLen);
				reason[reasonLen + 2] = ')'; reason[reasonLen + 3] = '\0';
				memcpy(disposition + 7, reasontmp, reasonLen);
				disposition[6] = ':'; disposition[reasonLen + 7] = '\0';
			} else if (work->stratum && err) {
				if (json_is_array(err)) {
					json_t *reason_val = json_array_get(err, 1);
					char *reason_str;

					if (reason_val && json_is_string(reason_val)) {
						reason_str = (char *)json_string_value(reason_val);
						snprintf(reason, 31, " (%s)", reason_str);
					}
				} else if (json_is_string(err)) {
					const char *s = json_string_value(err);
					snprintf(reason, 31, " (%s)", s);
				}
			}

			applog(LOG_NOTICE, "Rejected %s %s %d %s%s %s%s",
			       hashshow, cgpu->drv->name, cgpu->device_id, where, reason, resubmit ? "(resubmit)" : "", worktime);
			sharelog(disposition, work);
		}

		/* Once we have more than a nominal amount of sequential rejects,
		 * at least 10 and more than 3 mins at the current utility,
		 * disable the pool because some pool error is likely to have
		 * ensued. Do not do this if we know the share just happened to
		 * be stale due to networking delays.
		 */
		if (pool->seq_rejects > 10 && !work->stale && opt_disable_pool && enabled_pools > 1) {
			double utility = total_accepted / total_secs * 60;

			if (pool->seq_rejects > utility * 3 && enabled_pools > 1) {
				applog(LOG_WARNING, "Pool %d rejected %d sequential shares, disabling!",
				       pool->pool_no, pool->seq_rejects);
				reject_pool(pool);
				if (pool == current_pool())
					switch_pools(NULL);
				pool->seq_rejects = 0;
			}
		}
	}
}

static void show_hash(struct work *work, char *hashshow)
{
	unsigned char rhash[32];
	char diffdisp[16];
	unsigned long h32;
	uint32_t *hash32;
	uint64_t uintdiff;
	int ofs;

	swab256(rhash, work->hash);
	for (ofs = 0; ofs <= 28; ofs ++) {
		if (rhash[ofs])
			break;
	}
	hash32 = (uint32_t *)(rhash + ofs);
	h32 = be32toh(*hash32);
	uintdiff = round(work->work_difficulty);
	suffix_string(work->share_diff, diffdisp, sizeof (diffdisp), 0);
	snprintf(hashshow, 64, "%08lx Diff %s/%"PRIu64"%s", h32, diffdisp, uintdiff,
		 work->block? " BLOCK!" : "");
}

#ifdef HAVE_LIBCURL
static void text_print_status(int thr_id)
{
	struct cgpu_info *cgpu;
	char logline[256];

	cgpu = get_thr_cgpu(thr_id);
	if (cgpu) {
		get_statline(logline, sizeof(logline), cgpu);
		printf("%s\n", logline);
	}
}

static void print_status(int thr_id)
{
	if (!curses_active)
		text_print_status(thr_id);
}

static bool submit_upstream_work(struct work *work, CURL *curl, bool resubmit)
{
	json_t *val, *res, *err;
	char *s;
	bool rc = false;
	int thr_id = work->thr_id;
	struct cgpu_info *cgpu;
	struct pool *pool = work->pool;
	int rolltime;
	struct timeval tv_submit, tv_submit_reply;
	char hashshow[64 + 4] = "";
	char worktime[200] = "";
	struct timeval now;
	double dev_runtime;

	cgpu = get_thr_cgpu(thr_id);

	/* build JSON-RPC request */
	if (work->gbt) {
		char gbt_block[1024], varint[12];
		unsigned char data[80];

		flip80(data, work->data);
		__bin2hex(gbt_block, data, 80); // 160 length

		if (work->gbt_txns < 0xfd) {
			uint8_t val8 = work->gbt_txns;

			__bin2hex(varint, (const unsigned char *)&val8, 1);
		} else if (work->gbt_txns <= 0xffff) {
			uint16_t val16 = htole16(work->gbt_txns);

			strcat(gbt_block, "fd"); // +2
			__bin2hex(varint, (const unsigned char *)&val16, 2);
		} else {
			uint32_t val32 = htole32(work->gbt_txns);

			strcat(gbt_block, "fe"); // +2
			__bin2hex(varint, (const unsigned char *)&val32, 4);
		}
		strcat(gbt_block, varint); // +8 max
		strcat(gbt_block, work->coinbase);

		s = malloc(1024);
		if (unlikely(!s))
			quit(1, "Failed to malloc s in submit_upstream_work");
		sprintf(s, "{\"id\": 0, \"method\": \"submitblock\", \"params\": [\"%s", gbt_block);
		/* Has submit/coinbase support */
		if (!pool->has_gbt) {
			cg_rlock(&pool->gbt_lock);
			if (pool->txn_data)
				s = realloc_strcat(s, pool->txn_data);
			cg_runlock(&pool->gbt_lock);
		}
		if (work->job_id) {
			s = realloc_strcat(s, "\", {\"workid\": \"");
			s = realloc_strcat(s, work->job_id);
			s = realloc_strcat(s, "\"}]}");
		} else
			s = realloc_strcat(s, "\"]}");
	} else {
		char *hexstr;

		endian_flip128(work->data, work->data);

		/* build hex string */
		hexstr = bin2hex(work->data, 118);
		s = strdup("{\"method\": \"getwork\", \"params\": [ \"");
		s = realloc_strcat(s, hexstr);
		s = realloc_strcat(s, "\" ], \"id\":1}");
		free(hexstr);
	}
	applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->rpc_url, s);
	s = realloc_strcat(s, "\n");

	cgtime(&tv_submit);
	/* issue JSON-RPC request */
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, s, false, false, &rolltime, pool, true);
	cgtime(&tv_submit_reply);
	free(s);

	if (unlikely(!val)) {
		applog(LOG_INFO, "submit_upstream_work json_rpc_call failed");
		if (!pool_tset(pool, &pool->submit_fail)) {
			total_ro++;
			pool->remotefail_occasions++;
			if (opt_lowmem) {
				applog(LOG_WARNING, "Pool %d communication failure, discarding shares", pool->pool_no);
				goto out;
			}
			applog(LOG_WARNING, "Pool %d communication failure, caching submissions", pool->pool_no);
		}
		cgsleep_ms(5000);
		goto out;
	} else if (pool_tclear(pool, &pool->submit_fail))
		applog(LOG_WARNING, "Pool %d communication resumed, submitting work", pool->pool_no);

	res = json_object_get(val, "result");
	err = json_object_get(val, "error");

	if (!QUIET) {
		show_hash(work, hashshow);

		if (opt_worktime) {
			char workclone[20];
			struct tm *tm, tm_getwork, tm_submit_reply;
			double getwork_time = tdiff((struct timeval *)&(work->tv_getwork_reply),
							(struct timeval *)&(work->tv_getwork));
			double getwork_to_work = tdiff((struct timeval *)&(work->tv_work_start),
							(struct timeval *)&(work->tv_getwork_reply));
			double work_time = tdiff((struct timeval *)&(work->tv_work_found),
							(struct timeval *)&(work->tv_work_start));
			double work_to_submit = tdiff(&tv_submit,
							(struct timeval *)&(work->tv_work_found));
			double submit_time = tdiff(&tv_submit_reply, &tv_submit);
			int diffplaces = 3;

			time_t tmp_time = work->tv_getwork.tv_sec;
			tm = localtime(&tmp_time);
			memcpy(&tm_getwork, tm, sizeof(struct tm));
			tmp_time = tv_submit_reply.tv_sec;
			tm = localtime(&tmp_time);
			memcpy(&tm_submit_reply, tm, sizeof(struct tm));

			if (work->clone) {
				snprintf(workclone, sizeof(workclone), "C:%1.3f",
						tdiff((struct timeval *)&(work->tv_cloned),
						(struct timeval *)&(work->tv_getwork_reply)));
			}
			else
				strcpy(workclone, "O");

			if (work->work_difficulty < 1)
				diffplaces = 6;

			snprintf(worktime, sizeof(worktime),
				" <-%08lx.%08lx M:%c D:%1.*f G:%02d:%02d:%02d:%1.3f %s (%1.3f) W:%1.3f (%1.3f) S:%1.3f R:%02d:%02d:%02d",
				(unsigned long)be32toh(*(uint32_t *)&(work->data[28])),
				(unsigned long)be32toh(*(uint32_t *)&(work->data[24])),
				work->getwork_mode, diffplaces, work->work_difficulty,
				tm_getwork.tm_hour, tm_getwork.tm_min,
				tm_getwork.tm_sec, getwork_time, workclone,
				getwork_to_work, work_time, work_to_submit, submit_time,
				tm_submit_reply.tm_hour, tm_submit_reply.tm_min,
				tm_submit_reply.tm_sec);
		}
	}

	share_result(val, res, err, work, hashshow, resubmit, worktime);

	if (cgpu->dev_start_tv.tv_sec == 0)
		dev_runtime = total_secs;
	else {
		cgtime(&now);
		dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
	}

	if (dev_runtime < 1.0)
		dev_runtime = 1.0;

	cgpu->utility = cgpu->accepted / dev_runtime * 60;

	if (!opt_realquiet)
		print_status(thr_id);
	if (!want_per_device_stats) {
		char logline[256];

		get_statline(logline, sizeof(logline), cgpu);
		applog(LOG_INFO, "%s", logline);
	}

	json_decref(val);

	rc = true;
out:
	return rc;
}

static bool get_upstream_work(struct work *work, CURL *curl)
{
	struct pool *pool = work->pool;
	struct cgminer_pool_stats *pool_stats = &(pool->cgminer_pool_stats);
	struct timeval tv_elapsed;
	json_t *val = NULL;
	bool rc = false;
	char *url;

	url = pool->rpc_url;

	applog(LOG_DEBUG, "DBG: sending %s get RPC call: %s", url, pool->rpc_req);

	cgtime(&work->tv_getwork);

	val = json_rpc_call(curl, url, pool->rpc_userpass, pool->rpc_req, false,
			    false, &work->rolltime, pool, false);
	pool_stats->getwork_attempts++;

	if (likely(val)) {
		rc = work_decode(pool, work, val);
		if (unlikely(!rc))
			applog(LOG_DEBUG, "Failed to decode work in get_upstream_work");
	} else
		applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");

	cgtime(&work->tv_getwork_reply);
	timersub(&(work->tv_getwork_reply), &(work->tv_getwork), &tv_elapsed);
	pool_stats->getwork_wait_rolling += ((double)tv_elapsed.tv_sec + ((double)tv_elapsed.tv_usec / 1000000)) * 0.63;
	pool_stats->getwork_wait_rolling /= 1.63;

	timeradd(&tv_elapsed, &(pool_stats->getwork_wait), &(pool_stats->getwork_wait));
	if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_max), >)) {
		pool_stats->getwork_wait_max.tv_sec = tv_elapsed.tv_sec;
		pool_stats->getwork_wait_max.tv_usec = tv_elapsed.tv_usec;
	}
	if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_min), <)) {
		pool_stats->getwork_wait_min.tv_sec = tv_elapsed.tv_sec;
		pool_stats->getwork_wait_min.tv_usec = tv_elapsed.tv_usec;
	}
	pool_stats->getwork_calls++;

	work->pool = pool;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_POOL;
	calc_diff(work, 0);
	total_getworks++;
	pool->getwork_requested++;

	if (likely(val))
		json_decref(val);

	return rc;
}
#endif /* HAVE_LIBCURL */

/* Specifies whether we can use this pool for work or not. */
static bool pool_unworkable(struct pool *pool)
{
	if (pool->idle)
		return true;
	if (pool->enabled != POOL_ENABLED)
		return true;
	if (pool->has_stratum && !pool->stratum_active)
		return true;
	return false;
}

/* In balanced mode, the amount of diff1 solutions per pool is monitored as a
 * rolling average per 10 minutes and if pools start getting more, it biases
 * away from them to distribute work evenly. The share count is reset to the
 * rolling average every 10 minutes to not send all work to one pool after it
 * has been disabled/out for an extended period. */
static struct pool *select_balanced(struct pool *cp)
{
	int i, lowest = cp->shares;
	struct pool *ret = cp;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool_unworkable(pool))
			continue;
		if (pool->shares < lowest) {
			lowest = pool->shares;
			ret = pool;
		}
	}

	ret->shares++;
	return ret;
}

static struct pool *priority_pool(int choice);
static bool pool_unusable(struct pool *pool);

/* Select any active pool in a rotating fashion when loadbalance is chosen if
 * it has any quota left. */
static inline struct pool *select_pool(bool lagging)
{
	static int rotating_pool = 0;
	struct pool *pool, *cp;
	bool avail = false;
	int tested, i;

	cp = current_pool();

	if (pool_strategy == POOL_BALANCE) {
		pool = select_balanced(cp);
		goto out;
	}

	if (pool_strategy != POOL_LOADBALANCE && (!lagging || opt_fail_only)) {
		pool = cp;
		goto out;
	} else
		pool = NULL;

	for (i = 0; i < total_pools; i++) {
		struct pool *tp = pools[i];

		if (tp->quota_used < tp->quota_gcd) {
			avail = true;
			break;
		}
	}

	/* There are no pools with quota, so reset them. */
	if (!avail) {
		for (i = 0; i < total_pools; i++)
			pools[i]->quota_used = 0;
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
	}

	/* Try to find the first pool in the rotation that is usable */
	tested = 0;
	while (!pool && tested++ < total_pools) {
		pool = pools[rotating_pool];
		if (pool->quota_used++ < pool->quota_gcd) {
			if (!pool_unworkable(pool))
				break;
			/* Failover-only flag for load-balance means distribute
			 * unused quota to priority pool 0. */
			if (opt_fail_only)
				priority_pool(0)->quota_used--;
		}
		pool = NULL;
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
	}

	/* If there are no alive pools with quota, choose according to
	 * priority. */
	if (!pool) {
		for (i = 0; i < total_pools; i++) {
			struct pool *tp = priority_pool(i);

			if (!pool_unusable(tp)) {
				pool = tp;
				break;
			}
		}
	}

	/* If still nothing is usable, use the current pool */
	if (!pool)
		pool = cp;
out:
	applog(LOG_DEBUG, "Selecting pool %d for work", pool->pool_no);
	return pool;
}

/* truediffone == 0x00000000FFFF0000000000000000000000000000000000000000000000000000
 * Generate a 256 bit binary LE target by cutting up diff into 64 bit sized
 * portions or vice versa. */
static const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
static const double bits128 = 340282366920938463463374607431768211456.0;
static const double bits64 = 18446744073709551616.0;

/* Converts a little endian 256 bit value to a double */
static double le256todouble(const void *target)
{
	uint64_t *data64;
	double dcut64;

	data64 = (uint64_t *)(target + 24);
	dcut64 = le64toh(*data64) * bits192;

	data64 = (uint64_t *)(target + 16);
	dcut64 += le64toh(*data64) * bits128;

	data64 = (uint64_t *)(target + 8);
	dcut64 += le64toh(*data64) * bits64;

	data64 = (uint64_t *)(target);
	dcut64 += le64toh(*data64);

	return dcut64;
}

static double diff_from_target(void *target)
{
	double d64, dcut64;

	d64 = truediffone;
	dcut64 = le256todouble(target);
	if (unlikely(!dcut64))
		dcut64 = 1;
	return d64 / dcut64;
}

/*
 * Calculate the work->work_difficulty based on the work->target
 */
static void calc_diff(struct work *work, double known)
{
	struct cgminer_pool_stats *pool_stats = &(work->pool->cgminer_pool_stats);
	double difficulty;
	uint64_t uintdiff;

	if (known)
		work->work_difficulty = known;
	else
		work->work_difficulty = diff_from_target(work->target);

	difficulty = work->work_difficulty;

	pool_stats->last_diff = difficulty;
	uintdiff = round(difficulty);
	suffix_string(uintdiff, work->pool->diff, sizeof(work->pool->diff), 0);

	if (difficulty == pool_stats->min_diff)
		pool_stats->min_diff_count++;
	else if (difficulty < pool_stats->min_diff || pool_stats->min_diff == 0) {
		pool_stats->min_diff = difficulty;
		pool_stats->min_diff_count = 1;
	}

	if (difficulty == pool_stats->max_diff)
		pool_stats->max_diff_count++;
	else if (difficulty > pool_stats->max_diff) {
		pool_stats->max_diff = difficulty;
		pool_stats->max_diff_count = 1;
	}
}

static unsigned char bench_hidiff_bins[16][160];
static unsigned char bench_lodiff_bins[16][160];
static unsigned char bench_target[32];

/* Iterate over the lo and hi diff benchmark work items such that we find one
 * diff 32+ share every 32 work items. */
static void get_benchmark_work(struct work *work)
{
	work->work_difficulty = 32;
	memcpy(work->target, bench_target, 32);
	work->drv_rolllimit = 0;
	work->mandatory = true;
	work->pool = pools[0];
	cgtime(&work->tv_getwork);
	copy_time(&work->tv_getwork_reply, &work->tv_getwork);
	work->getwork_mode = GETWORK_MODE_BENCHMARK;
}

static void benchfile_dspwork(struct work *work, uint32_t nonce)
{
	char buf[1024];
	uint32_t dn;
	int i;

	dn = 0;
	for (i = 0; i < 4; i++) {
		dn *= 0x100;
		dn += nonce & 0xff;
		nonce /= 0x100;
	}

	if ((sizeof(work->data) * 2 + 1) > sizeof(buf))
		quithere(1, "BENCHFILE Invalid buf size");

	__bin2hex(buf, work->data, sizeof(work->data));

	applog(LOG_ERR, "BENCHFILE nonce %u=0x%08x for work=%s",
			(unsigned int)dn, (unsigned int)dn, buf);

}

static bool benchfile_get_work(struct work *work)
{
	char buf[1024];
	char item[1024];
	bool got = false;

	if (!benchfile_in) {
		if (opt_benchfile)
			benchfile_in = fopen(opt_benchfile, "r");
		else
			quit(1, "BENCHFILE Invalid benchfile NULL");

		if (!benchfile_in)
			quit(1, "BENCHFILE Failed to open benchfile '%s'", opt_benchfile);

		benchfile_line = 0;

		if (!fgets(buf, 1024, benchfile_in))
			quit(1, "BENCHFILE Failed to read benchfile '%s'", opt_benchfile);

		got = true;
		benchfile_work = 0;
	}

	if (!got) {
		if (!fgets(buf, 1024, benchfile_in)) {
			if (benchfile_work == 0)
				quit(1, "BENCHFILE No work in benchfile '%s'", opt_benchfile);
			fclose(benchfile_in);
			benchfile_in = NULL;
			return benchfile_get_work(work);
		}
	}

	do {
		benchfile_line++;

		// Empty lines and lines starting with '#' or '/' are ignored
		if (*buf != '\0' && *buf != '#' && *buf != '/') {
			char *commas[BENCHWORK_COUNT];
			int i, j, len;
			long nonce_time;

			commas[0] = buf;
			for (i = 1; i < BENCHWORK_COUNT; i++) {
				commas[i] = strchr(commas[i-1], ',');
				if (!commas[i]) {
					quit(1, "BENCHFILE Invalid input file line %d"
						" - field count is %d but should be %d",
						benchfile_line, i, BENCHWORK_COUNT);
				}
				len = commas[i] - commas[i-1];
				if (benchfile_data[i-1].length &&
				    (len != benchfile_data[i-1].length)) {
					quit(1, "BENCHFILE Invalid input file line %d "
						"field %d (%s) length is %d but should be %d",
						benchfile_line, i,
						benchfile_data[i-1].name,
						len, benchfile_data[i-1].length);
				}

				*(commas[i]++) = '\0';
			}

			// NonceTime may have LF's etc
			len = strlen(commas[BENCHWORK_NONCETIME]);
			if (len < benchfile_data[BENCHWORK_NONCETIME].length) {
				quit(1, "BENCHFILE Invalid input file line %d field %d"
					" (%s) length is %d but should be least %d",
					benchfile_line, BENCHWORK_NONCETIME+1,
					benchfile_data[BENCHWORK_NONCETIME].name, len,
					benchfile_data[BENCHWORK_NONCETIME].length);
			}

			sprintf(item, "0000000%c", commas[BENCHWORK_VERSION][0]);

			j = strlen(item);
			for (i = benchfile_data[BENCHWORK_PREVHASH].length-8; i >= 0; i -= 8) {
				sprintf(&(item[j]), "%.8s", &commas[BENCHWORK_PREVHASH][i]);
				j += 8;
			}

			for (i = benchfile_data[BENCHWORK_MERKLEROOT].length-8; i >= 0; i -= 8) {
				sprintf(&(item[j]), "%.8s", &commas[BENCHWORK_MERKLEROOT][i]);
				j += 8;
			}

			nonce_time = atol(commas[BENCHWORK_NONCETIME]);

			sprintf(&(item[j]), "%08lx", nonce_time);
			j += 8;

			strcpy(&(item[j]), commas[BENCHWORK_DIFFBITS]);
			j += benchfile_data[BENCHWORK_DIFFBITS].length;

			memset(work, 0, sizeof(*work));

			hex2bin(work->data, item, j >> 1);

			calc_midstate(work);

			benchfile_work++;

			return true;
		}
	} while (fgets(buf, 1024, benchfile_in));

	if (benchfile_work == 0)
		quit(1, "BENCHFILE No work in benchfile '%s'", opt_benchfile);
	fclose(benchfile_in);
	benchfile_in = NULL;
	return benchfile_get_work(work);
}

static void get_benchfile_work(struct work *work)
{
	benchfile_get_work(work);
	work->mandatory = true;
	work->pool = pools[0];
	cgtime(&work->tv_getwork);
	copy_time(&work->tv_getwork_reply, &work->tv_getwork);
	work->getwork_mode = GETWORK_MODE_BENCHMARK;
	calc_diff(work, 0);
}

#ifdef HAVE_CURSES
static void disable_curses_windows(void)
{
	leaveok(logwin, false);
	leaveok(statuswin, false);
	leaveok(mainwin, false);
	nocbreak();
	echo();
	delwin(logwin);
	delwin(statuswin);
}

/* Force locking of curses console_lock on shutdown since a dead thread might
 * have grabbed the lock. */
static bool curses_active_forcelocked(void)
{
	bool ret;

	mutex_trylock(&console_lock);
	ret = curses_active;
	if (!ret)
		unlock_curses();
	return ret;
}

static void disable_curses(void)
{
	if (curses_active_forcelocked()) {
		use_curses = false;
		curses_active = false;
		disable_curses_windows();
		delwin(mainwin);
		endwin();
#ifdef WIN32
		// Move the cursor to after curses output.
		HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		COORD coord;

		if (GetConsoleScreenBufferInfo(hout, &csbi)) {
			coord.X = 0;
			coord.Y = csbi.dwSize.Y - 1;
			SetConsoleCursorPosition(hout, coord);
		}
#endif
		unlock_curses();
	}
}
#endif

static void kill_timeout(struct thr_info *thr)
{
	cg_completion_timeout(&thr_info_cancel, thr, 1000);
}

static void kill_mining(void)
{
	struct thr_info *thr;
	int i;

	forcelog(LOG_DEBUG, "Killing off mining threads");
	/* Kill the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		pthread_t *pth = NULL;

		thr = get_thread(i);
		if (thr && PTH(thr) != 0L)
			pth = &thr->pth;
		thr_info_cancel(thr);
#ifndef WIN32
		if (pth && *pth)
			pthread_join(*pth, NULL);
#else
		if (pth && pth->p)
			pthread_join(*pth, NULL);
#endif
	}
}

static void __kill_work(void)
{
	struct thr_info *thr;
	int i;

	if (!successful_connect)
		return;

	forcelog(LOG_INFO, "Received kill message");

#ifdef USE_USBUTILS
	/* Best to get rid of it first so it doesn't
	 * try to create any new devices */
	forcelog(LOG_DEBUG, "Killing off HotPlug thread");
	thr = &control_thr[hotplug_thr_id];
	kill_timeout(thr);
#endif

	forcelog(LOG_DEBUG, "Killing off watchpool thread");
	/* Kill the watchpool thread */
	thr = &control_thr[watchpool_thr_id];
	kill_timeout(thr);

	forcelog(LOG_DEBUG, "Killing off watchdog thread");
	/* Kill the watchdog thread */
	thr = &control_thr[watchdog_thr_id];
	kill_timeout(thr);

	forcelog(LOG_DEBUG, "Shutting down mining threads");
	for (i = 0; i < mining_threads; i++) {
		struct cgpu_info *cgpu;

		thr = get_thread(i);
		if (!thr)
			continue;
		cgpu = thr->cgpu;
		if (!cgpu)
			continue;

		cgpu->shutdown = true;
	}

	sleep(1);

	cg_completion_timeout(&kill_mining, NULL, 3000);

	/* Stop the others */
	forcelog(LOG_DEBUG, "Killing off API thread");
	thr = &control_thr[api_thr_id];
	kill_timeout(thr);

#ifdef USE_USBUTILS
	/* Release USB resources in case it's a restart
	 * and not a QUIT */
	forcelog(LOG_DEBUG, "Releasing all USB devices");
	cg_completion_timeout(&usb_cleanup, NULL, 1000);

	forcelog(LOG_DEBUG, "Killing off usbres thread");
	thr = &control_thr[usbres_thr_id];
	kill_timeout(thr);
#endif

}

/* This should be the common exit path */
void kill_work(void)
{
	cg_completion_timeout(&__kill_work, NULL, 5000);

	quit(0, "Shutdown signal received.");
}

static
#ifdef WIN32
const
#endif
char **initial_args;

static void clean_up(bool restarting);

void app_restart(void)
{
	applog(LOG_WARNING, "Attempting to restart %s", packagename);

	cg_completion_timeout(&__kill_work, NULL, 5000);
	clean_up(true);

#if defined(unix) || defined(__APPLE__)
	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
#endif

	execv(initial_args[0], (EXECV_2ND_ARG_TYPE)initial_args);
	applog(LOG_WARNING, "Failed to restart application");
}

static void sighandler(int __maybe_unused sig)
{
	/* Restore signal handlers so we can still quit if kill_work fails */
	sigaction(SIGTERM, &termhandler, NULL);
	sigaction(SIGINT, &inthandler, NULL);
	kill_work();
}

static void _stage_work(struct work *work);

#define stage_work(WORK) do { \
	_stage_work(WORK); \
	WORK = NULL; \
} while (0)

/* Adjust an existing char ntime field with a relative noffset */
static void modify_ntime(char *ntime, int noffset)
{
	unsigned char bin[4];
	uint32_t h32, *be32 = (uint32_t *)bin;

	hex2bin(bin, ntime, 4);
	h32 = be32toh(*be32) + noffset;
	*be32 = htobe32(h32);
	__bin2hex(ntime, bin, 4);
}

void roll_work(struct work *work)
{
	uint32_t *work_ntime;
	uint32_t ntime;

	work_ntime = (uint32_t *)(work->data + 68);
	ntime = be32toh(*work_ntime);
	ntime++;
	*work_ntime = htobe32(ntime);
	local_work++;
	work->rolls++;
	work->nonce = 0;
	applog(LOG_DEBUG, "Successfully rolled work");
	/* Change the ntime field if this is stratum work */
	if (work->ntime)
		modify_ntime(work->ntime, 1);

	/* This is now a different work item so it needs a different ID for the
	 * hashtable */
	work->id = total_work_inc();
}

struct work *make_clone(struct work *work)
{
	struct work *work_clone = copy_work(work);

	work_clone->clone = true;
	cgtime((struct timeval *)&(work_clone->tv_cloned));
	work_clone->longpoll = false;
	work_clone->mandatory = false;
	/* Make cloned work appear slightly older to bias towards keeping the
	 * master work item which can be further rolled */
	work_clone->tv_staged.tv_sec -= 1;

	return work_clone;
}

#ifdef HAVE_LIBCURL
/* Called with pool_lock held. Recruit an extra curl if none are available for
 * this pool. */
static void recruit_curl(struct pool *pool)
{
	struct curl_ent *ce = calloc(sizeof(struct curl_ent), 1);

	if (unlikely(!ce))
		quit(1, "Failed to calloc in recruit_curl");

	ce->curl = curl_easy_init();
	if (unlikely(!ce->curl))
		quit(1, "Failed to init in recruit_curl");

	list_add(&ce->node, &pool->curlring);
	pool->curls++;
}

/* Grab an available curl if there is one. If not, then recruit extra curls
 * unless we are in a submit_fail situation, or we have opt_delaynet enabled
 * and there are already 5 curls in circulation. Limit total number to the
 * number of mining threads per pool as well to prevent blasting a pool during
 * network delays/outages. */
static struct curl_ent *pop_curl_entry(struct pool *pool)
{
	int curl_limit = opt_delaynet ? 5 : (mining_threads + opt_queue) * 2;
	bool recruited = false;
	struct curl_ent *ce;

	mutex_lock(&pool->pool_lock);
retry:
	if (!pool->curls) {
		recruit_curl(pool);
		recruited = true;
	} else if (list_empty(&pool->curlring)) {
		if (pool->curls >= curl_limit) {
			pthread_cond_wait(&pool->cr_cond, &pool->pool_lock);
			goto retry;
		} else {
			recruit_curl(pool);
			recruited = true;
		}
	}
	ce = list_entry(pool->curlring.next, struct curl_ent, node);
	list_del(&ce->node);
	mutex_unlock(&pool->pool_lock);

	if (recruited)
		applog(LOG_DEBUG, "Recruited curl for pool %d", pool->pool_no);
	return ce;
}

static void push_curl_entry(struct curl_ent *ce, struct pool *pool)
{
	mutex_lock(&pool->pool_lock);
	list_add_tail(&ce->node, &pool->curlring);
	cgtime(&ce->tv);
	pthread_cond_broadcast(&pool->cr_cond);
	mutex_unlock(&pool->pool_lock);
}

static bool stale_work(struct work *work, bool share);

static inline bool should_roll(struct work *work)
{
	struct timeval now;
	time_t expiry;

	if (work->pool != current_pool() && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE)
		return false;

	if (work->rolltime > opt_scantime)
		expiry = work->rolltime;
	else
		expiry = opt_scantime;
	expiry = expiry * 2 / 3;

	/* We shouldn't roll if we're unlikely to get one shares' duration
	 * work out of doing so */
	cgtime(&now);
	if (now.tv_sec - work->tv_staged.tv_sec > expiry)
		return false;

	return true;
}

/* Limit rolls to 7000 to not beyond 2 hours in the future where bitcoind will
 * reject blocks as invalid. */
static inline bool can_roll(struct work *work)
{
	return (!work->stratum && work->pool && work->rolltime && !work->clone &&
		work->rolls < 7000 && !stale_work(work, false));
}

static void *submit_work_thread(void *userdata)
{
	struct work *work = (struct work *)userdata;
	struct pool *pool = work->pool;
	bool resubmit = false;
	struct curl_ent *ce;

	pthread_detach(pthread_self());

	RenameThread("SubmitWork");

	applog(LOG_DEBUG, "Creating extra submit work thread");

	ce = pop_curl_entry(pool);
	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(work, ce->curl, resubmit)) {
		if (opt_lowmem) {
			applog(LOG_NOTICE, "Pool %d share being discarded to minimise memory cache", pool->pool_no);
			break;
		}
		resubmit = true;
		if (stale_work(work, true)) {
			applog(LOG_NOTICE, "Pool %d share became stale while retrying submit, discarding", pool->pool_no);

			mutex_lock(&stats_lock);
			total_stale++;
			pool->stale_shares++;
			total_diff_stale += work->work_difficulty;
			pool->diff_stale += work->work_difficulty;
			mutex_unlock(&stats_lock);

			free_work(work);
			break;
		}

		/* pause, then restart work-request loop */
		applog(LOG_INFO, "json_rpc_call failed on submit_work, retrying");
	}
	push_curl_entry(ce, pool);

	return NULL;
}

static bool clone_available(void)
{
	struct work *work_clone = NULL, *work, *tmp;
	bool cloned = false;

	mutex_lock(stgd_lock);
	if (!staged_rollable)
		goto out_unlock;

	HASH_ITER(hh, staged_work, work, tmp) {
		if (can_roll(work) && should_roll(work)) {
			roll_work(work);
			work_clone = make_clone(work);
			roll_work(work);
			cloned = true;
			break;
		}
	}

out_unlock:
	mutex_unlock(stgd_lock);

	if (cloned) {
		applog(LOG_DEBUG, "Pushing cloned available work to stage thread");
		stage_work(work_clone);
	}
	return cloned;
}

/* Clones work by rolling it if possible, and returning a clone instead of the
 * original work item which gets staged again to possibly be rolled again in
 * the future */
static struct work *clone_work(struct work *work)
{
	int mrs = mining_threads + opt_queue - total_staged();
	struct work *work_clone;
	bool cloned;

	if (mrs < 1)
		return work;

	cloned = false;
	work_clone = make_clone(work);
	while (mrs-- > 0 && can_roll(work) && should_roll(work)) {
		applog(LOG_DEBUG, "Pushing rolled converted work to stage thread");
		stage_work(work_clone);
		roll_work(work);
		work_clone = make_clone(work);
		/* Roll it again to prevent duplicates should this be used
		 * directly later on */
		roll_work(work);
		cloned = true;
	}

	if (cloned) {
		stage_work(work);
		return work_clone;
	}

	free_work(work_clone);

	return work;
}

#else /* HAVE_LIBCURL */
static void *submit_work_thread(void __maybe_unused *userdata)
{
	pthread_detach(pthread_self());
	return NULL;
}
#endif /* HAVE_LIBCURL */

/* Return an adjusted ntime if we're submitting work that a device has
 * internally offset the ntime. */
static char *offset_ntime(const char *ntime, int noffset)
{
	unsigned char bin[4];
	uint32_t h32, *be32 = (uint32_t *)bin;

	hex2bin(bin, ntime, 4);
	h32 = be32toh(*be32) + noffset;
	*be32 = htobe32(h32);

	return bin2hex(bin, 4);
}

/* Duplicates any dynamically allocated arrays within the work struct to
 * prevent a copied work struct from freeing ram belonging to another struct */
static void _copy_work(struct work *work, const struct work *base_work, int noffset)
{
	uint32_t id = work->id;

	clean_work(work);
	memcpy(work, base_work, sizeof(struct work));
	/* Keep the unique new id assigned during make_work to prevent copied
	 * work from having the same id. */
	work->id = id;
	if (base_work->job_id)
		work->job_id = strdup(base_work->job_id);
	if (base_work->nonce1)
		work->nonce1 = strdup(base_work->nonce1);
	if (base_work->ntime) {
		/* If we are passed an noffset the binary work->data ntime and
		 * the work->ntime hex string need to be adjusted. */
		if (noffset) {
			uint32_t *work_ntime = (uint32_t *)(work->data + 68);
			uint32_t ntime = be32toh(*work_ntime);

			ntime += noffset;
			*work_ntime = htobe32(ntime);
			work->ntime = offset_ntime(base_work->ntime, noffset);
		} else
			work->ntime = strdup(base_work->ntime);
	} else if (noffset) {
		uint32_t *work_ntime = (uint32_t *)(work->data + 68);
		uint32_t ntime = be32toh(*work_ntime);

		ntime += noffset;
		*work_ntime = htobe32(ntime);
	}
	if (base_work->coinbase)
		work->coinbase = strdup(base_work->coinbase);
}

void set_work_ntime(struct work *work, int ntime)
{
	uint32_t *work_ntime = (uint32_t *)(work->data + 68);

	*work_ntime = htobe32(ntime);
	if (work->ntime) {
		free(work->ntime);
		work->ntime = bin2hex((unsigned char *)work_ntime, 4);
	}
}

/* Generates a copy of an existing work struct, creating fresh heap allocations
 * for all dynamically allocated arrays within the struct. noffset is used for
 * when a driver has internally rolled the ntime, noffset is a relative value.
 * The macro copy_work() calls this function with an noffset of 0. */
struct work *copy_work_noffset(struct work *base_work, int noffset)
{
	struct work *work = make_work();

	_copy_work(work, base_work, noffset);

	return work;
}

void pool_died(struct pool *pool)
{
	if (!pool_tset(pool, &pool->idle)) {
		cgtime(&pool->tv_idle);
		if (pool == current_pool()) {
			applog(LOG_WARNING, "Pool %d %s not responding!", pool->pool_no, pool->rpc_url);
			switch_pools(NULL);
		} else
			applog(LOG_INFO, "Pool %d %s failed to return work", pool->pool_no, pool->rpc_url);
	}
}

static bool stale_work(struct work *work, bool share)
{
	struct timeval now;
	time_t work_expiry;
	struct pool *pool;
	int getwork_delay;

	if (opt_benchmark || opt_benchfile)
		return false;

	if (work->work_block != work_block) {
		applog(LOG_DEBUG, "Work stale due to block mismatch");
		return true;
	}

	/* Technically the rolltime should be correct but some pools
	 * advertise a broken expire= that is lower than a meaningful
	 * scantime */
	if (work->rolltime > opt_scantime)
		work_expiry = work->rolltime;
	else
		work_expiry = opt_expiry;

	pool = work->pool;

	if (!share && pool->has_stratum) {
		bool same_job;

		if (!pool->stratum_active || !pool->stratum_notify) {
			applog(LOG_DEBUG, "Work stale due to stratum inactive");
			return true;
		}

		same_job = true;

		cg_rlock(&pool->data_lock);
		if (strcmp(work->job_id, pool->swork.job_id))
			same_job = false;
		cg_runlock(&pool->data_lock);

		if (!same_job) {
			applog(LOG_DEBUG, "Work stale due to stratum job_id mismatch");
			return true;
		}
	}

	/* Factor in the average getwork delay of this pool, rounding it up to
	 * the nearest second */
	getwork_delay = pool->cgminer_pool_stats.getwork_wait_rolling * 5 + 1;
	work_expiry -= getwork_delay;
	if (unlikely(work_expiry < 5))
		work_expiry = 5;

	cgtime(&now);
	if ((now.tv_sec - work->tv_staged.tv_sec) >= work_expiry) {
		applog(LOG_DEBUG, "Work stale due to expiry");
		return true;
	}

	if (opt_fail_only && !share && pool != current_pool() && !work->mandatory &&
	    pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		applog(LOG_DEBUG, "Work stale due to fail only pool mismatch");
		return true;
	}

	return false;
}

uint64_t share_diff(const struct work *work)
{
	bool new_best = false;
	double d64, s64;
	uint64_t ret;

	d64 = truediffone;
	s64 = le256todouble(work->hash);
	if (unlikely(!s64))
		s64 = 0;

	ret = round(d64 / s64);

	cg_wlock(&control_lock);
	if (unlikely(ret > best_diff)) {
		new_best = true;
		best_diff = ret;
		suffix_string(best_diff, best_share, sizeof(best_share), 0);
	}
	if (unlikely(ret > work->pool->best_diff))
		work->pool->best_diff = ret;
	cg_wunlock(&control_lock);

	if (unlikely(new_best))
		applog(LOG_INFO, "New best share: %s", best_share);

	return ret;
}

static void regen_hash(struct work *work)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[80];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];

	flip80(swap32, data32);
	sha256(swap, 80, hash1);
	sha256(hash1, 32, (unsigned char *)(work->hash));
}

static bool cnx_needed(struct pool *pool);

/* Find the pool that currently has the highest priority */
static struct pool *priority_pool(int choice)
{
	struct pool *ret = NULL;
	int i;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->prio == choice) {
			ret = pool;
			break;
		}
	}

	if (unlikely(!ret)) {
		applog(LOG_ERR, "WTF No pool %d found!", choice);
		return pools[choice];
	}
	return ret;
}

/* Specifies whether we can switch to this pool or not. */
static bool pool_unusable(struct pool *pool)
{
	if (pool->idle)
		return true;
	if (pool->enabled != POOL_ENABLED)
		return true;
	return false;
}

void switch_pools(struct pool *selected)
{
	struct pool *pool, *last_pool;
	int i, pool_no, next_pool;

	cg_wlock(&control_lock);
	last_pool = currentpool;
	pool_no = currentpool->pool_no;

	/* Switch selected to pool number 0 and move the rest down */
	if (selected) {
		if (selected->prio != 0) {
			for (i = 0; i < total_pools; i++) {
				pool = pools[i];
				if (pool->prio < selected->prio)
					pool->prio++;
			}
			selected->prio = 0;
		}
	}

	switch (pool_strategy) {
		/* All of these set to the master pool */
		case POOL_BALANCE:
		case POOL_FAILOVER:
		case POOL_LOADBALANCE:
			for (i = 0; i < total_pools; i++) {
				pool = priority_pool(i);
				if (pool_unusable(pool))
					continue;
				pool_no = pool->pool_no;
				break;
			}
			break;
		/* Both of these simply increment and cycle */
		case POOL_ROUNDROBIN:
		case POOL_ROTATE:
			if (selected && !selected->idle) {
				pool_no = selected->pool_no;
				break;
			}
			next_pool = pool_no;
			/* Select the next alive pool */
			for (i = 1; i < total_pools; i++) {
				next_pool++;
				if (next_pool >= total_pools)
					next_pool = 0;
				pool = pools[next_pool];
				if (pool_unusable(pool))
					continue;
				pool_no = next_pool;
				break;
			}
			break;
		default:
			break;
	}

	currentpool = pools[pool_no];
	pool = currentpool;
	cg_wunlock(&control_lock);

	/* Set the lagging flag to avoid pool not providing work fast enough
	 * messages in failover only mode since  we have to get all fresh work
	 * as in restart_threads */
	if (opt_fail_only)
		pool_tset(pool, &pool->lagging);

	if (pool != last_pool && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		applog(LOG_WARNING, "Switching to pool %d %s", pool->pool_no, pool->rpc_url);
		if (pool_localgen(pool) || opt_fail_only)
			clear_pool_work(last_pool);
	}

	mutex_lock(&lp_lock);
	pthread_cond_broadcast(&lp_cond);
	mutex_unlock(&lp_lock);

}

void _discard_work(struct work *work)
{
	if (!work->clone && !work->rolls && !work->mined) {
		if (work->pool) {
			work->pool->discarded_work++;
			work->pool->quota_used--;
			work->pool->works--;
		}
		total_discarded++;
		applog(LOG_DEBUG, "Discarded work");
	} else
		applog(LOG_DEBUG, "Discarded cloned or rolled work");
	free_work(work);
}

static void wake_gws(void)
{
	mutex_lock(stgd_lock);
	pthread_cond_signal(&gws_cond);
	mutex_unlock(stgd_lock);
}

static void discard_stale(void)
{
	struct work *work, *tmp;
	int stale = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (stale_work(work, false)) {
			HASH_DEL(staged_work, work);
			discard_work(work);
			stale++;
		}
	}
	pthread_cond_signal(&gws_cond);
	mutex_unlock(stgd_lock);

	if (stale)
		applog(LOG_DEBUG, "Discarded %d stales that didn't match current hash", stale);
}

/* A generic wait function for threads that poll that will wait a specified
 * time tdiff waiting on the pthread conditional that is broadcast when a
 * work restart is required. Returns the value of pthread_cond_timedwait
 * which is zero if the condition was met or ETIMEDOUT if not.
 */
int restart_wait(struct thr_info *thr, unsigned int mstime)
{
	struct timeval now, then, tdiff;
	struct timespec abstime;
	int rc;

	tdiff.tv_sec = mstime / 1000;
	tdiff.tv_usec = mstime * 1000 - (tdiff.tv_sec * 1000000);
	cgtime(&now);
	timeradd(&now, &tdiff, &then);
	abstime.tv_sec = then.tv_sec;
	abstime.tv_nsec = then.tv_usec * 1000;

	mutex_lock(&restart_lock);
	if (thr->work_restart)
		rc = 0;
	else
		rc = pthread_cond_timedwait(&restart_cond, &restart_lock, &abstime);
	mutex_unlock(&restart_lock);

	return rc;
}

static void *restart_thread(void __maybe_unused *arg)
{
	struct pool *cp = current_pool();
	struct cgpu_info *cgpu;
	int i, mt;

	pthread_detach(pthread_self());

	/* Artificially set the lagging flag to avoid pool not providing work
	 * fast enough  messages after every long poll */
	pool_tset(cp, &cp->lagging);

	/* Discard staged work that is now stale */
	discard_stale();

	rd_lock(&mining_thr_lock);
	mt = mining_threads;
	rd_unlock(&mining_thr_lock);

	for (i = 0; i < mt; i++) {
		cgpu = mining_thr[i]->cgpu;
		if (unlikely(!cgpu))
			continue;
		if (cgpu->deven != DEV_ENABLED)
			continue;
		mining_thr[i]->work_restart = true;
		flush_queue(cgpu);
		cgpu->drv->flush_work(cgpu);
	}

	mutex_lock(&restart_lock);
	pthread_cond_broadcast(&restart_cond);
	mutex_unlock(&restart_lock);

#ifdef USE_USBUTILS
	/* Cancels any cancellable usb transfers. Flagged as such it means they
	 * are usualy waiting on a read result and it's safe to abort the read
	 * early. */
	cancel_usb_transfers();
#endif
	return NULL;
}

/* In order to prevent a deadlock via the various drv->flush_work
 * implementations we send the restart messages via a separate thread. */
static void restart_threads(void)
{
	pthread_t rthread;

	cgtime(&restart_tv_start);
	if (unlikely(pthread_create(&rthread, NULL, restart_thread, NULL)))
		quit(1, "Failed to create restart thread");
}

static void signal_work_update(void)
{
	int i;

	applog(LOG_INFO, "Work update message received");

	cgtime(&update_tv_start);
	rd_lock(&mining_thr_lock);
	for (i = 0; i < mining_threads; i++)
		mining_thr[i]->work_update = true;
	rd_unlock(&mining_thr_lock);
}

static void set_curblock(char *hexstr, unsigned char *bedata)
{
	int ofs;

	cg_wlock(&ch_lock);
	cgtime(&block_timeval);
	strcpy(current_hash, hexstr);
	memcpy(current_block, bedata, 32);
	get_timestamp(blocktime, sizeof(blocktime), &block_timeval);
	cg_wunlock(&ch_lock);

	for (ofs = 0; ofs <= 56; ofs++) {
		if (memcmp(&current_hash[ofs], "0", 1))
			break;
	}
	strncpy(prev_block, &current_hash[ofs], 8);
	prev_block[8] = '\0';

	applog(LOG_INFO, "New block: %s... diff %s", current_hash, block_diff);
}

/* Search to see if this string is from a block that has been seen before */
static bool block_exists(char *hexstr)
{
	struct block *s;

	rd_lock(&blk_lock);
	HASH_FIND_STR(blocks, hexstr, s);
	rd_unlock(&blk_lock);

	if (s)
		return true;
	return false;
}

/* Tests if this work is from a block that has been seen before */
static inline bool from_existing_block(struct work *work)
{
	char *hexstr = bin2hex(work->data + 8, 18);
	bool ret;

	ret = block_exists(hexstr);
	free(hexstr);
	return ret;
}

static int block_sort(struct block *blocka, struct block *blockb)
{
	return blocka->block_no - blockb->block_no;
}

/* Decode the current block difficulty which is in packed form */
static void set_blockdiff(const struct work *work)
{
	uint8_t pow = work->data[72];
	int powdiff = (8 * (0x1d - 3)) - (8 * (pow - 3));
	uint32_t diff32 = be32toh(*((uint32_t *)(work->data + 72))) & 0x00FFFFFF;
	double numerator = 0xFFFFULL << powdiff;
	double ddiff = numerator / (double)diff32;

	if (unlikely(current_diff != ddiff)) {
		suffix_string(ddiff, block_diff, sizeof(block_diff), 0);
		current_diff = ddiff;
		applog(LOG_NOTICE, "Network diff set to %s", block_diff);
	}
}

static bool test_work_current(struct work *work)
{
	struct pool *pool = work->pool;
	unsigned char bedata[32];
	char hexstr[68];
	bool ret = true;

	if (work->mandatory)
		return ret;

	swap256(bedata, work->data + 4);
	__bin2hex(hexstr, bedata, 32);

	/* Search to see if this block exists yet and if not, consider it a
	 * new block and set the current block details to this one */
	if (!block_exists(hexstr)) {
		struct block *s = calloc(sizeof(struct block), 1);
		int deleted_block = 0;

		if (unlikely(!s))
			quit (1, "test_work_current OOM");
		strcpy(s->hash, hexstr);
		s->block_no = new_blocks++;

		wr_lock(&blk_lock);
		/* Only keep the last hour's worth of blocks in memory since
		 * work from blocks before this is virtually impossible and we
		 * want to prevent memory usage from continually rising */
		if (HASH_COUNT(blocks) > 6) {
			struct block *oldblock;

			HASH_SORT(blocks, block_sort);
			oldblock = blocks;
			deleted_block = oldblock->block_no;
			HASH_DEL(blocks, oldblock);
			free(oldblock);
		}
		HASH_ADD_STR(blocks, hash, s);
		set_blockdiff(work);
		wr_unlock(&blk_lock);

		if (deleted_block)
			applog(LOG_DEBUG, "Deleted block %d from database", deleted_block);
		set_curblock(hexstr, bedata);
		/* Copy the information to this pool's prev_block since it
		 * knows the new block exists. */
		memcpy(pool->prev_block, bedata, 32);
		if (unlikely(new_blocks == 1)) {
			ret = false;
			goto out;
		}

		work->work_block = ++work_block;

		if (work->longpoll) {
			if (work->stratum) {
				applog(LOG_NOTICE, "Stratum from pool %d detected new block",
				       pool->pool_no);
			} else {
				applog(LOG_NOTICE, "%sLONGPOLL from pool %d detected new block",
				       work->gbt ? "GBT " : "", work->pool->pool_no);
			}
		} else if (have_longpoll && !pool->gbt_solo)
			applog(LOG_NOTICE, "New block detected on network before pool notification");
		else if (!pool->gbt_solo)
			applog(LOG_NOTICE, "New block detected on network");
		restart_threads();
	} else {
		if (memcmp(pool->prev_block, bedata, 32)) {
			/* Work doesn't match what this pool has stored as
			 * prev_block. Let's see if the work is from an old
			 * block or the pool is just learning about a new
			 * block. */
			if (memcmp(bedata, current_block, 32)) {
				/* Doesn't match current block. It's stale */
				applog(LOG_DEBUG, "Stale data from pool %d", pool->pool_no);
				ret = false;
			} else {
				/* Work is from new block and pool is up now
				 * current. */
				applog(LOG_INFO, "Pool %d now up to date", pool->pool_no);
				memcpy(pool->prev_block, bedata, 32);
			}
		}
#if 0
		/* This isn't ideal, this pool is still on an old block but
		 * accepting shares from it. To maintain fair work distribution
		 * we work on it anyway. */
		if (memcmp(bedata, current_block, 32))
			applog(LOG_DEBUG, "Pool %d still on old block", pool->pool_no);
#endif
		if (work->longpoll) {
			work->work_block = ++work_block;
			if (shared_strategy() || work->pool == current_pool()) {
				if (work->stratum) {
					applog(LOG_NOTICE, "Stratum from pool %d requested work restart",
					       pool->pool_no);
				} else {
					applog(LOG_NOTICE, "%sLONGPOLL from pool %d requested work restart",
					       work->gbt ? "GBT " : "", work->pool->pool_no);
				}
				restart_threads();
			}
		}
	}
out:
	work->longpoll = false;

	return ret;
}

static int tv_sort(struct work *worka, struct work *workb)
{
	return worka->tv_staged.tv_sec - workb->tv_staged.tv_sec;
}

static bool work_rollable(struct work *work)
{
	return (!work->clone && work->rolltime);
}

static bool hash_push(struct work *work)
{
	bool rc = true;

	mutex_lock(stgd_lock);
	if (work_rollable(work))
		staged_rollable++;
	if (likely(!getq->frozen)) {
		HASH_ADD_INT(staged_work, id, work);
		HASH_SORT(staged_work, tv_sort);
	} else
		rc = false;
	pthread_cond_broadcast(&getq->cond);
	mutex_unlock(stgd_lock);

	return rc;
}

static void _stage_work(struct work *work)
{
	applog(LOG_DEBUG, "Pushing work from pool %d to hash queue", work->pool->pool_no);
	work->work_block = work_block;
	test_work_current(work);
	work->pool->works++;
	hash_push(work);
}

#ifdef HAVE_CURSES
int curses_int(const char *query)
{
	int ret;
	char *cvar;

	cvar = curses_input(query);
	ret = atoi(cvar);
	free(cvar);
	return ret;
}
#endif

#ifdef HAVE_CURSES
static bool input_pool(bool live);
#endif

#ifdef HAVE_CURSES
static void display_pool_summary(struct pool *pool)
{
	double efficiency = 0.0;

	if (curses_active_locked()) {
		wlog("Pool: %s\n", pool->rpc_url);
		if (pool->solved)
			wlog("SOLVED %d BLOCK%s!\n", pool->solved, pool->solved > 1 ? "S" : "");
		if (!pool->has_stratum)
			wlog("%s own long-poll support\n", pool->hdr_path ? "Has" : "Does not have");
		wlog(" Queued work requests: %d\n", pool->getwork_requested);
		wlog(" Share submissions: %"PRId64"\n", pool->accepted + pool->rejected);
		wlog(" Accepted shares: %"PRId64"\n", pool->accepted);
		wlog(" Rejected shares: %"PRId64"\n", pool->rejected);
		wlog(" Accepted difficulty shares: %1.f\n", pool->diff_accepted);
		wlog(" Rejected difficulty shares: %1.f\n", pool->diff_rejected);
		if (pool->accepted || pool->rejected)
			wlog(" Reject ratio: %.1f%%\n", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));
		efficiency = pool->getwork_requested ? pool->accepted * 100.0 / pool->getwork_requested : 0.0;
		if (!pool_localgen(pool))
			wlog(" Efficiency (accepted / queued): %.0f%%\n", efficiency);

		wlog(" Items worked on: %d\n", pool->works);
		wlog(" Discarded work due to new blocks: %d\n", pool->discarded_work);
		wlog(" Stale submissions discarded due to new blocks: %d\n", pool->stale_shares);
		wlog(" Unable to get work from server occasions: %d\n", pool->getfail_occasions);
		wlog(" Submitting work remotely delay occasions: %d\n\n", pool->remotefail_occasions);
		unlock_curses();
	}
}
#endif

/* We can't remove the memory used for this struct pool because there may
 * still be work referencing it. We just remove it from the pools list */
void remove_pool(struct pool *pool)
{
	int i, last_pool = total_pools - 1;
	struct pool *other;

	/* Boost priority of any lower prio than this one */
	for (i = 0; i < total_pools; i++) {
		other = pools[i];
		if (other->prio > pool->prio)
			other->prio--;
	}

	if (pool->pool_no < last_pool) {
		/* Swap the last pool for this one */
		(pools[last_pool])->pool_no = pool->pool_no;
		pools[pool->pool_no] = pools[last_pool];
	}
	/* Give it an invalid number */
	pool->pool_no = total_pools;
	pool->removed = true;
	total_pools--;
}

/* add a mutex if this needs to be thread safe in the future */
static struct JE {
	char *buf;
	struct JE *next;
} *jedata = NULL;

static void json_escape_free()
{
	struct JE *jeptr = jedata;
	struct JE *jenext;

	jedata = NULL;

	while (jeptr) {
		jenext = jeptr->next;
		free(jeptr->buf);
		free(jeptr);
		jeptr = jenext;
	}
}

static char *json_escape(char *str)
{
	struct JE *jeptr;
	char *buf, *ptr;

	/* 2x is the max, may as well just allocate that */
	ptr = buf = malloc(strlen(str) * 2 + 1);

	jeptr = malloc(sizeof(*jeptr));

	jeptr->buf = buf;
	jeptr->next = jedata;
	jedata = jeptr;

	while (*str) {
		if (*str == '\\' || *str == '"')
			*(ptr++) = '\\';

		*(ptr++) = *(str++);
	}

	*ptr = '\0';

	return buf;
}

void write_config(FILE *fcfg)
{
	struct opt_table *opt;
	int i;

	/* Write pool values */
	fputs("{\n\"pools\" : [", fcfg);
	for(i = 0; i < total_pools; i++) {
		struct pool *pool = priority_pool(i);

		if (pool->quota != 1) {
			fprintf(fcfg, "%s\n\t{\n\t\t\"quota\" : \"%s%s%s%d;%s\",", i > 0 ? "," : "",
				pool->rpc_proxy ? json_escape((char *)proxytype(pool->rpc_proxytype)) : "",
				pool->rpc_proxy ? json_escape(pool->rpc_proxy) : "",
				pool->rpc_proxy ? "|" : "",
				pool->quota,
				json_escape(pool->rpc_url));
		} else {
			fprintf(fcfg, "%s\n\t{\n\t\t\"url\" : \"%s%s%s%s\",", i > 0 ? "," : "",
				pool->rpc_proxy ? json_escape((char *)proxytype(pool->rpc_proxytype)) : "",
				pool->rpc_proxy ? json_escape(pool->rpc_proxy) : "",
				pool->rpc_proxy ? "|" : "",
				json_escape(pool->rpc_url));
		}
		fprintf(fcfg, "\n\t\t\"user\" : \"%s\",", json_escape(pool->rpc_user));
		fprintf(fcfg, "\n\t\t\"pass\" : \"%s\"\n\t}", json_escape(pool->rpc_pass));
		}
	fputs("\n]\n", fcfg);

	/* Simple bool,int and char options */
	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name = strdup(opt->names);

		for (p = strtok(name, "|"); p; p = strtok(NULL, "|")) {
			if (p[1] != '-')
				continue;

			if (opt->desc == opt_hidden)
				continue;

			if (opt->type & OPT_NOARG &&
			   ((void *)opt->cb == (void *)opt_set_bool || (void *)opt->cb == (void *)opt_set_invbool) &&
			   (*(bool *)opt->u.arg == ((void *)opt->cb == (void *)opt_set_bool))) {
				fprintf(fcfg, ",\n\"%s\" : true", p+2);
				continue;
			}

			if (opt->type & OPT_HASARG &&
			    ((void *)opt->cb_arg == (void *)opt_set_intval ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_9999 ||
			     (void *)opt->cb_arg == (void *)set_int_1_to_65535 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_10 ||
			     (void *)opt->cb_arg == (void *)set_int_1_to_10 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_100 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_255 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_200 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_4 ||
			     (void *)opt->cb_arg == (void *)set_int_32_to_63 ||
			     (void *)opt->cb_arg == (void *)set_int_22_to_55 ||
			     (void *)opt->cb_arg == (void *)set_int_42_to_65)) {
				fprintf(fcfg, ",\n\"%s\" : \"%d\"", p+2, *(int *)opt->u.arg);
				continue;
			}

			if (opt->type & OPT_HASARG &&
			    (((void *)opt->cb_arg == (void *)set_float_125_to_500) ||
			     (void *)opt->cb_arg == (void *)set_float_100_to_250)) {
				fprintf(fcfg, ",\n\"%s\" : \"%.1f\"", p+2, *(float *)opt->u.arg);
				continue;
			}

			if (opt->type & (OPT_HASARG | OPT_PROCESSARG) &&
			    (opt->u.arg != &opt_set_null)) {
				char *carg = *(char **)opt->u.arg;

				if (carg)
					fprintf(fcfg, ",\n\"%s\" : \"%s\"", p+2, json_escape(carg));
				continue;
			}
		}
		free(name);
	}

	/* Special case options */
	if (pool_strategy == POOL_BALANCE)
		fputs(",\n\"balance\" : true", fcfg);
	if (pool_strategy == POOL_LOADBALANCE)
		fputs(",\n\"load-balance\" : true", fcfg);
	if (pool_strategy == POOL_ROUNDROBIN)
		fputs(",\n\"round-robin\" : true", fcfg);
	if (pool_strategy == POOL_ROTATE)
		fprintf(fcfg, ",\n\"rotate\" : \"%d\"", opt_rotate_period);
	fputs("\n}\n", fcfg);

	json_escape_free();
}

void zero_bestshare(void)
{
	int i;

	best_diff = 0;
	memset(best_share, 0, 8);
	suffix_string(best_diff, best_share, sizeof(best_share), 0);

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];
		pool->best_diff = 0;
	}
}

static struct timeval tv_hashmeter;
static time_t hashdisplay_t;

void zero_stats(void)
{
	int i;

	cgtime(&total_tv_start);
	copy_time(&tv_hashmeter, &total_tv_start);
	total_rolling = 0;
	rolling1 = 0;
	rolling5 = 0;
	rolling15 = 0;
	total_mhashes_done = 0;
	total_getworks = 0;
	total_accepted = 0;
	total_rejected = 0;
	hw_errors = 0;
	total_stale = 0;
	total_discarded = 0;
	local_work = 0;
	total_go = 0;
	total_ro = 0;
	total_secs = 1.0;
	total_diff1 = 0;
	found_blocks = 0;
	total_diff_accepted = 0;
	total_diff_rejected = 0;
	total_diff_stale = 0;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		pool->getwork_requested = 0;
		pool->accepted = 0;
		pool->rejected = 0;
		pool->stale_shares = 0;
		pool->discarded_work = 0;
		pool->getfail_occasions = 0;
		pool->remotefail_occasions = 0;
		pool->last_share_time = 0;
		pool->diff1 = 0;
		pool->diff_accepted = 0;
		pool->diff_rejected = 0;
		pool->diff_stale = 0;
		pool->last_share_diff = 0;
	}

	zero_bestshare();

	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = get_devices(i);

		copy_time(&cgpu->dev_start_tv, &total_tv_start);

		mutex_lock(&hash_lock);
		cgpu->total_mhashes = 0;
		cgpu->accepted = 0;
		cgpu->rejected = 0;
		cgpu->hw_errors = 0;
		cgpu->utility = 0.0;
		cgpu->last_share_pool_time = 0;
		cgpu->diff1 = 0;
		cgpu->diff_accepted = 0;
		cgpu->diff_rejected = 0;
		cgpu->last_share_diff = 0;
		mutex_unlock(&hash_lock);

		/* Don't take any locks in the driver zero stats function, as
		 * it's called async from everything else and we don't want to
		 * deadlock. */
		cgpu->drv->zero_stats(cgpu);
	}
}

static void set_highprio(void)
{
#ifndef WIN32
	int ret = nice(-10);

	if (!ret)
		applog(LOG_DEBUG, "Unable to set thread to high priority");
#else
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

static void set_lowprio(void)
{
#ifndef WIN32
	int ret = nice(10);

	if (!ret)
		applog(LOG_INFO, "Unable to set thread to low priority");
#else
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
}

#ifdef HAVE_CURSES
static void display_pools(void)
{
	struct pool *pool;
	int selected, i;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
updated:
	for (i = 0; i < total_pools; i++) {
		pool = pools[i];

		if (pool == current_pool())
			wattron(logwin, A_BOLD);
		if (pool->enabled != POOL_ENABLED)
			wattron(logwin, A_DIM);
		wlogprint("%d: ", pool->pool_no);
		switch (pool->enabled) {
			case POOL_ENABLED:
				wlogprint("Enabled ");
				break;
			case POOL_DISABLED:
				wlogprint("Disabled ");
				break;
			case POOL_REJECTING:
				wlogprint("Rejecting ");
				break;
		}
		wlogprint("%s Quota %d Prio %d: %s  User:%s\n",
			pool->idle? "Dead" : "Alive",
			pool->quota,
			pool->prio,
			pool->rpc_url, pool->rpc_user);
		wattroff(logwin, A_BOLD | A_DIM);
	}
retry:
	wlogprint("\nCurrent pool management strategy: %s\n",
		strategies[pool_strategy].s);
	if (pool_strategy == POOL_ROTATE)
		wlogprint("Set to rotate every %d minutes\n", opt_rotate_period);
	wlogprint("[F]ailover only %s\n", opt_fail_only ? "enabled" : "disabled");
	wlogprint("Pool [A]dd [R]emove [D]isable [E]nable [Q]uota change\n");
	wlogprint("[C]hange management strategy [S]witch pool [I]nformation\n");
	wlogprint("Or press any other key to continue\n");
	logwin_update();
	input = getch();

	if (!strncasecmp(&input, "a", 1)) {
		input_pool(true);
		goto updated;
	} else if (!strncasecmp(&input, "r", 1)) {
		if (total_pools <= 1) {
			wlogprint("Cannot remove last pool");
			goto retry;
		}
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		if (pool == current_pool())
			switch_pools(NULL);
		if (pool == current_pool()) {
			wlogprint("Unable to remove pool due to activity\n");
			goto retry;
		}
		disable_pool(pool);
		remove_pool(pool);
		goto updated;
	} else if (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		enable_pool(pool);
		switch_pools(pool);
		goto updated;
	} else if (!strncasecmp(&input, "d", 1)) {
		if (enabled_pools <= 1) {
			wlogprint("Cannot disable last pool");
			goto retry;
		}
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		disable_pool(pool);
		if (pool == current_pool())
			switch_pools(NULL);
		goto updated;
	} else if (!strncasecmp(&input, "e", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		enable_pool(pool);
		if (pool->prio < current_pool()->prio)
			switch_pools(pool);
		goto updated;
	} else if (!strncasecmp(&input, "c", 1)) {
		for (i = 0; i <= TOP_STRATEGY; i++)
			wlogprint("%d: %s\n", i, strategies[i].s);
		selected = curses_int("Select strategy number type");
		if (selected < 0 || selected > TOP_STRATEGY) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		if (selected == POOL_ROTATE) {
			opt_rotate_period = curses_int("Select interval in minutes");

			if (opt_rotate_period < 0 || opt_rotate_period > 9999) {
				opt_rotate_period = 0;
				wlogprint("Invalid selection\n");
				goto retry;
			}
		}
		pool_strategy = selected;
		switch_pools(NULL);
		goto updated;
	} else if (!strncasecmp(&input, "i", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		display_pool_summary(pool);
		goto retry;
	} else if (!strncasecmp(&input, "q", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		selected = curses_int("Set quota");
		if (selected < 0) {
			wlogprint("Invalid negative quota\n");
			goto retry;
		}
		pool->quota = selected;
		adjust_quota_gcd();
		goto updated;
	} else if (!strncasecmp(&input, "f", 1)) {
		opt_fail_only ^= true;
		goto updated;
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}

static void display_options(void)
{
	int selected;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
retry:
	wlogprint("[N]ormal [C]lear [S]ilent mode (disable all output)\n");
	wlogprint("[D]ebug:%s\n[P]er-device:%s\n[Q]uiet:%s\n[V]erbose:%s\n"
		  "[R]PC debug:%s\n[W]orkTime details:%s\nco[M]pact: %s\n"
		  "[T]oggle status switching:%s\n"
		  "w[I]descreen:%s\n"
		  "[Z]ero statistics\n"
		  "[L]og interval:%d\n",
		opt_debug ? "on" : "off",
	        want_per_device_stats? "on" : "off",
		opt_quiet ? "on" : "off",
		opt_log_output ? "on" : "off",
		opt_protocol ? "on" : "off",
		opt_worktime ? "on" : "off",
		opt_compact ? "on" : "off",
		switch_status ? "enabled" : "disabled",
		opt_widescreen ? "enabled" : "disabled",
		opt_log_interval);
	wlogprint("Select an option or any other key to return\n");
	logwin_update();
	input = getch();
	if (!strncasecmp(&input, "q", 1)) {
		opt_quiet ^= true;
		wlogprint("Quiet mode %s\n", opt_quiet ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "v", 1)) {
		opt_log_output ^= true;
		if (opt_log_output)
			opt_quiet = false;
		wlogprint("Verbose mode %s\n", opt_log_output ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "n", 1)) {
		opt_log_output = false;
		opt_debug = false;
		opt_quiet = false;
		opt_protocol = false;
		opt_compact = false;
		want_per_device_stats = false;
		wlogprint("Output mode reset to normal\n");
		switch_logsize(false);
		goto retry;
	} else if (!strncasecmp(&input, "d", 1)) {
		opt_debug ^= true;
		opt_log_output = opt_debug;
		if (opt_debug)
			opt_quiet = false;
		wlogprint("Debug mode %s\n", opt_debug ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "m", 1)) {
		opt_compact ^= true;
		wlogprint("Compact mode %s\n", opt_compact ? "enabled" : "disabled");
		switch_logsize(false);
		goto retry;
	} else if (!strncasecmp(&input, "p", 1)) {
		want_per_device_stats ^= true;
		opt_log_output = want_per_device_stats;
		wlogprint("Per-device stats %s\n", want_per_device_stats ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "r", 1)) {
		opt_protocol ^= true;
		if (opt_protocol)
			opt_quiet = false;
		wlogprint("RPC protocol debugging %s\n", opt_protocol ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "c", 1))
		clear_logwin();
	else if (!strncasecmp(&input, "l", 1)) {
		selected = curses_int("Interval in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_log_interval = selected;
		wlogprint("Log interval set to %d seconds\n", opt_log_interval);
		goto retry;
	} else if (!strncasecmp(&input, "s", 1)) {
		opt_realquiet = true;
	} else if (!strncasecmp(&input, "w", 1)) {
		opt_worktime ^= true;
		wlogprint("WorkTime details %s\n", opt_worktime ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "t", 1)) {
		switch_status ^= true;
		goto retry;
	} else if (!strncasecmp(&input, "i", 1)) {
		opt_widescreen ^= true;
		goto retry;
	} else if (!strncasecmp(&input, "z", 1)) {
		zero_stats();
		goto retry;
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}
#endif

void default_save_file(char *filename)
{
	if (default_config && *default_config) {
		strcpy(filename, default_config);
		return;
	}

#if defined(unix) || defined(__APPLE__)
	if (getenv("HOME") && *getenv("HOME")) {
	        strcpy(filename, getenv("HOME"));
		strcat(filename, "/");
	}
	else
		strcpy(filename, "");
	strcat(filename, ".cgminer/");
	mkdir(filename, 0777);
#else
	strcpy(filename, "");
#endif
	strcat(filename, def_conf);
}

#ifdef HAVE_CURSES
static void set_options(void)
{
	int selected;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
retry:
	wlogprint("[Q]ueue: %d\n[S]cantime: %d\n[E]xpiry: %d\n"
		  "[W]rite config file\n[C]gminer restart\n",
		opt_queue, opt_scantime, opt_expiry);
	wlogprint("Select an option or any other key to return\n");
	logwin_update();
	input = getch();

	if (!strncasecmp(&input, "q", 1)) {
		selected = curses_int("Extra work items to queue");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_queue = selected;
		if (opt_queue < max_queue)
			max_queue = opt_queue;
		goto retry;
	} else if  (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Set scantime in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_scantime = selected;
		goto retry;
	} else if  (!strncasecmp(&input, "e", 1)) {
		selected = curses_int("Set expiry time in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_expiry = selected;
		goto retry;
	} else if  (!strncasecmp(&input, "w", 1)) {
		FILE *fcfg;
		char *str, filename[PATH_MAX], prompt[PATH_MAX + 50];

		default_save_file(filename);
		snprintf(prompt, sizeof(prompt), "Config filename to write (Enter for default) [%s]", filename);
		str = curses_input(prompt);
		if (strcmp(str, "-1")) {
			struct stat statbuf;

			strcpy(filename, str);
			free(str);
			if (!stat(filename, &statbuf)) {
				wlogprint("File exists, overwrite?\n");
				input = getch();
				if (strncasecmp(&input, "y", 1))
					goto retry;
			}
		}
		else
			free(str);
		fcfg = fopen(filename, "w");
		if (!fcfg) {
			wlogprint("Cannot open or create file\n");
			goto retry;
		}
		write_config(fcfg);
		fclose(fcfg);
		goto retry;

	} else if (!strncasecmp(&input, "c", 1)) {
		wlogprint("Are you sure?\n");
		input = getch();
		if (!strncasecmp(&input, "y", 1))
			app_restart();
		else
			clear_logwin();
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}

#ifdef USE_USBUTILS
static void mt_enable(struct thr_info *mythr)
{
	cgsem_post(&mythr->sem);
}

static void set_usb(void)
{
	int selected, i, mt, enabled, disabled, zombie, total, blacklisted;
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	double val;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();

retry:
	enabled = 0;
	disabled = 0;
	zombie = 0;
	total = 0;
	blacklisted = 0;

	rd_lock(&mining_thr_lock);
	mt = mining_threads;
	rd_unlock(&mining_thr_lock);

	for (i = 0; i < mt; i++) {
		cgpu = mining_thr[i]->cgpu;
		if (unlikely(!cgpu))
			continue;
		if (cgpu->usbinfo.nodev)
			zombie++;
		else if  (cgpu->deven == DEV_DISABLED)
			disabled++;
		else
			enabled++;
		if (cgpu->blacklisted)
			blacklisted++;
		total++;
	}
	wlogprint("Hotplug interval:%d\n", hotplug_time);
	wlogprint("%d USB devices, %d enabled, %d disabled, %d zombie\n",
		  total, enabled, disabled, zombie);
	wlogprint("[S]ummary of device information\n");
	wlogprint("[E]nable device\n");
	wlogprint("[D]isable device\n");
	wlogprint("[U]nplug to allow hotplug restart\n");
	wlogprint("[R]eset device USB\n");
	wlogprint("[L]ist all known devices\n");
	wlogprint("[B]lacklist current device from current instance of cgminer\n");
	wlogprint("[W]hitelist previously blacklisted device\n");
	wlogprint("[H]otplug interval (0 to disable)\n");
	wlogprint("Select an option or any other key to return\n");
	logwin_update();
	input = getch();

	if (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Select device number");
		if (selected < 0 || selected >= mt)  {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		cgpu = mining_thr[selected]->cgpu;
		wlogprint("Device %03u:%03u\n", cgpu->usbinfo.bus_number, cgpu->usbinfo.device_address);
		wlogprint("Name %s\n", cgpu->drv->name);
		wlogprint("ID %d\n", cgpu->device_id);
		wlogprint("Enabled: %s\n", cgpu->deven != DEV_DISABLED ? "Yes" : "No");
		wlogprint("Temperature %.1f\n", cgpu->temp);
		wlogprint("MHS av %.0f\n", cgpu->total_mhashes / cgpu_runtime(cgpu));
		wlogprint("Accepted %d\n", cgpu->accepted);
		wlogprint("Rejected %d\n", cgpu->rejected);
		wlogprint("Hardware Errors %d\n", cgpu->hw_errors);
		wlogprint("Last Share Pool %d\n", cgpu->last_share_pool_time > 0 ? cgpu->last_share_pool : -1);
		wlogprint("Total MH %.1f\n", cgpu->total_mhashes);
		wlogprint("Diff1 Work %"PRId64"\n", cgpu->diff1);
		wlogprint("Difficulty Accepted %.1f\n", cgpu->diff_accepted);
		wlogprint("Difficulty Rejected %.1f\n", cgpu->diff_rejected);
		wlogprint("Last Share Difficulty %.1f\n", cgpu->last_share_diff);
		wlogprint("No Device: %s\n", cgpu->usbinfo.nodev ? "True" : "False");
		wlogprint("Last Valid Work %"PRIu64"\n", (uint64_t)cgpu->last_device_valid_work);
		val = 0;
		if (cgpu->hw_errors + cgpu->diff1)
			val = cgpu->hw_errors / (cgpu->hw_errors + cgpu->diff1);
		wlogprint("Device Hardware %.1f%%\n", val);
		val = 0;
		if (cgpu->diff1)
			val = cgpu->diff_rejected / cgpu->diff1;
		wlogprint("Device Rejected %.1f%%\n", val);
		goto retry;
	} else if (!strncasecmp(&input, "e", 1)) {
		selected = curses_int("Select device number");
		if (selected < 0 || selected >= mt)  {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		cgpu = mining_thr[selected]->cgpu;
		if (cgpu->usbinfo.nodev) {
			wlogprint("Device removed, unable to re-enable!\n");
			goto retry;
		}
		thr = get_thread(selected);
		cgpu->deven = DEV_ENABLED;
		mt_enable(thr);
		goto retry;
	} else if (!strncasecmp(&input, "d", 1)) {
		selected = curses_int("Select device number");
		if (selected < 0 || selected >= mt)  {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		cgpu = mining_thr[selected]->cgpu;
		cgpu->deven = DEV_DISABLED;
		goto retry;
	} else if (!strncasecmp(&input, "u", 1)) {
		selected = curses_int("Select device number");
		if (selected < 0 || selected >= mt)  {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		cgpu = mining_thr[selected]->cgpu;
		if (cgpu->usbinfo.nodev) {
			wlogprint("Device already removed, unable to unplug!\n");
			goto retry;
		}
		usb_nodev(cgpu);
		goto retry;
	} else if (!strncasecmp(&input, "r", 1)) {
		selected = curses_int("Select device number");
		if (selected < 0 || selected >= mt)  {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		cgpu = mining_thr[selected]->cgpu;
		if (cgpu->usbinfo.nodev) {
			wlogprint("Device already removed, unable to reset!\n");
			goto retry;
		}
		usb_reset(cgpu);
		goto retry;
	} else if (!strncasecmp(&input, "b", 1)) {
		selected = curses_int("Select device number");
		if (selected < 0 || selected >= mt)  {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		cgpu = mining_thr[selected]->cgpu;
		if (cgpu->usbinfo.nodev) {
			wlogprint("Device already removed, unable to blacklist!\n");
			goto retry;
		}
		blacklist_cgpu(cgpu);
		goto retry;
	} else if (!strncasecmp(&input, "w", 1)) {
		if (!blacklisted) {
			wlogprint("No blacklisted devices!\n");
			goto retry;
		}
		wlogprint("Blacklisted devices:\n");
		for (i = 0; i < mt; i++) {
			cgpu = mining_thr[i]->cgpu;
			if (unlikely(!cgpu))
				continue;
			if (cgpu->blacklisted) {
				wlogprint("%d: %s %d %03u:%03u\n", i, cgpu->drv->name,
					  cgpu->device_id, cgpu->usbinfo.bus_number,
					  cgpu->usbinfo.device_address);
			}
		}
		selected = curses_int("Select device number");
		if (selected < 0 || selected >= mt)  {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		cgpu = mining_thr[selected]->cgpu;
		if (!cgpu->blacklisted) {
			wlogprint("Device not blacklisted, unable to whitelist\n");
			goto retry;
		}
		whitelist_cgpu(cgpu);
		goto retry;
	} else if (!strncasecmp(&input, "h", 1)) {
		selected = curses_int("Select hotplug interval in seconds (0 to disable)");
		if (selected < 0 || selected > 9999)  {
			wlogprint("Invalid value\n");
			goto retry;
		}
		hotplug_time = selected;
		goto retry;
	} else if (!strncasecmp(&input, "l", 1)) {
		usb_list();
		goto retry;
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}
#endif

static void *input_thread(void __maybe_unused *userdata)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("Input");

	if (!curses_active)
		return NULL;

	while (1) {
		char input;

		input = getch();
		if (!strncasecmp(&input, "q", 1)) {
			kill_work();
			return NULL;
		} else if (!strncasecmp(&input, "d", 1))
			display_options();
		else if (!strncasecmp(&input, "p", 1))
			display_pools();
		else if (!strncasecmp(&input, "s", 1))
			set_options();
#ifdef USE_USBUTILS
		else if (!strncasecmp(&input, "u", 1))
			set_usb();
#endif
		if (opt_realquiet) {
			disable_curses();
			break;
		}
	}

	return NULL;
}
#endif

static void *api_thread(void *userdata)
{
	struct thr_info *mythr = userdata;

	pthread_detach(pthread_self());
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("API");

	set_lowprio();
	api(api_thr_id);

	PTH(mythr) = 0L;

	return NULL;
}

/* Sole work devices are serialised wrt calling get_work so they report in on
 * each pass through their scanhash function as well as in get_work whereas
 * queued work devices work asynchronously so get them to report in and out
 * only across get_work. */
static void thread_reportin(struct thr_info *thr)
{
	thr->getwork = false;
	cgtime(&thr->last);
	thr->cgpu->status = LIFE_WELL;
	thr->cgpu->device_last_well = time(NULL);
}

/* Tell the watchdog thread this thread is waiting on get work and should not
 * be restarted */
static void thread_reportout(struct thr_info *thr)
{
	thr->getwork = true;
	cgtime(&thr->last);
	thr->cgpu->status = LIFE_WELL;
	thr->cgpu->device_last_well = time(NULL);
}

static void hashmeter(int thr_id, uint64_t hashes_done)
{
	bool showlog = false;
	double tv_tdiff;
	time_t now_t;
	int diff_t;

	cgtime(&total_tv_end);
	tv_tdiff = tdiff(&total_tv_end, &tv_hashmeter);
	now_t = total_tv_end.tv_sec;
	diff_t = now_t - hashdisplay_t;
	if (diff_t >= opt_log_interval) {
		alt_status ^= switch_status;
		hashdisplay_t = now_t;
		showlog = true;
	} else if (thr_id < 0) {
		/* hashmeter is called by non-mining threads in case nothing
		 * has reported in to allow hashrate to converge to zero , but
		 * we only update if it has been more than opt_log_interval */
		return;
	}
	copy_time(&tv_hashmeter, &total_tv_end);

	if (thr_id >= 0) {
		struct thr_info *thr = get_thread(thr_id);
		struct cgpu_info *cgpu = thr->cgpu;
		double device_tdiff, thr_mhs;

		/* Update the last time this thread reported in */
		copy_time(&thr->last, &total_tv_end);
		cgpu->device_last_well = now_t;
		device_tdiff = tdiff(&total_tv_end, &cgpu->last_message_tv);
		copy_time(&cgpu->last_message_tv, &total_tv_end);
		thr_mhs = (double)hashes_done / device_tdiff / 1000000;
		applog(LOG_DEBUG, "[thread %d: %"PRIu64" hashes, %.1f mhash/sec]",
		       thr_id, hashes_done, thr_mhs);
		hashes_done /= 1000000;

		mutex_lock(&hash_lock);
		cgpu->total_mhashes += hashes_done;
		decay_time(&cgpu->rolling, hashes_done, device_tdiff, opt_log_interval);
		decay_time(&cgpu->rolling1, hashes_done, device_tdiff, 60.0);
		decay_time(&cgpu->rolling5, hashes_done, device_tdiff, 300.0);
		decay_time(&cgpu->rolling15, hashes_done, device_tdiff, 900.0);
		mutex_unlock(&hash_lock);

		if (want_per_device_stats && showlog) {
			char logline[256];

			get_statline(logline, sizeof(logline), cgpu);
			if (!curses_active) {
				printf("%s          \r", logline);
				fflush(stdout);
			} else
				applog(LOG_INFO, "%s", logline);
		}
	} else {
		/* No device has reported in, we have been called from the
		 * watchdog thread so decay all the hashrates */
		mutex_lock(&hash_lock);
		for (thr_id = 0; thr_id < mining_threads; thr_id++) {
			struct thr_info *thr = get_thread(thr_id);
			struct cgpu_info *cgpu = thr->cgpu;
			double device_tdiff  = tdiff(&total_tv_end, &cgpu->last_message_tv);

			copy_time(&cgpu->last_message_tv, &total_tv_end);
			decay_time(&cgpu->rolling, 0, device_tdiff, opt_log_interval);
			decay_time(&cgpu->rolling1, 0, device_tdiff, 60.0);
			decay_time(&cgpu->rolling5, 0, device_tdiff, 300.0);
			decay_time(&cgpu->rolling15, 0, device_tdiff, 900.0);
		}
		mutex_unlock(&hash_lock);
	}

	mutex_lock(&hash_lock);
	total_mhashes_done += hashes_done;
	decay_time(&total_rolling, hashes_done, tv_tdiff, opt_log_interval);
	decay_time(&rolling1, hashes_done, tv_tdiff, 60.0);
	decay_time(&rolling5, hashes_done, tv_tdiff, 300.0);
	decay_time(&rolling15, hashes_done, tv_tdiff, 900.0);
	global_hashrate = llround(total_rolling) * 1000000;
	total_secs = tdiff(&total_tv_end, &total_tv_start);
	if (showlog) {
		char displayed_hashes[16], displayed_rolling[16];
		char displayed_r1[16], displayed_r5[16], displayed_r15[16];
		uint64_t d64;

		d64 = (double)total_mhashes_done / total_secs * 1000000ull;
		suffix_string(d64, displayed_hashes, sizeof(displayed_hashes), 4);
		d64 = (double)total_rolling * 1000000ull;
		suffix_string(d64, displayed_rolling, sizeof(displayed_rolling), 4);
		d64 = (double)rolling1 * 1000000ull;
		suffix_string(d64, displayed_r1, sizeof(displayed_rolling), 4);
		d64 = (double)rolling5 * 1000000ull;
		suffix_string(d64, displayed_r5, sizeof(displayed_rolling), 4);
		d64 = (double)rolling15 * 1000000ull;
		suffix_string(d64, displayed_r15, sizeof(displayed_rolling), 4);

		snprintf(statusline, sizeof(statusline),
			"(%ds):%s (1m):%s (5m):%s (15m):%s (avg):%sh/s",
			opt_log_interval, displayed_rolling, displayed_r1, displayed_r5,
			displayed_r15, displayed_hashes);
	}
	mutex_unlock(&hash_lock);

	if (showlog) {
		if (!curses_active) {
			printf("%s          \r", statusline);
			fflush(stdout);
		} else
			applog(LOG_INFO, "%s", statusline);
	}
}

static void stratum_share_result(json_t *val, json_t *res_val, json_t *err_val,
				 struct stratum_share *sshare)
{
	struct work *work = sshare->work;
	time_t now_t = time(NULL);
	char hashshow[64];
	int srdiff;

	srdiff = now_t - sshare->sshare_sent;
	if (opt_debug || srdiff > 0) {
		applog(LOG_INFO, "Pool %d stratum share result lag time %d seconds",
		       work->pool->pool_no, srdiff);
	}
	show_hash(work, hashshow);
	share_result(val, res_val, err_val, work, hashshow, false, "");
}

/* Parses stratum json responses and tries to find the id that the request
 * matched to and treat it accordingly. */
static bool parse_stratum_response(struct pool *pool, char *s)
{
	json_t *val = NULL, *err_val, *res_val, *id_val;
	struct stratum_share *sshare;
	json_error_t err;
	bool ret = false;
	int id;

	val = JSON_LOADS(s, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");
	id_val = json_object_get(val, "id");

	if (json_is_null(id_val) || !id_val) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC non method decode failed: %s", ss);

		free(ss);

		goto out;
	}

	id = json_integer_value(id_val);

	mutex_lock(&sshare_lock);
	HASH_FIND_INT(stratum_shares, &id, sshare);
	if (sshare) {
		HASH_DEL(stratum_shares, sshare);
		pool->sshares--;
	}
	mutex_unlock(&sshare_lock);

	if (!sshare) {
		double pool_diff;

		if (!res_val)
			goto out;
		/* Since the share is untracked, we can only guess at what the
		 * work difficulty is based on the current pool diff. */
		cg_rlock(&pool->data_lock);
		pool_diff = pool->sdiff;
		cg_runlock(&pool->data_lock);

		if (json_is_true(res_val)) {
			applog(LOG_NOTICE, "Accepted untracked stratum share from pool %d", pool->pool_no);

			/* We don't know what device this came from so we can't
			 * attribute the work to the relevant cgpu */
			mutex_lock(&stats_lock);
			total_accepted++;
			pool->accepted++;
			total_diff_accepted += pool_diff;
			pool->diff_accepted += pool_diff;
			mutex_unlock(&stats_lock);
		} else {
			applog(LOG_NOTICE, "Rejected untracked stratum share from pool %d", pool->pool_no);

			mutex_lock(&stats_lock);
			total_rejected++;
			pool->rejected++;
			total_diff_rejected += pool_diff;
			pool->diff_rejected += pool_diff;
			mutex_unlock(&stats_lock);
		}
		goto out;
	}
	stratum_share_result(val, res_val, err_val, sshare);
	free_work(sshare->work);
	free(sshare);

	ret = true;
out:
	if (val)
		json_decref(val);

	return ret;
}

void clear_stratum_shares(struct pool *pool)
{
	struct stratum_share *sshare, *tmpshare;
	double diff_cleared = 0;
	int cleared = 0;

	mutex_lock(&sshare_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		if (sshare->work->pool == pool) {
			HASH_DEL(stratum_shares, sshare);
			diff_cleared += sshare->work->work_difficulty;
			free_work(sshare->work);
			pool->sshares--;
			free(sshare);
			cleared++;
		}
	}
	mutex_unlock(&sshare_lock);

	if (cleared) {
		applog(LOG_WARNING, "Lost %d shares due to stratum disconnect on pool %d", cleared, pool->pool_no);
		pool->stale_shares += cleared;
		total_stale += cleared;
		pool->diff_stale += diff_cleared;
		total_diff_stale += diff_cleared;
	}
}

void clear_pool_work(struct pool *pool)
{
	struct work *work, *tmp;
	int cleared = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (work->pool == pool) {
			HASH_DEL(staged_work, work);
			free_work(work);
			cleared++;
		}
	}
	mutex_unlock(stgd_lock);

	if (cleared)
		applog(LOG_INFO, "Cleared %d work items due to stratum disconnect on pool %d", cleared, pool->pool_no);
}

static int cp_prio(void)
{
	int prio;

	cg_rlock(&control_lock);
	prio = currentpool->prio;
	cg_runlock(&control_lock);

	return prio;
}

/* We only need to maintain a secondary pool connection when we need the
 * capacity to get work from the backup pools while still on the primary */
static bool cnx_needed(struct pool *pool)
{
	struct pool *cp;

	if (pool->enabled != POOL_ENABLED)
		return false;

	/* Balance strategies need all pools online */
	if (pool_strategy == POOL_BALANCE)
		return true;
	if (pool_strategy == POOL_LOADBALANCE)
		return true;

	/* Idle stratum pool needs something to kick it alive again */
	if (pool->has_stratum && pool->idle)
		return true;

	/* Getwork pools without opt_fail_only need backup pools up to be able
	 * to leak shares */
	cp = current_pool();
	if (cp == pool)
		return true;
	if (!pool_localgen(cp) && (!opt_fail_only || !cp->hdr_path))
		return true;
	/* If we're waiting for a response from shares submitted, keep the
	 * connection open. */
	if (pool->sshares)
		return true;
	/* If the pool has only just come to life and is higher priority than
	 * the current pool keep the connection open so we can fail back to
	 * it. */
	if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
		return true;
	/* We've run out of work, bring anything back to life. */
	if (no_work)
		return true;
	return false;
}

static void wait_lpcurrent(struct pool *pool);
static void pool_resus(struct pool *pool);
static void gen_stratum_work(struct pool *pool, struct work *work);

void stratum_resumed(struct pool *pool)
{
	if (pool_tclear(pool, &pool->idle)) {
		applog(LOG_INFO, "Stratum connection to pool %d resumed", pool->pool_no);
		pool_resus(pool);
	}
}

static bool supports_resume(struct pool *pool)
{
	bool ret;

	cg_rlock(&pool->data_lock);
	ret = (pool->sessionid != NULL);
	cg_runlock(&pool->data_lock);

	return ret;
}

/* One stratum receive thread per pool that has stratum waits on the socket
 * checking for new messages and for the integrity of the socket connection. We
 * reset the connection based on the integrity of the receive side only as the
 * send side will eventually expire data it fails to send. */
static void *stratum_rthread(void *userdata)
{
	struct pool *pool = (struct pool *)userdata;
	char threadname[16];

	pthread_detach(pthread_self());

	snprintf(threadname, sizeof(threadname), "%d/RStratum", pool->pool_no);
	RenameThread(threadname);

	while (42) {
		struct timeval timeout;
		int sel_ret;
		fd_set rd;
		char *s;

		if (unlikely(pool->removed)) {
			suspend_stratum(pool);
			break;
		}

		/* Check to see whether we need to maintain this connection
		 * indefinitely or just bring it up when we switch to this
		 * pool */
		if (!sock_full(pool) && !cnx_needed(pool)) {
			suspend_stratum(pool);
			clear_stratum_shares(pool);
			clear_pool_work(pool);

			wait_lpcurrent(pool);
			while (!restart_stratum(pool)) {
				if (pool->removed)
					goto out;
				if (enabled_pools > 1)
					cgsleep_ms(30000);
				else
					cgsleep_ms(3000);
			}
		}

		FD_ZERO(&rd);
		FD_SET(pool->sock, &rd);
		timeout.tv_sec = 90;
		timeout.tv_usec = 0;

		/* The protocol specifies that notify messages should be sent
		 * every minute so if we fail to receive any for 90 seconds we
		 * assume the connection has been dropped and treat this pool
		 * as dead */
		if (!sock_full(pool) && (sel_ret = select(pool->sock + 1, &rd, NULL, NULL, &timeout)) < 1) {
			applog(LOG_DEBUG, "Stratum select failed on pool %d with value %d", pool->pool_no, sel_ret);
			s = NULL;
		} else
			s = recv_line(pool);
		if (!s) {
			applog(LOG_NOTICE, "Stratum connection to pool %d interrupted", pool->pool_no);
			pool->getfail_occasions++;
			total_go++;

			/* If the socket to our stratum pool disconnects, all
			 * tracked submitted shares are lost and we will leak
			 * the memory if we don't discard their records. */
			if (!supports_resume(pool) || opt_lowmem)
				clear_stratum_shares(pool);
			clear_pool_work(pool);
			if (pool == current_pool())
				restart_threads();

			while (!restart_stratum(pool)) {
				if (pool->removed)
					goto out;
				cgsleep_ms(30000);
			}
			continue;
		}

		/* Check this pool hasn't died while being a backup pool and
		 * has not had its idle flag cleared */
		stratum_resumed(pool);

		if (!parse_method(pool, s) && !parse_stratum_response(pool, s))
			applog(LOG_INFO, "Unknown stratum msg: %s", s);
		else if (pool->swork.clean) {
			struct work *work = make_work();

			/* Generate a single work item to update the current
			 * block database */
			pool->swork.clean = false;
			gen_stratum_work(pool, work);
			work->longpoll = true;
			/* Return value doesn't matter. We're just informing
			 * that we may need to restart. */
			test_work_current(work);
			free_work(work);
		}
		free(s);
	}

out:
	return NULL;
}

/* Each pool has one stratum send thread for sending shares to avoid many
 * threads being created for submission since all sends need to be serialised
 * anyway. */
static void *stratum_sthread(void *userdata)
{
	struct pool *pool = (struct pool *)userdata;
	uint64_t last_nonce2 = 0;
	uint32_t last_nonce = 0;
	char threadname[16];

	pthread_detach(pthread_self());

	snprintf(threadname, sizeof(threadname), "%d/SStratum", pool->pool_no);
	RenameThread(threadname);

	pool->stratum_q = tq_new();
	if (!pool->stratum_q)
		quit(1, "Failed to create stratum_q in stratum_sthread");

	while (42) {
		char noncehex[12], nonce2hex[20], s[1024];
		struct stratum_share *sshare;
		uint32_t *hash32, nonce;
		unsigned char nonce2[8];
		uint64_t *nonce2_64;
		struct work *work;
		bool submitted;

		if (unlikely(pool->removed))
			break;

		work = tq_pop(pool->stratum_q, NULL);
		if (unlikely(!work))
			quit(1, "Stratum q returned empty work");

		if (unlikely(work->nonce2_len > 8)) {
			applog(LOG_ERR, "Pool %d asking for inappropriately long nonce2 length %d",
			       pool->pool_no, (int)work->nonce2_len);
			applog(LOG_ERR, "Not attempting to submit shares");
			free_work(work);
			continue;
		}

		nonce = *((uint32_t *)(work->data + 76));
		nonce2_64 = (uint64_t *)nonce2;
		*nonce2_64 = htole64(work->nonce2);
		/* Filter out duplicate shares */
		if (unlikely(nonce == last_nonce && *nonce2_64 == last_nonce2)) {
			applog(LOG_INFO, "Filtering duplicate share to pool %d",
			       pool->pool_no);
			free_work(work);
			continue;
		}
		last_nonce = nonce;
		last_nonce2 = *nonce2_64;
		__bin2hex(noncehex, (const unsigned char *)&nonce, 4);
		__bin2hex(nonce2hex, nonce2, work->nonce2_len);

		sshare = calloc(sizeof(struct stratum_share), 1);
		hash32 = (uint32_t *)work->hash;
		submitted = false;

		sshare->sshare_time = time(NULL);
		/* This work item is freed in parse_stratum_response */
		sshare->work = work;
		memset(s, 0, 1024);

		mutex_lock(&sshare_lock);
		/* Give the stratum share a unique id */
		sshare->id = swork_id++;
		mutex_unlock(&sshare_lock);

		snprintf(s, sizeof(s),
			"{\"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"], \"id\": %d, \"method\": \"mining.submit\"}",
			pool->rpc_user, work->job_id, nonce2hex, work->ntime, noncehex, sshare->id);

		applog(LOG_INFO, "Submitting share %08lx to pool %d",
					(long unsigned int)htole32(hash32[6]), pool->pool_no);

		/* Try resubmitting for up to 2 minutes if we fail to submit
		 * once and the stratum pool nonce1 still matches suggesting
		 * we may be able to resume. */
		while (time(NULL) < sshare->sshare_time + 120) {
			bool sessionid_match;

			if (likely(stratum_send(pool, s, strlen(s)))) {
				mutex_lock(&sshare_lock);
				HASH_ADD_INT(stratum_shares, id, sshare);
				pool->sshares++;
				mutex_unlock(&sshare_lock);

				if (pool_tclear(pool, &pool->submit_fail))
						applog(LOG_WARNING, "Pool %d communication resumed, submitting work", pool->pool_no);
				applog(LOG_DEBUG, "Successfully submitted, adding to stratum_shares db");
				submitted = true;
				break;
			}
			if (!pool_tset(pool, &pool->submit_fail) && cnx_needed(pool)) {
				applog(LOG_WARNING, "Pool %d stratum share submission failure", pool->pool_no);
				total_ro++;
				pool->remotefail_occasions++;
			}

			if (opt_lowmem) {
				applog(LOG_DEBUG, "Lowmem option prevents resubmitting stratum share");
				break;
			}

			cg_rlock(&pool->data_lock);
			sessionid_match = (pool->nonce1 && !strcmp(work->nonce1, pool->nonce1));
			cg_runlock(&pool->data_lock);

			if (!sessionid_match) {
				applog(LOG_DEBUG, "No matching session id for resubmitting stratum share");
				break;
			}
			/* Retry every 5 seconds */
			sleep(5);
		}

		if (unlikely(!submitted)) {
			applog(LOG_DEBUG, "Failed to submit stratum share, discarding");
			free_work(work);
			free(sshare);
			pool->stale_shares++;
			total_stale++;
		} else {
			int ssdiff;

			sshare->sshare_sent = time(NULL);
			ssdiff = sshare->sshare_sent - sshare->sshare_time;
			if (opt_debug || ssdiff > 0) {
				applog(LOG_INFO, "Pool %d stratum share submission lag time %d seconds",
				       pool->pool_no, ssdiff);
			}
		}
	}

	/* Freeze the work queue but don't free up its memory in case there is
	 * work still trying to be submitted to the removed pool. */
	tq_freeze(pool->stratum_q);

	return NULL;
}

static void init_stratum_threads(struct pool *pool)
{
	have_longpoll = true;

	if (unlikely(pthread_create(&pool->stratum_sthread, NULL, stratum_sthread, (void *)pool)))
		quit(1, "Failed to create stratum sthread");
	if (unlikely(pthread_create(&pool->stratum_rthread, NULL, stratum_rthread, (void *)pool)))
		quit(1, "Failed to create stratum rthread");
}

static void *longpoll_thread(void *userdata);

static bool stratum_works(struct pool *pool)
{
	applog(LOG_INFO, "Testing pool %d stratum %s", pool->pool_no, pool->stratum_url);
	if (!extract_sockaddr(pool->stratum_url, &pool->sockaddr_url, &pool->stratum_port))
		return false;

	if (!initiate_stratum(pool))
		return false;

	return true;
}

#ifdef HAVE_LIBCURL
static void __setup_gbt_solo(struct pool *pool)
{
	cg_wlock(&pool->gbt_lock);
	memcpy(pool->coinbase, scriptsig_header_bin, 41);
	pool->coinbase[41 + pool->n1_len + 4 + 1 + 8] = 25;
	memcpy(pool->coinbase + 41 + pool->n1_len + 4 + 1 + 8 + 1, pool->script_pubkey, 25);
	cg_wunlock(&pool->gbt_lock);
}

static bool setup_gbt_solo(CURL *curl, struct pool *pool)
{
	char s[256];
	int uninitialised_var(rolltime);
	bool ret = false;
	json_t *val = NULL, *res_val, *valid_val;

	if (!opt_btc_address) {
		applog(LOG_ERR, "No BTC address specified, unable to mine solo on %s",
		       pool->rpc_url);
		goto out;
	}
	snprintf(s, 256, "{\"method\": \"validateaddress\", \"params\": [\"%s\"]}\n", opt_btc_address);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, s, true,
			    false, &rolltime, pool, false);
	if (!val)
		goto out;
	res_val = json_object_get(val, "result");
	if (!res_val)
		goto out;
	valid_val = json_object_get(res_val, "isvalid");
	if (!valid_val)
		goto out;
	if (!json_is_true(valid_val)) {
		applog(LOG_ERR, "Bitcoin address %s is NOT valid", opt_btc_address);
		goto out;
	}
	applog(LOG_NOTICE, "Solo mining to valid address: %s", opt_btc_address);
	ret = true;
	address_to_pubkeyhash(pool->script_pubkey, opt_btc_address);
	hex2bin(scriptsig_header_bin, scriptsig_header, 41);
	__setup_gbt_solo(pool);

	if (opt_debug) {
		char *cb = bin2hex(pool->coinbase, pool->coinbase_len);

		applog(LOG_DEBUG, "Pool %d coinbase %s", pool->pool_no, cb);
		free(cb);
	}
	pool->gbt_curl = curl_easy_init();
	if (unlikely(!pool->gbt_curl))
		quit(1, "GBT CURL initialisation failed");

out:
	if (val)
		json_decref(val);
	return ret;
}
#else
static bool setup_gbt_solo(CURL __maybe_unused *curl, struct pool __maybe_unused *pool)
{
	return false;
}
#endif

static void pool_start_lp(struct pool *pool)
{
	if (!pool->lp_started) {
		pool->lp_started = true;
		if (unlikely(pthread_create(&pool->longpoll_thread, NULL, longpoll_thread, (void *)pool)))
			quit(1, "Failed to create pool longpoll thread");
	}
}

static bool pool_active(struct pool *pool, bool pinging)
{
	struct timeval tv_getwork, tv_getwork_reply;
	json_t *val = NULL;
	bool ret = false;
	CURL *curl;
	int uninitialised_var(rolltime);

	if (pool->has_gbt)
		applog(LOG_DEBUG, "Retrieving block template from pool %s", pool->rpc_url);
	else
		applog(LOG_INFO, "Testing pool %s", pool->rpc_url);

	/* This is the central point we activate stratum when we can */
retry_stratum:
	if (pool->has_stratum) {
		/* We create the stratum thread for each pool just after
		 * successful authorisation. Once the init flag has been set
		 * we never unset it and the stratum thread is responsible for
		 * setting/unsetting the active flag */
		bool init = pool_tset(pool, &pool->stratum_init);

		if (!init) {
			bool ret = initiate_stratum(pool) && auth_stratum(pool);

			if (ret)
				init_stratum_threads(pool);
			else
				pool_tclear(pool, &pool->stratum_init);
			return ret;
		}
		return pool->stratum_active;
	}

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return false;
	}

	/* Probe for GBT support on first pass */
	if (!pool->probed) {
		applog(LOG_DEBUG, "Probing for GBT support");
		val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass,
				    gbt_req, true, false, &rolltime, pool, false);
		if (val) {
			bool append = false, submit = false, transactions = false;
			json_t *res_val, *mutables;
			int i, mutsize = 0;

			res_val = json_object_get(val, "result");
			if (res_val) {
				mutables = json_object_get(res_val, "mutable");
				mutsize = json_array_size(mutables);
			}

			for (i = 0; i < mutsize; i++) {
				json_t *arrval = json_array_get(mutables, i);

				if (json_is_string(arrval)) {
					const char *mutable = json_string_value(arrval);

					if (!strncasecmp(mutable, "coinbase/append", 15))
						append = true;
					else if (!strncasecmp(mutable, "submit/coinbase", 15))
						submit = true;
					else if (!strncasecmp(mutable, "transactions", 12))
						transactions = true;
				}
			}
			json_decref(val);

			/* Only use GBT if it supports coinbase append and
			 * submit coinbase */
			if (append && submit) {
				pool->has_gbt = true;
				pool->rpc_req = gbt_req;
			} else if (transactions) {
				pool->gbt_solo = true;
				pool->rpc_req = gbt_solo_req;
			}
		}
		/* Reset this so we can probe fully just after this. It will be
		 * set to true that time.*/
		pool->probed = false;

		if (pool->has_gbt)
			applog(LOG_DEBUG, "GBT coinbase + append support found, switching to GBT protocol");
		else if (pool->gbt_solo)
			applog(LOG_DEBUG, "GBT coinbase without append found, switching to GBT solo protocol");
		else
			applog(LOG_DEBUG, "No GBT coinbase + append support found, using getwork protocol");
	}

	cgtime(&tv_getwork);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass,
			    pool->rpc_req, true, false, &rolltime, pool, false);
	cgtime(&tv_getwork_reply);

	/* Detect if a http getwork pool has an X-Stratum header at startup,
	 * and if so, switch to that in preference to getwork if it works */
	if (pool->stratum_url && !opt_fix_protocol && stratum_works(pool)) {
		applog(LOG_NOTICE, "Switching pool %d %s to %s", pool->pool_no, pool->rpc_url, pool->stratum_url);
		if (!pool->rpc_url)
			pool->rpc_url = strdup(pool->stratum_url);
		pool->has_stratum = true;
		curl_easy_cleanup(curl);

		goto retry_stratum;
	}

	if (val) {
		struct work *work = make_work();
		bool rc;

		rc = work_decode(pool, work, val);
		if (rc) {
			if (pool->gbt_solo) {
				ret = setup_gbt_solo(curl, pool);
				if (ret)
					pool_start_lp(pool);
				free_work(work);
				goto out;
			}
			applog(LOG_DEBUG, "Successfully retrieved and deciphered work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			work->pool = pool;
			work->rolltime = rolltime;
			copy_time(&work->tv_getwork, &tv_getwork);
			copy_time(&work->tv_getwork_reply, &tv_getwork_reply);
			work->getwork_mode = GETWORK_MODE_TESTPOOL;
			calc_diff(work, 0);
			applog(LOG_DEBUG, "Pushing pooltest work to base pool");

			stage_work(work);
			total_getworks++;
			pool->getwork_requested++;
			ret = true;
		} else {
			applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			free_work(work);
		}

		if (pool->lp_url)
			goto out;

		/* Decipher the longpoll URL, if any, and store it in ->lp_url */
		if (pool->hdr_path) {
			char *copy_start, *hdr_path;
			bool need_slash = false;
			size_t siz;

			hdr_path = pool->hdr_path;
			if (strstr(hdr_path, "://")) {
				pool->lp_url = hdr_path;
				hdr_path = NULL;
			} else {
				/* absolute path, on current server */
				copy_start = (*hdr_path == '/') ? (hdr_path + 1) : hdr_path;
				if (pool->rpc_url[strlen(pool->rpc_url) - 1] != '/')
					need_slash = true;

				siz = strlen(pool->rpc_url) + strlen(copy_start) + 2;
				pool->lp_url = malloc(siz);
				if (!pool->lp_url) {
					applog(LOG_ERR, "Malloc failure in pool_active");
					return false;
				}

				snprintf(pool->lp_url, siz, "%s%s%s", pool->rpc_url, need_slash ? "/" : "", copy_start);
			}
		} else
			pool->lp_url = NULL;

		pool_start_lp(pool);
	} else {
		/* If we failed to parse a getwork, this could be a stratum
		 * url without the prefix stratum+tcp:// so let's check it */
		if (initiate_stratum(pool)) {
			pool->has_stratum = true;
			goto retry_stratum;
		}
		applog(LOG_DEBUG, "FAILED to retrieve work from pool %u %s",
		       pool->pool_no, pool->rpc_url);
		if (!pinging && !pool->idle)
			applog(LOG_WARNING, "Pool %u slow/down or URL or credentials invalid", pool->pool_no);
	}
out:
	if (val)
		json_decref(val);
	curl_easy_cleanup(curl);
	return ret;
}

static void pool_resus(struct pool *pool)
{
	pool->seq_getfails = 0;
	if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
		applog(LOG_WARNING, "Pool %d %s alive, testing stability", pool->pool_no, pool->rpc_url);
	else
		applog(LOG_INFO, "Pool %d %s alive", pool->pool_no, pool->rpc_url);
}

static bool work_filled;
static bool work_emptied;

/* If this is called non_blocking, it will return NULL for work so that must
 * be handled. */
static struct work *hash_pop(bool blocking)
{
	struct work *work = NULL, *tmp;
	int hc;

	mutex_lock(stgd_lock);
	if (!HASH_COUNT(staged_work)) {
		/* Increase the queue if we reach zero and we know we can reach
		 * the maximum we're asking for. */
		if (work_filled && max_queue < opt_queue) {
			max_queue++;
			work_filled = false;
		}
		work_emptied = true;
		if (!blocking)
			goto out_unlock;
		do {
			struct timespec then;
			struct timeval now;
			int rc;

			cgtime(&now);
			then.tv_sec = now.tv_sec + 10;
			then.tv_nsec = now.tv_usec * 1000;
			pthread_cond_signal(&gws_cond);
			rc = pthread_cond_timedwait(&getq->cond, stgd_lock, &then);
			/* Check again for !no_work as multiple threads may be
				* waiting on this condition and another may set the
				* bool separately. */
			if (rc && !no_work) {
				no_work = true;
				applog(LOG_WARNING, "Waiting for work to be available from pools.");
			}
		} while (!HASH_COUNT(staged_work));
	}

	if (no_work) {
		applog(LOG_WARNING, "Work available from pools, resuming.");
		no_work = false;
	}

	hc = HASH_COUNT(staged_work);
	/* Find clone work if possible, to allow masters to be reused */
	if (hc > staged_rollable) {
		HASH_ITER(hh, staged_work, work, tmp) {
			if (!work_rollable(work))
				break;
		}
	} else
		work = staged_work;
	HASH_DEL(staged_work, work);
	if (work_rollable(work))
		staged_rollable--;

	/* Signal the getwork scheduler to look for more work */
	pthread_cond_signal(&gws_cond);

	/* Signal hash_pop again in case there are mutliple hash_pop waiters */
	pthread_cond_signal(&getq->cond);

	/* Keep track of last getwork grabbed */
	last_getwork = time(NULL);
out_unlock:
	mutex_unlock(stgd_lock);

	return work;
}

static void gen_hash(unsigned char *data, unsigned char *hash, int len)
{
	unsigned char hash1[32];

	sha256(data, len, hash1);
	sha256(hash1, 32, hash);
}

void set_target(unsigned char *dest_target, double diff)
{
	unsigned char target[32];
	uint64_t *data64, h64;
	double d64, dcut64;

	if (unlikely(diff == 0.0)) {
		/* This shouldn't happen but best we check to prevent a crash */
		applog(LOG_ERR, "Diff zero passed to set_target");
		diff = 1.0;
	}

	d64 = truediffone;
	d64 /= diff;

	dcut64 = d64 / bits192;
	h64 = dcut64;
	data64 = (uint64_t *)(target + 24);
	*data64 = htole64(h64);
	dcut64 = h64;
	dcut64 *= bits192;
	d64 -= dcut64;

	dcut64 = d64 / bits128;
	h64 = dcut64;
	data64 = (uint64_t *)(target + 16);
	*data64 = htole64(h64);
	dcut64 = h64;
	dcut64 *= bits128;
	d64 -= dcut64;

	dcut64 = d64 / bits64;
	h64 = dcut64;
	data64 = (uint64_t *)(target + 8);
	*data64 = htole64(h64);
	dcut64 = h64;
	dcut64 *= bits64;
	d64 -= dcut64;

	h64 = d64;
	data64 = (uint64_t *)(target);
	*data64 = htole64(h64);

	if (opt_debug) {
		char *htarget = bin2hex(target, 32);

		applog(LOG_DEBUG, "Generated target %s", htarget);
		free(htarget);
	}
	memcpy(dest_target, target, 32);
}

#if defined (USE_AVALON2) || defined (USE_AVALON4) || defined (USE_HASHRATIO)
bool submit_nonce2_nonce(struct thr_info *thr, struct pool *pool, struct pool *real_pool,
			 uint32_t nonce2, uint32_t nonce,  uint32_t ntime)
{
	const int thr_id = thr->id;
	struct cgpu_info *cgpu = thr->cgpu;
	struct device_drv *drv = cgpu->drv;
	struct work *work = make_work();
	bool ret;

	cg_wlock(&pool->data_lock);
	pool->nonce2 = nonce2;
	cg_wunlock(&pool->data_lock);

	gen_stratum_work(pool, work);
	while (ntime--) {
		roll_work(work);
	}

	work->pool = real_pool;

	work->thr_id = thr_id;
	work->work_block = work_block;
	work->pool->works++;

	work->mined = true;
	work->device_diff = MIN(drv->max_diff, work->work_difficulty);
	work->device_diff = MAX(drv->min_diff, work->device_diff);

	ret = submit_nonce(thr, work, nonce);
	free_work(work);
	return ret;
}
#endif

/* Generates stratum based work based on the most recent notify information
 * from the pool. This will keep generating work while a pool is down so we use
 * other means to detect when the pool has died in stratum_thread */
static void gen_stratum_work(struct pool *pool, struct work *work)
{
	unsigned char merkle_root[32], merkle_sha[64];
	uint32_t *data32, *swap32;
	uint64_t nonce2le;
	int i;

	cg_wlock(&pool->data_lock);

	/* Update coinbase. Always use an LE encoded nonce2 to fill in values
	 * from left to right and prevent overflow errors with small n2sizes */
	nonce2le = htole64(pool->nonce2);
	memcpy(pool->coinbase + pool->nonce2_offset, &nonce2le, pool->n2size);
	work->nonce2 = pool->nonce2++;
	work->nonce2_len = pool->n2size;

	/* Downgrade to a read lock to read off the pool variables */
	cg_dwlock(&pool->data_lock);

	/* Generate merkle root */
	gen_hash(pool->coinbase, merkle_root, pool->coinbase_len);
	memcpy(merkle_sha, merkle_root, 32);
	for (i = 0; i < pool->merkles; i++) {
		memcpy(merkle_sha + 32, pool->swork.merkle_bin[i], 32);
		gen_hash(merkle_sha, merkle_root, 64);
		memcpy(merkle_sha, merkle_root, 32);
	}
	data32 = (uint32_t *)merkle_sha;
	swap32 = (uint32_t *)merkle_root;
	flip32(swap32, data32);

	/* Copy the data template from header_bin */
	memcpy(work->data, pool->header_bin, 112);
	memcpy(work->data + 36, merkle_root, 32);

	/* Store the stratum work diff to check it still matches the pool's
	 * stratum diff when submitting shares */
	work->sdiff = pool->sdiff;

	/* Copy parameters required for share submission */
	work->job_id = strdup(pool->swork.job_id);
	work->nonce1 = strdup(pool->nonce1);
	work->ntime = strdup(pool->ntime);
	cg_runlock(&pool->data_lock);

	if (opt_debug) {
		char *header, *merkle_hash;

		header = bin2hex(work->data, 112);
		merkle_hash = bin2hex((const unsigned char *)merkle_root, 32);
		applog(LOG_DEBUG, "Generated stratum merkle %s", merkle_hash);
		applog(LOG_DEBUG, "Generated stratum header %s", header);
		applog(LOG_DEBUG, "Work job_id %s nonce2 %"PRIu64" ntime %s", work->job_id,
		       work->nonce2, work->ntime);
		free(header);
		free(merkle_hash);
	}

	calc_midstate(work);
	set_target(work->target, work->sdiff);

	local_work++;
	work->pool = pool;
	work->stratum = true;
	work->nonce = 0;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_STRATUM;
	work->work_block = work_block;
	/* Nominally allow a driver to ntime roll 60 seconds */
	work->drv_rolllimit = 60;
	calc_diff(work, work->sdiff);

	cgtime(&work->tv_staged);
}

#ifdef HAVE_LIBCURL
static void gen_solo_work(struct pool *pool, struct work *work);

/* Use the one instance of gbt_curl, protecting the bool with the gbt_lock but
 * avoiding holding the lock once we've set the bool. */
static void get_gbt_curl(struct pool *pool, int poll)
{
	cg_ilock(&pool->gbt_lock);
	while (pool->gbt_curl_inuse) {
		cg_uilock(&pool->gbt_lock);
		cgsleep_ms(poll);
		cg_ilock(&pool->gbt_lock);
	}
	cg_ulock(&pool->gbt_lock);
	pool->gbt_curl_inuse = true;
	cg_wunlock(&pool->gbt_lock);
}

/* No need for locking here */
static inline void release_gbt_curl(struct pool *pool)
{
	pool->gbt_curl_inuse = false;
}

static void update_gbt_solo(struct pool *pool)
{
	struct work *work = make_work();
	int rolltime;
	json_t *val;

	get_gbt_curl(pool, 10);
retry:
	/* Bitcoind doesn't like many open RPC connections. */
	curl_easy_setopt(pool->gbt_curl, CURLOPT_FORBID_REUSE, 1);
	val = json_rpc_call(pool->gbt_curl, pool->rpc_url, pool->rpc_userpass, pool->rpc_req,
			    true, false, &rolltime, pool, false);

	if (likely(val)) {
		bool rc = work_decode(pool, work, val);

		if (rc) {
			__setup_gbt_solo(pool);
			gen_solo_work(pool, work);
			stage_work(work);
		} else
			free_work(work);
		json_decref(val);
	} else {
		applog(LOG_DEBUG, "Pool %d json_rpc_call failed on get gbt, retrying in 5s",
		       pool->pool_no);
		if (++pool->seq_getfails > 5) {
			pool_died(pool);
			goto out;
		}
		cgsleep_ms(5000);
		goto retry;
	}
out:
	release_gbt_curl(pool);
}

static void gen_solo_work(struct pool *pool, struct work *work)
{
	unsigned char merkle_root[32], merkle_sha[64];
	uint32_t *data32, *swap32;
	struct timeval now;
	uint64_t nonce2le;
	int i;

	cgtime(&now);
	if (now.tv_sec - pool->tv_lastwork.tv_sec > 60)
		update_gbt_solo(pool);

	cg_wlock(&pool->gbt_lock);

	/* Update coinbase. Always use an LE encoded nonce2 to fill in values
	 * from left to right and prevent overflow errors with small n2sizes */
	nonce2le = htole64(pool->nonce2);
	memcpy(pool->coinbase + pool->nonce2_offset, &nonce2le, pool->n2size);
	work->nonce2 = pool->nonce2++;
	work->nonce2_len = pool->n2size;
	work->gbt_txns = pool->transactions + 1;

	/* Downgrade to a read lock to read off the pool variables */
	cg_dwlock(&pool->gbt_lock);
	work->coinbase = bin2hex(pool->coinbase, pool->coinbase_len);
	/* Generate merkle root */
	gen_hash(pool->coinbase, merkle_root, pool->coinbase_len);
	memcpy(merkle_sha, merkle_root, 32);
	for (i = 0; i < pool->merkles; i++) {
		unsigned char *merkle_bin;

		merkle_bin = pool->merklebin + (i * 32);
		memcpy(merkle_sha + 32, merkle_bin, 32);
		gen_hash(merkle_sha, merkle_root, 64);
		memcpy(merkle_sha, merkle_root, 32);
	}
	data32 = (uint32_t *)merkle_sha;
	swap32 = (uint32_t *)merkle_root;
	flip32(swap32, data32);

	/* Copy the data template from header_bin */
	memcpy(work->data, pool->header_bin, 112);
	memcpy(work->data + 36, merkle_root, 32);

	work->sdiff = pool->sdiff;

	/* Copy parameters required for share submission */
	work->ntime = strdup(pool->ntime);
	memcpy(work->target, pool->gbt_target, 32);
	cg_runlock(&pool->gbt_lock);

	if (opt_debug) {
		char *header, *merkle_hash;

		header = bin2hex(work->data, 112);
		merkle_hash = bin2hex((const unsigned char *)merkle_root, 32);
		applog(LOG_DEBUG, "Generated GBT solo merkle %s", merkle_hash);
		applog(LOG_DEBUG, "Generated GBT solo header %s", header);
		applog(LOG_DEBUG, "Work nonce2 %"PRIu64" ntime %s", work->nonce2,
		       work->ntime);
		free(header);
		free(merkle_hash);
	}

	calc_midstate(work);

	local_work++;
	work->gbt = true;
	work->pool = pool;
	work->nonce = 0;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_SOLO;
	work->work_block = work_block;
	/* Nominally allow a driver to ntime roll 60 seconds */
	work->drv_rolllimit = 60;
	calc_diff(work, work->sdiff);

	cgtime(&work->tv_staged);
}
#endif

/* The time difference in seconds between when this device last got work via
 * get_work() and generated a valid share. */
int share_work_tdiff(struct cgpu_info *cgpu)
{
	return last_getwork - cgpu->last_device_valid_work;
}

static void set_benchmark_work(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->lodiff += cgpu->direction;
	if (cgpu->lodiff < 1)
		cgpu->direction = 1;
	if (cgpu->lodiff > 15) {
		cgpu->direction = -1;
		if (++cgpu->hidiff > 15)
			cgpu->hidiff = 0;
		memcpy(work, &bench_hidiff_bins[cgpu->hidiff][0], 160);
	} else
		memcpy(work, &bench_lodiff_bins[cgpu->lodiff][0], 160);
}

struct work *get_work(struct thr_info *thr, const int thr_id)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct work *work = NULL;
	time_t diff_t;

	thread_reportout(thr);
	applog(LOG_DEBUG, "Popping work from get queue to get work");
	diff_t = time(NULL);
	while (!work) {
		work = hash_pop(true);
		if (stale_work(work, false)) {
			discard_work(work);
			wake_gws();
		}
	}
	diff_t = time(NULL) - diff_t;
	/* Since this is a blocking function, we need to add grace time to
	 * the device's last valid work to not make outages appear to be
	 * device failures. */
	if (diff_t > 0) {
		applog(LOG_DEBUG, "Get work blocked for %d seconds", (int)diff_t);
		cgpu->last_device_valid_work += diff_t;
	}
	applog(LOG_DEBUG, "Got work from get queue to get work for thread %d", thr_id);

	work->thr_id = thr_id;
	if (opt_benchmark)
		set_benchmark_work(cgpu, work);

	thread_reportin(thr);
	work->mined = true;
	work->device_diff = MIN(cgpu->drv->max_diff, work->work_difficulty);
	work->device_diff = MAX(cgpu->drv->min_diff, work->device_diff);
	return work;
}

/* Submit a copy of the tested, statistic recorded work item asynchronously */
static void submit_work_async(struct work *work)
{
	struct pool *pool = work->pool;
	pthread_t submit_thread;

	cgtime(&work->tv_work_found);
	if (opt_benchmark) {
		struct cgpu_info *cgpu = get_thr_cgpu(work->thr_id);

		mutex_lock(&stats_lock);
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		cgpu->diff_accepted += work->work_difficulty;
		total_diff_accepted += work->work_difficulty;
		pool->diff_accepted += work->work_difficulty;
		mutex_unlock(&stats_lock);

		applog(LOG_NOTICE, "Accepted %s %d benchmark share nonce %08x",
		       cgpu->drv->name, cgpu->device_id, *(uint32_t *)(work->data + 64 + 12));
		return;
	}

	if (stale_work(work, true)) {
		if (opt_submit_stale)
			applog(LOG_NOTICE, "Pool %d stale share detected, submitting as user requested", pool->pool_no);
		else if (pool->submit_old)
			applog(LOG_NOTICE, "Pool %d stale share detected, submitting as pool requested", pool->pool_no);
		else {
			applog(LOG_NOTICE, "Pool %d stale share detected, discarding", pool->pool_no);
			sharelog("discard", work);

			mutex_lock(&stats_lock);
			total_stale++;
			pool->stale_shares++;
			total_diff_stale += work->work_difficulty;
			pool->diff_stale += work->work_difficulty;
			mutex_unlock(&stats_lock);

			free_work(work);
			return;
		}
		work->stale = true;
	}

	if (work->stratum) {
		applog(LOG_DEBUG, "Pushing pool %d work to stratum queue", pool->pool_no);
		if (unlikely(!tq_push(pool->stratum_q, work))) {
			applog(LOG_DEBUG, "Discarding work from removed pool");
			free_work(work);
		}
	} else {
		applog(LOG_DEBUG, "Pushing submit work to work thread");
		if (unlikely(pthread_create(&submit_thread, NULL, submit_work_thread, (void *)work)))
			quit(1, "Failed to create submit_work_thread");
	}
}

void inc_hw_errors(struct thr_info *thr)
{
	applog(LOG_INFO, "%s %d: invalid nonce - HW error", thr->cgpu->drv->name,
	       thr->cgpu->device_id);

	mutex_lock(&stats_lock);
	hw_errors++;
	thr->cgpu->hw_errors++;
	mutex_unlock(&stats_lock);

	thr->cgpu->drv->hw_error(thr);
}

/* Fills in the work nonce and builds the output data in work->hash */
static void rebuild_nonce(struct work *work, uint32_t nonce)
{
	uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);

	*work_nonce = htole32(nonce);

	regen_hash(work);
}

/* For testing a nonce against diff 1 */
bool test_nonce(struct work *work, uint32_t nonce)
{
	uint32_t *hash_32 = (uint32_t *)(work->hash + 28);

	rebuild_nonce(work, nonce);
	return (*hash_32 == 0);
}

/* For testing a nonce against an arbitrary diff */
bool test_nonce_diff(struct work *work, uint32_t nonce, double diff)
{
	uint64_t *hash64 = (uint64_t *)(work->hash + 24), diff64;

	rebuild_nonce(work, nonce);
	diff64 = 0x00000000ffff0000ULL;
	diff64 /= diff;

	return (le64toh(*hash64) <= diff64);
}

static void update_work_stats(struct thr_info *thr, struct work *work)
{
	double test_diff = current_diff;

	work->share_diff = share_diff(work);

	if (unlikely(work->share_diff >= test_diff)) {
		work->block = true;
		work->pool->solved++;
		found_blocks++;
		work->mandatory = true;
		applog(LOG_NOTICE, "Found block for pool %d!", work->pool->pool_no);
	}

	mutex_lock(&stats_lock);
	total_diff1 += work->device_diff;
	thr->cgpu->diff1 += work->device_diff;
	work->pool->diff1 += work->device_diff;
	thr->cgpu->last_device_valid_work = time(NULL);
	mutex_unlock(&stats_lock);
}

/* To be used once the work has been tested to be meet diff1 and has had its
 * nonce adjusted. Returns true if the work target is met. */
bool submit_tested_work(struct thr_info *thr, struct work *work)
{
	struct work *work_out;
	update_work_stats(thr, work);

	if (!fulltest(work->hash, work->target)) {
		applog(LOG_INFO, "%s %d: Share above target", thr->cgpu->drv->name,
		       thr->cgpu->device_id);
		return false;
	}
	work_out = copy_work(work);
	submit_work_async(work_out);
	return true;
}

/* Rudimentary test to see if cgpu has returned the same nonce twice in a row which is
 * always going to be a duplicate which should be reported as a hw error. */
static bool new_nonce(struct thr_info *thr, uint32_t nonce)
{
	struct cgpu_info *cgpu = thr->cgpu;

	if (unlikely(cgpu->last_nonce == nonce)) {
		applog(LOG_INFO, "%s %d duplicate share detected as HW error",
		       cgpu->drv->name, cgpu->device_id);
		return false;
	}
	cgpu->last_nonce = nonce;
	return true;
}

/* Returns true if nonce for work was a valid share and not a dupe of the very last
 * nonce submitted by this device. */
bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	if (new_nonce(thr, nonce) && test_nonce(work, nonce))
		submit_tested_work(thr, work);
	else {
		inc_hw_errors(thr);
		return false;
	}

	if (opt_benchfile && opt_benchfile_display)
		benchfile_dspwork(work, nonce);

	return true;
}

/* Allows drivers to submit work items where the driver has changed the ntime
 * value by noffset. Must be only used with a work protocol that does not ntime
 * roll itself intrinsically to generate work (eg stratum). We do not touch
 * the original work struct, but the copy of it only. */
bool submit_noffset_nonce(struct thr_info *thr, struct work *work_in, uint32_t nonce,
			  int noffset)
{
	struct work *work = make_work();
	bool ret = false;

	_copy_work(work, work_in, noffset);
	if (!test_nonce(work, nonce)) {
		free_work(work);
		inc_hw_errors(thr);
		goto out;
	}
	update_work_stats(thr, work);

	if (opt_benchfile && opt_benchfile_display)
		benchfile_dspwork(work, nonce);

	ret = true;
	if (!fulltest(work->hash, work->target)) {
		free_work(work);
		applog(LOG_INFO, "%s %d: Share above target", thr->cgpu->drv->name,
		       thr->cgpu->device_id);
		goto  out;
	}
	submit_work_async(work);

out:
	return ret;
}

static inline bool abandon_work(struct work *work, struct timeval *wdiff, uint64_t hashes)
{
	if (wdiff->tv_sec > opt_scantime || hashes >= 0xfffffffe ||
	    stale_work(work, false))
		return true;
	return false;
}

static void mt_disable(struct thr_info *mythr, const int thr_id,
		       struct device_drv *drv)
{
	applog(LOG_WARNING, "Thread %d being disabled", thr_id);
	mythr->cgpu->rolling = 0;
	applog(LOG_DEBUG, "Waiting on sem in miner thread");
	cgsem_wait(&mythr->sem);
	applog(LOG_WARNING, "Thread %d being re-enabled", thr_id);
	drv->thread_enable(mythr);
}

/* The main hashing loop for devices that are slow enough to work on one work
 * item at a time, without a queue, aborting work before the entire nonce
 * range has been hashed if needed. */
static void hash_sole_work(struct thr_info *mythr)
{
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	struct timeval getwork_start, tv_start, *tv_end, tv_workstart, tv_lastupdate;
	struct cgminer_stats *dev_stats = &(cgpu->cgminer_stats);
	struct cgminer_stats *pool_stats;
	/* Try to cycle approximately 5 times before each log update */
	const long cycle = opt_log_interval / 5 ? : 1;
	const bool primary = (!mythr->device_thread) || mythr->primary_thread;
	struct timeval diff, sdiff, wdiff = {0, 0};
	uint32_t max_nonce = drv->can_limit_work(mythr);
	int64_t hashes_done = 0;

	tv_end = &getwork_start;
	cgtime(&getwork_start);
	sdiff.tv_sec = sdiff.tv_usec = 0;
	cgtime(&tv_lastupdate);

	while (likely(!cgpu->shutdown)) {
		struct work *work = get_work(mythr, thr_id);
		int64_t hashes;

		mythr->work_restart = false;
		cgpu->new_work = true;

		cgtime(&tv_workstart);
		work->nonce = 0;
		cgpu->max_hashes = 0;
		if (!drv->prepare_work(mythr, work)) {
			applog(LOG_ERR, "work prepare failed, exiting "
				"mining thread %d", thr_id);
			break;
		}
		work->device_diff = MIN(drv->max_diff, work->work_difficulty);
		work->device_diff = MAX(drv->min_diff, work->device_diff);

		do {
			cgtime(&tv_start);

			subtime(&tv_start, &getwork_start);

			addtime(&getwork_start, &dev_stats->getwork_wait);
			if (time_more(&getwork_start, &dev_stats->getwork_wait_max))
				copy_time(&dev_stats->getwork_wait_max, &getwork_start);
			if (time_less(&getwork_start, &dev_stats->getwork_wait_min))
				copy_time(&dev_stats->getwork_wait_min, &getwork_start);
			dev_stats->getwork_calls++;

			pool_stats = &(work->pool->cgminer_stats);

			addtime(&getwork_start, &pool_stats->getwork_wait);
			if (time_more(&getwork_start, &pool_stats->getwork_wait_max))
				copy_time(&pool_stats->getwork_wait_max, &getwork_start);
			if (time_less(&getwork_start, &pool_stats->getwork_wait_min))
				copy_time(&pool_stats->getwork_wait_min, &getwork_start);
			pool_stats->getwork_calls++;

			cgtime(&(work->tv_work_start));

			/* Only allow the mining thread to be cancelled when
			 * it is not in the driver code. */
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			thread_reportin(mythr);
			hashes = drv->scanhash(mythr, work, work->nonce + max_nonce);
			thread_reportout(mythr);

			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			pthread_testcancel();

			/* tv_end is == &getwork_start */
			cgtime(&getwork_start);

			if (unlikely(hashes == -1)) {
				applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
				cgpu->deven = DEV_DISABLED;
				dev_error(cgpu, REASON_THREAD_ZERO_HASH);
				cgpu->shutdown = true;
				break;
			}

			hashes_done += hashes;
			if (hashes > cgpu->max_hashes)
				cgpu->max_hashes = hashes;

			timersub(tv_end, &tv_start, &diff);
			sdiff.tv_sec += diff.tv_sec;
			sdiff.tv_usec += diff.tv_usec;
			if (sdiff.tv_usec > 1000000) {
				++sdiff.tv_sec;
				sdiff.tv_usec -= 1000000;
			}

			timersub(tv_end, &tv_workstart, &wdiff);

			if (unlikely((long)sdiff.tv_sec < cycle)) {
				int mult;

				if (likely(max_nonce == 0xffffffff))
					continue;

				mult = 1000000 / ((sdiff.tv_usec + 0x400) / 0x400) + 0x10;
				mult *= cycle;
				if (max_nonce > (0xffffffff * 0x400) / mult)
					max_nonce = 0xffffffff;
				else
					max_nonce = (max_nonce * mult) / 0x400;
			} else if (unlikely(sdiff.tv_sec > cycle))
				max_nonce = max_nonce * cycle / sdiff.tv_sec;
			else if (unlikely(sdiff.tv_usec > 100000))
				max_nonce = max_nonce * 0x400 / (((cycle * 1000000) + sdiff.tv_usec) / (cycle * 1000000 / 0x400));

			timersub(tv_end, &tv_lastupdate, &diff);
			/* Update the hashmeter at most 5 times per second */
			if ((hashes_done && (diff.tv_sec > 0 || diff.tv_usec > 200000)) ||
			    diff.tv_sec >= opt_log_interval) {
				hashmeter(thr_id, hashes_done);
				hashes_done = 0;
				copy_time(&tv_lastupdate, tv_end);
			}

			if (unlikely(mythr->work_restart)) {
				/* Apart from device_thread 0, we stagger the
				 * starting of every next thread to try and get
				 * all devices busy before worrying about
				 * getting work for their extra threads */
				if (!primary) {
					struct timespec rgtp;

					rgtp.tv_sec = 0;
					rgtp.tv_nsec = 250 * mythr->device_thread * 1000000;
					nanosleep(&rgtp, NULL);
				}
				break;
			}

			if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
				mt_disable(mythr, thr_id, drv);

			sdiff.tv_sec = sdiff.tv_usec = 0;
		} while (!abandon_work(work, &wdiff, cgpu->max_hashes));
		free_work(work);
	}
	cgpu->deven = DEV_DISABLED;
}

/* Put a new unqueued work item in cgpu->unqueued_work under cgpu->qlock till
 * the driver tells us it's full so that it may extract the work item using
 * the get_queued() function which adds it to the hashtable on
 * cgpu->queued_work. */
static void fill_queue(struct thr_info *mythr, struct cgpu_info *cgpu, struct device_drv *drv, const int thr_id)
{
	do {
		bool need_work;

		/* Do this lockless just to know if we need more unqueued work. */
		need_work = (!cgpu->unqueued_work);

		/* get_work is a blocking function so do it outside of lock
		 * to prevent deadlocks with other locks. */
		if (need_work) {
			struct work *work = get_work(mythr, thr_id);

			wr_lock(&cgpu->qlock);
			/* Check we haven't grabbed work somehow between
			 * checking and picking up the lock. */
			if (likely(!cgpu->unqueued_work))
				cgpu->unqueued_work = work;
			else
				need_work = false;
			wr_unlock(&cgpu->qlock);

			if (unlikely(!need_work))
				discard_work(work);
		}
		/* The queue_full function should be used by the driver to
		 * actually place work items on the physical device if it
		 * does have a queue. */
	} while (!drv->queue_full(cgpu));
}

/* Add a work item to a cgpu's queued hashlist */
void __add_queued(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->queued_count++;
	HASH_ADD_INT(cgpu->queued_work, id, work);
}

struct work *__get_queued(struct cgpu_info *cgpu)
{
	struct work *work = NULL;

	if (cgpu->unqueued_work) {
		work = cgpu->unqueued_work;
		if (unlikely(stale_work(work, false))) {
			discard_work(work);
		} else
			__add_queued(cgpu, work);
		cgpu->unqueued_work = NULL;
		wake_gws();
	}

	return work;
}

/* This function is for retrieving one work item from the unqueued pointer and
 * adding it to the hashtable of queued work. Code using this function must be
 * able to handle NULL as a return which implies there is no work available. */
struct work *get_queued(struct cgpu_info *cgpu)
{
	struct work *work;

	wr_lock(&cgpu->qlock);
	work = __get_queued(cgpu);
	wr_unlock(&cgpu->qlock);

	return work;
}

void add_queued(struct cgpu_info *cgpu, struct work *work)
{
	wr_lock(&cgpu->qlock);
	__add_queued(cgpu, work);
	wr_unlock(&cgpu->qlock);
}

/* Get fresh work and add it to cgpu's queued hashlist */
struct work *get_queue_work(struct thr_info *thr, struct cgpu_info *cgpu, int thr_id)
{
	struct work *work = get_work(thr, thr_id);

	add_queued(cgpu, work);
	return work;
}

/* This function is for finding an already queued work item in the
 * given que hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The calling function must lock access to the que if it is required.
 * The common values for midstatelen, offset, datalen are 32, 64, 12 */
struct work *__find_work_bymidstate(struct work *que, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work, *tmp, *ret = NULL;

	HASH_ITER(hh, que, work, tmp) {
		if (memcmp(work->midstate, midstate, midstatelen) == 0 &&
		    memcmp(work->data + offset, data, datalen) == 0) {
			ret = work;
			break;
		}
	}

	return ret;
}

/* This function is for finding an already queued work item in the
 * device's queued_work hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The common values for midstatelen, offset, datalen are 32, 64, 12 */
struct work *find_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *ret;

	rd_lock(&cgpu->qlock);
	ret = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	rd_unlock(&cgpu->qlock);

	return ret;
}

struct work *clone_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work, *ret = NULL;

	rd_lock(&cgpu->qlock);
	work = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	if (work)
		ret = copy_work(work);
	rd_unlock(&cgpu->qlock);

	return ret;
}

/* This function is for finding an already queued work item in the
 * given que hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The calling function must lock access to the que if it is required. */
struct work *__find_work_byid(struct work *que, uint32_t id)
{
	struct work *work, *tmp, *ret = NULL;

	HASH_ITER(hh, que, work, tmp) {
		if (work->id == id) {
			ret = work;
			break;
		}
	}

	return ret;
}

struct work *find_queued_work_byid(struct cgpu_info *cgpu, uint32_t id)
{
	struct work *ret;

	rd_lock(&cgpu->qlock);
	ret = __find_work_byid(cgpu->queued_work, id);
	rd_unlock(&cgpu->qlock);

	return ret;
}

void __work_completed(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->queued_count--;
	HASH_DEL(cgpu->queued_work, work);
}

/* This iterates over a queued hashlist finding work started more than secs
 * seconds ago and discards the work as completed. The driver must set the
 * work->tv_work_start value appropriately. Returns the number of items aged. */
int age_queued_work(struct cgpu_info *cgpu, double secs)
{
	struct work *work, *tmp;
	struct timeval tv_now;
	int aged = 0;

	cgtime(&tv_now);

	wr_lock(&cgpu->qlock);
	HASH_ITER(hh, cgpu->queued_work, work, tmp) {
		if (tdiff(&tv_now, &work->tv_work_start) > secs) {
			__work_completed(cgpu, work);
			free_work(work);
			aged++;
		}
	}
	wr_unlock(&cgpu->qlock);

	return aged;
}

/* This function should be used by queued device drivers when they're sure
 * the work struct is no longer in use. */
void work_completed(struct cgpu_info *cgpu, struct work *work)
{
	wr_lock(&cgpu->qlock);
	__work_completed(cgpu, work);
	wr_unlock(&cgpu->qlock);

	free_work(work);
}

/* Combines find_queued_work_bymidstate and work_completed in one function
 * withOUT destroying the work so the driver must free it. */
struct work *take_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work;

	wr_lock(&cgpu->qlock);
	work = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	if (work)
		__work_completed(cgpu, work);
	wr_unlock(&cgpu->qlock);

	return work;
}

void flush_queue(struct cgpu_info *cgpu)
{
	struct work *work = NULL;

	if (unlikely(!cgpu))
		return;

	/* Use only a trylock in case we get into a deadlock with a queueing
	 * function holding the read lock when we're called. */
	if (wr_trylock(&cgpu->qlock))
		return;
	work = cgpu->unqueued_work;
	cgpu->unqueued_work = NULL;
	wr_unlock(&cgpu->qlock);

	if (work) {
		free_work(work);
		applog(LOG_DEBUG, "Discarded queued work item");
	}
}

/* This version of hash work is for devices that are fast enough to always
 * perform a full nonce range and need a queue to maintain the device busy.
 * Work creation and destruction is not done from within this function
 * directly. */
void hash_queued_work(struct thr_info *mythr)
{
	struct timeval tv_start = {0, 0}, tv_end;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	const int thr_id = mythr->id;
	int64_t hashes_done = 0;

	while (likely(!cgpu->shutdown)) {
		struct timeval diff;
		int64_t hashes;

		mythr->work_update = false;

		fill_queue(mythr, cgpu, drv, thr_id);

		hashes = drv->scanwork(mythr);

		/* Reset the bool here in case the driver looks for it
		 * synchronously in the scanwork loop. */
		mythr->work_restart = false;

		if (unlikely(hashes == -1 )) {
			applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
			cgpu->deven = DEV_DISABLED;
			dev_error(cgpu, REASON_THREAD_ZERO_HASH);
			break;
		}

		hashes_done += hashes;
		cgtime(&tv_end);
		timersub(&tv_end, &tv_start, &diff);
		/* Update the hashmeter at most 5 times per second */
		if ((hashes_done && (diff.tv_sec > 0 || diff.tv_usec > 200000)) ||
		    diff.tv_sec >= opt_log_interval) {
			hashmeter(thr_id, hashes_done);
			hashes_done = 0;
			copy_time(&tv_start, &tv_end);
		}

		if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
			mt_disable(mythr, thr_id, drv);

		if (mythr->work_update)
			drv->update_work(cgpu);
	}
	cgpu->deven = DEV_DISABLED;
}

/* This version of hash_work is for devices drivers that want to do their own
 * work management entirely, usually by using get_work(). Note that get_work
 * is a blocking function and will wait indefinitely if no work is available
 * so this must be taken into consideration in the driver. */
void hash_driver_work(struct thr_info *mythr)
{
	struct timeval tv_start = {0, 0}, tv_end;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	const int thr_id = mythr->id;
	int64_t hashes_done = 0;

	while (likely(!cgpu->shutdown)) {
		struct timeval diff;
		int64_t hashes;

		mythr->work_update = false;

		hashes = drv->scanwork(mythr);

		/* Reset the bool here in case the driver looks for it
		 * synchronously in the scanwork loop. */
		mythr->work_restart = false;

		if (unlikely(hashes == -1 )) {
			applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
			cgpu->deven = DEV_DISABLED;
			dev_error(cgpu, REASON_THREAD_ZERO_HASH);
			break;
		}

		hashes_done += hashes;
		cgtime(&tv_end);
		timersub(&tv_end, &tv_start, &diff);
		/* Update the hashmeter at most 5 times per second */
		if ((hashes_done && (diff.tv_sec > 0 || diff.tv_usec > 200000)) ||
		    diff.tv_sec >= opt_log_interval) {
			hashmeter(thr_id, hashes_done);
			hashes_done = 0;
			copy_time(&tv_start, &tv_end);
		}

		if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
			mt_disable(mythr, thr_id, drv);

		if (mythr->work_update)
			drv->update_work(cgpu);
	}
	cgpu->deven = DEV_DISABLED;
}

void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	char threadname[16];

        snprintf(threadname, sizeof(threadname), "%d/Miner", thr_id);
	RenameThread(threadname);

	thread_reportout(mythr);
	if (!drv->thread_init(mythr)) {
		dev_error(cgpu, REASON_THREAD_FAIL_INIT);
		goto out;
	}

	applog(LOG_DEBUG, "Waiting on sem in miner thread");
	cgsem_wait(&mythr->sem);

	cgpu->last_device_valid_work = time(NULL);
	drv->hash_work(mythr);
	drv->thread_shutdown(mythr);
out:
	return NULL;
}

enum {
	STAT_SLEEP_INTERVAL		= 1,
	STAT_CTR_INTERVAL		= 10000000,
	FAILURE_INTERVAL		= 30,
};

#ifdef HAVE_LIBCURL
/* Stage another work item from the work returned in a longpoll */
static void convert_to_work(json_t *val, int rolltime, struct pool *pool, struct timeval *tv_lp, struct timeval *tv_lp_reply)
{
	struct work *work;
	bool rc;

	work = make_work();

	rc = work_decode(pool, work, val);
	if (unlikely(!rc)) {
		applog(LOG_ERR, "Could not convert longpoll data to work");
		free_work(work);
		return;
	}
	total_getworks++;
	pool->getwork_requested++;
	work->pool = pool;
	work->rolltime = rolltime;
	copy_time(&work->tv_getwork, tv_lp);
	copy_time(&work->tv_getwork_reply, tv_lp_reply);
	calc_diff(work, 0);

	if (pool->enabled == POOL_REJECTING)
		work->mandatory = true;

	if (pool->has_gbt)
		gen_gbt_work(pool, work);
	work->longpoll = true;
	work->getwork_mode = GETWORK_MODE_LP;

	/* We'll be checking this work item twice, but we already know it's
	 * from a new block so explicitly force the new block detection now
	 * rather than waiting for it to hit the stage thread. This also
	 * allows testwork to know whether LP discovered the block or not. */
	test_work_current(work);

	/* Don't use backup LPs as work if we have failover-only enabled. Use
	 * the longpoll work from a pool that has been rejecting shares as a
	 * way to detect when the pool has recovered.
	 */
	if (pool != current_pool() && opt_fail_only && pool->enabled != POOL_REJECTING) {
		free_work(work);
		return;
	}

	work = clone_work(work);

	applog(LOG_DEBUG, "Pushing converted work to stage thread");

	stage_work(work);
	applog(LOG_DEBUG, "Converted longpoll data to work");
}

/* If we want longpoll, enable it for the chosen default pool, or, if
 * the pool does not support longpoll, find the first one that does
 * and use its longpoll support */
static struct pool *select_longpoll_pool(struct pool *cp)
{
	int i;

	if (cp->hdr_path || cp->has_gbt || cp->gbt_solo)
		return cp;
	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->has_stratum || pool->hdr_path)
			return pool;
	}
	return NULL;
}
#endif /* HAVE_LIBCURL */

/* This will make the longpoll thread wait till it's the current pool, or it
 * has been flagged as rejecting, before attempting to open any connections.
 */
static void wait_lpcurrent(struct pool *pool)
{
	while (!cnx_needed(pool) && (pool->enabled == POOL_DISABLED ||
	       (pool != current_pool() && pool_strategy != POOL_LOADBALANCE &&
	       pool_strategy != POOL_BALANCE))) {
		mutex_lock(&lp_lock);
		pthread_cond_wait(&lp_cond, &lp_lock);
		mutex_unlock(&lp_lock);
	}
}

#ifdef HAVE_LIBCURL
static void *longpoll_thread(void *userdata)
{
	struct pool *cp = (struct pool *)userdata;
	/* This *pool is the source of the actual longpoll, not the pool we've
	 * tied it to */
	struct timeval start, reply, end;
	struct pool *pool = NULL;
	char threadname[16];
	CURL *curl = NULL;
	int failures = 0;
	char lpreq[1024];
	char *lp_url;
	int rolltime;

	snprintf(threadname, sizeof(threadname), "%d/Longpoll", cp->pool_no);
	RenameThread(threadname);

retry_pool:
	pool = select_longpoll_pool(cp);
	if (!pool) {
		applog(LOG_WARNING, "No suitable long-poll found for %s", cp->rpc_url);
		while (!pool) {
			cgsleep_ms(60000);
			pool = select_longpoll_pool(cp);
		}
	}

	if (pool->has_stratum) {
		applog(LOG_WARNING, "Block change for %s detection via %s stratum",
		       cp->rpc_url, pool->rpc_url);
		goto out;
	}

	if (pool->gbt_solo) {
		applog(LOG_WARNING, "Block change for %s detection via getblockcount polling",
		       cp->rpc_url);
		while (42) {
			json_t *val, *res_val = NULL;

			if (unlikely(pool->removed))
				return NULL;

			cgtime(&start);
			wait_lpcurrent(cp);
			sprintf(lpreq, "{\"id\": 0, \"method\": \"getblockcount\"}\n");

			/* We will be making another call immediately after this
			 * one to get the height so allow this curl to be reused.*/
			get_gbt_curl(pool, 500);
			curl_easy_setopt(pool->gbt_curl, CURLOPT_FORBID_REUSE, 0);
			val = json_rpc_call(pool->gbt_curl, pool->rpc_url, pool->rpc_userpass, lpreq, true,
					    false, &rolltime, pool, false);
			release_gbt_curl(pool);

			if (likely(val))
				res_val = json_object_get(val, "result");
			if (likely(res_val)) {
				int height = json_integer_value(res_val);
				const char *prev_hash;

				failures = 0;
				json_decref(val);
				if (height >= cp->height) {
					applog(LOG_WARNING, "Block height change to %d detected on pool %d",
					       height, cp->pool_no);
					update_gbt_solo(pool);
					continue;
				}

				sprintf(lpreq, "{\"id\": 0, \"method\": \"getblockhash\", \"params\": [%d]}\n", height);
				get_gbt_curl(pool, 500);
				curl_easy_setopt(pool->gbt_curl, CURLOPT_FORBID_REUSE, 1);
				val = json_rpc_call(pool->gbt_curl, pool->rpc_url, pool->rpc_userpass,
						    lpreq, true, false, &rolltime, pool, false);
				release_gbt_curl(pool);

				if (val) {
					/* Do a comparison on a short stretch of
					 * the hash to make sure it hasn't changed
					 * due to mining on an orphan branch. */
					prev_hash = json_string_value(json_object_get(val, "result"));
					if (unlikely(prev_hash && strncasecmp(prev_hash + 56, pool->prev_hash, 8))) {
						applog(LOG_WARNING, "Mining on orphan branch detected, switching!");
						update_gbt_solo(pool);
					}
					json_decref(val);
				}

				cgsleep_ms(500);
			} else {
				if (val)
					json_decref(val);
				cgtime(&end);
				if (end.tv_sec - start.tv_sec > 30)
					continue;
				if (failures == 1)
					applog(LOG_WARNING, "longpoll failed for %s, retrying every 30s", lp_url);
				cgsleep_ms(30000);
			}
		}
	}

	curl = curl_easy_init();
	if (unlikely(!curl))
		quit (1, "Longpoll CURL initialisation failed");

	/* Any longpoll from any pool is enough for this to be true */
	have_longpoll = true;

	wait_lpcurrent(cp);

	if (pool->has_gbt) {
		lp_url = pool->rpc_url;
		applog(LOG_WARNING, "GBT longpoll ID activated for %s", lp_url);
	} else {
		strcpy(lpreq, getwork_req);

		lp_url = pool->lp_url;
		if (cp == pool)
			applog(LOG_WARNING, "Long-polling activated for %s", lp_url);
		else
			applog(LOG_WARNING, "Long-polling activated for %s via %s", cp->rpc_url, lp_url);
	}

	while (42) {
		json_t *val, *soval;

		wait_lpcurrent(cp);

		cgtime(&start);

		/* Update the longpollid every time, but do it under lock to
		 * avoid races */
		if (pool->has_gbt) {
			cg_rlock(&pool->gbt_lock);
			snprintf(lpreq, sizeof(lpreq),
				"{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": "
				"[{\"capabilities\": [\"coinbasetxn\", \"workid\", \"coinbase/append\"], "
				"\"longpollid\": \"%s\"}]}\n", pool->longpollid);
			cg_runlock(&pool->gbt_lock);
		}

		/* Longpoll connections can be persistent for a very long time
		 * and any number of issues could have come up in the meantime
		 * so always establish a fresh connection instead of relying on
		 * a persistent one. */
		curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
		val = json_rpc_call(curl, lp_url, pool->rpc_userpass,
				    lpreq, false, true, &rolltime, pool, false);

		cgtime(&reply);

		if (likely(val)) {
			soval = json_object_get(json_object_get(val, "result"), "submitold");
			if (soval)
				pool->submit_old = json_is_true(soval);
			else
				pool->submit_old = false;
			convert_to_work(val, rolltime, pool, &start, &reply);
			failures = 0;
			json_decref(val);
		} else {
			/* Some pools regularly drop the longpoll request so
			 * only see this as longpoll failure if it happens
			 * immediately and just restart it the rest of the
			 * time. */
			cgtime(&end);
			if (end.tv_sec - start.tv_sec > 30)
				continue;
			if (failures == 1)
				applog(LOG_WARNING, "longpoll failed for %s, retrying every 30s", lp_url);
			cgsleep_ms(30000);
		}

		if (pool != cp) {
			pool = select_longpoll_pool(cp);
			if (pool->has_stratum) {
				applog(LOG_WARNING, "Block change for %s detection via %s stratum",
				       cp->rpc_url, pool->rpc_url);
				break;
			}
			if (unlikely(!pool))
				goto retry_pool;
		}

		if (unlikely(pool->removed))
			break;
	}

out:
	curl_easy_cleanup(curl);

	return NULL;
}
#else /* HAVE_LIBCURL */
static void *longpoll_thread(void __maybe_unused *userdata)
{
	pthread_detach(pthread_self());
	return NULL;
}
#endif /* HAVE_LIBCURL */

void reinit_device(struct cgpu_info *cgpu)
{
	if (cgpu->deven == DEV_DISABLED)
		return;

#ifdef USE_USBUTILS
	/* Attempt a usb device reset if the device has gone sick */
	if (cgpu->usbdev && cgpu->usbdev->handle)
		libusb_reset_device(cgpu->usbdev->handle);
#endif
	cgpu->drv->reinit_device(cgpu);
}

static struct timeval rotate_tv;

/* We reap curls if they are unused for over a minute */
static void reap_curl(struct pool *pool)
{
	struct curl_ent *ent, *iter;
	struct timeval now;
	int reaped = 0;

	cgtime(&now);

	mutex_lock(&pool->pool_lock);
	list_for_each_entry_safe(ent, iter, &pool->curlring, node) {
		if (pool->curls < 2)
			break;
		if (now.tv_sec - ent->tv.tv_sec > 300) {
			reaped++;
			pool->curls--;
			list_del(&ent->node);
			curl_easy_cleanup(ent->curl);
			free(ent);
		}
	}
	mutex_unlock(&pool->pool_lock);

	if (reaped)
		applog(LOG_DEBUG, "Reaped %d curl%s from pool %d", reaped, reaped > 1 ? "s" : "", pool->pool_no);
}

/* Prune old shares we haven't had a response about for over 2 minutes in case
 * the pool never plans to respond and we're just leaking memory. If we get a
 * response beyond that time they will be seen as untracked shares. */
static void prune_stratum_shares(struct pool *pool)
{
	struct stratum_share *sshare, *tmpshare;
	time_t current_time = time(NULL);
	int cleared = 0;

	mutex_lock(&sshare_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		if (sshare->work->pool == pool && current_time > sshare->sshare_time + 120) {
			HASH_DEL(stratum_shares, sshare);
			free_work(sshare->work);
			free(sshare);
			cleared++;
		}
	}
	mutex_unlock(&sshare_lock);

	if (cleared) {
		applog(LOG_WARNING, "Lost %d shares due to no stratum share response from pool %d",
		       cleared, pool->pool_no);
		pool->stale_shares += cleared;
		total_stale += cleared;
	}
}

static void *watchpool_thread(void __maybe_unused *userdata)
{
	int intervals = 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("Watchpool");

	set_lowprio();

	while (42) {
		struct timeval now;
		int i;

		if (++intervals > 20)
			intervals = 0;
		cgtime(&now);

		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			if (!opt_benchmark && !opt_benchfile) {
				reap_curl(pool);
				prune_stratum_shares(pool);
			}

			/* Get a rolling utility per pool over 10 mins */
			if (intervals > 19) {
				double shares = pool->diff1 - pool->last_shares;

				pool->last_shares = pool->diff1;
				pool->utility = (pool->utility + shares * 0.63) / 1.63;
				pool->shares = pool->utility;
			}

			if (pool->enabled == POOL_DISABLED)
				continue;

			/* Don't start testing a pool if its test thread
			 * from startup is still doing its first attempt. */
			if (unlikely(pool->testing))
				continue;

			/* Test pool is idle once every minute */
			if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 30) {
				if (pool_active(pool, true) && pool_tclear(pool, &pool->idle))
					pool_resus(pool);
				else
					cgtime(&pool->tv_idle);
			}

			/* Only switch pools if the failback pool has been
			 * alive for more than 5 minutes to prevent
			 * intermittently failing pools from being used. */
			if (!pool->idle && pool_strategy == POOL_FAILOVER && pool->prio < cp_prio() &&
			    now.tv_sec - pool->tv_idle.tv_sec > 300) {
				applog(LOG_WARNING, "Pool %d %s stable for 5 mins",
				       pool->pool_no, pool->rpc_url);
				switch_pools(NULL);
			}
		}

		if (current_pool()->idle)
			switch_pools(NULL);

		if (pool_strategy == POOL_ROTATE && now.tv_sec - rotate_tv.tv_sec > 60 * opt_rotate_period) {
			cgtime(&rotate_tv);
			switch_pools(NULL);
		}

		cgsleep_ms(30000);

	}
	return NULL;
}

/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
#define WATCHDOG_INTERVAL		2
#define WATCHDOG_SICK_TIME		120
#define WATCHDOG_DEAD_TIME		600
#define WATCHDOG_SICK_COUNT		(WATCHDOG_SICK_TIME/WATCHDOG_INTERVAL)
#define WATCHDOG_DEAD_COUNT		(WATCHDOG_DEAD_TIME/WATCHDOG_INTERVAL)

static void *watchdog_thread(void __maybe_unused *userdata)
{
	const unsigned int interval = WATCHDOG_INTERVAL;
	struct timeval zero_tv;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("Watchdog");

	set_lowprio();
	memset(&zero_tv, 0, sizeof(struct timeval));
	cgtime(&rotate_tv);

	while (1) {
		int i;
		struct timeval now;

		sleep(interval);

		discard_stale();

		hashmeter(-1, 0);

#ifdef HAVE_CURSES
		if (curses_active_locked()) {
			struct cgpu_info *cgpu;
			int count;

			change_logwinsize();
			curses_print_status();
			count = 0;
			for (i = 0; i < total_devices; i++) {
				cgpu = get_devices(i);
#ifndef USE_USBUTILS
				if (cgpu)
#else
				if (cgpu && !cgpu->usbinfo.nodev)
#endif
					curses_print_devstatus(cgpu, i, count++);
			}
#ifdef USE_USBUTILS
			for (i = 0; i < total_devices; i++) {
				cgpu = get_devices(i);
				if (cgpu && cgpu->usbinfo.nodev)
					curses_print_devstatus(cgpu, i, count++);
			}
#endif
			touchwin(statuswin);
			wrefresh(statuswin);
			touchwin(logwin);
			wrefresh(logwin);
			unlock_curses();
		}
#endif

		cgtime(&now);

		if (!sched_paused && !should_run()) {
			applog(LOG_WARNING, "Pausing execution as per stop time %02d:%02d scheduled",
			       schedstop.tm.tm_hour, schedstop.tm.tm_min);
			if (!schedstart.enable) {
				quit(0, "Terminating execution as planned");
				break;
			}

			applog(LOG_WARNING, "Will restart execution as scheduled at %02d:%02d",
			       schedstart.tm.tm_hour, schedstart.tm.tm_min);
			sched_paused = true;

			rd_lock(&mining_thr_lock);
			for (i = 0; i < mining_threads; i++)
				mining_thr[i]->pause = true;
			rd_unlock(&mining_thr_lock);
		} else if (sched_paused && should_run()) {
			applog(LOG_WARNING, "Restarting execution as per start time %02d:%02d scheduled",
				schedstart.tm.tm_hour, schedstart.tm.tm_min);
			if (schedstop.enable)
				applog(LOG_WARNING, "Will pause execution as scheduled at %02d:%02d",
					schedstop.tm.tm_hour, schedstop.tm.tm_min);
			sched_paused = false;

			for (i = 0; i < mining_threads; i++) {
				struct thr_info *thr;

				thr = get_thread(i);

				/* Don't touch disabled devices */
				if (thr->cgpu->deven == DEV_DISABLED)
					continue;
				thr->pause = false;
				applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
				cgsem_post(&thr->sem);
			}
		}

		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = get_devices(i);
			struct thr_info *thr = cgpu->thr[0];
			enum dev_enable *denable;
			char dev_str[8];

			if (!thr)
				continue;

			cgpu->drv->get_stats(cgpu);

			denable = &cgpu->deven;
			snprintf(dev_str, sizeof(dev_str), "%s %d", cgpu->drv->name, cgpu->device_id);

			/* Thread is waiting on getwork or disabled */
			if (thr->getwork || *denable == DEV_DISABLED)
				continue;

			if (cgpu->status != LIFE_WELL && (now.tv_sec - thr->last.tv_sec < WATCHDOG_SICK_TIME)) {
				if (cgpu->status != LIFE_INIT)
				applog(LOG_ERR, "%s: Recovered, declaring WELL!", dev_str);
				cgpu->status = LIFE_WELL;
				cgpu->device_last_well = time(NULL);
			} else if (cgpu->status == LIFE_WELL && (now.tv_sec - thr->last.tv_sec > WATCHDOG_SICK_TIME)) {
				cgpu->rolling = 0;
				cgpu->status = LIFE_SICK;
				applog(LOG_ERR, "%s: Idle for more than 60 seconds, declaring SICK!", dev_str);
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_SICK_IDLE_60);
				if (opt_restart) {
					applog(LOG_ERR, "%s: Attempting to restart", dev_str);
					reinit_device(cgpu);
				}
			} else if (cgpu->status == LIFE_SICK && (now.tv_sec - thr->last.tv_sec > WATCHDOG_DEAD_TIME)) {
				cgpu->status = LIFE_DEAD;
				applog(LOG_ERR, "%s: Not responded for more than 10 minutes, declaring DEAD!", dev_str);
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_DEAD_IDLE_600);
			} else if (now.tv_sec - thr->sick.tv_sec > 60 &&
				   (cgpu->status == LIFE_SICK || cgpu->status == LIFE_DEAD)) {
				/* Attempt to restart a GPU that's sick or dead once every minute */
				cgtime(&thr->sick);
				if (opt_restart)
					reinit_device(cgpu);
			}
		}
	}

	return NULL;
}

static void log_print_status(struct cgpu_info *cgpu)
{
	char logline[255];

	get_statline(logline, sizeof(logline), cgpu);
	applog(LOG_WARNING, "%s", logline);
}

static void noop_get_statline(char __maybe_unused *buf, size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu);
void blank_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info __maybe_unused *cgpu);

void print_summary(void)
{
	struct timeval diff;
	int hours, mins, secs, i;
	double utility, displayed_hashes, work_util;

	timersub(&total_tv_end, &total_tv_start, &diff);
	hours = diff.tv_sec / 3600;
	mins = (diff.tv_sec % 3600) / 60;
	secs = diff.tv_sec % 60;

	utility = total_accepted / total_secs * 60;
	work_util = total_diff1 / total_secs * 60;

	applog(LOG_WARNING, "\nSummary of runtime statistics:\n");
	applog(LOG_WARNING, "Started at %s", datestamp);
	if (total_pools == 1)
		applog(LOG_WARNING, "Pool: %s", pools[0]->rpc_url);
	applog(LOG_WARNING, "Runtime: %d hrs : %d mins : %d secs", hours, mins, secs);
	displayed_hashes = total_mhashes_done / total_secs;

	applog(LOG_WARNING, "Average hashrate: %.1f Mhash/s", displayed_hashes);
	applog(LOG_WARNING, "Solved blocks: %d", found_blocks);
	applog(LOG_WARNING, "Best share difficulty: %s", best_share);
	applog(LOG_WARNING, "Share submissions: %"PRId64, total_accepted + total_rejected);
	applog(LOG_WARNING, "Accepted shares: %"PRId64, total_accepted);
	applog(LOG_WARNING, "Rejected shares: %"PRId64, total_rejected);
	applog(LOG_WARNING, "Accepted difficulty shares: %1.f", total_diff_accepted);
	applog(LOG_WARNING, "Rejected difficulty shares: %1.f", total_diff_rejected);
	if (total_accepted || total_rejected)
		applog(LOG_WARNING, "Reject ratio: %.1f%%", (double)(total_rejected * 100) / (double)(total_accepted + total_rejected));
	applog(LOG_WARNING, "Hardware errors: %d", hw_errors);
	applog(LOG_WARNING, "Utility (accepted shares / min): %.2f/min", utility);
	applog(LOG_WARNING, "Work Utility (diff1 shares solved / min): %.2f/min\n", work_util);

	applog(LOG_WARNING, "Stale submissions discarded due to new blocks: %"PRId64, total_stale);
	applog(LOG_WARNING, "Unable to get work from server occasions: %d", total_go);
	applog(LOG_WARNING, "Work items generated locally: %d", local_work);
	applog(LOG_WARNING, "Submitting work remotely delay occasions: %d", total_ro);
	applog(LOG_WARNING, "New blocks detected on network: %d\n", new_blocks);

	if (total_pools > 1) {
		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			applog(LOG_WARNING, "Pool: %s", pool->rpc_url);
			if (pool->solved)
				applog(LOG_WARNING, "SOLVED %d BLOCK%s!", pool->solved, pool->solved > 1 ? "S" : "");
			applog(LOG_WARNING, " Share submissions: %"PRId64, pool->accepted + pool->rejected);
			applog(LOG_WARNING, " Accepted shares: %"PRId64, pool->accepted);
			applog(LOG_WARNING, " Rejected shares: %"PRId64, pool->rejected);
			applog(LOG_WARNING, " Accepted difficulty shares: %1.f", pool->diff_accepted);
			applog(LOG_WARNING, " Rejected difficulty shares: %1.f", pool->diff_rejected);
			if (pool->accepted || pool->rejected)
				applog(LOG_WARNING, " Reject ratio: %.1f%%", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));

			applog(LOG_WARNING, " Items worked on: %d", pool->works);
			applog(LOG_WARNING, " Stale submissions discarded due to new blocks: %d", pool->stale_shares);
			applog(LOG_WARNING, " Unable to get work from server occasions: %d", pool->getfail_occasions);
			applog(LOG_WARNING, " Submitting work remotely delay occasions: %d\n", pool->remotefail_occasions);
		}
	}

	applog(LOG_WARNING, "Summary of per device statistics:\n");
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = get_devices(i);

		cgpu->drv->get_statline_before = &blank_get_statline_before;
		cgpu->drv->get_statline = &noop_get_statline;
		log_print_status(cgpu);
	}

	if (opt_shares) {
		applog(LOG_WARNING, "Mined %.0f accepted shares of %d requested\n", total_diff_accepted, opt_shares);
		if (opt_shares > total_diff_accepted)
			applog(LOG_WARNING, "WARNING - Mined only %.0f shares of %d requested.", total_diff_accepted, opt_shares);
	}
	applog(LOG_WARNING, " ");

	fflush(stderr);
	fflush(stdout);
}

static void clean_up(bool restarting)
{
#ifdef USE_USBUTILS
	usb_polling = false;
	pthread_join(usb_poll_thread, NULL);
        libusb_exit(NULL);
#endif

	cgtime(&total_tv_end);
#ifdef WIN32
	timeEndPeriod(1);
#endif
#ifdef HAVE_CURSES
	disable_curses();
#endif
	if (!restarting && !opt_realquiet && successful_connect)
		print_summary();

	curl_global_cleanup();
}

/* Should all else fail and we're unable to clean up threads due to locking
 * issues etc, just silently exit. */
static void *killall_thread(void __maybe_unused *arg)
{
	pthread_detach(pthread_self());
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	sleep(5);
	exit(1);
	return NULL;
}

void __quit(int status, bool clean)
{
	pthread_t killall_t;

	if (unlikely(pthread_create(&killall_t, NULL, killall_thread, NULL)))
		exit(1);

	if (clean)
		clean_up(false);
#ifdef HAVE_CURSES
	else
		disable_curses();
#endif

#if defined(unix) || defined(__APPLE__)
	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
#endif
	pthread_cancel(killall_t);

	exit(status);
}

void _quit(int status)
{
	__quit(status, true);
}

#ifdef HAVE_CURSES
char *curses_input(const char *query)
{
	char *input;

	echo();
	input = malloc(255);
	if (!input)
		quit(1, "Failed to malloc input");
	leaveok(logwin, false);
	wlogprint("%s:\n", query);
	wgetnstr(logwin, input, 255);
	if (!strlen(input))
		strcpy(input, "-1");
	leaveok(logwin, true);
	noecho();
	return input;
}
#endif

static bool pools_active = false;

static void *test_pool_thread(void *arg)
{
	struct pool *pool = (struct pool *)arg;

	if (!pool->blocking)
		pthread_detach(pthread_self());
retry:
	if (pool_active(pool, false)) {
		pool_tset(pool, &pool->lagging);
		pool_tclear(pool, &pool->idle);
		bool first_pool = false;

		cg_wlock(&control_lock);
		if (!pools_active) {
			currentpool = pool;
			if (pool->pool_no != 0)
				first_pool = true;
			pools_active = true;
		}
		cg_wunlock(&control_lock);

		if (unlikely(first_pool))
			applog(LOG_NOTICE, "Switching to pool %d %s - first alive pool", pool->pool_no, pool->rpc_url);

		pool_resus(pool);
		switch_pools(NULL);
	} else {
		pool_died(pool);
		sleep(5);
		goto retry;
	}

	pool->testing = false;

	return NULL;
}

/* Always returns true that the pool details were added unless we are not
 * live, implying this is the only pool being added, so if no pools are
 * active it returns false. */
bool add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass)
{
	size_t siz;

	url = get_proxy(url, pool);

	pool->rpc_url = url;
	pool->rpc_user = user;
	pool->rpc_pass = pass;
	siz = strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2;
	pool->rpc_userpass = malloc(siz);
	if (!pool->rpc_userpass)
		quit(1, "Failed to malloc userpass");
	snprintf(pool->rpc_userpass, siz, "%s:%s", pool->rpc_user, pool->rpc_pass);

	pool->testing = true;
	pool->idle = true;
	pool->blocking = !live;
	enable_pool(pool);

	pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
	if (!live) {
		pthread_join(pool->test_thread, NULL);
		return pools_active;
	}
	return true;
}

#ifdef HAVE_CURSES
static bool input_pool(bool live)
{
	char *url = NULL, *user = NULL, *pass = NULL;
	struct pool *pool;
	bool ret = false;

	immedok(logwin, true);
	wlogprint("Input server details.\n");

	url = curses_input("URL");
	if (!strcmp(url, "-1"))
		goto out;

	user = curses_input("Username");
	if (!strcmp(user, "-1"))
		goto out;

	pass = curses_input("Password");
	if (!strcmp(pass, "-1")) {
		free(pass);
		pass = strdup("");
	}

	pool = add_pool();

	if (!detect_stratum(pool, url) && strncmp(url, "http://", 7) &&
	    strncmp(url, "https://", 8)) {
		char *httpinput;

		httpinput = malloc(256);
		if (!httpinput)
			quit(1, "Failed to malloc httpinput");
		strcpy(httpinput, "http://");
		strncat(httpinput, url, 248);
		free(url);
		url = httpinput;
	}

	ret = add_pool_details(pool, live, url, user, pass);
out:
	immedok(logwin, false);

	if (!ret) {
		free(url);
		free(user);
		free(pass);
	}
	return ret;
}
#endif

#if defined(unix) || defined(__APPLE__)
static void fork_monitor()
{
	// Make a pipe: [readFD, writeFD]
	int pfd[2];
	int r = pipe(pfd);

	if (r < 0) {
		perror("pipe - failed to create pipe for --monitor");
		exit(1);
	}

	// Make stderr write end of pipe
	fflush(stderr);
	r = dup2(pfd[1], 2);
	if (r < 0) {
		perror("dup2 - failed to alias stderr to write end of pipe for --monitor");
		exit(1);
	}
	r = close(pfd[1]);
	if (r < 0) {
		perror("close - failed to close write end of pipe for --monitor");
		exit(1);
	}

	// Don't allow a dying monitor to kill the main process
	sighandler_t sr0 = signal(SIGPIPE, SIG_IGN);
	sighandler_t sr1 = signal(SIGPIPE, SIG_IGN);
	if (SIG_ERR == sr0 || SIG_ERR == sr1) {
		perror("signal - failed to edit signal mask for --monitor");
		exit(1);
	}

	// Fork a child process
	forkpid = fork();
	if (forkpid < 0) {
		perror("fork - failed to fork child process for --monitor");
		exit(1);
	}

	// Child: launch monitor command
	if (0 == forkpid) {
		// Make stdin read end of pipe
		r = dup2(pfd[0], 0);
		if (r < 0) {
			perror("dup2 - in child, failed to alias read end of pipe to stdin for --monitor");
			exit(1);
		}
		close(pfd[0]);
		if (r < 0) {
			perror("close - in child, failed to close read end of  pipe for --monitor");
			exit(1);
		}

		// Launch user specified command
		execl("/bin/bash", "/bin/bash", "-c", opt_stderr_cmd, (char*)NULL);
		perror("execl - in child failed to exec user specified command for --monitor");
		exit(1);
	}

	// Parent: clean up unused fds and bail
	r = close(pfd[0]);
	if (r < 0) {
		perror("close - failed to close read end of pipe for --monitor");
		exit(1);
	}
}
#endif // defined(unix)

#ifdef HAVE_CURSES
static void enable_curses_windows(void)
{
	int x,y;

	getmaxyx(mainwin, y, x);
	statuswin = newwin(logstart, x, 0, 0);
	leaveok(statuswin, true);
	logwin = newwin(y - logcursor, 0, logcursor, 0);
	idlok(logwin, true);
	scrollok(logwin, true);
	leaveok(logwin, true);
	cbreak();
	noecho();
}
void enable_curses(void) {
	lock_curses();
	if (curses_active) {
		unlock_curses();
		return;
	}

	mainwin = initscr();
	enable_curses_windows();
	curses_active = true;
	statusy = logstart;
	unlock_curses();
}
#endif

static int cgminer_id_count = 0;

/* Various noop functions for drivers that don't support or need their
 * variants. */
static void noop_reinit_device(struct cgpu_info __maybe_unused *cgpu)
{
}

void blank_get_statline_before(char __maybe_unused *buf,size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu)
{
}

static void noop_get_statline(char __maybe_unused *buf, size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu)
{
}

static bool noop_get_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return true;
}

static bool noop_thread_prepare(struct thr_info __maybe_unused *thr)
{
	return true;
}

static uint64_t noop_can_limit_work(struct thr_info __maybe_unused *thr)
{
	return 0xffffffff;
}

static bool noop_thread_init(struct thr_info __maybe_unused *thr)
{
	return true;
}

static bool noop_prepare_work(struct thr_info __maybe_unused *thr, struct work __maybe_unused *work)
{
	return true;
}

static void noop_hw_error(struct thr_info __maybe_unused *thr)
{
}

static void noop_thread_shutdown(struct thr_info __maybe_unused *thr)
{
}

static void noop_thread_enable(struct thr_info __maybe_unused *thr)
{
}

static void noop_detect(bool __maybe_unused hotplug)
{
}

static struct api_data *noop_get_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void noop_hash_work(struct thr_info __maybe_unused *thr)
{
}

#define noop_flush_work noop_reinit_device
#define noop_update_work noop_reinit_device
#define noop_queue_full noop_get_stats
#define noop_zero_stats noop_reinit_device
#define noop_identify_device noop_reinit_device

/* Fill missing driver drv functions with noops */
void fill_device_drv(struct device_drv *drv)
{
	if (!drv->drv_detect)
		drv->drv_detect = &noop_detect;
	if (!drv->reinit_device)
		drv->reinit_device = &noop_reinit_device;
	if (!drv->get_statline_before)
		drv->get_statline_before = &blank_get_statline_before;
	if (!drv->get_statline)
		drv->get_statline = &noop_get_statline;
	if (!drv->get_stats)
		drv->get_stats = &noop_get_stats;
	if (!drv->thread_prepare)
		drv->thread_prepare = &noop_thread_prepare;
	if (!drv->can_limit_work)
		drv->can_limit_work = &noop_can_limit_work;
	if (!drv->thread_init)
		drv->thread_init = &noop_thread_init;
	if (!drv->prepare_work)
		drv->prepare_work = &noop_prepare_work;
	if (!drv->hw_error)
		drv->hw_error = &noop_hw_error;
	if (!drv->thread_shutdown)
		drv->thread_shutdown = &noop_thread_shutdown;
	if (!drv->thread_enable)
		drv->thread_enable = &noop_thread_enable;
	if (!drv->hash_work)
		drv->hash_work = &hash_sole_work;
	if (!drv->flush_work)
		drv->flush_work = &noop_flush_work;
	if (!drv->update_work)
		drv->update_work = &noop_update_work;
	if (!drv->queue_full)
		drv->queue_full = &noop_queue_full;
	if (!drv->zero_stats)
		drv->zero_stats = &noop_zero_stats;
	/* If drivers support internal diff they should set a max_diff or
	 * we will assume they don't and set max to 1. */
	if (!drv->max_diff)
		drv->max_diff = 1;
}

void null_device_drv(struct device_drv *drv)
{
	drv->drv_detect = &noop_detect;
	drv->reinit_device = &noop_reinit_device;
	drv->get_statline_before = &blank_get_statline_before;
	drv->get_statline = &noop_get_statline;
	drv->get_api_stats = &noop_get_api_stats;
	drv->get_stats = &noop_get_stats;
	drv->identify_device = &noop_identify_device;
	drv->set_device = NULL;

	drv->thread_prepare = &noop_thread_prepare;
	drv->can_limit_work = &noop_can_limit_work;
	drv->thread_init = &noop_thread_init;
	drv->prepare_work = &noop_prepare_work;

	/* This should make the miner thread just exit */
	drv->hash_work = &noop_hash_work;

	drv->hw_error = &noop_hw_error;
	drv->thread_shutdown = &noop_thread_shutdown;
	drv->thread_enable = &noop_thread_enable;

	drv->zero_stats = &noop_zero_stats;

	drv->hash_work = &noop_hash_work;

	drv->queue_full = &noop_queue_full;
	drv->flush_work = &noop_flush_work;
	drv->update_work = &noop_update_work;

	drv->zero_stats = &noop_zero_stats;
	drv->max_diff = 1;
	drv->min_diff = 1;
}

void enable_device(struct cgpu_info *cgpu)
{
	cgpu->deven = DEV_ENABLED;

	wr_lock(&devices_lock);
	devices[cgpu->cgminer_id = cgminer_id_count++] = cgpu;
	wr_unlock(&devices_lock);

	if (hotplug_mode)
		new_threads += cgpu->threads;
	else
		mining_threads += cgpu->threads;

	rwlock_init(&cgpu->qlock);
	cgpu->queued_work = NULL;
}

struct _cgpu_devid_counter {
	char name[4];
	int lastid;
	UT_hash_handle hh;
};

static void adjust_mostdevs(void)
{
	if (total_devices - zombie_devs > most_devices)
		most_devices = total_devices - zombie_devs;
}

#ifdef USE_ICARUS
bool icarus_get_device_id(struct cgpu_info *cgpu)
{
	static struct _cgpu_devid_counter *devids = NULL;
	struct _cgpu_devid_counter *d;

	HASH_FIND_STR(devids, cgpu->drv->name, d);
	if (d)
		return (d->lastid + 1);
	else
		return 0;
}
#endif

bool add_cgpu(struct cgpu_info *cgpu)
{
	static struct _cgpu_devid_counter *devids = NULL;
	struct _cgpu_devid_counter *d;

	HASH_FIND_STR(devids, cgpu->drv->name, d);
	if (d)
		cgpu->device_id = ++d->lastid;
	else {
		d = malloc(sizeof(*d));
		memcpy(d->name, cgpu->drv->name, sizeof(d->name));
		cgpu->device_id = d->lastid = 0;
		HASH_ADD_STR(devids, name, d);
	}

	wr_lock(&devices_lock);
	devices = realloc(devices, sizeof(struct cgpu_info *) * (total_devices + new_devices + 2));
	wr_unlock(&devices_lock);

	mutex_lock(&stats_lock);
	cgpu->last_device_valid_work = time(NULL);
	mutex_unlock(&stats_lock);

	if (hotplug_mode)
		devices[total_devices + new_devices++] = cgpu;
	else
		devices[total_devices++] = cgpu;

	adjust_mostdevs();
#ifdef USE_USBUTILS
	if (cgpu->usbdev && !cgpu->unique_id && cgpu->usbdev->serial_string &&
	    strlen(cgpu->usbdev->serial_string) > 4)
		cgpu->unique_id = str_text(cgpu->usbdev->serial_string);
#endif
	return true;
}

struct device_drv *copy_drv(struct device_drv *drv)
{
	struct device_drv *copy;

	if (unlikely(!(copy = malloc(sizeof(*copy))))) {
		quit(1, "Failed to allocate device_drv copy of %s (%s)",
				drv->name, drv->copy ? "copy" : "original");
	}
	memcpy(copy, drv, sizeof(*copy));
	copy->copy = true;
	return copy;
}

#ifdef USE_USBUTILS
static void hotplug_process(void)
{
	struct thr_info *thr;
	int i, j;

	for (i = 0; i < new_devices; i++) {
		struct cgpu_info *cgpu;
		int dev_no = total_devices + i;

		cgpu = devices[dev_no];
		enable_device(cgpu);
		cgpu->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
		cgpu->rolling = cgpu->total_mhashes = 0;
	}

	wr_lock(&mining_thr_lock);
	mining_thr = realloc(mining_thr, sizeof(thr) * (mining_threads + new_threads + 1));

	if (!mining_thr)
		quit(1, "Failed to hotplug realloc mining_thr");
	for (i = 0; i < new_threads; i++) {
		mining_thr[mining_threads + i] = calloc(1, sizeof(*thr));
		if (!mining_thr[mining_threads + i])
			quit(1, "Failed to hotplug calloc mining_thr[%d]", i);
	}

	// Start threads
	for (i = 0; i < new_devices; ++i) {
		struct cgpu_info *cgpu = devices[total_devices];
		cgpu->thr = malloc(sizeof(*cgpu->thr) * (cgpu->threads+1));
		cgpu->thr[cgpu->threads] = NULL;
		cgpu->status = LIFE_INIT;
		cgtime(&(cgpu->dev_start_tv));

		for (j = 0; j < cgpu->threads; ++j) {
			thr = __get_thread(mining_threads);
			thr->id = mining_threads;
			thr->cgpu = cgpu;
			thr->device_thread = j;

			if (!cgpu->drv->thread_prepare(thr)) {
				null_device_drv(cgpu->drv);
				cgpu->deven = DEV_DISABLED;
				continue;
			}

			if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
				quit(1, "hotplug thread %d create failed", thr->id);

			cgpu->thr[j] = thr;

			/* Enable threads for devices set not to mine but disable
			 * their queue in case we wish to enable them later */
			if (cgpu->deven != DEV_DISABLED) {
				applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
				cgsem_post(&thr->sem);
			}

			mining_threads++;
		}
		total_devices++;
		applog(LOG_WARNING, "Hotplug: %s added %s %i", cgpu->drv->dname, cgpu->drv->name, cgpu->device_id);
	}
	wr_unlock(&mining_thr_lock);

	adjust_mostdevs();
#ifdef HAVE_CURSES
	switch_logsize(true);
#endif
}

#define DRIVER_DRV_DETECT_HOTPLUG(X) X##_drv.drv_detect(true);

static void *hotplug_thread(void __maybe_unused *userdata)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("Hotplug");

	hotplug_mode = true;

	cgsleep_ms(5000);

	while (0x2a) {
// Version 0.1 just add the devices on - worry about using nodev later

		if (hotplug_time == 0)
			cgsleep_ms(5000);
		else {
			new_devices = 0;
			new_threads = 0;

			/* Use the DRIVER_PARSE_COMMANDS macro to detect all
			 * devices */
			DRIVER_PARSE_COMMANDS(DRIVER_DRV_DETECT_HOTPLUG)

			if (new_devices)
				hotplug_process();

			// hotplug_time >0 && <=9999
			cgsleep_ms(hotplug_time * 1000);
		}
	}

	return NULL;
}
#endif

static void probe_pools(void)
{
	int i;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		pool->testing = true;
		pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
	}
}

#define DRIVER_FILL_DEVICE_DRV(X) fill_device_drv(&X##_drv);
#define DRIVER_DRV_DETECT_ALL(X) X##_drv.drv_detect(false);

#ifdef USE_USBUTILS
static void *libusb_poll_thread(void __maybe_unused *arg)
{
	struct timeval tv_end = {1, 0};

	RenameThread("USBPoll");

	while (usb_polling)
		libusb_handle_events_timeout_completed(NULL, &tv_end, NULL);

	/* Cancel any cancellable usb transfers */
	cancel_usb_transfers();

	/* Keep event handling going until there are no async transfers in
	 * flight. */
	do {
		libusb_handle_events_timeout_completed(NULL, &tv_end, NULL);
	} while (async_usb_transfers());

	return NULL;
}

static void initialise_usb(void) {
	int err = libusb_init(NULL);

	if (err) {
		fprintf(stderr, "libusb_init() failed err %d", err);
		fflush(stderr);
		quit(1, "libusb_init() failed");
	}
	initialise_usblocks();
	usb_polling = true;
	pthread_create(&usb_poll_thread, NULL, libusb_poll_thread, NULL);
}
#else
#define initialise_usb() {}
#endif

int main(int argc, char *argv[])
{
	struct sigaction handler;
	struct work *work = NULL;
	bool pool_msg = false;
	struct thr_info *thr;
	struct block *block;
	int i, j, slept = 0;
	unsigned int k;
	char *s;

	/* This dangerous functions tramples random dynamically allocated
	 * variables so do it before anything at all */
	if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
		early_quit(1, "Failed to curl_global_init");

# ifdef __linux
	/* If we're on a small lowspec platform with only one CPU, we should
	 * yield after dropping a lock to allow a thread waiting for it to be
	 * able to get CPU time to grab the lock. */
	if (sysconf(_SC_NPROCESSORS_ONLN) == 1)
		selective_yield = &sched_yield;
#endif

#if LOCK_TRACKING
	// Must be first
	if (unlikely(pthread_mutex_init(&lockstat_lock, NULL)))
		quithere(1, "Failed to pthread_mutex_init lockstat_lock errno=%d", errno);
#endif

	initial_args = malloc(sizeof(char *) * (argc + 1));
	for  (i = 0; i < argc; i++)
		initial_args[i] = strdup(argv[i]);
	initial_args[argc] = NULL;

	mutex_init(&hash_lock);
	mutex_init(&console_lock);
	cglock_init(&control_lock);
	mutex_init(&stats_lock);
	mutex_init(&sharelog_lock);
	cglock_init(&ch_lock);
	mutex_init(&sshare_lock);
	rwlock_init(&blk_lock);
	rwlock_init(&netacc_lock);
	rwlock_init(&mining_thr_lock);
	rwlock_init(&devices_lock);

	mutex_init(&lp_lock);
	if (unlikely(pthread_cond_init(&lp_cond, NULL)))
		early_quit(1, "Failed to pthread_cond_init lp_cond");

	mutex_init(&restart_lock);
	if (unlikely(pthread_cond_init(&restart_cond, NULL)))
		early_quit(1, "Failed to pthread_cond_init restart_cond");

	if (unlikely(pthread_cond_init(&gws_cond, NULL)))
		early_quit(1, "Failed to pthread_cond_init gws_cond");

	/* Create a unique get work queue */
	getq = tq_new();
	if (!getq)
		early_quit(1, "Failed to create getq");
	/* We use the getq mutex as the staged lock */
	stgd_lock = &getq->mutex;

	initialise_usb();

	snprintf(packagename, sizeof(packagename), "%s %s", PACKAGE, VERSION);

	handler.sa_handler = &sighandler;
	handler.sa_flags = 0;
	sigemptyset(&handler.sa_mask);
	sigaction(SIGTERM, &handler, &termhandler);
	sigaction(SIGINT, &handler, &inthandler);
#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#else
	timeBeginPeriod(1);
#endif
	opt_kernel_path = alloca(PATH_MAX);
	strcpy(opt_kernel_path, CGMINER_PREFIX);
	cgminer_path = alloca(PATH_MAX);
	s = strdup(argv[0]);
	strcpy(cgminer_path, dirname(s));
	free(s);
	strcat(cgminer_path, "/");

	devcursor = 8;
	logstart = devcursor + 1;
	logcursor = logstart + 1;

	block = calloc(sizeof(struct block), 1);
	if (unlikely(!block))
		quit (1, "main OOM");
	for (i = 0; i < 36; i++)
		strcat(block->hash, "0");
	HASH_ADD_STR(blocks, hash, block);
	strcpy(current_hash, block->hash);

	INIT_LIST_HEAD(&scan_devices);

	/* parse command line */
	opt_register_table(opt_config_table,
			   "Options for both config file and command line");
	opt_register_table(opt_cmdline_table,
			   "Options for command line only");

	opt_parse(&argc, argv, applog_and_exit);
	if (argc != 1)
		early_quit(1, "Unexpected extra commandline arguments");

	if (!config_loaded)
		load_default_config();

	if (opt_benchmark || opt_benchfile) {
		struct pool *pool;

		pool = add_pool();
		pool->rpc_url = malloc(255);
		if (opt_benchfile)
			strcpy(pool->rpc_url, "Benchfile");
		else
			strcpy(pool->rpc_url, "Benchmark");
		pool->rpc_user = pool->rpc_url;
		pool->rpc_pass = pool->rpc_url;
		pool->rpc_userpass = pool->rpc_url;
		pool->sockaddr_url = pool->rpc_url;
		strncpy(pool->diff, "?", sizeof(pool->diff)-1);
		pool->diff[sizeof(pool->diff)-1] = '\0';
		enable_pool(pool);
		pool->idle = false;
		successful_connect = true;

		for (i = 0; i < 16; i++) {
			hex2bin(&bench_hidiff_bins[i][0], &bench_hidiffs[i][0], 160);
			hex2bin(&bench_lodiff_bins[i][0], &bench_lodiffs[i][0], 160);
		}
		set_target(bench_target, 32);
	}

#ifdef HAVE_CURSES
	if (opt_realquiet || opt_display_devs)
		use_curses = false;

	if (use_curses)
		enable_curses();
#endif

	applog(LOG_WARNING, "Started %s", packagename);
	if (cnfbuf) {
		applog(LOG_NOTICE, "Loaded configuration file %s", cnfbuf);
		switch (fileconf_load) {
			case 0:
				applog(LOG_WARNING, "Fatal JSON error in configuration file.");
				applog(LOG_WARNING, "Configuration file could not be used.");
				break;
			case -1:
				applog(LOG_WARNING, "Error in configuration file, partially loaded.");
				if (use_curses)
					applog(LOG_WARNING, "Start cgminer with -T to see what failed to load.");
				break;
			default:
				break;
		}
		free(cnfbuf);
		cnfbuf = NULL;
	}

	strcat(opt_kernel_path, "/");

	if (want_per_device_stats)
		opt_log_output = true;

	if (opt_scantime < 0)
		opt_scantime = 60;

	total_control_threads = 8;
	control_thr = calloc(total_control_threads, sizeof(*thr));
	if (!control_thr)
		early_quit(1, "Failed to calloc control_thr");

	gwsched_thr_id = 0;

#ifdef USE_USBUTILS
	usb_initialise();

	// before device detection
	cgsem_init(&usb_resource_sem);
	usbres_thr_id = 1;
	thr = &control_thr[usbres_thr_id];
	if (thr_info_create(thr, NULL, usb_resource_thread, thr))
		early_quit(1, "usb resource thread create failed");
	pthread_detach(thr->pth);
#endif

	/* Use the DRIVER_PARSE_COMMANDS macro to fill all the device_drvs */
	DRIVER_PARSE_COMMANDS(DRIVER_FILL_DEVICE_DRV)

	/* Use the DRIVER_PARSE_COMMANDS macro to detect all devices */
	DRIVER_PARSE_COMMANDS(DRIVER_DRV_DETECT_ALL)

	if (opt_display_devs) {
		applog(LOG_ERR, "Devices detected:");
		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = devices[i];
			if (cgpu->name)
				applog(LOG_ERR, " %2d. %s %d: %s (driver: %s)", i, cgpu->drv->name, cgpu->device_id, cgpu->name, cgpu->drv->dname);
			else
				applog(LOG_ERR, " %2d. %s %d (driver: %s)", i, cgpu->drv->name, cgpu->device_id, cgpu->drv->dname);
		}
		early_quit(0, "%d devices listed", total_devices);
	}

	mining_threads = 0;
	for (i = 0; i < total_devices; ++i)
		enable_device(devices[i]);

#ifdef USE_USBUTILS
	if (!total_devices) {
		applog(LOG_WARNING, "No devices detected!");
		applog(LOG_WARNING, "Waiting for USB hotplug devices or press q to quit");
	}
#else
	if (!total_devices)
		early_quit(1, "All devices disabled, cannot mine!");
#endif

	most_devices = total_devices;

	load_temp_cutoffs();

	for (i = 0; i < total_devices; ++i)
		devices[i]->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

	if (!opt_compact) {
		logstart += most_devices;
		logcursor = logstart + 1;
#ifdef HAVE_CURSES
		check_winsizes();
#endif
	}

	if (!total_pools) {
		applog(LOG_WARNING, "Need to specify at least one pool server.");
#ifdef HAVE_CURSES
		if (!use_curses || !input_pool(false))
#endif
			early_quit(1, "Pool setup failed");
	}

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];
		size_t siz;

		pool->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
		pool->cgminer_pool_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

		if (!pool->rpc_userpass) {
			if (!pool->rpc_pass)
				pool->rpc_pass = strdup("");
			if (!pool->rpc_user)
				early_quit(1, "No login credentials supplied for pool %u %s", i, pool->rpc_url);
			siz = strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2;
			pool->rpc_userpass = malloc(siz);
			if (!pool->rpc_userpass)
				early_quit(1, "Failed to malloc userpass");
			snprintf(pool->rpc_userpass, siz, "%s:%s", pool->rpc_user, pool->rpc_pass);
		}
	}
	/* Set the currentpool to pool 0 */
	currentpool = pools[0];

#ifdef HAVE_SYSLOG_H
	if (use_syslog)
		openlog(PACKAGE, LOG_PID, LOG_USER);
#endif

	#if defined(unix) || defined(__APPLE__)
		if (opt_stderr_cmd)
			fork_monitor();
	#endif // defined(unix)

	mining_thr = calloc(mining_threads, sizeof(thr));
	if (!mining_thr)
		early_quit(1, "Failed to calloc mining_thr");
	for (i = 0; i < mining_threads; i++) {
		mining_thr[i] = calloc(1, sizeof(*thr));
		if (!mining_thr[i])
			early_quit(1, "Failed to calloc mining_thr[%d]", i);
	}

	// Start threads
	k = 0;
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];
		cgpu->thr = malloc(sizeof(*cgpu->thr) * (cgpu->threads+1));
		cgpu->thr[cgpu->threads] = NULL;
		cgpu->status = LIFE_INIT;

		for (j = 0; j < cgpu->threads; ++j, ++k) {
			thr = get_thread(k);
			thr->id = k;
			thr->cgpu = cgpu;
			thr->device_thread = j;

			if (!cgpu->drv->thread_prepare(thr))
				continue;

			if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
				early_quit(1, "thread %d create failed", thr->id);

			cgpu->thr[j] = thr;

			/* Enable threads for devices set not to mine but disable
			 * their queue in case we wish to enable them later */
			if (cgpu->deven != DEV_DISABLED) {
				applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
				cgsem_post(&thr->sem);
			}
		}
	}

	if (opt_benchmark || opt_benchfile)
		goto begin_bench;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool  = pools[i];

		enable_pool(pool);
		pool->idle = true;
	}

	/* Look for at least one active pool before starting */
	applog(LOG_NOTICE, "Probing for an alive pool");
	probe_pools();
	do {
		sleep(1);
		slept++;
	} while (!pools_active && slept < 60);

	while (!pools_active) {
		if (!pool_msg) {
			applog(LOG_ERR, "No servers were found that could be used to get work from.");
			applog(LOG_ERR, "Please check the details from the list below of the servers you have input");
			applog(LOG_ERR, "Most likely you have input the wrong URL, forgotten to add a port, or have not set up workers");
			for (i = 0; i < total_pools; i++) {
				struct pool *pool = pools[i];

				applog(LOG_WARNING, "Pool: %d  URL: %s  User: %s  Password: %s",
				i, pool->rpc_url, pool->rpc_user, pool->rpc_pass);
			}
			pool_msg = true;
			if (use_curses)
				applog(LOG_ERR, "Press any key to exit, or cgminer will wait indefinitely for an alive pool.");
		}
		if (!use_curses)
			early_quit(0, "No servers could be used! Exiting.");
#ifdef HAVE_CURSES
		touchwin(logwin);
		wrefresh(logwin);
		halfdelay(10);
		if (getch() != ERR)
			early_quit(0, "No servers could be used! Exiting.");
		cbreak();
#endif
	};

begin_bench:
	total_mhashes_done = 0;
	for (i = 0; i < total_devices; i++) {
		struct cgpu_info *cgpu = devices[i];

		cgpu->rolling = cgpu->total_mhashes = 0;
	}

	cgtime(&total_tv_start);
	cgtime(&total_tv_end);
	cgtime(&tv_hashmeter);
	get_datestamp(datestamp, sizeof(datestamp), &total_tv_start);

	watchpool_thr_id = 2;
	thr = &control_thr[watchpool_thr_id];
	/* start watchpool thread */
	if (thr_info_create(thr, NULL, watchpool_thread, NULL))
		early_quit(1, "watchpool thread create failed");
	pthread_detach(thr->pth);

	watchdog_thr_id = 3;
	thr = &control_thr[watchdog_thr_id];
	/* start watchdog thread */
	if (thr_info_create(thr, NULL, watchdog_thread, NULL))
		early_quit(1, "watchdog thread create failed");
	pthread_detach(thr->pth);

	/* Create API socket thread */
	api_thr_id = 5;
	thr = &control_thr[api_thr_id];
	if (thr_info_create(thr, NULL, api_thread, thr))
		early_quit(1, "API thread create failed");

#ifdef USE_USBUTILS
	hotplug_thr_id = 6;
	thr = &control_thr[hotplug_thr_id];
	if (thr_info_create(thr, NULL, hotplug_thread, thr))
		early_quit(1, "hotplug thread create failed");
	pthread_detach(thr->pth);
#endif

#ifdef HAVE_CURSES
	/* Create curses input thread for keyboard input. Create this last so
	 * that we know all threads are created since this can call kill_work
	 * to try and shut down all previous threads. */
	input_thr_id = 7;
	thr = &control_thr[input_thr_id];
	if (thr_info_create(thr, NULL, input_thread, thr))
		early_quit(1, "input thread create failed");
	pthread_detach(thr->pth);
#endif

	/* Just to be sure */
	if (total_control_threads != 8)
		early_quit(1, "incorrect total_control_threads (%d) should be 8", total_control_threads);

	set_highprio();

	/* Once everything is set up, main() becomes the getwork scheduler */
	while (42) {
		int ts, max_staged = max_queue;
		struct pool *pool, *cp;
		bool lagging = false;

		if (opt_work_update)
			signal_work_update();
		opt_work_update = false;
		cp = current_pool();

		/* If the primary pool is a getwork pool and cannot roll work,
		 * try to stage one extra work per mining thread */
		if (!pool_localgen(cp) && !staged_rollable)
			max_staged += mining_threads;

		mutex_lock(stgd_lock);
		ts = __total_staged();

		if (!pool_localgen(cp) && !ts && !opt_fail_only)
			lagging = true;

		/* Wait until hash_pop tells us we need to create more work */
		if (ts > max_staged) {
			if (work_emptied && max_queue < opt_queue) {
				max_queue++;
				work_emptied = false;
			}
			work_filled = true;
			pthread_cond_wait(&gws_cond, stgd_lock);
			ts = __total_staged();
		}
		mutex_unlock(stgd_lock);

		if (ts > max_staged) {
			/* Keeps slowly generating work even if it's not being
			 * used to keep last_getwork incrementing and to see
			 * if pools are still alive. */
			if (work_emptied && max_queue < opt_queue) {
				max_queue++;
				work_emptied = false;
			}
			work_filled = true;
			work = hash_pop(false);
			if (work)
				discard_work(work);
			continue;
		}

		if (work)
			discard_work(work);
		work = make_work();

		if (lagging && !pool_tset(cp, &cp->lagging)) {
			applog(LOG_WARNING, "Pool %d not providing work fast enough", cp->pool_no);
			cp->getfail_occasions++;
			total_go++;
			if (!pool_localgen(cp) && max_queue < opt_queue)
				applog(LOG_INFO, "Increasing queue to %d", ++max_queue);
		}
		pool = select_pool(lagging);
retry:
		if (pool->has_stratum) {
			while (!pool->stratum_active || !pool->stratum_notify) {
				struct pool *altpool = select_pool(true);

				cgsleep_ms(5000);
				if (altpool != pool) {
					pool = altpool;
					goto retry;
				}
			}
			gen_stratum_work(pool, work);
			applog(LOG_DEBUG, "Generated stratum work");
			stage_work(work);
			continue;
		}

		if (opt_benchfile) {
			get_benchfile_work(work);
			applog(LOG_DEBUG, "Generated benchfile work");
			stage_work(work);
			continue;
		} else if (opt_benchmark) {
			get_benchmark_work(work);
			applog(LOG_DEBUG, "Generated benchmark work");
			stage_work(work);
			continue;
		}

#ifdef HAVE_LIBCURL
		struct curl_ent *ce;

		if (pool->gbt_solo) {
			while (pool->idle) {
				struct pool *altpool = select_pool(true);

				cgsleep_ms(5000);
				if (altpool != pool) {
					pool = altpool;
					goto retry;
				}
			}
			gen_solo_work(pool, work);
			applog(LOG_DEBUG, "Generated GBT SOLO work");
			stage_work(work);
			continue;
		}

		if (pool->has_gbt) {
			while (pool->idle) {
				struct pool *altpool = select_pool(true);

				cgsleep_ms(5000);
				if (altpool != pool) {
					pool = altpool;
					goto retry;
				}
			}
			gen_gbt_work(pool, work);
			applog(LOG_DEBUG, "Generated GBT work");
			stage_work(work);
			continue;
		}

		if (clone_available()) {
			applog(LOG_DEBUG, "Cloned getwork work");
			free_work(work);
			continue;
		}

		work->pool = pool;
		ce = pop_curl_entry(pool);
		/* obtain new work from bitcoin via JSON-RPC */
		if (!get_upstream_work(work, ce->curl)) {
			applog(LOG_DEBUG, "Pool %d json_rpc_call failed on get work, retrying in 5s", pool->pool_no);
			/* Make sure the pool just hasn't stopped serving
			 * requests but is up as we'll keep hammering it */
			if (++pool->seq_getfails > mining_threads + opt_queue)
				pool_died(pool);
			cgsleep_ms(5000);
			push_curl_entry(ce, pool);
			pool = select_pool(!opt_fail_only);
			free_work(work);
			goto retry;
		}
		if (ts >= max_staged)
			pool_tclear(pool, &pool->lagging);
		if (pool_tclear(pool, &pool->idle))
			pool_resus(pool);

		applog(LOG_DEBUG, "Generated getwork work");
		stage_work(work);
		push_curl_entry(ce, pool);
#endif
	}

	return 0;
}

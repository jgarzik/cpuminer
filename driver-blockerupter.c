#include "config.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <math.h>
#ifndef WIN32
#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#else
#include <windows.h>
#include <io.h>
#endif

#include "elist.h"
#include "miner.h"
#include "driver-blockerupter.h"
#include "usbutils.h"

static void blockerupter_space_mode(struct cgpu_info *blockerupter)
{
	int interface;
	unsigned int bits = 0;

	interface = usb_interface(blockerupter);

	bits |= CP210X_BITS_DATA_8;
	bits |= CP210X_BITS_PARITY_SPACE;

	usb_transfer_data(blockerupter, CP210X_TYPE_OUT, CP210X_SET_LINE_CTL, bits, interface, NULL, 0, C_SETPARITY);
}

static void blockerupter_mark_mode(struct cgpu_info *blockerupter)
{
	int interface;
	unsigned int bits = 0;

	interface = usb_interface(blockerupter);

	bits |= CP210X_BITS_DATA_8;
	bits |= CP210X_BITS_PARITY_MARK;

	usb_transfer_data(blockerupter, CP210X_TYPE_OUT, CP210X_SET_LINE_CTL, bits, interface, NULL, 0, C_SETPARITY);

}

static void blockerupter_init_com(struct cgpu_info *blockerupter)
{
	uint32_t baudrate;
	int interface;

	if (blockerupter->usbinfo.nodev)
		return;

	interface = usb_interface(blockerupter);

	// Enable the UART
	usb_transfer_data(blockerupter, CP210X_TYPE_OUT, CP210X_REQUEST_IFC_ENABLE,
			  CP210X_VALUE_UART_ENABLE, interface, NULL, 0, C_ENABLE_UART);
	if (blockerupter->usbinfo.nodev)
		return;

	// Set data control
	usb_transfer_data(blockerupter, CP210X_TYPE_OUT, CP210X_REQUEST_DATA, CP210X_VALUE_DATA,
			  interface, NULL, 0, C_SETDATA);

	if (blockerupter->usbinfo.nodev)
		return;

	// Set the baud
	baudrate = BET_BAUD;

	usb_transfer_data(blockerupter, CP210X_TYPE_OUT, CP210X_REQUEST_BAUD, 0,
			  interface, &baudrate, sizeof (baudrate), C_SETBAUD);

	// Set space mode
	blockerupter_space_mode(blockerupter);
}

static int blockerupter_send(struct cgpu_info *blockerupter, char *data, int len)
{
	int err;
	int bytes_sent;

	if (unlikely(blockerupter->usbinfo.nodev))
		return SEND_FAIL;

	err = usb_write(blockerupter, data, len, &bytes_sent, C_BET_WRITE);

	if (err || bytes_sent != len) {
		applog(LOG_DEBUG, "blockerupter: Send (%d/%d)", bytes_sent, len);
		return SEND_FAIL;
	}

	return SEND_OK;
}

static int blockerupter_read(struct cgpu_info *blockerupter, char *data, int len)
{
	int err;
	int bytes_read;

	if (unlikely(blockerupter->usbinfo.nodev))
		return READ_FAIL;

	err = usb_read_timeout(blockerupter, data, len, &bytes_read, 2, C_BET_READ);

	if (err || bytes_read != len) {
		applog(LOG_DEBUG, "blockerupter: Read (%d/%d)", bytes_read, len);
		return READ_FAIL;
	}

	return READ_OK;
}

static void blockerupter_setclock(struct cgpu_info *blockerupter, uint8_t clock)
{
	struct blockerupter_info *info;
	info = blockerupter->device_data;
	char command;
	int err;

	command = C_GCK | clock;
	info->clock = clock;
	err = blockerupter_send(blockerupter, &command, 1);
	if (!err)
		applog(LOG_DEBUG, "%s%d: Set Clock to %d MHz", blockerupter->drv->name,
		       blockerupter->device_id, (clock + 1) * 10 / 2);
}

static void blockerupter_setdiff(struct cgpu_info *blockerupter, int diff)
{
	struct blockerupter_info *info;
	info = blockerupter->device_data;
	char command,bits;
	int err;
	int local_diff;

	// min_diff for driver is 64
	if (diff >= 262144) {
		bits = 3;
		local_diff = 262144;
	} else if (diff >= 4096) {
		bits = 2;
		local_diff = 4096;
	} else {
		bits = 1;
		local_diff = 64;
	}

	if (local_diff == info->diff)
		return;

	command = C_DIF | bits;
	err = blockerupter_send(blockerupter, &command, 1);
	if (!err) {
		applog(LOG_DEBUG, "%s%d: Set Diff Bits to %d", blockerupter->drv->name,
		       blockerupter->device_id, bits);
		info->diff = local_diff;
	}
}

static void blockerupter_setrolling(struct cgpu_info *blockerupter, uint8_t rolling)
{
	struct blockerupter_info *info;
	info = blockerupter->device_data;
	char command;
	int err;

	command = C_LPO | rolling;
	err = blockerupter_send(blockerupter, &command, 1);

	if (!err) {
		applog(LOG_DEBUG, "%s%d: Set nTime Rolling to %d seconds", blockerupter->drv->name,
		       blockerupter->device_id, (rolling + 1) * 30);
		info->rolling = (rolling + 1) * 30;
	}
}

static void blockerupter_init(struct cgpu_info *blockerupter)
{
	struct blockerupter_info *info;

	info = blockerupter->device_data;
	// Set Clock
	if (!opt_bet_clk || opt_bet_clk< 19 || opt_bet_clk > 31) {
		opt_bet_clk = BET_CLOCK_DEFAULT;
	}
	blockerupter_setclock(blockerupter, opt_bet_clk);
	info->clock = (opt_bet_clk + 1) * 10;
	info->expected = info->clock * 24 * 32 * info->found / 1000.0;
	// Set Diff
	blockerupter_setdiff(blockerupter, BET_DIFF_DEFAULT);
	info->diff = BET_DIFF_DEFAULT;
	// Set nTime Rolling
	blockerupter_setrolling(blockerupter, BET_ROLLING_DEFAULT);
	info->rolling = (BET_ROLLING_DEFAULT + 1) * 30;
	cgtime(&info->start_time);
}

static struct cgpu_info *blockerupter_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct blockerupter_info *info;
	struct cgpu_info *blockerupter = usb_alloc_cgpu(&blockerupter_drv, 1);
	int i, err;
	char reset = C_RES;

	if (!usb_init(blockerupter, dev, found)) {
		applog(LOG_ERR, "Blockerupter usb init failed");
		blockerupter = usb_free_cgpu(blockerupter);
		return NULL;
	}

	blockerupter->device_data = (struct blockerupter_info *) malloc(sizeof(struct blockerupter_info));
	info = blockerupter->device_data;
	memset(info, 0, sizeof(blockerupter_info));
	blockerupter_init_com(blockerupter);

	err = blockerupter_send(blockerupter, &reset, 1);
	if (err) {
		applog(LOG_ERR, "Blockerupter detect failed");
		blockerupter = usb_free_cgpu(blockerupter);
		return NULL;
	}
	cgsleep_ms(5000);


	for (i = 0; i < BET_MAXBOARDS; i++) {
		char detect, answer;

		answer = 0;
		detect = C_ASK | (uint8_t)i;
		blockerupter_send(blockerupter, &detect, 1);
		blockerupter_read(blockerupter, &answer, 1);
		if (answer == A_WAL) {
		     applog(LOG_DEBUG, "BlockErupter found Board: %d", i);
		     info->boards[i] = 1;
		     info->found++;
		} else {
		     applog(LOG_DEBUG, "BlockErupter missing board: %d, received %02x",
			    i, answer);
		}
	}

	if (!info->found) {
		usb_free_cgpu(blockerupter);
		free(info);
		return NULL;
	} else {
		blockerupter->threads = 1;
		add_cgpu(blockerupter);
		applog(LOG_DEBUG, "Add BlockErupter with %d/%d Boards", info->found,
		       BET_MAXBOARDS);
		blockerupter_init(blockerupter);
		return blockerupter;
	}
}

static inline void blockerupter_detect(bool __maybe_unused hotplug)
{
	usb_detect(&blockerupter_drv, blockerupter_detect_one);
}

static struct api_data *blockerupter_api_stats(struct cgpu_info *blockerupter)
{
	struct blockerupter_info *info = blockerupter->device_data;
	struct api_data *root = NULL;
	struct timeval now, elapsed;
	char buf[32];
	int i;

	cgtime(&now);
	timersub(&now, &info->start_time, &elapsed);

	info->hashrate = elapsed.tv_sec ? info->hashes * 4.295 / elapsed.tv_sec : 0;
	info->eff = info->hashrate / info->expected;

	root = api_add_int(root, "Nonces", &info->nonces, false);
	root = api_add_uint8(root, "Board", &info->found, false);
	root = api_add_int(root, "Clock", &info->clock, false);
	root = api_add_int(root,"Accepted", &info->accepted, false);
	root = api_add_double(root, "HashRate", &info->hashrate , false);
	root = api_add_double(root, "Expected", &info->expected , false);
	root = api_add_double(root, "Efficiency", &info->eff, false);
	for (i = 0; i < BET_MAXBOARDS; i++) {
		double brd_hashrate;

		if (info->boards[i]) {
		     sprintf(buf, "Board%02d accepted", i);
		     root = api_add_int(root, buf, &info->b_info[i].accepted, false);
		     sprintf(buf, "Board%02d nonces", i);
		     root = api_add_int(root, buf, &info->b_info[i].nonces, false);
		     sprintf(buf, "Board%02d hwerror", i);
		     root = api_add_double(root, buf, &info->b_info[i].hwe, false);
		     sprintf(buf, "Board%02d hashrate", i);
		     brd_hashrate = elapsed.tv_sec ? info->b_info[i].hashes * 4.295 / elapsed.tv_sec : 0;
		     root = api_add_double(root, buf, &brd_hashrate, false);
		}
	}

	return root;
}

static bool blockerupter_prepare(struct thr_info *thr)
{
	struct cgpu_info *blockerupter = thr->cgpu;
	struct blockerupter_info *info = blockerupter->device_data;

	cglock_init(&(info->pool.data_lock));

	return true;
}

static void blockerupter_sendjob(struct cgpu_info *blockerupter, int board)
{
	struct blockerupter_info *info = blockerupter->device_data;
	struct thr_info *thr = blockerupter->thr[0];
	struct work *work;
	uint8_t command, answer;
	int err;

	work = get_work(thr, thr->id);
	memcpy(&info->works[info->work_idx],work,sizeof(struct work));

	blockerupter_setdiff(blockerupter,floor(work->work_difficulty));

	command = C_JOB | (uint8_t)board;
	blockerupter_send(blockerupter, (char *)&command, 1);
	blockerupter_mark_mode(blockerupter);
	cgsleep_ms(1);

	blockerupter_send(blockerupter, (char *)(work->midstate), 32);
	blockerupter_send(blockerupter, (char *)&(work->data[64]), 12);
	blockerupter_send(blockerupter, (char *)&work->nonce2, 4);
	blockerupter_send(blockerupter, (char *)&info->work_idx, 1);

	cgsleep_ms(1);
	blockerupter_space_mode(blockerupter);

	answer = 0;
	err = blockerupter_read(blockerupter, (char *)&answer, 1);

	cgtime(&info->last_job);

	if (err || answer != A_GET) {
		applog(LOG_ERR, "%s%d: Sync Error", blockerupter->drv->name, blockerupter->device_id);
	} else {
		info->b_info[board].job_count++;
		applog(LOG_DEBUG, "%s%d: Sent work %d to board %d", blockerupter->drv->name,
		       blockerupter->device_id, info->work_idx, board);
	}

	info->work_idx++;
	if (info->work_idx >= BET_WORK_FIFO)
		info->work_idx = 0;
}

static uint64_t blockerupter_checknonce(struct cgpu_info *blockerupter, struct blockerupter_response *resp, int board)
{
	uint8_t test;
	struct blockerupter_info *info;
	struct thr_info *thr = blockerupter->thr[0];
	struct work work;
	uint32_t nonce;
	uint64_t hashes=0;
	int i;
	struct board_info *cur_brd;
	struct asic_info  *cur_asic;

	info = blockerupter->device_data;
	work = info->works[resp->work_idx];

	nonce = *(uint32_t *)resp->nonce;

	applog(LOG_DEBUG, "%s%d: Nonce %08x from board %d, asic %d for work %d",
	       blockerupter->drv->name, blockerupter->device_id, *(uint32_t *) resp->nonce,
	       board, resp->chip, resp->work_idx);

	memcpy(work.data + 4 + 32 + 32, resp->ntime, 4);
	__bin2hex(work.ntime, resp->ntime, 4);

	info->nonces++;
	cur_brd = &info->b_info[board];
	cur_brd->nonces++;
	cur_asic = &info->b_info[board].asics[resp->chip];
	cur_asic->nonces++;

	for (i = 0; i < BET_NONCE_FIX; i++) {
		test = test_nonce_diff(&work, nonce + i, (double)info->diff);
		if (test) {
		     applog(LOG_DEBUG, "%s%d: Nonce Fix Pass @%d", blockerupter->drv->name,
			    blockerupter->device_id, i);
		     info->hashes += info->diff;
		     cur_brd->hashes += info->diff;
		     cur_asic->hashes += info->diff;
		     if (test_nonce_diff(&work, nonce + i, work.work_difficulty)) {
			if (submit_nonce(thr, &work, nonce + i)) {
				hashes += floor(work.work_difficulty) * (uint64_t) 0xffffffff;
				info->accepted++;
				cur_brd->accepted++;
				cur_asic->accepted++;
			}
		     }
		     break;
		}
	}

	if (i == BET_NONCE_FIX) {
		applog(LOG_DEBUG, "%s%d: Nonce Fix Failed", blockerupter->drv->name,
		       blockerupter->device_id);
		cur_brd->bad++;
		cur_brd->hwe = cur_brd->nonces ? (double)cur_brd->bad / cur_brd->nonces : 0;
		cur_asic->bad++;
		cur_asic->hwe = cur_asic->nonces ? (double)cur_asic->bad / cur_asic->nonces : 0;
	}
	return hashes;
}

static uint64_t blockerupter_getresp(struct cgpu_info *blockerupter, int board)
{
	struct blockerupter_response *resp;
	int err;
	uint64_t hashes = 0;

	resp = (struct blockerupter_response *) malloc(BET_RESP_SZ);
	err = blockerupter_read(blockerupter, (char *)resp, BET_RESP_SZ);
	if (!err)
		hashes = blockerupter_checknonce(blockerupter, resp, board);
	free(resp);
	return hashes;
}

static int64_t blockerupter_scanhash(struct thr_info *thr)
{
	struct cgpu_info *blockerupter = thr->cgpu;
	struct blockerupter_info *info = blockerupter->device_data;
	char ask;
	uint8_t answer;
	int i;
	int64_t hashes=0;

	if (unlikely(blockerupter->usbinfo.nodev)) {
		applog(LOG_ERR, "%s%d: Device disappeared, shutting down thread",
		       blockerupter->drv->name, blockerupter->device_id);
		return -1;
	}

	for (i = 0; i < BET_MAXBOARDS; i++) {
		if (!info->boards[i])
			continue;
		ask = C_ASK | (uint8_t)i;
		blockerupter_send(blockerupter, &ask, 1);
		cgsleep_ms(1);
		answer = 0;
		blockerupter_read(blockerupter, (char *)&answer, 1);

		switch (answer) {
		case A_WAL:
		     blockerupter_sendjob(blockerupter, i);
		     break;
		case A_YES:
		     hashes += blockerupter_getresp(blockerupter, i);
		     break;
		case A_NO:
		     break;
		default:
		     applog(LOG_ERR, "%s%d: Unexpected value %02x received", blockerupter->drv->name,
			    blockerupter->device_id, answer);
		     break;
		}
	}

	return hashes;
}

static void blockerupter_flush_work(struct cgpu_info *blockerupter)
{
	uint8_t command = C_LPO | BET_ROLLING_DEFAULT;

	blockerupter_send(blockerupter, (char *)&command, 1);
}

struct device_drv blockerupter_drv = {
	.drv_id = DRIVER_blockerupter,
	.dname = "blockerupter",
	.name = "BET",
	.min_diff = 64,
	.get_api_stats = blockerupter_api_stats,
	.drv_detect = blockerupter_detect,
	.thread_prepare = blockerupter_prepare,
	.hash_work = hash_driver_work,
	.flush_work = blockerupter_flush_work,
	.scanwork = blockerupter_scanhash
};

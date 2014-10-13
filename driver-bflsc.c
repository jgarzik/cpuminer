/*
 * Copyright 2013 Andrew Smith
 * Copyright 2013-2014 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include "miner.h"
#include "usbutils.h"
#include "uthash.h"
#include "driver-bflsc.h"

int opt_bflsc_overheat = BFLSC_TEMP_OVERHEAT;

static const char *blank = "";

static enum driver_version drv_ver(struct cgpu_info *bflsc, const char *ver)
{
	char *tmp;

	if (strstr(ver, "1.0.0"))
		return BFLSC_DRV1;

	if (strstr(ver, "1.0.") || strstr(ver, "1.1.")) {
		applog(LOG_WARNING, "%s detect (%s) Warning assuming firmware '%s' is Ver1",
			bflsc->drv->dname, bflsc->device_path, ver);
		return BFLSC_DRV1;
	}

	if (strstr(ver, "1.2."))
		return BFLSC_DRV2;

	tmp = str_text((char *)ver);
	applog(LOG_INFO, "%s detect (%s) Warning unknown firmware '%s' using Ver2",
	       bflsc->drv->dname, bflsc->device_path, tmp);
	free(tmp);
	return BFLSC_DRV2;
}

static void xlinkstr(char *xlink, size_t siz, int dev, struct bflsc_info *sc_info)
{
	if (dev > 0)
		snprintf(xlink, siz, " x-%d", dev);
	else {
		if (sc_info->sc_count > 1)
			strcpy(xlink, " master");
		else
			*xlink = '\0';
	}
}

static void bflsc_applog(struct cgpu_info *bflsc, int dev, enum usb_cmds cmd, int amount, int err)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	char xlink[17];

	xlinkstr(xlink, sizeof(xlink), dev, sc_info);

	usb_applog(bflsc, cmd, xlink, amount, err);
}

// Break an input up into lines with LFs removed
// false means an error, but if *lines > 0 then data was also found
// error would be no data or missing LF at the end
static bool tolines(struct cgpu_info *bflsc, int dev, char *buf, int *lines, char ***items, enum usb_cmds cmd)
{
	bool ok = false;
	char *ptr;

#define p_lines (*lines)
#define p_items (*items)

	p_lines = 0;
	p_items = NULL;

	if (!buf || !(*buf)) {
		applog(LOG_DEBUG, "USB: %s%i: (%d) empty %s",
			bflsc->drv->name, bflsc->device_id, dev, usb_cmdname(cmd));
		return ok;
	}

	ptr = strdup(buf);
	while (ptr && *ptr) {
		p_items = realloc(p_items, ++p_lines * sizeof(*p_items));
		if (unlikely(!p_items))
			quit(1, "Failed to realloc p_items in tolines");
		p_items[p_lines-1] = ptr;
		ptr = strchr(ptr, '\n');
		if (ptr)
			*(ptr++) = '\0';
		else {
			applog(LOG_DEBUG, "USB: %s%i: (%d) missing lf(s) in %s",
				bflsc->drv->name, bflsc->device_id, dev, usb_cmdname(cmd));
			return ok;
		}
	}
	ok = true;

	return ok;
}

static void freetolines(int *lines, char ***items)
{
	if (*lines > 0) {
		free(**items);
		free(*items);
	}
	*lines = 0;
	*items = NULL;
}

enum breakmode {
	NOCOLON,
	ONECOLON,
	ALLCOLON // Temperature uses this
};

// Break down a single line into 'fields'
// 'lf' will be a pointer to the final LF if it is there (or NULL)
// firstname will be the allocated buf copy pointer which is also
//  the string before ':' for ONECOLON and ALLCOLON
// If any string is missing the ':' when it was expected, false is returned
static bool breakdown(enum breakmode mode, char *buf, int *count, char **firstname, char ***fields, char **lf)
{
	char *ptr, *colon, *comma;
	bool ok = false;

#define p_count (*count)
#define p_firstname (*firstname)
#define p_fields (*fields)
#define p_lf (*lf)

	p_count = 0;
	p_firstname = NULL;
	p_fields = NULL;
	p_lf = NULL;

	if (!buf || !(*buf))
		return ok;

	ptr = p_firstname = strdup(buf);
	p_lf = strchr(p_firstname, '\n');
	if (mode == ONECOLON) {
		colon = strchr(ptr, ':');
		if (colon) {
			ptr = colon;
			*(ptr++) = '\0';
		} else
			return ok;
	}

	while (ptr && *ptr) {
		if (mode == ALLCOLON) {
			colon = strchr(ptr, ':');
			if (colon)
				ptr = colon + 1;
			else
				return ok;
		}
		comma = strchr(ptr, ',');
		if (comma)
			*(comma++) = '\0';
		p_fields = realloc(p_fields, ++p_count * sizeof(*p_fields));
		if (unlikely(!p_fields))
			quit(1, "Failed to realloc p_fields in breakdown");
		p_fields[p_count-1] = ptr;
		ptr = comma;
	}

	ok = true;
	return ok;
}

static void freebreakdown(int *count, char **firstname, char ***fields)
{
	if (*firstname)
		free(*firstname);
	if (*count > 0)
		free(*fields);
	*count = 0;
	*firstname = NULL;
	*fields = NULL;
}

static bool isokerr(int err, char *buf, int amount)
{
	if (err < 0 || amount < (int)BFLSC_OK_LEN)
		return false;
	else {
		if (strstr(buf, BFLSC_ANERR)) {
			applog(LOG_INFO, "BFLSC not ok err: %s", buf);
			return false;
		} else
			return true;
	}
}

// send+receive dual stage - always single line replies
static int send_recv_ds(struct cgpu_info *bflsc, int dev, int *stage, bool *sent, int *amount, char *send1, int send1_len, enum usb_cmds send1_cmd,  enum usb_cmds recv1_cmd, char *send2, int send2_len, enum usb_cmds send2_cmd, enum usb_cmds recv2_cmd, char *recv, int recv_siz)
{
	struct DataForwardToChain data;
	int len, err, tried;

	if (dev == 0) {
		usb_buffer_clear(bflsc);

		*stage = 1;
		*sent = false;
		err = usb_write(bflsc, send1, send1_len, amount, send1_cmd);
		if (err < 0 || *amount < send1_len)
			return err;

		*sent = true;
		err = usb_read_nl(bflsc, recv, recv_siz, amount, recv1_cmd);
		if (!isokerr(err, recv, *amount))
			return err;

		usb_buffer_clear(bflsc);

		*stage = 2;
		*sent = false;
		err = usb_write(bflsc, send2, send2_len, amount, send2_cmd);
		if (err < 0 || *amount < send2_len)
			return err;

		*sent = true;
		err = usb_read_nl(bflsc, recv, recv_siz, amount, recv2_cmd);

		return err;
	}

	data.header = BFLSC_XLINKHDR;
	data.deviceAddress = (uint8_t)dev;
	tried = 0;
	while (tried++ < 3) {
		data.payloadSize = send1_len;
		memcpy(data.payloadData, send1, send1_len);
		len = DATAFORWARDSIZE(data);

		usb_buffer_clear(bflsc);

		*stage = 1;
		*sent = false;
		err = usb_write(bflsc, (char *)&data, len, amount, send1_cmd);
		if (err < 0 || *amount < send1_len)
			return err;

		*sent = true;
		err = usb_read_nl(bflsc, recv, recv_siz, amount, recv1_cmd);

		if (err != LIBUSB_SUCCESS)
			return err;

		// x-link timeout? - try again?
		if (strstr(recv, BFLSC_XTIMEOUT))
			continue;

		if (!isokerr(err, recv, *amount))
			return err;

		data.payloadSize = send2_len;
		memcpy(data.payloadData, send2, send2_len);
		len = DATAFORWARDSIZE(data);

		usb_buffer_clear(bflsc);

		*stage = 2;
		*sent = false;
		err = usb_write(bflsc, (char *)&data, len, amount, send2_cmd);
		if (err < 0 || *amount < send2_len)
			return err;

		*sent = true;
		err = usb_read_nl(bflsc, recv, recv_siz, amount, recv2_cmd);

		if (err != LIBUSB_SUCCESS)
			return err;

		// x-link timeout? - try again?
		if (strstr(recv, BFLSC_XTIMEOUT))
			continue;

		// SUCCESS - return it
		break;
	}
	return err;
}

#define READ_OK true
#define READ_NL false

// send+receive single stage
static int send_recv_ss(struct cgpu_info *bflsc, int dev, bool *sent, int *amount, char *send, int send_len, enum usb_cmds send_cmd, char *recv, int recv_siz, enum usb_cmds recv_cmd, bool read_ok)
{
	struct DataForwardToChain data;
	int len, err, tried;

	if (dev == 0) {
		usb_buffer_clear(bflsc);

		*sent = false;
		err = usb_write(bflsc, send, send_len, amount, send_cmd);
		if (err < 0 || *amount < send_len) {
			// N.B. thus !(*sent) directly implies err < 0 or *amount < send_len
			return err;
		}

		*sent = true;
		if (read_ok == READ_OK)
			err = usb_read_ok(bflsc, recv, recv_siz, amount, recv_cmd);
		else
			err = usb_read_nl(bflsc, recv, recv_siz, amount, recv_cmd);

		return err;
	}

	data.header = BFLSC_XLINKHDR;
	data.deviceAddress = (uint8_t)dev;
	data.payloadSize = send_len;
	memcpy(data.payloadData, send, send_len);
	len = DATAFORWARDSIZE(data);

	tried = 0;
	while (tried++ < 3) {
		usb_buffer_clear(bflsc);

		*sent = false;
		err = usb_write(bflsc, (char *)&data, len, amount, recv_cmd);
		if (err < 0 || *amount < send_len)
			return err;

		*sent = true;
		if (read_ok == READ_OK)
			err = usb_read_ok(bflsc, recv, recv_siz, amount, recv_cmd);
		else
			err = usb_read_nl(bflsc, recv, recv_siz, amount, recv_cmd);

		if (err != LIBUSB_SUCCESS && err != LIBUSB_ERROR_TIMEOUT)
			return err;

		// read_ok can err timeout if it's looking for OK<LF>
		// TODO: add a usb_read() option to spot the ERR: and convert end=OK<LF> to just <LF>
		// x-link timeout? - try again?
		if ((err == LIBUSB_SUCCESS || (read_ok == READ_OK && err == LIBUSB_ERROR_TIMEOUT)) &&
			strstr(recv, BFLSC_XTIMEOUT))
				continue;

		// SUCCESS or TIMEOUT - return it
		break;
	}
	return err;
}

static int write_to_dev(struct cgpu_info *bflsc, int dev, char *buf, int buflen, int *amount, enum usb_cmds cmd)
{
	struct DataForwardToChain data;
	int len;

	/*
	 * The protocol is syncronous so any previous excess can be
	 * discarded and assumed corrupt data or failed USB transfers
	 */
	usb_buffer_clear(bflsc);

	if (dev == 0)
		return usb_write(bflsc, buf, buflen, amount, cmd);

	data.header = BFLSC_XLINKHDR;
	data.deviceAddress = (uint8_t)dev;
	data.payloadSize = buflen;
	memcpy(data.payloadData, buf, buflen);
	len = DATAFORWARDSIZE(data);

	return usb_write(bflsc, (char *)&data, len, amount, cmd);
}

static void bflsc_send_flush_work(struct cgpu_info *bflsc, int dev)
{
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	bool sent;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return;

	mutex_lock(&bflsc->device_mutex);
	err = send_recv_ss(bflsc, dev, &sent, &amount,
				BFLSC_QFLUSH, BFLSC_QFLUSH_LEN, C_QUEFLUSH,
				buf, sizeof(buf)-1, C_QUEFLUSHREPLY, READ_NL);
	mutex_unlock(&bflsc->device_mutex);

	if (!sent)
		bflsc_applog(bflsc, dev, C_QUEFLUSH, amount, err);
	else {
		// TODO: do we care if we don't get 'OK'? (always will in normal processing)
	}
}

/* return True = attempted usb_read_ok()
 * set ignore to true means no applog/ignore errors */
static bool bflsc_qres(struct cgpu_info *bflsc, char *buf, size_t bufsiz, int dev, int *err, int *amount, bool ignore)
{
	bool readok = false;

	mutex_lock(&(bflsc->device_mutex));
	*err = send_recv_ss(bflsc, dev, &readok, amount,
				BFLSC_QRES, BFLSC_QRES_LEN, C_REQUESTRESULTS,
				buf, bufsiz-1, C_GETRESULTS, READ_OK);
	mutex_unlock(&(bflsc->device_mutex));

	if (!readok) {
		if (!ignore)
			bflsc_applog(bflsc, dev, C_REQUESTRESULTS, *amount, *err);

		// TODO: do what? flag as dead device?
		// count how many times it has happened and reset/fail it
		// or even make sure it is all x-link and that means device
		// has failed after some limit of this?
		// of course all other I/O must also be failing ...
	} else {
		if (*err < 0 || *amount < 1) {
			if (!ignore)
				bflsc_applog(bflsc, dev, C_GETRESULTS, *amount, *err);

			// TODO: do what? ... see above
		}
	}

	return readok;
}

static void __bflsc_initialise(struct cgpu_info *bflsc)
{
	int err, interface;

// TODO: does x-link bypass the other device FTDI? (I think it does)
//	So no initialisation required except for the master device?

	if (bflsc->usbinfo.nodev)
		return;

	interface = usb_interface(bflsc);
	// Reset
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_RESET, interface, C_RESET);

	applog(LOG_DEBUG, "%s%i: reset got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	usb_ftdi_set_latency(bflsc);

	if (bflsc->usbinfo.nodev)
		return;

	// Set data control
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_DATA,
				FTDI_VALUE_DATA_BAS, interface, C_SETDATA);

	applog(LOG_DEBUG, "%s%i: setdata got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Set the baud
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, FTDI_VALUE_BAUD_BAS,
				(FTDI_INDEX_BAUD_BAS & 0xff00) | interface,
				C_SETBAUD);

	applog(LOG_DEBUG, "%s%i: setbaud got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
				FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Set Modem Control
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
				FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Clear any sent data
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_PURGE_TX, interface, C_PURGETX);

	applog(LOG_DEBUG, "%s%i: purgetx got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (bflsc->usbinfo.nodev)
		return;

	// Clear any received data
	err = usb_transfer(bflsc, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
				FTDI_VALUE_PURGE_RX, interface, C_PURGERX);

	applog(LOG_DEBUG, "%s%i: purgerx got err %d",
		bflsc->drv->name, bflsc->device_id, err);

	if (!bflsc->cutofftemp)
		bflsc->cutofftemp = opt_bflsc_overheat;
}

static void bflsc_initialise(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	int dev;

	mutex_lock(&(bflsc->device_mutex));
	__bflsc_initialise(bflsc);
	mutex_unlock(&(bflsc->device_mutex));

	for (dev = 0; dev < sc_info->sc_count; dev++) {
		bflsc_send_flush_work(bflsc, dev);
		bflsc_qres(bflsc, buf, sizeof(buf), dev, &err, &amount, true);
	}
}

static bool getinfo(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	struct bflsc_dev sc_dev;
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	char **items, *firstname, **fields, *lf;
	bool res, ok = false;
	int i, lines, count;
	char *tmp;

	/*
	 * Kano's first dev Jalapeno output:
	 * DEVICE: BitFORCE SC<LF>
	 * FIRMWARE: 1.0.0<LF>
	 * ENGINES: 30<LF>
	 * FREQUENCY: [UNKNOWN]<LF>
	 * XLINK MODE: MASTER<LF>
	 * XLINK PRESENT: YES<LF>
	 * --DEVICES IN CHAIN: 0<LF>
	 * --CHAIN PRESENCE MASK: 00000000<LF>
	 * OK<LF>
	 */

	/*
	 * Don't use send_recv_ss() since we have a different receive timeout
	 * Also getinfo() is called multiple times if it fails anyway
	 */
	err = write_to_dev(bflsc, dev, BFLSC_DETAILS, BFLSC_DETAILS_LEN, &amount, C_REQUESTDETAILS);
	if (err < 0 || amount != BFLSC_DETAILS_LEN) {
		applog(LOG_ERR, "%s detect (%s) send details request failed (%d:%d)",
			bflsc->drv->dname, bflsc->device_path, amount, err);
		return ok;
	}

	err = usb_read_ok_timeout(bflsc, buf, sizeof(buf)-1, &amount,
				  BFLSC_INFO_TIMEOUT, C_GETDETAILS);
	if (err < 0 || amount < 1) {
		if (err < 0) {
			applog(LOG_ERR, "%s detect (%s) get details return invalid/timed out (%d:%d)",
					bflsc->drv->dname, bflsc->device_path, amount, err);
		} else {
			applog(LOG_ERR, "%s detect (%s) get details returned nothing (%d:%d)",
					bflsc->drv->dname, bflsc->device_path, amount, err);
		}
		return ok;
	}

	memset(&sc_dev, 0, sizeof(struct bflsc_dev));
	sc_info->sc_count = 1;
	res = tolines(bflsc, dev, &(buf[0]), &lines, &items, C_GETDETAILS);
	if (!res)
		return ok;

	tmp = str_text(buf);
	strncpy(sc_dev.getinfo, tmp, sizeof(sc_dev.getinfo));
	sc_dev.getinfo[sizeof(sc_dev.getinfo)-1] = '\0';
	free(tmp);

	for (i = 0; i < lines-2; i++) {
		res = breakdown(ONECOLON, items[i], &count, &firstname, &fields, &lf);
		if (lf)
			*lf = '\0';
		if (!res || count != 1) {
			tmp = str_text(items[i]);
			applogsiz(LOG_WARNING, BFLSC_APPLOGSIZ,
					"%s detect (%s) invalid details line: '%s' %d",
					bflsc->drv->dname, bflsc->device_path, tmp, count);
			free(tmp);
			dev_error(bflsc, REASON_DEV_COMMS_ERROR);
			goto mata;
		}
		if (strstr(firstname, BFLSC_DI_FIRMWARE)) {
			sc_dev.firmware = strdup(fields[0]);
			sc_info->driver_version = drv_ver(bflsc, sc_dev.firmware);
		}
		else if (Strcasestr(firstname, BFLSC_DI_ENGINES)) {
			sc_dev.engines = atoi(fields[0]);
			if (sc_dev.engines < 1) {
				tmp = str_text(items[i]);
				applogsiz(LOG_WARNING, BFLSC_APPLOGSIZ,
						"%s detect (%s) invalid engine count: '%s'",
						bflsc->drv->dname, bflsc->device_path, tmp);
				free(tmp);
				goto mata;
			}
		}
		else if (strstr(firstname, BFLSC_DI_XLINKMODE))
			sc_dev.xlink_mode = strdup(fields[0]);
		else if (strstr(firstname, BFLSC_DI_XLINKPRESENT))
			sc_dev.xlink_present = strdup(fields[0]);
		else if (strstr(firstname, BFLSC_DI_DEVICESINCHAIN)) {
			if (fields[0][0] == '0' ||
			    (fields[0][0] == ' ' && fields[0][1] == '0'))
				sc_info->sc_count = 1;
			else
				sc_info->sc_count = atoi(fields[0]);
			if (sc_info->sc_count < 1 || sc_info->sc_count > 30) {
				tmp = str_text(items[i]);
				applogsiz(LOG_WARNING, BFLSC_APPLOGSIZ,
						"%s detect (%s) invalid x-link count: '%s'",
						bflsc->drv->dname, bflsc->device_path, tmp);
				free(tmp);
				goto mata;
			}
		}
		else if (strstr(firstname, BFLSC_DI_CHIPS))
			sc_dev.chips = strdup(fields[0]);
		else if (strstr(firstname, BFLSC28_DI_ASICS))
			sc_dev.chips = strdup(fields[0]);

		freebreakdown(&count, &firstname, &fields);
	}

	if (sc_info->driver_version == BFLSC_DRVUNDEF) {
		applog(LOG_WARNING, "%s detect (%s) missing %s",
			bflsc->drv->dname, bflsc->device_path, BFLSC_DI_FIRMWARE);
		goto ne;
	}

	sc_info->sc_devs = calloc(sc_info->sc_count, sizeof(struct bflsc_dev));
	if (unlikely(!sc_info->sc_devs))
		quit(1, "Failed to calloc in getinfo");
	memcpy(&(sc_info->sc_devs[0]), &sc_dev, sizeof(sc_dev));
	// TODO: do we care about getting this info for the rest if > 0 x-link

	ok = true;
	goto ne;

mata:
	freebreakdown(&count, &firstname, &fields);
	ok = false;
ne:
	freetolines(&lines, &items);
	return ok;
}

static bool bflsc28_queue_full(struct cgpu_info *bflsc);

static struct cgpu_info *bflsc_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct bflsc_info *sc_info = NULL;
	char buf[BFLSC_BUFSIZ+1];
	int i, err, amount;
	struct timeval init_start, init_now;
	int init_sleep, init_count;
	bool ident_first, sent;
	char *newname;
	uint16_t latency;

	struct cgpu_info *bflsc = usb_alloc_cgpu(&bflsc_drv, 1);

	sc_info = calloc(1, sizeof(*sc_info));
	if (unlikely(!sc_info))
		quit(1, "Failed to calloc sc_info in bflsc_detect_one");
	// TODO: fix ... everywhere ...
	bflsc->device_data = (FILE *)sc_info;

	if (!usb_init(bflsc, dev, found))
		goto shin;

	// Allow 2 complete attempts if the 1st time returns an unrecognised reply
	ident_first = true;
retry:
	init_count = 0;
	init_sleep = REINIT_TIME_FIRST_MS;
	cgtime(&init_start);
reinit:
	__bflsc_initialise(bflsc);

	err = send_recv_ss(bflsc, 0, &sent, &amount,
				BFLSC_IDENTIFY, BFLSC_IDENTIFY_LEN, C_REQUESTIDENTIFY,
				buf, sizeof(buf)-1, C_GETIDENTIFY, READ_NL);

	if (!sent) {
		applog(LOG_ERR, "%s detect (%s) send identify request failed (%d:%d)",
			bflsc->drv->dname, bflsc->device_path, amount, err);
		goto unshin;
	}

	if (err < 0 || amount < 1) {
		init_count++;
		cgtime(&init_now);
		if (us_tdiff(&init_now, &init_start) <= REINIT_TIME_MAX) {
			if (init_count == 2) {
				applog(LOG_WARNING, "%s detect (%s) 2nd init failed (%d:%d) - retrying",
					bflsc->drv->dname, bflsc->device_path, amount, err);
			}
			cgsleep_ms(init_sleep);
			if ((init_sleep * 2) <= REINIT_TIME_MAX_MS)
				init_sleep *= 2;
			goto reinit;
		}

		if (init_count > 0)
			applog(LOG_WARNING, "%s detect (%s) init failed %d times %.2fs",
				bflsc->drv->dname, bflsc->device_path, init_count, tdiff(&init_now, &init_start));

		if (err < 0) {
			applog(LOG_ERR, "%s detect (%s) error identify reply (%d:%d)",
				bflsc->drv->dname, bflsc->device_path, amount, err);
		} else {
			applog(LOG_ERR, "%s detect (%s) empty identify reply (%d)",
				bflsc->drv->dname, bflsc->device_path, amount);
		}

		goto unshin;
	}
	buf[amount] = '\0';

	if (unlikely(!strstr(buf, BFLSC_BFLSC) && !strstr(buf, BFLSC_BFLSC28))) {
		applog(LOG_DEBUG, "%s detect (%s) found an FPGA '%s' ignoring",
			bflsc->drv->dname, bflsc->device_path, buf);
		goto unshin;
	}

	if (unlikely(strstr(buf, BFLSC_IDENTITY))) {
		if (ident_first) {
			applog(LOG_DEBUG, "%s detect (%s) didn't recognise '%s' trying again ...",
				bflsc->drv->dname, bflsc->device_path, buf);
			ident_first = false;
			goto retry;
		}
		applog(LOG_DEBUG, "%s detect (%s) didn't recognise '%s' on 2nd attempt",
			bflsc->drv->dname, bflsc->device_path, buf);
		goto unshin;
	}

	int tries = 0;
	while (7734) {
		if (getinfo(bflsc, 0))
			break;

		// N.B. we will get displayed errors each time it fails
		if (++tries > 2)
			goto unshin;

		cgsleep_ms(40);
	}

	switch (sc_info->driver_version) {
		case BFLSC_DRV1:
			sc_info->que_size = BFLSC_QUE_SIZE_V1;
			sc_info->que_full_enough = BFLSC_QUE_FULL_ENOUGH_V1;
			sc_info->que_watermark = BFLSC_QUE_WATERMARK_V1;
			sc_info->que_low = BFLSC_QUE_LOW_V1;
			sc_info->que_noncecount = QUE_NONCECOUNT_V1;
			sc_info->que_fld_min = QUE_FLD_MIN_V1;
			sc_info->que_fld_max = QUE_FLD_MAX_V1;
			// Only Jalapeno uses 1.0.0
			sc_info->flush_size = 1;
			break;
		case BFLSC_DRV2:
		case BFLSC_DRVUNDEF:
		default:
			sc_info->driver_version = BFLSC_DRV2;

			sc_info->que_size = BFLSC_QUE_SIZE_V2;
			sc_info->que_full_enough = BFLSC_QUE_FULL_ENOUGH_V2;
			sc_info->que_watermark = BFLSC_QUE_WATERMARK_V2;
			sc_info->que_low = BFLSC_QUE_LOW_V2;
			sc_info->que_noncecount = QUE_NONCECOUNT_V2;
			sc_info->que_fld_min = QUE_FLD_MIN_V2;
			sc_info->que_fld_max = QUE_FLD_MAX_V2;
			// TODO: this can be reduced to total chip count
			sc_info->flush_size = 16 * sc_info->sc_count;
			break;
	}

	// Set parallelization based on the getinfo() response if it is present
	if (sc_info->sc_devs[0].chips && strlen(sc_info->sc_devs[0].chips)) {
		if (strstr(sc_info->sc_devs[0].chips, BFLSC_DI_CHIPS_PARALLEL)) {
			sc_info->que_noncecount = QUE_NONCECOUNT_V2;
			sc_info->que_fld_min = QUE_FLD_MIN_V2;
			sc_info->que_fld_max = QUE_FLD_MAX_V2;
		} else {
			sc_info->que_noncecount = QUE_NONCECOUNT_V1;
			sc_info->que_fld_min = QUE_FLD_MIN_V1;
			sc_info->que_fld_max = QUE_FLD_MAX_V1;
		}
	}

	sc_info->scan_sleep_time = BAS_SCAN_TIME;
	sc_info->results_sleep_time = BFLSC_RES_TIME;
	sc_info->default_ms_work = BAS_WORK_TIME;
	latency = BAS_LATENCY;

	/* When getinfo() "FREQUENCY: [UNKNOWN]" is fixed -
	 * use 'freq * engines' to estimate.
	 * Otherwise for now: */
	newname = NULL;
	if (sc_info->sc_count > 1) {
		newname = BFLSC_MINIRIG;
		sc_info->scan_sleep_time = BAM_SCAN_TIME;
		sc_info->default_ms_work = BAM_WORK_TIME;
		bflsc->usbdev->ident = IDENT_BAM;
		latency = BAM_LATENCY;
	} else {
		if (sc_info->sc_devs[0].engines < 34) { // 16 * 2 + 2
			newname = BFLSC_JALAPENO;
			sc_info->scan_sleep_time = BAJ_SCAN_TIME;
			sc_info->default_ms_work = BAJ_WORK_TIME;
			bflsc->usbdev->ident = IDENT_BAJ;
			latency = BAJ_LATENCY;
		} else if (sc_info->sc_devs[0].engines < 130)  { // 16 * 8 + 2
			newname = BFLSC_LITTLESINGLE;
			sc_info->scan_sleep_time = BAL_SCAN_TIME;
			sc_info->default_ms_work = BAL_WORK_TIME;
			bflsc->usbdev->ident = IDENT_BAL;
			latency = BAL_LATENCY;
		}
	}

	sc_info->ident = usb_ident(bflsc);
	if (sc_info->ident == IDENT_BMA) {
		bflsc->drv->queue_full = &bflsc28_queue_full;
		sc_info->scan_sleep_time = BMA_SCAN_TIME;
		sc_info->default_ms_work = BMA_WORK_TIME;
		sc_info->results_sleep_time = BMA_RES_TIME;
	}

	if (latency != bflsc->usbdev->found->latency) {
		bflsc->usbdev->found->latency = latency;
		usb_ftdi_set_latency(bflsc);
	}

	for (i = 0; i < sc_info->sc_count; i++)
		sc_info->sc_devs[i].ms_work = sc_info->default_ms_work;

	if (newname) {
		if (!bflsc->drv->copy)
			bflsc->drv = copy_drv(bflsc->drv);
		bflsc->drv->name = newname;
	}

	// We have a real BFLSC!
	applog(LOG_DEBUG, "%s (%s) identified as: '%s'",
		bflsc->drv->dname, bflsc->device_path, bflsc->drv->name);

	if (!add_cgpu(bflsc))
		goto unshin;

	update_usb_stats(bflsc);

	mutex_init(&bflsc->device_mutex);
	rwlock_init(&sc_info->stat_lock);

	return bflsc;

unshin:

	usb_uninit(bflsc);

shin:

	free(bflsc->device_data);
	bflsc->device_data = NULL;

	if (bflsc->name != blank) {
		free(bflsc->name);
		bflsc->name = NULL;
	}

	bflsc = usb_free_cgpu(bflsc);

	return NULL;
}

static void bflsc_detect(bool __maybe_unused hotplug)
{
	usb_detect(&bflsc_drv, bflsc_detect_one);
}

static void get_bflsc_statline_before(char *buf, size_t bufsiz, struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	float temp = 0;
	float vcc2 = 0;
	int i;

	rd_lock(&(sc_info->stat_lock));
	for (i = 0; i < sc_info->sc_count; i++) {
		if (sc_info->sc_devs[i].temp1 > temp)
			temp = sc_info->sc_devs[i].temp1;
		if (sc_info->sc_devs[i].temp2 > temp)
			temp = sc_info->sc_devs[i].temp2;
		if (sc_info->sc_devs[i].vcc2 > vcc2)
			vcc2 = sc_info->sc_devs[i].vcc2;
	}
	rd_unlock(&(sc_info->stat_lock));

	tailsprintf(buf, bufsiz, "max%3.0fC %4.2fV", temp, vcc2);
}

static void flush_one_dev(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	struct work *work, *tmp;
	bool did = false;

	bflsc_send_flush_work(bflsc, dev);

	rd_lock(&bflsc->qlock);

	HASH_ITER(hh, bflsc->queued_work, work, tmp) {
		if (work->subid == dev) {
			// devflag is used to flag stale work
			work->devflag = true;
			did = true;
		}
	}

	rd_unlock(&bflsc->qlock);

	if (did) {
		wr_lock(&(sc_info->stat_lock));
		sc_info->sc_devs[dev].flushed = true;
		sc_info->sc_devs[dev].flush_id = sc_info->sc_devs[dev].result_id;
		sc_info->sc_devs[dev].work_queued = 0;
		wr_unlock(&(sc_info->stat_lock));
	}
}

static void bflsc_flush_work(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	int dev;

	for (dev = 0; dev < sc_info->sc_count; dev++)
		flush_one_dev(bflsc, dev);
}

static void bflsc_set_volt(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	char buf[BFLSC_BUFSIZ+1];
	char msg[16];
	int err, amount;
	bool sent;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return;

	snprintf(msg, sizeof(msg), "V%dX", sc_info->volt_next);

	mutex_lock(&bflsc->device_mutex);

	err = send_recv_ss(bflsc, dev, &sent, &amount,
				msg, strlen(msg), C_SETVOLT,
				buf, sizeof(buf)-1, C_REPLYSETVOLT, READ_NL);
	mutex_unlock(&(bflsc->device_mutex));

	if (!sent)
		bflsc_applog(bflsc, dev, C_SETVOLT, amount, err);
	else {
		// Don't care
	}

	sc_info->volt_next_stat = false;

	return;
}

static void bflsc_set_clock(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	char buf[BFLSC_BUFSIZ+1];
	char msg[16];
	int err, amount;
	bool sent;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return;

	snprintf(msg, sizeof(msg), "F%XX", sc_info->clock_next);

	mutex_lock(&bflsc->device_mutex);

	err = send_recv_ss(bflsc, dev, &sent, &amount,
				msg, strlen(msg), C_SETCLOCK,
				buf, sizeof(buf)-1, C_REPLYSETCLOCK, READ_NL);
	mutex_unlock(&(bflsc->device_mutex));

	if (!sent)
		bflsc_applog(bflsc, dev, C_SETCLOCK, amount, err);
	else {
		// Don't care
	}

	sc_info->clock_next_stat = false;

	return;
}

static void bflsc_flash_led(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	bool sent;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return;

	// It is not critical flashing the led so don't get stuck if we
	// can't grab the mutex now
	if (mutex_trylock(&bflsc->device_mutex))
		return;

	err = send_recv_ss(bflsc, dev, &sent, &amount,
				BFLSC_FLASH, BFLSC_FLASH_LEN, C_REQUESTFLASH,
				buf, sizeof(buf)-1, C_FLASHREPLY, READ_NL);
	mutex_unlock(&(bflsc->device_mutex));

	if (!sent)
		bflsc_applog(bflsc, dev, C_REQUESTFLASH, amount, err);
	else {
		// Don't care
	}

	// Once we've tried - don't do it until told to again
	// - even if it failed
	sc_info->flash_led = false;

	return;
}

/* Flush and stop all work if the device reaches the thermal cutoff temp, or
 * temporarily stop queueing work if it's in the throttling range. */
static void bflsc_manage_temp(struct cgpu_info *bflsc, struct bflsc_dev *sc_dev,
			      int dev, float temp)
{
	bflsc->temp = temp;
	if (bflsc->cutofftemp > 0) {
		int cutoff = bflsc->cutofftemp;
		int throttle = cutoff - BFLSC_TEMP_THROTTLE;
		int recover = cutoff - BFLSC_TEMP_RECOVER;

		if (sc_dev->overheat) {
			if (temp < recover)
				sc_dev->overheat = false;
		} else if (temp > throttle) {
			sc_dev->overheat = true;
			if (temp > cutoff) {
				applog(LOG_WARNING, "%s%i: temp (%.1f) hit thermal cutoff limit %d, stopping work!",
				       bflsc->drv->name, bflsc->device_id, temp, cutoff);
				dev_error(bflsc, REASON_DEV_THERMAL_CUTOFF);
				flush_one_dev(bflsc, dev);

			} else {
				applog(LOG_NOTICE, "%s%i: temp (%.1f) hit thermal throttle limit %d, throttling",
				       bflsc->drv->name, bflsc->device_id, temp, throttle);
			}
		}
	}
}

static bool bflsc_get_temp(struct cgpu_info *bflsc, int dev)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	struct bflsc_dev *sc_dev;
	char temp_buf[BFLSC_BUFSIZ+1];
	char volt_buf[BFLSC_BUFSIZ+1];
	char *tmp;
	int err, amount;
	char *firstname, **fields, *lf;
	char xlink[17];
	int count;
	bool res, sent;
	float temp, temp1, temp2;
	float vcc1, vcc2, vmain;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return false;

	if (dev >= sc_info->sc_count) {
		applog(LOG_ERR, "%s%i: temp invalid xlink device %d - limit %d",
			bflsc->drv->name, bflsc->device_id, dev, sc_info->sc_count - 1);
		return false;
	}

	if (sc_info->volt_next_stat || sc_info->clock_next_stat) {
		if (sc_info->volt_next_stat)
			bflsc_set_volt(bflsc, dev);
		if (sc_info->clock_next_stat)
			bflsc_set_clock(bflsc, dev);
		return true;
	}

	// Flash instead of Temp
	if (sc_info->flash_led) {
		bflsc_flash_led(bflsc, dev);
		return true;
	}

	xlinkstr(xlink, sizeof(xlink), dev, sc_info);

	/* It is not very critical getting temp so don't get stuck if we
	 * can't grab the mutex here */
	if (mutex_trylock(&bflsc->device_mutex))
		return false;

	err = send_recv_ss(bflsc, dev, &sent, &amount,
				BFLSC_TEMPERATURE, BFLSC_TEMPERATURE_LEN, C_REQUESTTEMPERATURE,
				temp_buf, sizeof(temp_buf)-1, C_GETTEMPERATURE, READ_NL);
	mutex_unlock(&(bflsc->device_mutex));

	if (!sent) {
		applog(LOG_ERR, "%s%i: Error: Request%s temp invalid/timed out (%d:%d)",
				bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		return false;
	} else {
		if (err < 0 || amount < 1) {
			if (err < 0) {
				applog(LOG_ERR, "%s%i: Error: Get%s temp return invalid/timed out (%d:%d)",
						bflsc->drv->name, bflsc->device_id, xlink, amount, err);
			} else {
				applog(LOG_ERR, "%s%i: Error: Get%s temp returned nothing (%d:%d)",
						bflsc->drv->name, bflsc->device_id, xlink, amount, err);
			}
			return false;
		}
	}

	// Ignore it if we can't get the V
	if (mutex_trylock(&bflsc->device_mutex))
		return false;

	err = send_recv_ss(bflsc, dev, &sent, &amount,
				BFLSC_VOLTAGE, BFLSC_VOLTAGE_LEN, C_REQUESTVOLTS,
				volt_buf, sizeof(volt_buf)-1, C_GETVOLTS, READ_NL);
	mutex_unlock(&(bflsc->device_mutex));

	if (!sent) {
		applog(LOG_ERR, "%s%i: Error: Request%s volts invalid/timed out (%d:%d)",
				bflsc->drv->name, bflsc->device_id, xlink, amount, err);
		return false;
	} else {
		if (err < 0 || amount < 1) {
			if (err < 0) {
				applog(LOG_ERR, "%s%i: Error: Get%s volt return invalid/timed out (%d:%d)",
						bflsc->drv->name, bflsc->device_id, xlink, amount, err);
			} else {
				applog(LOG_ERR, "%s%i: Error: Get%s volt returned nothing (%d:%d)",
						bflsc->drv->name, bflsc->device_id, xlink, amount, err);
			}
			return false;
		}
	}

	res = breakdown(ALLCOLON, temp_buf, &count, &firstname, &fields, &lf);
	if (lf)
		*lf = '\0';
	if (!res || count < 2 || !lf) {
		tmp = str_text(temp_buf);
		applog(LOG_WARNING, "%s%i: Invalid%s temp reply: '%s'",
				bflsc->drv->name, bflsc->device_id, xlink, tmp);
		free(tmp);
		freebreakdown(&count, &firstname, &fields);
		dev_error(bflsc, REASON_DEV_COMMS_ERROR);
		return false;
	}

	temp = temp1 = (float)atoi(fields[0]);
	temp2 = (float)atoi(fields[1]);

	freebreakdown(&count, &firstname, &fields);

	res = breakdown(NOCOLON, volt_buf, &count, &firstname, &fields, &lf);
	if (lf)
		*lf = '\0';
	if (!res || count != 3 || !lf) {
		tmp = str_text(volt_buf);
		applog(LOG_WARNING, "%s%i: Invalid%s volt reply: '%s'",
				bflsc->drv->name, bflsc->device_id, xlink, tmp);
		free(tmp);
		freebreakdown(&count, &firstname, &fields);
		dev_error(bflsc, REASON_DEV_COMMS_ERROR);
		return false;
	}

	sc_dev = &sc_info->sc_devs[dev];
	vcc1 = (float)atoi(fields[0]) / 1000.0;
	vcc2 = (float)atoi(fields[1]) / 1000.0;
	vmain = (float)atoi(fields[2]) / 1000.0;

	freebreakdown(&count, &firstname, &fields);

	if (vcc1 > 0 || vcc2 > 0 || vmain > 0) {
		wr_lock(&(sc_info->stat_lock));
		if (vcc1 > 0) {
			if (unlikely(sc_dev->vcc1 == 0))
				sc_dev->vcc1 = vcc1;
			else {
				sc_dev->vcc1 += vcc1 * 0.63;
				sc_dev->vcc1 /= 1.63;
			}
		}
		if (vcc2 > 0) {
			if (unlikely(sc_dev->vcc2 == 0))
				sc_dev->vcc2 = vcc2;
			else {
				sc_dev->vcc2 += vcc2 * 0.63;
				sc_dev->vcc2 /= 1.63;
			}
		}
		if (vmain > 0) {
			if (unlikely(sc_dev->vmain == 0))
				sc_dev->vmain = vmain;
			else {
				sc_dev->vmain += vmain * 0.63;
				sc_dev->vmain /= 1.63;
			}
		}
		wr_unlock(&(sc_info->stat_lock));
	}

	if (temp1 > 0 || temp2 > 0) {
		wr_lock(&(sc_info->stat_lock));
		if (unlikely(!sc_dev->temp1))
			sc_dev->temp1 = temp1;
		else {
			sc_dev->temp1 += temp1 * 0.63;
			sc_dev->temp1 /= 1.63;
		}
		if (unlikely(!sc_dev->temp2))
			sc_dev->temp2 = temp2;
		else {
			sc_dev->temp2 += temp2 * 0.63;
			sc_dev->temp2 /= 1.63;
		}
		if (temp1 > sc_dev->temp1_max) {
			sc_dev->temp1_max = temp1;
			sc_dev->temp1_max_time = time(NULL);
		}
		if (temp2 > sc_dev->temp2_max) {
			sc_dev->temp2_max = temp2;
			sc_dev->temp2_max_time = time(NULL);
		}

		if (unlikely(sc_dev->temp1_5min_av == 0))
			sc_dev->temp1_5min_av = temp1;
		else {
			sc_dev->temp1_5min_av += temp1 * .0042;
			sc_dev->temp1_5min_av /= 1.0042;
		}
		if (unlikely(sc_dev->temp2_5min_av == 0))
			sc_dev->temp2_5min_av = temp2;
		else {
			sc_dev->temp2_5min_av += temp2 * .0042;
			sc_dev->temp2_5min_av /= 1.0042;
		}
		wr_unlock(&(sc_info->stat_lock));

		if (temp < temp2)
			temp = temp2;

		bflsc_manage_temp(bflsc, sc_dev, dev, temp);
	}

	return true;
}

static void inc_core_errors(struct bflsc_info *info, int8_t core)
{
	if (info->ident == IDENT_BMA) {
		if (core >= 0)
			info->cortex_hw[core]++;
	} else {
		if (core >= 0 && core < 16)
			info->core_hw[core]++;
	}
}

static void inc_bflsc_errors(struct thr_info *thr, struct bflsc_info *info, int8_t core)
{
	inc_hw_errors(thr);
	inc_core_errors(info, core);
}

static void inc_bflsc_nonces(struct bflsc_info *info, int8_t core)
{
	if (info->ident == IDENT_BMA) {
		if (core >= 0)
			info->cortex_nonces[core]++;
	} else {
		if (core >= 0 && core < 16)
			info->core_nonces[core]++;
	}
}

struct work *bflsc_work_by_uid(struct cgpu_info *bflsc, struct bflsc_info *sc_info, int id)
{
	struct bflsc_work *bwork;
	struct work *work = NULL;

	wr_lock(&bflsc->qlock);
	HASH_FIND_INT(sc_info->bworks, &id, bwork);
	if (likely(bwork)) {
		HASH_DEL(sc_info->bworks, bwork);
		work = bwork->work;
		free(bwork);
	}
	wr_unlock(&bflsc->qlock);

	return work;
}

static void process_nonces(struct cgpu_info *bflsc, int dev, char *xlink, char *data, int count, char **fields, int *nonces)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	struct thr_info *thr = bflsc->thr[0];
	struct work *work = NULL;
	int8_t core = -1;
	uint32_t nonce;
	int i, num, x;
	char *tmp;
	bool res;

	if (count < sc_info->que_fld_min) {
		tmp = str_text(data);
		applogsiz(LOG_INFO, BFLSC_APPLOGSIZ,
				"%s%i:%s work returned too small (%d,%s)",
				bflsc->drv->name, bflsc->device_id, xlink, count, tmp);
		free(tmp);
		inc_bflsc_errors(thr, sc_info, core);
		return;
	}

	if (sc_info->ident == IDENT_BMA) {
		unsigned int ucore;

		if (sscanf(fields[QUE_CC], "%x", &ucore) == 1)
			core = ucore;
	} else if (sc_info->que_noncecount != QUE_NONCECOUNT_V1) {
		unsigned int ucore;

		if (sscanf(fields[QUE_CHIP_V2], "%x", &ucore) == 1)
			core = ucore;
	}

	if (count > sc_info->que_fld_max) {
		applog(LOG_INFO, "%s%i:%s work returned too large (%d) processing %d anyway",
		       bflsc->drv->name, bflsc->device_id, xlink, count, sc_info->que_fld_max);
		count = sc_info->que_fld_max;
		inc_bflsc_errors(thr, sc_info, core);
	}

	num = atoi(fields[sc_info->que_noncecount]);
	if (num != count - sc_info->que_fld_min) {
		tmp = str_text(data);
		applogsiz(LOG_INFO, BFLSC_APPLOGSIZ,
				"%s%i:%s incorrect data count (%d) will use %d instead from (%s)",
				bflsc->drv->name, bflsc->device_id, xlink, num,
				count - sc_info->que_fld_max, tmp);
		free(tmp);
		inc_bflsc_errors(thr, sc_info, core);
	}

	if (sc_info->ident == IDENT_BMA) {
		int uid;

		if (sscanf(fields[QUE_UID], "%04x", &uid) == 1)
			work = bflsc_work_by_uid(bflsc, sc_info, uid);
	} else {
		char midstate[MIDSTATE_BYTES] = {}, blockdata[MERKLE_BYTES] = {};

		if (!hex2bin((unsigned char *)midstate, fields[QUE_MIDSTATE], MIDSTATE_BYTES) ||
		    !hex2bin((unsigned char *)blockdata, fields[QUE_BLOCKDATA], MERKLE_BYTES)) {
			applog(LOG_INFO, "%s%i:%s Failed to convert binary data to hex result - ignored",
			       bflsc->drv->name, bflsc->device_id, xlink);
			inc_bflsc_errors(thr, sc_info, core);
			return;
		}

		work = take_queued_work_bymidstate(bflsc, midstate, MIDSTATE_BYTES,
						blockdata, MERKLE_OFFSET, MERKLE_BYTES);
	}
	if (!work) {
		if (sc_info->not_first_work) {
			applog(LOG_INFO, "%s%i:%s failed to find nonce work - can't be processed - ignored",
			       bflsc->drv->name, bflsc->device_id, xlink);
			inc_bflsc_errors(thr, sc_info, core);
		}
		return;
	}

	res = false;
	x = 0;
	for (i = sc_info->que_fld_min; i < count; i++) {
		if (strlen(fields[i]) != 8) {
			tmp = str_text(data);
			applogsiz(LOG_INFO, BFLSC_APPLOGSIZ,
					"%s%i:%s invalid nonce (%s) will try to process anyway",
					bflsc->drv->name, bflsc->device_id, xlink, tmp);
			free(tmp);
		}

		hex2bin((void*)&nonce, fields[i], 4);
		nonce = htobe32(nonce);
		res = submit_nonce(thr, work, nonce);
		if (res) {
			wr_lock(&(sc_info->stat_lock));
			sc_info->sc_devs[dev].nonces_found++;
			wr_unlock(&(sc_info->stat_lock));

			(*nonces)++;
			x++;
			inc_bflsc_nonces(sc_info, core);
		} else
			inc_core_errors(sc_info, core);
	}

	wr_lock(&(sc_info->stat_lock));
	if (res)
		sc_info->sc_devs[dev].result_id++;
	if (x > QUE_MAX_RESULTS)
		x = QUE_MAX_RESULTS + 1;
	(sc_info->result_size[x])++;
	sc_info->sc_devs[dev].work_complete++;
	sc_info->sc_devs[dev].hashes_unsent += FULLNONCE;
	// If not flushed (stale)
	if (!(work->devflag))
		sc_info->sc_devs[dev].work_queued -= 1;
	wr_unlock(&(sc_info->stat_lock));

	free_work(work);
}

static int process_results(struct cgpu_info *bflsc, int dev, char *pbuf, int *nonces, int *in_process)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	char **items, *firstname, **fields, *lf;
	int que = 0, i, lines, count;
	char *tmp, *tmp2, *buf;
	char xlink[17];
	bool res;

	*nonces = 0;
	*in_process = 0;

	xlinkstr(xlink, sizeof(xlink), dev, sc_info);

	buf = strdup(pbuf);
	if (!strncmp(buf, "INPROCESS", 9))
		sscanf(buf, "INPROCESS:%d\n%s", in_process, pbuf);
	res = tolines(bflsc, dev, buf, &lines, &items, C_GETRESULTS);
	if (!res || lines < 1) {
		tmp = str_text(pbuf);
		applogsiz(LOG_ERR, BFLSC_APPLOGSIZ,
				"%s%i:%s empty result (%s) ignored",
				bflsc->drv->name, bflsc->device_id, xlink, tmp);
		free(tmp);
		goto arigatou;
	}

	if (lines < QUE_RES_LINES_MIN) {
		tmp = str_text(pbuf);
		applogsiz(LOG_ERR, BFLSC_APPLOGSIZ,
				"%s%i:%s result of %d too small (%s) ignored",
				bflsc->drv->name, bflsc->device_id, xlink, lines, tmp);
		free(tmp);
		goto arigatou;
	}

	breakdown(ONECOLON, items[1], &count, &firstname, &fields, &lf);
	if (count < 1) {
		tmp = str_text(pbuf);
		tmp2 = str_text(items[1]);
		applogsiz(LOG_ERR, BFLSC_APPLOGSIZ,
				"%s%i:%s empty result count (%s) in (%s) ignoring",
				bflsc->drv->name, bflsc->device_id, xlink, tmp2, tmp);
		free(tmp2);
		free(tmp);
		goto arigatou;
	} else if (count != 1) {
		tmp = str_text(pbuf);
		tmp2 = str_text(items[1]);
		applogsiz(LOG_ERR, BFLSC_APPLOGSIZ,
				"%s%i:%s incorrect result count %d (%s) in (%s) will try anyway",
				bflsc->drv->name, bflsc->device_id, xlink, count, tmp2, tmp);
		free(tmp2);
		free(tmp);
	}

	que = atoi(fields[0]);
	if (que != (lines - QUE_RES_LINES_MIN)) {
		i = que;
		// 1+ In case the last line isn't 'OK' - try to process it
		que = 1 + lines - QUE_RES_LINES_MIN;

		tmp = str_text(pbuf);
		tmp2 = str_text(items[0]);
		applogsiz(LOG_ERR, BFLSC_APPLOGSIZ,
				"%s%i:%s incorrect result count %d (%s) will try %d (%s)",
				bflsc->drv->name, bflsc->device_id, xlink, i, tmp2, que, tmp);
		free(tmp2);
		free(tmp);

	}

	freebreakdown(&count, &firstname, &fields);

	for (i = 0; i < que; i++) {
		res = breakdown(NOCOLON, items[i + QUE_RES_LINES_MIN - 1], &count, &firstname, &fields, &lf);
		if (likely(res))
			process_nonces(bflsc, dev, &(xlink[0]), items[i], count, fields, nonces);
		else
			applogsiz(LOG_ERR, BFLSC_APPLOGSIZ,
					"%s%i:%s failed to process nonce %s",
					bflsc->drv->name, bflsc->device_id, xlink, items[i]);
		freebreakdown(&count, &firstname, &fields);
		sc_info->not_first_work = true;
	}

arigatou:
	freetolines(&lines, &items);
	free(buf);

	return que;
}

#define TVF(tv) ((float)((tv)->tv_sec) + ((float)((tv)->tv_usec) / 1000000.0))
#define TVFMS(tv) (TVF(tv) * 1000.0)

// Thread to simply keep looking for results
static void *bflsc_get_results(void *userdata)
{
	struct cgpu_info *bflsc = (struct cgpu_info *)userdata;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	struct timeval elapsed, now;
	float oldest, f;
	char buf[BFLSC_BUFSIZ+1];
	int err, amount;
	int i, que, dev, nonces;
	bool readok;

	cgtime(&now);
	for (i = 0; i < sc_info->sc_count; i++) {
		copy_time(&(sc_info->sc_devs[i].last_check_result), &now);
		copy_time(&(sc_info->sc_devs[i].last_dev_result), &now);
		copy_time(&(sc_info->sc_devs[i].last_nonce_result), &now);
	}

	while (sc_info->shutdown == false) {
		cgtimer_t ts_start;
		int in_process;

		if (bflsc->usbinfo.nodev)
			return NULL;

		dev = -1;
		oldest = FLT_MAX;
		cgtime(&now);

		// Find the first oldest ... that also needs checking
		for (i = 0; i < sc_info->sc_count; i++) {
			timersub(&now, &(sc_info->sc_devs[i].last_check_result), &elapsed);
			f = TVFMS(&elapsed);
			if (f < oldest && f >= sc_info->sc_devs[i].ms_work) {
				f = oldest;
				dev = i;
			}
		}

		if (bflsc->usbinfo.nodev)
			return NULL;

		cgsleep_prepare_r(&ts_start);
		if (dev == -1)
			goto utsura;

		cgtime(&(sc_info->sc_devs[dev].last_check_result));

		readok = bflsc_qres(bflsc, buf, sizeof(buf), dev, &err, &amount, false);
		if (err < 0 || (!readok && amount != BFLSC_QRES_LEN) || (readok && amount < 1)) {
			// TODO: do what else?
		} else {
			que = process_results(bflsc, dev, buf, &nonces, &in_process);
			sc_info->not_first_work = true; // in case it failed processing it
			if (que > 0)
				cgtime(&(sc_info->sc_devs[dev].last_dev_result));
			if (nonces > 0)
				cgtime(&(sc_info->sc_devs[dev].last_nonce_result));

			/* There are more results queued so do not sleep */
			if (in_process)
				continue;
			// TODO: if not getting results ... reinit?
		}

utsura:
		cgsleep_ms_r(&ts_start, sc_info->results_sleep_time);
	}

	return NULL;
}

static bool bflsc_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);

	if (thr_info_create(&(sc_info->results_thr), NULL, bflsc_get_results, (void *)bflsc)) {
		applog(LOG_ERR, "%s%i: thread create failed", bflsc->drv->name, bflsc->device_id);
		return false;
	}
	pthread_detach(sc_info->results_thr.pth);

	return true;
}

static void bflsc_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);

	bflsc_flush_work(bflsc);
	sc_info->shutdown = true;
}

static void bflsc_thread_enable(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;

	if (bflsc->usbinfo.nodev)
		return;

	bflsc_initialise(bflsc);
}

static bool bflsc_send_work(struct cgpu_info *bflsc, int dev, bool mandatory)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	struct FullNonceRangeJob data;
	char buf[BFLSC_BUFSIZ+1];
	bool sent, ret = false;
	struct work *work;
	int err, amount;
	int len, try;
	int stage;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return false;

	// TODO: handle this everywhere
	if (sc_info->sc_devs[dev].overheat == true)
		return false;

	// Initially code only deals with sending one work item
	data.payloadSize = BFLSC_JOBSIZ;
	data.endOfBlock = BFLSC_EOB;

	len = sizeof(struct FullNonceRangeJob);

	/* On faster devices we have a lot of lock contention so only
	 * mandatorily grab the lock and send work if the queue is empty since
	 * we have a submit queue. */
	if (mandatory)
		mutex_lock(&(bflsc->device_mutex));
	else {
		if (mutex_trylock(&bflsc->device_mutex))
			return ret;
	}

	work = get_queued(bflsc);
	if (unlikely(!work)) {
		mutex_unlock(&bflsc->device_mutex);
		return ret;
	}
	memcpy(data.midState, work->midstate, MIDSTATE_BYTES);
	memcpy(data.blockData, work->data + MERKLE_OFFSET, MERKLE_BYTES);
	try = 0;
re_send:
	err = send_recv_ds(bflsc, dev, &stage, &sent, &amount,
				BFLSC_QJOB, BFLSC_QJOB_LEN, C_REQUESTQUEJOB, C_REQUESTQUEJOBSTATUS,
				(char *)&data, len, C_QUEJOB, C_QUEJOBSTATUS,
				buf, sizeof(buf)-1);
	mutex_unlock(&(bflsc->device_mutex));

	switch (stage) {
		case 1:
			if (!sent) {
				bflsc_applog(bflsc, dev, C_REQUESTQUEJOB, amount, err);
				goto out;
			} else {
				// TODO: handle other errors ...

				// Try twice
				if (try++ < 1 && amount > 1 &&
					strstr(buf, BFLSC_TIMEOUT))
						goto re_send;

				bflsc_applog(bflsc, dev, C_REQUESTQUEJOBSTATUS, amount, err);
				goto out;
			}
			break;
		case 2:
			if (!sent) {
				bflsc_applog(bflsc, dev, C_QUEJOB, amount, err);
				goto out;
			} else {
				if (!isokerr(err, buf, amount)) {
					// TODO: check for QUEUE FULL and set work_queued to sc_info->que_size
					//  and report a code bug LOG_ERR - coz it should never happen
					// TODO: handle other errors ...

					// Try twice
					if (try++ < 1 && amount > 1 &&
						strstr(buf, BFLSC_TIMEOUT))
							goto re_send;

					bflsc_applog(bflsc, dev, C_QUEJOBSTATUS, amount, err);
					goto out;
				}
			}
			break;
	}

	wr_lock(&(sc_info->stat_lock));
	sc_info->sc_devs[dev].work_queued++;
	wr_unlock(&(sc_info->stat_lock));

	work->subid = dev;
	ret = true;
out:
	if (unlikely(!ret))
		work_completed(bflsc, work);
	return ret;
}

#define JP_COMMAND 0
#define JP_STREAMLENGTH 2
#define JP_SIGNATURE 4
#define JP_JOBSINARRY 5
#define JP_JOBSARRY 6
#define JP_ARRAYSIZE 45

static bool bflsc28_queue_full(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = bflsc->device_data;
	int created, queued = 0, create, i, offset;
	struct work *base_work, *work, *works[10];
	char *buf, *field, *ptr;
	bool sent, ret = false;
	uint16_t *streamlen;
	uint8_t *job_pack;
	int err, amount;

	job_pack = alloca(2 + // Command
			  2 + // StreamLength
			  1 + // Signature
			  1 + // JobsInArray
			  JP_ARRAYSIZE * 10 +// Array of up to 10 Job Structs
			  1 // EndOfWrapper
			  );

	if (bflsc->usbinfo.nodev)
		return true;

	/* Don't send any work if this device is overheating */
	if (sc_info->sc_devs[0].overheat == true)
		return true;

	wr_lock(&bflsc->qlock);
	base_work = __get_queued(bflsc);
	if (likely(base_work))
		__work_completed(bflsc, base_work);
	wr_unlock(&bflsc->qlock);

	if (unlikely(!base_work))
		return ret;
	created = 1;

	create = 9;
	if (base_work->drv_rolllimit < create)
		create = base_work->drv_rolllimit;

	works[0] = base_work;
	for (i = 1; i <= create ; i++) {
		created++;
		work = make_clone(base_work);
		roll_work(base_work);
		works[i] = work;
	}

	memcpy(job_pack, "WX", 2);
	streamlen = (uint16_t *)&job_pack[JP_STREAMLENGTH];
	*streamlen = created * JP_ARRAYSIZE + 7;
	job_pack[JP_SIGNATURE] = 0xc1;
	job_pack[JP_JOBSINARRY] = created;
	offset = JP_JOBSARRY;

	/* Create the maximum number of work items we can queue by nrolling one */
	for (i = 0; i < created; i++) {
		work = works[i];
		memcpy(job_pack + offset, work->midstate, MIDSTATE_BYTES);
		offset += MIDSTATE_BYTES;
		memcpy(job_pack + offset, work->data + MERKLE_OFFSET, MERKLE_BYTES);
		offset += MERKLE_BYTES;
		job_pack[offset] = 0xaa; // EndOfBlock signature
		offset++;
	}
	job_pack[offset++] = 0xfe; // EndOfWrapper

	buf = alloca(BFLSC_BUFSIZ + 1);
	mutex_lock(&bflsc->device_mutex);
	err = send_recv_ss(bflsc, 0, &sent, &amount, (char *)job_pack, offset,
			   C_REQUESTQUEJOB, buf, BFLSC_BUFSIZ, C_REQUESTQUEJOBSTATUS, READ_NL);
	mutex_unlock(&bflsc->device_mutex);

	if (!isokerr(err, buf, amount)) {
		if (!strncasecmp(buf, "ERR:QUEUE FULL", 14)) {
			applog(LOG_DEBUG, "%s%d: Queue full",
			       bflsc->drv->name, bflsc->device_id);
			ret = true;
		} else {
			applog(LOG_WARNING, "%s%d: Queue response not ok %s",
			 bflsc->drv->name, bflsc->device_id, buf);
		}
		goto out;
	}

	ptr = alloca(strlen(buf));
	if (sscanf(buf, "OK:QUEUED %d:%s", &queued, ptr) != 2) {
		applog(LOG_WARNING, "%s%d: Failed to parse queue response %s",
		       bflsc->drv->name, bflsc->device_id, buf);
		goto out;
	}
	if (queued < 1 || queued > 10) {
		applog(LOG_WARNING, "%s%d: Invalid queued count %d",
		       bflsc->drv->name, bflsc->device_id, queued);
		queued = 0;
		goto out;
	}
	for (i = 0; i < queued; i++) {
		struct bflsc_work *bwork, *oldbwork;
		unsigned int uid;

		work = works[i];
		field = Strsep(&ptr, ",");
		if (!field) {
			applog(LOG_WARNING, "%s%d: Ran out of queued IDs after %d of %d",
			       bflsc->drv->name, bflsc->device_id, i, queued);
			queued = i - 1;
			goto out;
		}
		sscanf(field, "%04x", &uid);
		bwork = calloc(sizeof(struct bflsc_work), 1);
		bwork->id = uid;
		bwork->work = work;

		wr_lock(&bflsc->qlock);
		HASH_REPLACE_INT(sc_info->bworks, id, bwork, oldbwork);
		if (oldbwork) {
			free_work(oldbwork->work);
			free(oldbwork);
		}
		wr_unlock(&bflsc->qlock);
		sc_info->sc_devs[0].work_queued++;
	}
	if (queued < created)
		ret = true;
out:
	for (i = queued; i < created; i++) {
		work = works[i];
		discard_work(work);
	}
	return ret;
}

static bool bflsc_queue_full(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	int i, dev, tried, que;
	bool ret = false;
	int tries = 0;

	tried = -1;
	// if something is wrong with a device try the next one available
	// TODO: try them all? Add an unavailable flag to sc_devs[i] init to 0 here first
	while (++tries < 3) {
		bool mandatory = false;

		// Device is gone - shouldn't normally get here
		if (bflsc->usbinfo.nodev) {
			ret = true;
			break;
		}

		dev = -1;
		rd_lock(&(sc_info->stat_lock));
		// Anything waiting - gets the work first
		for (i = 0; i < sc_info->sc_count; i++) {
			// TODO: and ignore x-link dead - once I work out how to decide it is dead
			if (i != tried && sc_info->sc_devs[i].work_queued == 0 &&
			    !sc_info->sc_devs[i].overheat) {
				dev = i;
				break;
			}
		}

		if (dev == -1) {
			que = sc_info->que_size * 10; // 10x is certainly above the MAX it could be
			// The first device with the smallest amount queued
			for (i = 0; i < sc_info->sc_count; i++) {
				if (i != tried && sc_info->sc_devs[i].work_queued < que &&
				    !sc_info->sc_devs[i].overheat) {
					dev = i;
					que = sc_info->sc_devs[i].work_queued;
				}
			}
			if (que > sc_info->que_full_enough)
				dev = -1;
			else if (que < sc_info->que_low)
				mandatory = true;
		}
		rd_unlock(&(sc_info->stat_lock));

		// nothing needs work yet
		if (dev == -1) {
			ret = true;
			break;
		}

		if (bflsc_send_work(bflsc, dev, mandatory))
			break;
		else
			tried = dev;
	}

	return ret;
}

static int64_t bflsc_scanwork(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	int64_t ret, unsent;
	bool flushed, cleanup;
	struct work *work, *tmp;
	int dev, waited, i;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return -1;

	flushed = false;
	// Single lock check if any are flagged as flushed
	rd_lock(&(sc_info->stat_lock));
	for (dev = 0; dev < sc_info->sc_count; dev++)
		flushed |= sc_info->sc_devs[dev].flushed;
	rd_unlock(&(sc_info->stat_lock));

	// > 0 flagged as flushed
	if (flushed) {
// TODO: something like this ......
		for (dev = 0; dev < sc_info->sc_count; dev++) {
			cleanup = false;

			// Is there any flushed work that can be removed?
			rd_lock(&(sc_info->stat_lock));
			if (sc_info->sc_devs[dev].flushed) {
				if (sc_info->sc_devs[dev].result_id > (sc_info->sc_devs[dev].flush_id + sc_info->flush_size))
					cleanup = true;
			}
			rd_unlock(&(sc_info->stat_lock));

			// yes remove the flushed work that can be removed
			if (cleanup) {
				wr_lock(&bflsc->qlock);
				HASH_ITER(hh, bflsc->queued_work, work, tmp) {
					if (work->devflag && work->subid == dev) {
						bflsc->queued_count--;
						HASH_DEL(bflsc->queued_work, work);
						discard_work(work);
					}
				}
				wr_unlock(&bflsc->qlock);

				wr_lock(&(sc_info->stat_lock));
				sc_info->sc_devs[dev].flushed = false;
				wr_unlock(&(sc_info->stat_lock));
			}
		}
	}

	waited = restart_wait(thr, sc_info->scan_sleep_time);
	if (waited == ETIMEDOUT && sc_info->ident != IDENT_BMA) {
		unsigned int old_sleep_time, new_sleep_time = 0;
		int min_queued = sc_info->que_size;
		/* Only adjust the scan_sleep_time if we did not receive a
		 * restart message while waiting. Try to adjust sleep time
		 * so we drop to sc_info->que_watermark before getting more work.
		 */

		rd_lock(&sc_info->stat_lock);
		old_sleep_time = sc_info->scan_sleep_time;
		for (i = 0; i < sc_info->sc_count; i++) {
			if (sc_info->sc_devs[i].work_queued < min_queued)
				min_queued = sc_info->sc_devs[i].work_queued;
		}
		rd_unlock(&sc_info->stat_lock);
		new_sleep_time = old_sleep_time;

		/* Increase slowly but decrease quickly */
		if (min_queued > sc_info->que_full_enough && old_sleep_time < BFLSC_MAX_SLEEP)
			new_sleep_time = old_sleep_time * 21 / 20;
		else if (min_queued < sc_info->que_low)
			new_sleep_time = old_sleep_time * 2 / 3;

		/* Do not sleep more than BFLSC_MAX_SLEEP so we can always
		 * report in at least 2 results per 5s log interval. */
		if (new_sleep_time != old_sleep_time) {
			if (new_sleep_time > BFLSC_MAX_SLEEP)
				new_sleep_time = BFLSC_MAX_SLEEP;
			else if (new_sleep_time == 0)
				new_sleep_time = 1;
			applog(LOG_DEBUG, "%s%i: Changed scan sleep time to %d",
			       bflsc->drv->name, bflsc->device_id, new_sleep_time);

			wr_lock(&sc_info->stat_lock);
			sc_info->scan_sleep_time = new_sleep_time;
			wr_unlock(&sc_info->stat_lock);
		}
	}

	// Count up the work done since we last were here
	ret = 0;
	wr_lock(&(sc_info->stat_lock));
	for (dev = 0; dev < sc_info->sc_count; dev++) {
		unsent = sc_info->sc_devs[dev].hashes_unsent;
		sc_info->sc_devs[dev].hashes_unsent = 0;
		sc_info->sc_devs[dev].hashes_sent += unsent;
		sc_info->hashes_sent += unsent;
		ret += unsent;
	}
	wr_unlock(&(sc_info->stat_lock));

	return ret;
}

#define BFLSC_OVER_TEMP 75

/* Set the fanspeed to auto for any valid value <= BFLSC_OVER_TEMP,
 * or max for any value > BFLSC_OVER_TEMP or if we don't know the temperature. */
static void bflsc_set_fanspeed(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)bflsc->device_data;
	char buf[BFLSC_BUFSIZ+1];
	char data[16+1];
	int amount;
	bool sent;

	if ((bflsc->temp <= BFLSC_OVER_TEMP && bflsc->temp > 0 && sc_info->fanauto) ||
	    ((bflsc->temp > BFLSC_OVER_TEMP || !bflsc->temp) && !sc_info->fanauto))
		return;

	if (bflsc->temp > BFLSC_OVER_TEMP || !bflsc->temp) {
		strcpy(data, BFLSC_FAN4);
		sc_info->fanauto = false;
	} else {
		strcpy(data, BFLSC_FANAUTO);
		sc_info->fanauto = true;
	}

	applog(LOG_DEBUG, "%s%i: temp=%.0f over=%d set fan to %s",
				bflsc->drv->name, bflsc->device_id, bflsc->temp,
				BFLSC_OVER_TEMP, data);

	mutex_lock(&bflsc->device_mutex);
	send_recv_ss(bflsc, 0, &sent, &amount,
				data, strlen(data), C_SETFAN,
				buf, sizeof(buf)-1, C_FANREPLY, READ_NL);
	mutex_unlock(&bflsc->device_mutex);
}

static bool bflsc_get_stats(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	bool allok = true;
	int i;

	// Device is gone
	if (bflsc->usbinfo.nodev)
		return false;

	for (i = 0; i < sc_info->sc_count; i++) {
		if (!bflsc_get_temp(bflsc, i))
			allok = false;

		// Device is gone
		if (bflsc->usbinfo.nodev)
			return false;

		if (i < (sc_info->sc_count - 1))
			cgsleep_ms(BFLSC_TEMP_SLEEPMS);
	}

	bflsc_set_fanspeed(bflsc);

	return allok;
}

static char *bflsc_set(struct cgpu_info *bflsc, char *option, char *setting, char *replybuf)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	int val;

	if (sc_info->ident != IDENT_BMA) {
		strcpy(replybuf, "no set options available");
		return replybuf;
	}

	if (strcasecmp(option, "help") == 0) {
		sprintf(replybuf, "volt: range 0-9 clock: range 0-15");
		return replybuf;
	}

	if (strcasecmp(option, "volt") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing volt setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 0 || val > 9) {
			sprintf(replybuf, "invalid volt: '%s' valid range 0-9",
					  setting);
		}

		sc_info->volt_next = val;
		sc_info->volt_next_stat = true;

		return NULL;
	}

	if (strcasecmp(option, "clock") == 0) {
		if (!setting || !*setting) {
			sprintf(replybuf, "missing clock setting");
			return replybuf;
		}

		val = atoi(setting);
		if (val < 0 || val > 15) {
			sprintf(replybuf, "invalid clock: '%s' valid range 0-15",
					  setting);
		}

		sc_info->clock_next = val;
		sc_info->clock_next_stat = true;

		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

static void bflsc_identify(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);

	// TODO: handle x-link
	sc_info->flash_led = true;
}

static bool bflsc_thread_init(struct thr_info *thr)
{
	struct cgpu_info *bflsc = thr->cgpu;

	if (bflsc->usbinfo.nodev)
		return false;

	bflsc_initialise(bflsc);

	return true;
}

// there should be a new API function to return device info that isn't the standard stuff
// instead of bflsc_api_stats - since the stats should really just be internal code info
// and the new one should be UNusual device stats/extra details - like the stuff below

static struct api_data *bflsc_api_stats(struct cgpu_info *bflsc)
{
	struct bflsc_info *sc_info = (struct bflsc_info *)(bflsc->device_data);
	struct api_data *root = NULL;
	char data[4096];
	char buf[256];
	int i, j, off;
	size_t len;

//if no x-link ... etc
	rd_lock(&(sc_info->stat_lock));
	root = api_add_temp(root, "Temp1", &(sc_info->sc_devs[0].temp1), true);
	root = api_add_temp(root, "Temp2", &(sc_info->sc_devs[0].temp2), true);
	root = api_add_volts(root, "Vcc1", &(sc_info->sc_devs[0].vcc1), true);
	root = api_add_volts(root, "Vcc2", &(sc_info->sc_devs[0].vcc2), true);
	root = api_add_volts(root, "Vmain", &(sc_info->sc_devs[0].vmain), true);
	root = api_add_temp(root, "Temp1 Max", &(sc_info->sc_devs[0].temp1_max), true);
	root = api_add_temp(root, "Temp2 Max", &(sc_info->sc_devs[0].temp2_max), true);
	root = api_add_time(root, "Temp1 Max Time", &(sc_info->sc_devs[0].temp1_max_time), true);
	root = api_add_time(root, "Temp2 Max Time", &(sc_info->sc_devs[0].temp2_max_time), true);
	root = api_add_int(root, "Work Queued", &(sc_info->sc_devs[0].work_queued), true);
	root = api_add_int(root, "Work Complete", &(sc_info->sc_devs[0].work_complete), true);
	root = api_add_bool(root, "Overheat", &(sc_info->sc_devs[0].overheat), true);
	root = api_add_uint64(root, "Flush ID", &(sc_info->sc_devs[0].flush_id), true);
	root = api_add_uint64(root, "Result ID", &(sc_info->sc_devs[0].result_id), true);
	root = api_add_bool(root, "Flushed", &(sc_info->sc_devs[0].flushed), true);
	root = api_add_uint(root, "Scan Sleep", &(sc_info->scan_sleep_time), true);
	root = api_add_uint(root, "Results Sleep", &(sc_info->results_sleep_time), true);
	root = api_add_uint(root, "Work ms", &(sc_info->default_ms_work), true);

	buf[0] = '\0';
	for (i = 0; i <= QUE_MAX_RESULTS + 1; i++)
		tailsprintf(buf, sizeof(buf), "%s%"PRIu64, (i > 0) ? "/" : "", sc_info->result_size[i]);
	root = api_add_string(root, "Result Size", buf, true);

	rd_unlock(&(sc_info->stat_lock));

	i = (int)(sc_info->driver_version);
	root = api_add_int(root, "Driver", &i, true);
	root = api_add_string(root, "Firmware", sc_info->sc_devs[0].firmware, false);
	root = api_add_string(root, "Chips", sc_info->sc_devs[0].chips, false);
	root = api_add_int(root, "Que Size", &(sc_info->que_size), false);
	root = api_add_int(root, "Que Full", &(sc_info->que_full_enough), false);
	root = api_add_int(root, "Que Watermark", &(sc_info->que_watermark), false);
	root = api_add_int(root, "Que Low", &(sc_info->que_low), false);
	root = api_add_escape(root, "GetInfo", sc_info->sc_devs[0].getinfo, false);

/*
else a whole lot of something like these ... etc
	root = api_add_temp(root, "X-%d-Temp1", &(sc_info->temp1), false);
	root = api_add_temp(root, "X-%d-Temp2", &(sc_info->temp2), false);
	root = api_add_volts(root, "X-%d-Vcc1", &(sc_info->vcc1), false);
	root = api_add_volts(root, "X-%d-Vcc2", &(sc_info->vcc2), false);
	root = api_add_volts(root, "X-%d-Vmain", &(sc_info->vmain), false);
*/
	if (sc_info->ident == IDENT_BMA) {
		for (i = 0; i < 128; i += 16) {
			data[0] = '\0';
			off = 0;
			for (j = 0; j < 16; j++) {
				len = snprintf(data+off, sizeof(data)-off,
						"%s%"PRIu64,
						j > 0 ? " " : "",
						sc_info->cortex_nonces[i+j]);
				if (len >= (sizeof(data)-off))
					off = sizeof(data)-1;
				else {
					if (len > 0)
						off += len;
				}
			}
			sprintf(buf, "Cortex %02x-%02x Nonces", i, i+15);
			root = api_add_string(root, buf, data, true);
		}
		for (i = 0; i < 128; i += 16) {
			data[0] = '\0';
			off = 0;
			for (j = 0; j < 16; j++) {
				len = snprintf(data+off, sizeof(data)-off,
						"%s%"PRIu64,
						j > 0 ? " " : "",
						sc_info->cortex_hw[i+j]);
				if (len >= (sizeof(data)-off))
					off = sizeof(data)-1;
				else {
					if (len > 0)
						off += len;
				}
			}
			sprintf(buf, "Cortex %02x-%02x HW Errors", i, i+15);
			root = api_add_string(root, buf, data, true);
		}
	} else if (sc_info->que_noncecount != QUE_NONCECOUNT_V1) {
		for (i = 0; i < 16; i++) {
			sprintf(buf, "Core%d Nonces", i);
			root = api_add_uint64(root, buf, &sc_info->core_nonces[i], false);
		}
		for (i = 0; i < 16; i++) {
			sprintf(buf, "Core%d HW Errors", i);
			root = api_add_uint64(root, buf, &sc_info->core_hw[i], false);
		}
	}

	return root;
}

struct device_drv bflsc_drv = {
	.drv_id = DRIVER_bflsc,
	.dname = "BitForceSC",
	.name = BFLSC_SINGLE,
	.drv_detect = bflsc_detect,
	.get_api_stats = bflsc_api_stats,
	.get_statline_before = get_bflsc_statline_before,
	.get_stats = bflsc_get_stats,
	.set_device = bflsc_set,
	.identify_device = bflsc_identify,
	.thread_prepare = bflsc_thread_prepare,
	.thread_init = bflsc_thread_init,
	.hash_work = hash_queued_work,
	.scanwork = bflsc_scanwork,
	.queue_full = bflsc_queue_full,
	.flush_work = bflsc_flush_work,
	.thread_shutdown = bflsc_shutdown,
	.thread_enable = bflsc_thread_enable
};

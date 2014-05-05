/*
 * Copyright 2013-2014 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include "miner.h"
#include "driver-bitfury.h"
#include "sha2.h"
#include "mcp2210.h"
#include "libbitfury.h"

int opt_bxf_temp_target = BXF_TEMP_TARGET / 10;
int opt_nfu_bits = 50;
int opt_bxm_bits = 54;
int opt_bxf_bits = 54;
int opt_bxf_debug;
int opt_osm_led_mode = 4;

/* Wait longer 1/3 longer than it would take for a full nonce range */
#define BF1WAIT 1600
#define BF1MSGSIZE 7
#define BF1INFOSIZE 14

#define TWELVE_MHZ 12000000

//Low port pins
#define SK      1
#define DO      2
#define DI      4
#define CS      8
#define GPIO0   16
#define GPIO1   32
#define GPIO2   64
#define GPIO3   128

//GPIO pins
#define GPIOL0  0
#define GPIOL1  1
#define GPIOL2  2
#define GPIOL3  3
#define GPIOH   4
#define GPIOH1  5
#define GPIOH2  6
#define GPIOH3  7
#define GPIOH4  8
#define GPIOH5  9
#define GPIOH6  10
#define GPIOH7  11

#define DEFAULT_DIR            (SK | DO | CS | GPIO0 | GPIO1 | GPIO2 | GPIO3)  /* Setup default input or output state per FTDI for SPI */
#define DEFAULT_STATE          (CS)                                       /* CS idles high, CLK idles LOW for SPI0 */

//MPSSE commands from FTDI AN_108
#define INVALID_COMMAND           0xAB
#define ENABLE_ADAPTIVE_CLOCK     0x96
#define DISABLE_ADAPTIVE_CLOCK    0x97
#define ENABLE_3_PHASE_CLOCK      0x8C
#define DISABLE_3_PHASE_CLOCK     0x8D
#define TCK_X5                    0x8A
#define TCK_D5                    0x8B
#define CLOCK_N_CYCLES            0x8E
#define CLOCK_N8_CYCLES           0x8F
#define PULSE_CLOCK_IO_HIGH       0x94
#define PULSE_CLOCK_IO_LOW        0x95
#define CLOCK_N8_CYCLES_IO_HIGH   0x9C
#define CLOCK_N8_CYCLES_IO_LOW    0x9D
#define TRISTATE_IO               0x9E
#define TCK_DIVISOR               0x86
#define LOOPBACK_END              0x85
#define SET_OUT_ADBUS             0x80
#define SET_OUT_ACBUS             0x82
#define WRITE_BYTES_SPI0          0x11
#define READ_WRITE_BYTES_SPI0     0x31

static void bf1_empty_buffer(struct cgpu_info *bitfury)
{
	char buf[512];
	int amount;

	do {
		usb_read_once(bitfury, buf, 512, &amount, C_BF1_FLUSH);
	} while (amount);
}

static bool bf1_open(struct cgpu_info *bitfury)
{
	uint32_t buf[2];
	int err;

	bf1_empty_buffer(bitfury);
	/* Magic sequence to reset device only really needed for windows but
	 * harmless on linux. */
	buf[0] = 0x80250000;
	buf[1] = 0x00000800;
	err = usb_transfer(bitfury, 0, 9, 1, 0, C_ATMEL_RESET);
	if (!err)
		err = usb_transfer(bitfury, 0x21, 0x22, 0, 0, C_ATMEL_OPEN);
	if (!err) {
		err = usb_transfer_data(bitfury, 0x21, 0x20, 0x0000, 0, buf,
					BF1MSGSIZE, C_ATMEL_INIT);
	}

	if (err < 0) {
		applog(LOG_INFO, "%s %d: Failed to open with error %s", bitfury->drv->name,
		       bitfury->device_id, libusb_error_name(err));
	}
	return (err == BF1MSGSIZE);
}

static void bf1_close(struct cgpu_info *bitfury)
{
	bf1_empty_buffer(bitfury);
}

static void bf1_identify(struct cgpu_info *bitfury)
{
	int amount;

	usb_write(bitfury, "L", 1, &amount, C_BF1_IDENTIFY);
}

static void bitfury_identify(struct cgpu_info *bitfury)
{
	struct bitfury_info *info = bitfury->device_data;

	switch(info->ident) {
		case IDENT_BF1:
			bf1_identify(bitfury);
			break;
		case IDENT_BXF:
		case IDENT_OSM:
		default:
			break;
	}
}

static bool bf1_getinfo(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	int amount, err;
	char buf[16];

	err = usb_write(bitfury, "I", 1, &amount, C_BF1_REQINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to write REQINFO",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	err = usb_read(bitfury, buf, BF1INFOSIZE, &amount, C_BF1_GETINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to read GETINFO",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	if (amount != BF1INFOSIZE) {
		applog(LOG_INFO, "%s %d: Getinfo received %d bytes instead of %d",
		       bitfury->drv->name, bitfury->device_id, amount, BF1INFOSIZE);
		return false;
	}
	info->version = buf[1];
	memcpy(&info->product, buf + 2, 8);
	memcpy(&info->serial, buf + 10, 4);
	bitfury->unique_id = bin2hex((unsigned char *)buf + 10, 4);

	applog(LOG_INFO, "%s %d: Getinfo returned version %d, product %s serial %s", bitfury->drv->name,
	       bitfury->device_id, info->version, info->product, bitfury->unique_id);
	bf1_empty_buffer(bitfury);
	return true;
}

static bool bf1_reset(struct cgpu_info *bitfury)
{
	int amount, err;
	char buf[16];

	err = usb_write(bitfury, "R", 1, &amount, C_BF1_REQRESET);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to write REQRESET",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	err = usb_read_timeout(bitfury, buf, BF1MSGSIZE, &amount, BF1WAIT,
			       C_BF1_GETRESET);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to read GETRESET",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	if (amount != BF1MSGSIZE) {
		applog(LOG_INFO, "%s %d: Getreset received %d bytes instead of %d",
		       bitfury->drv->name, bitfury->device_id, amount, BF1MSGSIZE);
		return false;
	}
	applog(LOG_DEBUG, "%s %d: Getreset returned %s", bitfury->drv->name,
	       bitfury->device_id, buf);
	bf1_empty_buffer(bitfury);
	return true;
}

static bool bxf_send_msg(struct cgpu_info *bitfury, char *buf, enum usb_cmds cmd)
{
	int err, amount, len;

	if (unlikely(bitfury->usbinfo.nodev))
		return false;

	if (opt_bxf_debug) {
		char *strbuf = str_text(buf);

		applog(LOG_ERR, "%s %d: >BXF [%s]", bitfury->drv->name, bitfury->device_id, strbuf);
		free(strbuf);
	}

	len = strlen(buf);
	applog(LOG_DEBUG, "%s %d: Sending %s", bitfury->drv->name, bitfury->device_id, buf);
	err = usb_write(bitfury, buf, len, &amount, cmd);
	if (err || amount != len) {
		applog(LOG_WARNING, "%s %d: Error %d sending %s sent %d of %d", bitfury->drv->name,
		       bitfury->device_id, err, usb_cmdname(cmd), amount, len);
		return false;
	}
	return true;
}

static bool bxf_send_debugmode(struct cgpu_info *bitfury)
{
	char buf[16];

	sprintf(buf, "debug-mode %d\n", opt_bxf_debug);
	return bxf_send_msg(bitfury, buf, C_BXF_DEBUGMODE);
}

static bool bxf_send_ledmode(struct cgpu_info *bitfury)
{
	char buf[16];

	sprintf(buf, "led-mode %d\n", opt_osm_led_mode);
	return bxf_send_msg(bitfury, buf, C_BXF_LEDMODE);
}

/* Returns the amount received only if we receive a full message, otherwise
 * it returns the err value. */
static int bxf_recv_msg(struct cgpu_info *bitfury, char *buf)
{
	int err, amount;

	err = usb_read_nl(bitfury, buf, 512, &amount, C_BXF_READ);
	if (amount)
		applog(LOG_DEBUG, "%s %d: Received %s", bitfury->drv->name, bitfury->device_id, buf);
	if (!err)
		return amount;
	return err;
}

/* Keep reading till the first timeout or error */
static void bxf_clear_buffer(struct cgpu_info *bitfury)
{
	int err, retries = 0;
	char buf[512];

	do {
		err = bxf_recv_msg(bitfury, buf);
		usb_buffer_clear(bitfury);
		if (err < 0)
			break;
	} while (retries++ < 10);
}

static bool bxf_send_flush(struct cgpu_info *bitfury)
{
	char buf[8];

	sprintf(buf, "flush\n");
	return bxf_send_msg(bitfury, buf, C_BXF_FLUSH);
}

static bool bxf_detect_one(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	int err, retries = 0;
	char buf[512];

	if (!bxf_send_flush(bitfury))
		return false;

	bxf_clear_buffer(bitfury);

	sprintf(buf, "version\n");
	if (!bxf_send_msg(bitfury, buf, C_BXF_VERSION))
		return false;

	do {
		err = bxf_recv_msg(bitfury, buf);
		if (err < 0 && err != LIBUSB_ERROR_TIMEOUT)
			return false;
		if (err > 0 && !strncmp(buf, "version", 7)) {
			sscanf(&buf[8], "%d.%d rev %d chips %d", &info->ver_major,
			       &info->ver_minor, &info->hw_rev, &info->chips);
			applog(LOG_INFO, "%s %d: Version %d.%d rev %d chips %d",
			       bitfury->drv->name, bitfury->device_id, info->ver_major,
			       info->ver_minor, info->hw_rev, info->chips);
			break;
		}
		/* Keep parsing if the buffer is full without counting it as
		 * a retry. */
		if (usb_buffer_size(bitfury))
			continue;
	} while (retries++ < 10);

	if (!add_cgpu(bitfury))
		quit(1, "Failed to add_cgpu in bxf_detect_one");

	update_usb_stats(bitfury);
	applog(LOG_INFO, "%s %d: Successfully initialised %s",
	       bitfury->drv->name, bitfury->device_id, bitfury->device_path);

	/* Sanity check and recognise variations */
	if (info->chips <= 2 || info->chips > 999)
		info->chips = 2;
	else if (info->chips <= 6 && info->ident == IDENT_BXF)
		bitfury->drv->name = "HXF";
	else if (info->chips > 6 && info->ident == IDENT_BXF)
		bitfury->drv->name = "MXF";
	info->filtered_hw = calloc(sizeof(int), info->chips);
	info->job = calloc(sizeof(int), info->chips);
	info->submits = calloc(sizeof(int), info->chips);
	if (!info->filtered_hw || !info->job || !info->submits)
		quit(1, "Failed to calloc bxf chip arrays");
	info->total_nonces = 1;
	info->temp_target = opt_bxf_temp_target * 10;
	/* This unsets it to make sure it gets set on the first pass */
	info->maxroll = -1;

	return true;
}

static bool bf1_detect_one(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	if (!bf1_open(bitfury))
		goto out_close;

	/* Send getinfo request */
	if (!bf1_getinfo(bitfury, info))
		goto out_close;

	/* Send reset request */
	if (!bf1_reset(bitfury))
		goto out_close;

	bf1_identify(bitfury);
	bf1_empty_buffer(bitfury);

	if (!add_cgpu(bitfury))
		quit(1, "Failed to add_cgpu in bf1_detect_one");

	update_usb_stats(bitfury);
	applog(LOG_INFO, "%s %d: Successfully initialised %s",
	       bitfury->drv->name, bitfury->device_id, bitfury->device_path);

	/* This does not artificially raise hashrate, it simply allows the
	 * hashrate to adapt quickly on starting. */
	info->total_nonces = 1;

	return true;
out_close:
	bf1_close(bitfury);
	return false;
}

static void nfu_close(struct cgpu_info *bitfury)
{
	struct bitfury_info *info = bitfury->device_data;
	struct mcp_settings *mcp = &info->mcp;
	int i;

	mcp2210_spi_cancel(bitfury);

	/* Set all pins to input mode, ignoring return code */
	for (i = 0; i < 9; i++) {
		mcp->direction.pin[i] = MCP2210_GPIO_INPUT;
		mcp->value.pin[i] = MCP2210_GPIO_PIN_LOW;
	}
	mcp2210_set_gpio_settings(bitfury, mcp);
}

static bool nfu_reinit(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	bool ret = true;
	int i;

	for (i = 0; i < info->chips; i++) {
		spi_clear_buf(info);
		spi_add_break(info);
		spi_add_fasync(info, i);
		spi_set_freq(info);
		spi_send_conf(info);
		spi_send_init(info);
		spi_reset(bitfury, info);
		ret = info->spi_txrx(bitfury, info);
		if (!ret)
			break;
	}
	return ret;
}

static bool nfu_set_spi_settings(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	struct mcp_settings *mcp = &info->mcp;

	return mcp2210_set_spi_transfer_settings(bitfury, mcp->bitrate, mcp->icsv,
		mcp->acsv, mcp->cstdd, mcp->ldbtcsd, mcp->sdbd, mcp->bpst, mcp->spimode);
}

static void nfu_alloc_arrays(struct bitfury_info *info)
{
	info->payload = calloc(sizeof(struct bitfury_payload), info->chips);
	info->oldbuf = calloc(sizeof(unsigned int) * 17, info->chips);
	info->job_switched = calloc(sizeof(bool), info->chips);
	info->second_run = calloc(sizeof(bool), info->chips);
	info->work = calloc(sizeof(struct work *), info->chips);
	info->owork = calloc(sizeof(struct work *), info->chips);
	info->submits = calloc(sizeof(int *), info->chips);
}

static bool nfu_detect_one(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	struct mcp_settings *mcp = &info->mcp;
	char buf[MCP2210_BUFFER_LENGTH];
	unsigned int length;
	bool ret = false;
	int i, val;

	/* Identify number of chips, and use it in device name if it can fit
	 * into 3 chars, otherwise use generic NFU name. */
	val = sscanf(bitfury->usbdev->prod_string, "NanoFury NF%u ", &info->chips);
	if (val < 1)
		info->chips = 1;
	else if (info->chips < 10) {
		sprintf(info->product, "NF%u", info->chips);
		bitfury->drv->name = info->product;
	}
	nfu_alloc_arrays(info);

	info->spi_txrx = &mcp_spi_txrx;
	mcp2210_get_gpio_settings(bitfury, mcp);

	for (i = 0; i < 9; i++) {
		/* Set all pins to GPIO mode */
		mcp->designation.pin[i] = MCP2210_PIN_GPIO;
		/* Set all pins to input mode */
		mcp->direction.pin[i] = MCP2210_GPIO_INPUT;
		mcp->value.pin[i] = MCP2210_GPIO_PIN_LOW;
	}

	/* Set LED and PWR pins to output and high */
	mcp->direction.pin[NFU_PIN_LED] = mcp->direction.pin[NFU_PIN_PWR_EN] = MCP2210_GPIO_OUTPUT;
	mcp->value.pin[NFU_PIN_LED] = mcp->value.pin[NFU_PIN_PWR_EN] = MCP2210_GPIO_PIN_HIGH;
	mcp->direction.pin[NFU_PIN_PWR_EN0] = MCP2210_GPIO_OUTPUT;
	mcp->value.pin[NFU_PIN_PWR_EN0] = MCP2210_GPIO_PIN_LOW;

	mcp->direction.pin[4] = MCP2210_GPIO_OUTPUT;
	mcp->designation.pin[4] = MCP2210_PIN_CS;

	if (!mcp2210_set_gpio_settings(bitfury, mcp))
		goto out;

	if (opt_debug) {
		struct gpio_pin gp;

		mcp2210_get_gpio_pindirs(bitfury, &gp);
		for (i = 0; i < 9; i++) {
			applog(LOG_DEBUG, "%s %d: Pin dir %d %d", bitfury->drv->name,
			       bitfury->device_id, i, gp.pin[i]);
		}
		mcp2210_get_gpio_pinvals(bitfury, &gp);
		for (i = 0; i < 9; i++) {
			applog(LOG_DEBUG, "%s %d: Pin val %d %d", bitfury->drv->name,
			       bitfury->device_id, i, gp.pin[i]);
		}
		mcp2210_get_gpio_pindes(bitfury, &gp);
		for (i = 0; i < 9; i++) {
			applog(LOG_DEBUG, "%s %d: Pin des %d %d", bitfury->drv->name,
			       bitfury->device_id, i, gp.pin[i]);
		}
	}

	/* Cancel any transfers in progress */
	if (!mcp2210_spi_cancel(bitfury))
		goto out;
	if (!mcp2210_get_spi_transfer_settings(bitfury, &mcp->bitrate, &mcp->icsv,
	    &mcp->acsv, &mcp->cstdd, &mcp->ldbtcsd, &mcp->sdbd, &mcp->bpst, &mcp->spimode))
		goto out;
	mcp->bitrate = 200000; // default to 200kHz
	mcp->icsv = 0xffff;
	mcp->acsv = 0xffef;
	mcp->cstdd = mcp->ldbtcsd = mcp->sdbd = mcp->spimode = 0;
	mcp->bpst = 1;
	if (!nfu_set_spi_settings(bitfury, info))
		goto out;

	buf[0] = 0;
	length = 1;
	if (!mcp2210_spi_transfer(bitfury, mcp, buf, &length))
		goto out;
	/* after this command SCK_OVRRIDE should read the same as current SCK
	 * value (which for mode 0 should be 0) */
	if (!mcp2210_get_gpio_pinval(bitfury, NFU_PIN_SCK_OVR, &val))
		goto out;
	if (val != MCP2210_GPIO_PIN_LOW)
		goto out;

	/* switch SCK to polarity (default SCK=1 in mode 2) */
	mcp->spimode = 2;
	if (!nfu_set_spi_settings(bitfury, info))
		goto out;
	buf[0] = 0;
	length = 1;
	if (!mcp2210_spi_transfer(bitfury, mcp, buf, &length))
		goto out;
	/* after this command SCK_OVRRIDE should read the same as current SCK
	 * value (which for mode 2 should be 1) */
	if (!mcp2210_get_gpio_pinval(bitfury, NFU_PIN_SCK_OVR, &val))
		goto out;
	if (val != MCP2210_GPIO_PIN_HIGH)
		goto out;

	/* switch SCK to polarity (default SCK=0 in mode 0) */
	mcp->spimode = 0;
	if (!nfu_set_spi_settings(bitfury, info))
		goto out;
	buf[0] = 0;
	length = 1;
	if (!mcp2210_spi_transfer(bitfury, mcp, buf, &length))
		goto out;
	if (!mcp2210_get_gpio_pinval(bitfury, NFU_PIN_SCK_OVR, &val))
		goto out;
	if (val != MCP2210_GPIO_PIN_LOW)
		goto out;

	info->osc6_bits = opt_nfu_bits;
	if (!nfu_reinit(bitfury, info))
		goto out;

	ret = true;
	if (!add_cgpu(bitfury))
		quit(1, "Failed to add_cgpu in nfu_detect_one");

	update_usb_stats(bitfury);
	applog(LOG_INFO, "%s %d: Successfully initialised %s",
	       bitfury->drv->name, bitfury->device_id, bitfury->device_path);
	spi_clear_buf(info);

	info->total_nonces = info->chips;
out:
	if (!ret)
		nfu_close(bitfury);

	return ret;
}

static bool bxm_purge_buffers(struct cgpu_info *bitfury)
{
	int err;

	err = usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_RESET_REQUEST, SIO_RESET_PURGE_RX, 1, C_BXM_PURGERX);
	if (err)
		return false;
	err = usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_RESET_REQUEST, SIO_RESET_PURGE_TX, 1, C_BXM_PURGETX);
	if (err)
		return false;
	return true;
}

/* Calculate required divisor for desired frequency see FTDI AN_108 page 19*/
static uint16_t calc_divisor(uint32_t system_clock, uint32_t freq)
{
	uint16_t divisor = system_clock / freq;

	divisor /= 2;
	divisor -= 1;
	return divisor;
}

static void bxm_shutdown(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	int chip_n;

	for (chip_n = 0; chip_n < 2; chip_n++) {
		spi_clear_buf(info);
		spi_add_break(info);
		spi_add_fasync(info, chip_n);
		spi_config_reg(info, 4, 0);
		info->spi_txrx(bitfury, info);
	}
}

static void bxm_close(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	unsigned char bitmask = 0;
	unsigned char mode = BITMODE_RESET;
	unsigned short usb_val = bitmask;

	bxm_shutdown(bitfury, info);

	//Need to do BITMODE_RESET before usb close per FTDI
	usb_val |= (mode << 8);
	usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_SET_BITMODE_REQUEST, usb_val, 1, C_BXM_SETBITMODE);
}

static bool bxm_open(struct cgpu_info *bitfury)
{
	unsigned char mode = BITMODE_RESET;
	unsigned char bitmask = 0;
	unsigned short usb_val = bitmask;
	uint32_t system_clock = TWELVE_MHZ;
	uint32_t freq = 200000;
	uint16_t divisor = calc_divisor(system_clock,freq);
	int amount, err;
	char buf[4];

	/* Enable the transaction translator emulator for these devices
	 * otherwise we may write to them too quickly. */
	bitfury->usbdev->tt = true;

	err = usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_RESET_REQUEST, SIO_RESET_SIO, 1, C_BXM_SRESET);
	if (err)
		return false;
	err = usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_SET_LATENCY_TIMER_REQUEST, BXM_LATENCY_MS, 1, C_BXM_SETLATENCY);
	if (err)
		return false;
	err = usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_SET_EVENT_CHAR_REQUEST, 0x00, 1, C_BXM_SECR);
	if (err)
		return false;

	//Do a BITMODE_RESET
	usb_val |= (mode << 8);
	err = usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_SET_BITMODE_REQUEST, usb_val, 1, C_BXM_SETBITMODE);
	if (err)
		return false;
	//Now set to MPSSE mode
	bitmask = 0;
	mode = BITMODE_MPSSE;
	usb_val = bitmask;
	usb_val |= (mode << 8);
	err = usb_transfer(bitfury, FTDI_TYPE_OUT, SIO_SET_BITMODE_REQUEST, usb_val, 1, C_BXM_SETBITMODE);
	if (err)
		return false;

	//Now set the clock divisor
	//First send just the 0x8B command to set the system clock to 12MHz
	memset(buf, 0, 4);
	buf[0] = TCK_D5;
	err = usb_write(bitfury, buf, 1, &amount, C_BXM_CLOCK);
	if (err || amount != 1)
		return false;

	buf[0] = TCK_DIVISOR;
	buf[1] = (divisor & 0xFF);
	buf[2] = ((divisor >> 8) & 0xFF);
	err = usb_write(bitfury, buf, 3, &amount, C_BXM_CLOCKDIV);
	if (err || amount != 3)
		return false;

	//Disable internal loopback
	buf[0] = LOOPBACK_END;
	err = usb_write(bitfury, buf, 1, &amount, C_BXM_LOOP);
	if (err || amount != 1)
		return false;

	//Now set direction and idle (initial) states for the pins
	buf[0] = SET_OUT_ADBUS;
	buf[1] = DEFAULT_STATE; //Bitmask for LOW_PORT
	buf[2] = DEFAULT_DIR;
	err = usb_write(bitfury, buf, 3, &amount, C_BXM_ADBUS);
	if (err || amount != 3)
		return false;

	//Set the pin states for the HIGH_BITS port as all outputs, all low
	buf[0] = SET_OUT_ACBUS;
	buf[1] = 0x00; //Bitmask for HIGH_PORT
	buf[2] = 0xFF;
	err = usb_write(bitfury, buf, 3, &amount, C_BXM_ACBUS);
	if (err || amount != 3)
		return false;

	return true;
}

static bool bxm_set_CS_low(struct cgpu_info *bitfury)
{
	char buf[4] = { 0 };
	int err, amount;

	buf[0] = SET_OUT_ADBUS;
	buf[1] &= ~DEFAULT_STATE; //Bitmask for LOW_PORT
	buf[2] = DEFAULT_DIR;
	err = usb_write(bitfury, buf, 3, &amount, C_BXM_CSLOW);
	if (err || amount != 3)
		return false;

	return true;
}

static bool bxm_set_CS_high(struct cgpu_info *bitfury)
{
	char buf[4] = { 0 };
	int err, amount;

	buf[0] = SET_OUT_ADBUS;
	buf[1] = DEFAULT_STATE; //Bitmask for LOW_PORT
	buf[2] = DEFAULT_DIR;
	err = usb_write(bitfury, buf, 3, &amount, C_BXM_CSHIGH);
	if (err || amount != 3)
		return false;

	return true;
}

static bool bxm_reset_bitfury(struct cgpu_info *bitfury)
{
	char buf[20] = { 0 };
	char rst_buf[8] = {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};
	int err, amount;

	//Set the FTDI CS pin HIGH. This will gate the clock to the Bitfury chips so we can send the reset sequence.
	if (!bxm_set_CS_high(bitfury))
		return false;

	buf[0] = WRITE_BYTES_SPI0;
	buf[1] = (uint8_t)16 - (uint8_t)1;
	buf[2] = 0;
	memcpy(&buf[3], rst_buf, 8);
	memcpy(&buf[11], rst_buf, 8);
	err = usb_write(bitfury, buf, 19, &amount, C_BXM_RESET);
	if (err || amount != 19)
		return false;

	if (!bxm_set_CS_low(bitfury))
		return false;

	return true;
}

static bool bxm_reinit(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	bool ret;
	int i;

	for (i = 0; i < 2; i++) {
		spi_clear_buf(info);
		spi_add_break(info);
		spi_add_fasync(info, i);
		spi_set_freq(info);
		spi_send_conf(info);
		spi_send_init(info);
		ret = info->spi_txrx(bitfury, info);
		if (!ret)
			break;
	}
	return ret;
}

static bool bxm_detect_one(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	bool ret;

	info->spi_txrx = &ftdi_spi_txrx;
	ret = bxm_open(bitfury);
	if (!ret)
		goto out;
	ret = bxm_purge_buffers(bitfury);
	if (!ret)
		goto out;
	ret = bxm_reset_bitfury(bitfury);
	if (!ret)
		goto out;
	ret = bxm_purge_buffers(bitfury);
	if (!ret)
		goto out;

	/* Do a dummy read */
	memset(info->spibuf, 0, 80);
	info->spibufsz = 80;
	ret = info->spi_txrx(bitfury, info);
	if (!ret)
		goto out;
	info->osc6_bits = opt_bxm_bits;
	/* Only have 2 chip devices for now */
	info->chips = 2;
	nfu_alloc_arrays(info);

	ret = bxm_reinit(bitfury, info);
	if (!ret)
		goto out;

	if (!add_cgpu(bitfury))
		quit(1, "Failed to add_cgpu in bxm_detect_one");

	update_usb_stats(bitfury);
	applog(LOG_INFO, "%s %d: Successfully initialised %s",
	       bitfury->drv->name, bitfury->device_id, bitfury->device_path);
	spi_clear_buf(info);

	info->total_nonces = 1;
out:
	if (!ret)
		bxm_close(bitfury, info);
	return ret;
}

static struct cgpu_info *bitfury_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *bitfury;
	struct bitfury_info *info;
	enum sub_ident ident;
	bool ret = false;

	bitfury = usb_alloc_cgpu(&bitfury_drv, 1);

	if (!usb_init(bitfury, dev, found))
		goto out;
	applog(LOG_INFO, "%s %d: Found at %s", bitfury->drv->name,
	       bitfury->device_id, bitfury->device_path);

	info = calloc(sizeof(struct bitfury_info), 1);
	if (!info)
		quit(1, "Failed to calloc info in bitfury_detect_one");
	bitfury->device_data = info;
	info->ident = ident = usb_ident(bitfury);
	switch (ident) {
		case IDENT_BF1:
			ret = bf1_detect_one(bitfury, info);
			break;
		case IDENT_BXF:
		case IDENT_OSM:
			ret = bxf_detect_one(bitfury, info);
			break;
		case IDENT_NFU:
			ret = nfu_detect_one(bitfury, info);
			break;
		case IDENT_BXM:
			ret = bxm_detect_one(bitfury, info);
			break;
		default:
			applog(LOG_INFO, "%s %d: Unrecognised bitfury device",
			       bitfury->drv->name, bitfury->device_id);
			break;
	}

	if (!ret) {
		free(info);
		usb_uninit(bitfury);
out:
		bitfury = usb_free_cgpu(bitfury);
	}
	return bitfury;
}

static void bitfury_detect(bool __maybe_unused hotplug)
{
	usb_detect(&bitfury_drv, bitfury_detect_one);
}

static void adjust_bxf_chips(struct cgpu_info *bitfury, struct bitfury_info *info, int chip)
{
	int chips = chip + 1;
	size_t old, new;

	if (likely(chips <= info->chips))
		return;
	if (chips > 999)
		return;
	old = sizeof(int) * info->chips;
	new = sizeof(int) * chips;
	applog(LOG_INFO, "%s %d: Adjust chip size to %d", bitfury->drv->name, bitfury->device_id,
	       chips);

	recalloc(info->filtered_hw, old, new);
	recalloc(info->job, old, new);
	recalloc(info->submits, old, new);
	if (info->chips == 2 && chips <= 6 && info->ident == IDENT_BXF)
		bitfury->drv->name = "HXF";
	else if (info->chips <= 6 && chips > 6 && info->ident == IDENT_BXF)
		bitfury->drv->name = "MXF";
	info->chips = chips;
}

static void parse_bxf_submit(struct cgpu_info *bitfury, struct bitfury_info *info, char *buf)
{
	struct work *match_work, *tmp, *work = NULL;
	struct thr_info *thr = info->thr;
	uint32_t nonce, timestamp;
	int workid, chip = -1;

	if (!sscanf(&buf[7], "%x %x %x %d", &nonce, &workid, &timestamp, &chip)) {
		applog(LOG_WARNING, "%s %d: Failed to parse submit response",
		       bitfury->drv->name, bitfury->device_id);
		return;
	}
	adjust_bxf_chips(bitfury, info, chip);
	if (unlikely(chip >= info->chips || chip < 0)) {
		applog(LOG_INFO, "%s %d: Invalid submit chip number %d",
		       bitfury->drv->name, bitfury->device_id, chip);
	} else
		info->submits[chip]++;

	applog(LOG_DEBUG, "%s %d: Parsed nonce %u workid %d timestamp %u",
	       bitfury->drv->name, bitfury->device_id, nonce, workid, timestamp);

	rd_lock(&bitfury->qlock);
	HASH_ITER(hh, bitfury->queued_work, match_work, tmp) {
		if (match_work->subid == workid) {
			work = copy_work(match_work);
			break;
		}
	}
	rd_unlock(&bitfury->qlock);

	if (!work) {
		/* Discard first results from any previous run */
		if (unlikely(!info->valid))
			return;

		applog(LOG_INFO, "%s %d: No matching work", bitfury->drv->name, bitfury->device_id);

		mutex_lock(&info->lock);
		info->no_matching_work++;
		mutex_unlock(&info->lock);

		inc_hw_errors(thr);
		return;
	}
	/* Set the device start time from when we first get valid results */
	if (unlikely(!info->valid)) {
		info->valid = true;
		cgtime(&bitfury->dev_start_tv);
	}
	set_work_ntime(work, timestamp);
	if (submit_nonce(thr, work, nonce)) {
		mutex_lock(&info->lock);
		info->nonces++;
		mutex_unlock(&info->lock);
	}
	free_work(work);
}

static bool bxf_send_clock(struct cgpu_info *bitfury, struct bitfury_info *info,
			   uint8_t clockspeed)
{
	char buf[64];

	info->clocks = clockspeed;
	sprintf(buf, "clock %d %d\n", clockspeed, clockspeed);
	return bxf_send_msg(bitfury, buf, C_BXF_CLOCK);
}

static void parse_bxf_temp(struct cgpu_info *bitfury, struct bitfury_info *info, char *buf)
{
	uint8_t clockspeed = info->clocks;
	int decitemp;

	if (!sscanf(&buf[5], "%d", &decitemp)) {
		applog(LOG_INFO, "%s %d: Failed to parse temperature",
		       bitfury->drv->name, bitfury->device_id);
		return;
	}

	mutex_lock(&info->lock);
	bitfury->temp = (double)decitemp / 10;
	if (decitemp > info->max_decitemp) {
		info->max_decitemp = decitemp;
		applog(LOG_DEBUG, "%s %d: New max decitemp %d", bitfury->drv->name,
		       bitfury->device_id, decitemp);
	}
	mutex_unlock(&info->lock);

	if (decitemp > info->temp_target + BXF_TEMP_HYSTERESIS) {
		if (info->clocks <= BXF_CLOCK_MIN)
			goto out;
		applog(LOG_WARNING, "%s %d: Hit overheat temperature of %d, throttling!",
		       bitfury->drv->name, bitfury->device_id, decitemp);
		clockspeed = BXF_CLOCK_MIN;
		goto out;
	}
	if (decitemp > info->temp_target) {
		if (info->clocks <= BXF_CLOCK_MIN)
			goto out;
		if (decitemp < info->last_decitemp)
			goto out;
		applog(LOG_INFO, "%s %d: Temp %d over target and not falling, decreasing clock",
		       bitfury->drv->name, bitfury->device_id, decitemp);
		clockspeed = info->clocks - 1;
		goto out;
	}
	if (decitemp <= info->temp_target && decitemp >= info->temp_target - BXF_TEMP_HYSTERESIS) {
		if (decitemp == info->last_decitemp)
			goto out;
		if (decitemp > info->last_decitemp) {
			if (info->clocks <= BXF_CLOCK_MIN)
				goto out;
			applog(LOG_DEBUG, "%s %d: Temp %d in target and rising, decreasing clock",
			       bitfury->drv->name, bitfury->device_id, decitemp);
			clockspeed = info->clocks - 1;
			goto out;
		}
		/* implies: decitemp < info->last_decitemp */
		if (info->clocks >= opt_bxf_bits)
			goto out;
		applog(LOG_DEBUG, "%s %d: Temp %d in target and falling, increasing clock",
		       bitfury->drv->name, bitfury->device_id, decitemp);
		clockspeed = info->clocks + 1;
		goto out;
	}
	/* implies: decitemp < info->temp_target - BXF_TEMP_HYSTERESIS */
	if (info->clocks >= opt_bxf_bits)
		goto out;
	applog(LOG_DEBUG, "%s %d: Temp %d below target, increasing clock",
		bitfury->drv->name, bitfury->device_id, decitemp);
	clockspeed = info->clocks + 1;
out:
	bxf_send_clock(bitfury, info, clockspeed);
	info->last_decitemp = decitemp;
}

static void bxf_update_work(struct cgpu_info *bitfury, struct bitfury_info *info);

static void parse_bxf_needwork(struct cgpu_info *bitfury, struct bitfury_info *info,
			       char *buf)
{
	int needed;

	if (!sscanf(&buf[9], "%d", &needed)) {
		applog(LOG_INFO, "%s %d: Failed to parse needwork",
		       bitfury->drv->name, bitfury->device_id);
		return;
	}
	while (needed-- > 0)
		bxf_update_work(bitfury, info);
}

static void parse_bxf_job(struct cgpu_info *bitfury, struct bitfury_info *info, char *buf)
{
	int job_id, timestamp, chip;

	if (sscanf(&buf[4], "%x %x %x", &job_id, &timestamp, &chip) != 3) {
		applog(LOG_INFO, "%s %d: Failed to parse job",
		       bitfury->drv->name, bitfury->device_id);
		return;
	}
	adjust_bxf_chips(bitfury, info, chip);
	if (chip >= info->chips || chip < 0) {
		applog(LOG_INFO, "%s %d: Invalid job chip number %d",
		       bitfury->drv->name, bitfury->device_id, chip);
		return;
	}
	++info->job[chip];
}

static void parse_bxf_hwerror(struct cgpu_info *bitfury, struct bitfury_info *info, char *buf)
{
	int chip;

	if (!sscanf(&buf[8], "%d", &chip)) {
		applog(LOG_INFO, "%s %d: Failed to parse hwerror",
		       bitfury->drv->name, bitfury->device_id);
		return;
	}
	adjust_bxf_chips(bitfury, info, chip);
	if (chip >= info->chips || chip < 0) {
		applog(LOG_INFO, "%s %d: Invalid hwerror chip number %d",
		       bitfury->drv->name, bitfury->device_id, chip);
		return;
	}
	++info->filtered_hw[chip];
}

#define PARSE_BXF_MSG(MSG) \
	msg = strstr(buf, #MSG); \
	if (msg) { \
		parse_bxf_##MSG(bitfury, info, msg); \
		continue; \
	}

static void *bxf_get_results(void *userdata)
{
	struct cgpu_info *bitfury = userdata;
	struct bitfury_info *info = bitfury->device_data;
	char threadname[24], buf[512];

	snprintf(threadname, 24, "bxf_recv/%d", bitfury->device_id);

	/* We operate the device at lowest diff since it's not a lot of results
	 * to process and gives us a better indicator of the nonce return rate
	 * and hardware errors. */
	sprintf(buf, "target ffffffff\n");
	if (!bxf_send_msg(bitfury, buf, C_BXF_TARGET))
		goto out;

	/* Read thread sends the first work item to get the device started
	 * since it will roll ntime and make work itself from there on. */
	bxf_update_work(bitfury, info);
	bxf_update_work(bitfury, info);

	while (likely(!bitfury->shutdown)) {
		char *msg, *strbuf;
		int err;

		if (unlikely(bitfury->usbinfo.nodev))
			break;

		err = bxf_recv_msg(bitfury, buf);
		if (err < 0) {
			if (err != LIBUSB_ERROR_TIMEOUT)
				break;
			continue;
		}
		if (!err)
			continue;

		if (opt_bxf_debug) {
			strbuf = str_text(buf);
			applog(LOG_ERR, "%s %d: < [%s]",
				bitfury->drv->name, bitfury->device_id, strbuf);
			free(strbuf);
		}

                PARSE_BXF_MSG(submit);
		PARSE_BXF_MSG(temp);
		PARSE_BXF_MSG(needwork);
		PARSE_BXF_MSG(job);
		PARSE_BXF_MSG(hwerror);

		if (buf[0] != '#') {
			strbuf = str_text(buf);
			applog(LOG_DEBUG, "%s %d: Unrecognised string %s",
			       bitfury->drv->name, bitfury->device_id, strbuf);
			free(strbuf);
		}
	}
out:
	return NULL;
}

static bool bxf_prepare(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	bxf_send_ledmode(bitfury);
	bxf_send_debugmode(bitfury);

	mutex_init(&info->lock);
	if (pthread_create(&info->read_thr, NULL, bxf_get_results, (void *)bitfury))
		quit(1, "Failed to create bxf read_thr");

	return bxf_send_clock(bitfury, info, opt_bxf_bits);
}

static bool bitfury_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury_info *info = bitfury->device_data;

	info->thr = thr;

	switch(info->ident) {
		case IDENT_BXF:
		case IDENT_OSM:
			return bxf_prepare(bitfury, info);
			break;
		case IDENT_BF1:
		default:
			return true;
	}
}

static int64_t bitfury_rate(struct bitfury_info *info)
{
	double nonce_rate;
	int64_t ret = 0;

	info->cycles++;
	info->total_nonces += info->nonces;
	info->saved_nonces += info->nonces;
	info->nonces = 0;
	nonce_rate = (double)info->total_nonces / (double)info->cycles;
	if (info->saved_nonces >= nonce_rate) {
		info->saved_nonces -= nonce_rate;
		ret = (double)0xffffffff * nonce_rate;
	}
	return ret;
}

static int64_t bf1_scan(struct thr_info *thr, struct cgpu_info *bitfury,
			struct bitfury_info *info)
{
	int amount, i, aged, total = 0, ms_diff;
	char readbuf[512], buf[45];
	struct work *work, *tmp;
	struct timeval tv_now;
	int64_t ret = 0;

	work = get_queue_work(thr, bitfury, thr->id);
	if (unlikely(thr->work_restart)) {
		work_completed(bitfury, work);
		goto out;
	}

	buf[0] = 'W';
	memcpy(buf + 1, work->midstate, 32);
	memcpy(buf + 33, work->data + 64, 12);

	/* New results may spill out from the latest work, making us drop out
	 * too early so read whatever we get for the first half nonce and then
	 * look for the results to prev work. */
	cgtime(&tv_now);
	ms_diff = 600 - ms_tdiff(&tv_now, &info->tv_start);
	if (ms_diff > 0) {
		usb_read_timeout_cancellable(bitfury, readbuf, 512, &amount, ms_diff,
					     C_BF1_GETRES);
		total += amount;
	}

	/* Now look for the bulk of the previous work results, they will come
	 * in a batch following the first data. */
	cgtime(&tv_now);
	ms_diff = BF1WAIT - ms_tdiff(&tv_now, &info->tv_start);
	/* If a work restart was sent, just empty the buffer. */
	if (unlikely(ms_diff < 10 || thr->work_restart))
		ms_diff = 10;
	usb_read_once_timeout_cancellable(bitfury, readbuf + total, BF1MSGSIZE,
					  &amount, ms_diff, C_BF1_GETRES);
	total += amount;
	while (amount) {
		usb_read_once_timeout(bitfury, readbuf + total, 512 - total, &amount, 10,
				      C_BF1_GETRES);
		total += amount;
	};

	/* Don't send whatever work we've stored if we got a restart */
	if (unlikely(thr->work_restart))
		goto out;

	/* Send work */
	cgtime(&work->tv_work_start);
	usb_write(bitfury, buf, 45, &amount, C_BF1_REQWORK);
	cgtime(&info->tv_start);

	/* Get response acknowledging work */
	usb_read(bitfury, buf, BF1MSGSIZE, &amount, C_BF1_GETWORK);

out:
	/* Search for what work the nonce matches in order of likelihood. Last
	 * entry is end of result marker. */
	for (i = 0; i < total - BF1MSGSIZE; i += BF1MSGSIZE) {
		bool found = false;
		uint32_t nonce;

		/* Ignore state & switched data in results for now. */
		memcpy(&nonce, readbuf + i + 3, 4);
		nonce = decnonce(nonce);

		rd_lock(&bitfury->qlock);
		HASH_ITER(hh, bitfury->queued_work, work, tmp) {
			if (bitfury_checkresults(thr, work, nonce)) {
				info->nonces++;
				found = true;
				break;
			}
		}
		rd_unlock(&bitfury->qlock);

		if (!found) {
			if (likely(info->valid))
				inc_hw_errors(thr);
		} else if (unlikely(!info->valid)) {
			info->valid = true;
			cgtime(&bitfury->dev_start_tv);
		}
	}

	cgtime(&tv_now);

	/* This iterates over the hashlist finding work started more than 6
	 * seconds ago. */
	aged = age_queued_work(bitfury, 6.0);
	if (aged) {
		applog(LOG_DEBUG, "%s %d: Aged %d work items", bitfury->drv->name,
		       bitfury->device_id, aged);
	}

	ret = bitfury_rate(info);

	if (unlikely(bitfury->usbinfo.nodev)) {
		applog(LOG_WARNING, "%s %d: Device disappeared, disabling thread",
		       bitfury->drv->name, bitfury->device_id);
		ret = -1;
	}
	return ret;
}

static int64_t bxf_scan(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	int ms, aged;
	int64_t ret;

	bxf_update_work(bitfury, info);
	ms = 1200 / info->chips;
	if (ms < 100)
		ms = 100;
	cgsleep_ms(ms);

	mutex_lock(&info->lock);
	ret = bitfury_rate(info);
	mutex_unlock(&info->lock);

	/* Keep no more than the last 90 seconds worth of work items in the
	 * hashlist */
	aged = age_queued_work(bitfury, 90.0);
	if (aged) {
		applog(LOG_DEBUG, "%s %d: Aged %d work items", bitfury->drv->name,
		       bitfury->device_id, aged);
	}

	if (unlikely(bitfury->usbinfo.nodev)) {
		applog(LOG_WARNING, "%s %d: Device disappeared, disabling thread",
		       bitfury->drv->name, bitfury->device_id);
		ret = -1;
	}
	return ret;
}

static void bitfury_check_work(struct thr_info *thr, struct cgpu_info *bitfury,
			       struct bitfury_info *info, int chip_n)
{
	if (!info->work[chip_n]) {
		info->work[chip_n] = get_work(thr, thr->id);
		if (unlikely(thr->work_restart)) {
			free_work(info->work[chip_n]);
			info->work[chip_n] = NULL;
			return;
		}
		bitfury_work_to_payload(&info->payload[chip_n], info->work[chip_n]);
	}

	if (unlikely(bitfury->usbinfo.nodev))
		return;

	if (!libbitfury_sendHashData(thr, bitfury, info, chip_n))
		usb_nodev(bitfury);

	if (info->job_switched[chip_n]) {
		if (likely(info->owork[chip_n]))
			free_work(info->owork[chip_n]);
		info->owork[chip_n] = info->work[chip_n];
		info->work[chip_n] = NULL;
	}

}

static int64_t nfu_scan(struct thr_info *thr, struct cgpu_info *bitfury,
			struct bitfury_info *info)
{
	int64_t ret = 0;
	int i;

	for (i = 0; i < info->chips; i++)
		bitfury_check_work(thr, bitfury, info, i);

	ret = bitfury_rate(info);

	if (unlikely(bitfury->usbinfo.nodev)) {
		applog(LOG_WARNING, "%s %d: Device disappeared, disabling thread",
		       bitfury->drv->name, bitfury->device_id);
		ret = -1;
	}

	return ret;
}

static int64_t bitfury_scanwork(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury_info *info = bitfury->device_data;
	int64_t ret = -1;

	if (unlikely(share_work_tdiff(bitfury) > 60)) {
		if (info->failing) {
			if (share_work_tdiff(bitfury) > 120) {
				applog(LOG_ERR, "%s %d: Device failed to respond to restart",
				       bitfury->drv->name, bitfury->device_id);
				return ret;
			}
		} else {
			applog(LOG_WARNING, "%s %d: No valid hashes for over 1 minute, attempting to reset",
			       bitfury->drv->name, bitfury->device_id);
			usb_reset(bitfury);
			info->failing = true;
		}
	}

	if (unlikely(bitfury->usbinfo.nodev))
		return ret;

	switch(info->ident) {
		case IDENT_BF1:
			ret = bf1_scan(thr, bitfury, info);
			break;
		case IDENT_BXF:
		case IDENT_OSM:
			ret = bxf_scan(bitfury, info);
			break;
		case IDENT_NFU:
		case IDENT_BXM:
			ret = nfu_scan(thr, bitfury, info);
			break;
		default:
			ret = 0;
			break;
	}
	if (ret > 0)
		info->failing = false;
	return ret;
}

static void bxf_send_maxroll(struct cgpu_info *bitfury, int maxroll)
{
	char buf[20];

	sprintf(buf, "maxroll %d\n", maxroll);
	bxf_send_msg(bitfury, buf, C_BXF_MAXROLL);
}

static bool bxf_send_work(struct cgpu_info *bitfury, struct work *work)
{
	char buf[512], hexwork[156];

	__bin2hex(hexwork, work->data, 76);
	sprintf(buf, "work %s %x\n", hexwork, work->subid);
	return bxf_send_msg(bitfury, buf, C_BXF_WORK);
}

static void bxf_update_work(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	struct thr_info *thr = info->thr;
	struct work *work;

	if (unlikely(bitfury->usbinfo.nodev))
		return;

	work = get_queue_work(thr, bitfury, thr->id);
	if (work->drv_rolllimit != info->maxroll) {
		info->maxroll = work->drv_rolllimit;
		bxf_send_maxroll(bitfury, info->maxroll);
	}

	mutex_lock(&info->lock);
	work->subid = ++info->work_id;
	mutex_unlock(&info->lock);

	cgtime(&work->tv_work_start);
	bxf_send_work(bitfury, work);
}

static void bitfury_flush_work(struct cgpu_info *bitfury)
{
	struct bitfury_info *info = bitfury->device_data;

	switch(info->ident) {
		case IDENT_BXF:
		case IDENT_OSM:
			bxf_send_flush(bitfury);
			bxf_update_work(bitfury, info);
			bxf_update_work(bitfury, info);
		case IDENT_BF1:
		default:
			break;
	}
}

static void bitfury_update_work(struct cgpu_info *bitfury)
{
	struct bitfury_info *info = bitfury->device_data;

	switch(info->ident) {
		case IDENT_BXF:
		case IDENT_OSM:
			bxf_update_work(bitfury, info);
		case IDENT_BF1:
		default:
			break;
	}
}

static struct api_data *bf1_api_stats(struct bitfury_info *info)
{
	struct api_data *root = NULL;
	double nonce_rate;
	char serial[16];
	int version;

	version = info->version;
	root = api_add_int(root, "Version", &version, true);
	root = api_add_string(root, "Product", info->product, false);
	sprintf(serial, "%08x", info->serial);
	root = api_add_string(root, "Serial", serial, true);
	nonce_rate = (double)info->total_nonces / (double)info->cycles;
	root = api_add_double(root, "NonceRate", &nonce_rate, true);

	return root;
}

static struct api_data *bxf_api_stats(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	struct api_data *root = NULL;
	double nonce_rate;
	char buf[32];
	int i;

	sprintf(buf, "%d.%d", info->ver_major, info->ver_minor);
	root = api_add_string(root, "Version", buf, true);
	root = api_add_int(root, "Revision", &info->hw_rev,  false);
	root = api_add_int(root, "Chips", &info->chips, false);
	nonce_rate = (double)info->total_nonces / (double)info->cycles;
	root = api_add_double(root, "NonceRate", &nonce_rate, true);
	root = api_add_int(root, "NoMatchingWork", &info->no_matching_work, false);
	root = api_add_double(root, "Temperature", &bitfury->temp, false);
	root = api_add_int(root, "Max DeciTemp", &info->max_decitemp, false);
	root = api_add_uint8(root, "Clock", &info->clocks, false);
	for (i = 0; i < info->chips; i++) {
		sprintf(buf, "Core%d hwerror", i);
		root = api_add_int(root, buf, &info->filtered_hw[i], false);
		sprintf(buf, "Core%d jobs", i);
		root = api_add_int(root, buf, &info->job[i], false);
		sprintf(buf, "Core%d submits", i);
		root = api_add_int(root, buf, &info->submits[i], false);
	}

	return root;
}

static struct api_data *nfu_api_stats(struct bitfury_info *info)
{
	struct api_data *root = NULL;
	char buf[32];
	int i;

	root = api_add_int(root, "Chips", &info->chips, false);
	for (i = 0; i < info->chips; i++) {
		sprintf(buf, "Core%d submits", i);
		root = api_add_int(root, buf, &info->submits[i], false);
	}
	return root;
}

static struct api_data *bitfury_api_stats(struct cgpu_info *cgpu)
{
	struct bitfury_info *info = cgpu->device_data;

	switch(info->ident) {
		case IDENT_BF1:
			return bf1_api_stats(info);
			break;
		case IDENT_BXF:
		case IDENT_OSM:
			return bxf_api_stats(cgpu, info);
			break;
		case IDENT_NFU:
		case IDENT_BXM:
			return nfu_api_stats(info);
			break;
		default:
			break;
	}
	return NULL;
}

static void bitfury_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info *cgpu)
{
	struct bitfury_info *info = cgpu->device_data;

	switch(info->ident) {
		case IDENT_BXF:
		case IDENT_OSM:
			tailsprintf(buf, bufsiz, "%5.1fC", cgpu->temp);
			break;
		default:
			break;
	}
}

static void bf1_init(struct cgpu_info *bitfury)
{
	bf1_close(bitfury);
	bf1_open(bitfury);
	bf1_reset(bitfury);
}

static void bitfury_init(struct cgpu_info *bitfury)
{
	struct bitfury_info *info = bitfury->device_data;

	switch(info->ident) {
		case IDENT_BF1:
			bf1_init(bitfury);
			break;
		default:
			break;
	}
}

static void bxf_close(struct bitfury_info *info)
{
	pthread_join(info->read_thr, NULL);
	mutex_destroy(&info->lock);
}

static void bitfury_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury_info *info = bitfury->device_data;

	switch(info->ident) {
		case IDENT_BF1:
			bf1_close(bitfury);
			break;
		case IDENT_BXF:
		case IDENT_OSM:
			bxf_close(info);
			break;
		case IDENT_NFU:
			nfu_close(bitfury);
			break;
		case IDENT_BXM:
			bxm_close(bitfury, info);
			break;
		default:
			break;
	}
	usb_nodev(bitfury);
}

/* Currently hardcoded to BF1 devices */
struct device_drv bitfury_drv = {
	.drv_id = DRIVER_bitfury,
	.dname = "bitfury",
	.name = "BF1",
	.drv_detect = bitfury_detect,
	.thread_prepare = bitfury_prepare,
	.hash_work = &hash_driver_work,
	.scanwork = bitfury_scanwork,
	.flush_work = bitfury_flush_work,
	.update_work = bitfury_update_work,
	.get_api_stats = bitfury_api_stats,
	.get_statline_before = bitfury_get_statline_before,
	.reinit_device = bitfury_init,
	.thread_shutdown = bitfury_shutdown,
	.identify_device = bitfury_identify
};

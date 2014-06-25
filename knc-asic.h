#ifndef _CGMINER_NEPTUNE_H
#define _CGMINER_NEPTUNE_H

#include <stdint.h>
#include "miner.h"

struct knc_die_info {
	enum {
		KNC_VERSION_UNKNOWN = 0,
		KNC_VERSION_JUPITER,
		KNC_VERSION_NEPTUNE
	} version;
	int cores;
};

int knc_prepare_report(uint8_t *request, int die, int core);
int knc_prepare_neptune_setwork(uint8_t *request, int die, int core, int slot, struct work *work, int clean);
int knc_prepare_jupiter_setwork(uint8_t *request, int die, int core, int slot, struct work *work);
int knc_prepare_jupiter_halt(uint8_t *request, int die, int core);
int knc_prepare_neptune_halt(uint8_t *request, int die, int core);

void knc_prepare_neptune_message(int request_length, const uint8_t *request, uint8_t *buffer);

#define KNC_ACCEPTED    (1<<0)
#define KNC_ERR_CRC     (1<<1)
#define KNC_ERR_ACK     (1<<2)
#define KNC_ERR_CRCACK  (1<<3)
#define KNC_ERR_MASK	(~(KNC_ACCEPTED))

int knc_prepare_transfer(uint8_t *txbuf, int offset, int size, int channel, int request_length, const uint8_t *request, int response_length);
int knc_verify_response(uint8_t *rxbuf, int len, int response_length);
int knc_syncronous_transfer(void *ctx, int channel, int request_length, const uint8_t *request, int response_length, uint8_t *response);

int knc_detect_die(void *ctx, int channel, int die, struct knc_die_info *die_info);

#endif

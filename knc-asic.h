#ifndef _CGMINER_NEPTUNE_H
#define _CGMINER_NEPTUNE_H

#include <stdint.h>
#include "miner.h"

/* ASIC Command codes */
#define	KNC_ASIC_CMD_GETINFO             0x80
#define KNC_ASIC_CMD_SETWORK             0x81
#define KNC_ASIC_CMD_SETWORK_CLEAN       0x83        /* Neptune */
#define KNC_ASIC_CMD_HALT                0x83        /* Jupiter */
#define KNC_ASIC_CMD_REPORT              0x82

/* Status byte */
#define KNC_ASIC_ACK_CRC                    (1<<5)
#define KNC_ASIC_ACK_ACCEPT                 (1<<2)
#define KNC_ASIC_ACK_MASK                   (~(KNC_ASIC_ACK_CRC|KNC_ASIC_ACK_ACCEPT))
#define KNC_ASIC_ACK_MATCH                  ((1<<7)|(1<<0))

/* Version word */
#define KNC_ASIC_VERSION_JUPITER            0xa001
#define KNC_ASIC_VERSION_NEPTUNE            0xa002

/* Limits of current chips & I/O board */
#define KNC_MAX_CORES_PER_DIE	360
#define KNC_MAX_ASICS 6

struct knc_die_info {
	enum {
		KNC_VERSION_UNKNOWN = 0,
		KNC_VERSION_JUPITER,
		KNC_VERSION_NEPTUNE
	} version;
	char want_work[KNC_MAX_CORES_PER_DIE];
	int cores;
};

int knc_prepare_report(uint8_t *request, int die, int core);
int knc_prepare_neptune_setwork(uint8_t *request, int die, int core, int slot, struct work *work, int clean);
int knc_prepare_jupiter_setwork(uint8_t *request, int die, int core, int slot, struct work *work);
int knc_prepare_jupiter_halt(uint8_t *request, int die, int core);
int knc_prepare_neptune_halt(uint8_t *request, int die, int core);

int knc_decode_info(uint8_t *response, struct knc_die_info *die_info);

void knc_prepare_neptune_message(int request_length, const uint8_t *request, uint8_t *buffer);

#define KNC_ACCEPTED    (1<<0)
#define KNC_ERR_CRC     (1<<1)
#define KNC_ERR_ACK     (1<<2)
#define KNC_ERR_CRCACK  (1<<3)
#define KNC_ERR_MASK	(~(KNC_ACCEPTED))

int knc_prepare_transfer(uint8_t *txbuf, int offset, int size, int channel, int request_length, const uint8_t *request, int response_length);
int knc_decode_response(uint8_t *rxbuf, int request_length, uint8_t **response, int response_length);
int knc_syncronous_transfer(void *ctx, int channel, int request_length, const uint8_t *request, int response_length, uint8_t *response);

int knc_detect_die(void *ctx, int channel, int die, struct knc_die_info *die_info);

#endif

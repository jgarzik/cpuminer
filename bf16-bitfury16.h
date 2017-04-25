#ifndef BF16_BITFURY16_H
#define BF16_BITFURY16_H

#include <stdint.h>

#include "bf16-spidevice.h"

/******************************************************
 *                      Macros
 ******************************************************/

#define LIST_PUSH_HEAD(_list, _item) { \
	if (_list->tail == NULL) {         \
		_list->tail = _item;           \
		_list->head = _item;           \
	} else {                           \
		_list->head->prev = _item;     \
		_item->next = _list->head;     \
		_list->head = _item;           \
	}                                  \
}

#define LIST_PUSH_TAIL(_list, _item) { \
	if (_list->head == NULL) {         \
		_list->head = _item;           \
		_list->tail = _item;           \
	} else {                           \
		_list->tail->next = _item;     \
		_item->prev = _list->tail;     \
		_list->tail = _item;           \
	}                                  \
}

#define LIST_POP_HEAD(_list) {           \
	if (_list->head != _list->tail) {    \
		_list->head = _list->head->next; \
		_list->head->prev = NULL;        \
	} else {                             \
		_list->head = NULL;              \
		_list->tail = NULL;              \
	}                                    \
}

#define LIST_POP_TAIL(_list) {           \
	if (_list->head != _list->tail) {    \
		_list->tail = _list->tail->prev; \
		_list->tail->next = NULL;        \
	} else {                             \
		_list->head = NULL;              \
		_list->tail = NULL;              \
	}                                    \
}

#define LIST_REMOVE(_list, _item) {          \
	if (_list->head != _list->tail) {        \
		if (_list->head == _item) {          \
			_list->head = _list->head->next; \
			_list->head->prev = NULL;        \
		} else if (_list->tail == _item) {   \
			_list->tail = _list->tail->prev; \
			_list->tail->next = NULL;        \
		} else {                             \
			bf_data_t* prev;                 \
			bf_data_t* next;                 \
			prev = _item->prev;              \
			next = _item->next;              \
			prev->next = next;               \
			next->prev = prev;               \
		}	                                 \
	} else {                                 \
		_list->head = NULL;                  \
		_list->tail = NULL;                  \
	}                                        \
}

#define L_LOCK(_list)   pthread_mutex_lock  (&_list->lock);
#define L_UNLOCK(_list) pthread_mutex_unlock(&_list->lock);

/******************************************************
 *                    Constants
 ******************************************************/

#define CHIP_COEFF	        4.295
#define CHIP_CMD_NUM        8
#define CMD_BUFFER_LEN      4096

/******************************************************
 *                   Enumerations
 ******************************************************/

typedef enum {
	CHIP_CMD_TASK_STATUS,
	CHIP_CMD_TASK_WRITE,
	CHIP_CMD_TASK_SWITCH,
	CHIP_CMD_READ_NONCE  = 0x04,
	CHIP_CMD_SET_CLOCK   = 0x08,
	CHIP_CMD_TOGGLE      = 0x10,
	CHIP_CMD_SET_MASK    = 0x20,
	CHIP_CMD_CREATE_CHANNEL
} bf_cmd_code_t;

/* chip state enumeration */
typedef enum {
	UNINITIALIZED,
	TOGGLE_SET,
	CLOCK_SET,
	MASK_SET,
	TASK_SENT,
	TASK_SWITCHED,
	FAILING,
	DISABLED
} bf_chip_status_t;

enum bf_channel_id {
	BF250_NONE  = 0x00,
	BF250_LOCAL = 0x04,
	BF250_CHAN1 = 0x06,
	BF250_CHAN2 = 0x07
};

typedef enum {
	RENONCE_STAGE0,
	RENONCE_STAGE1,
	RENONCE_STAGE2,
	RENONCE_STAGE3,
	RENONCE_STAGE_FINISHED
} bf_renonce_stage_t;

/******************************************************
 *                 Type Definitions
 ******************************************************/

/* list definition */
struct bf_data {
	struct bf_data* next;
	struct bf_data* prev;
	void*           data;
};

typedef struct bf_data bf_data_t;

typedef struct {
	bf_data_t*      head;
	bf_data_t*      tail;
	uint32_t        count;
	pthread_mutex_t lock;
} bf_list_t;

/* general chip command staff */
typedef struct {
	bf_cmd_code_t  cmd_code;
	char           cmd_description[32];
} bf_cmd_description_t;

typedef struct {
	int8_t         board_id;
	int8_t         bcm250_id;
	int8_t         chip_id;
} bf_chip_address_t;

typedef struct {
	bf_chip_address_t   chip_address;
	uint8_t             depth;
	bf_cmd_code_t       cmd_code;
	uint8_t             data_length;
	uint8_t             tx[128];
	uint8_t             rx[64];
	uint8_t             status;
	uint8_t             checksum;
	bool                checksum_error;
	uint8_t             nonce_checksum;
	bool                nonce_checksum_error;
} bf_command_t;

/* work stuff */
typedef struct {
	uint32_t    midstate[8];
	uint32_t    m7;
	uint32_t    ntime;
	uint32_t    nbits;
} bf_payload_t;

typedef struct {
	struct work*    work;
	bf_payload_t    payload;
	bool            rolled;
	time_t          generated;
} bf_workd_t;

typedef struct {
	struct work     work;
	bf_payload_t    payload;
	uint8_t         task[80];
} bf_works_t;

/* nonceworker stuff */
typedef struct {
	bf_chip_address_t   chip_address;
	bf_chip_address_t   src_address;
	bf_works_t          cwork;
	bf_works_t          owork;
	uint32_t            nonce;
} bf_noncework_t;

/* renonceworker stuff */
typedef struct {
	bf_chip_address_t   src_address;
	uint32_t            nonce;
} bf_renoncework_t;

/* nonce recalculation staff */
/* nonce list */
typedef struct {
	uint32_t            nonce;
} bf_nonce_t;

/* task + nonces list */
typedef struct {
	uint32_t            id;
	uint32_t            nonce;
	bf_works_t          cwork;
	bf_works_t          owork;
	bf_chip_address_t   src_address;
	bf_renonce_stage_t  stage;
	bool                sent;
	bool                received;
	bool                match;
} bf_renonce_t;

/* command buffer staff */
typedef struct bf_cmd {
	bf_chip_address_t   chip_address;   /* address of chip calculating result */
	bf_chip_address_t   src_address;    /* track chip address during nonce recalculation */
	bf_works_t          work;
	uint32_t            id;             /* renonce id */
	uint8_t             depth;
	bf_cmd_code_t       cmd_code;
	uint8_t             data_length;
	uint8_t             checksum;
} bf_cmd_t;

#define CMD(_item)          ((bf_cmd_t *)          (_item->data))
#define NONCE(_item)        ((bf_nonce_t *)        (_item->data))
#define RENONCE(_item)      ((bf_renonce_t *)      (_item->data))
#define NONCEWORK(_item)    ((bf_noncework_t *)    (_item->data))
#define RENONCEWORK(_item)  ((bf_renoncework_t *)  (_item->data))
#define WORKD(_item)        ((bf_workd_t *)        (_item->data))
#define WORKS(_item)        ((bf_works_t *)        (_item->data))

typedef struct {
	bf_chip_address_t   chip_address;
	bf_chip_address_t   src_address;
	bf_works_t          work;
	uint32_t            id;
	bf_cmd_code_t       cmd_code;
	uint8_t             status;
	uint8_t             checksum_expected;
	uint8_t             checksum_received;
	bool                checksum_error;
	uint8_t             nonce_checksum_expected;
	uint8_t             nonce_checksum_received;
	bool                nonce_checksum_error;
} bf_cmd_status_t;

typedef enum {
	EMPTY,
	TX_READY,
	EXECUTED
} bf_cmd_buffer_status_t;

typedef struct {
	bf_list_t*              cmd_list;

	uint8_t*                tx_buffer;
	uint8_t*                rx_buffer;
	uint32_t                free_bytes; /* TX buffer bytes free */
	uint32_t                tx_offset;
	uint32_t                rx_offset;
	bf_cmd_buffer_status_t  status;
} bf_cmd_buffer_t;

/******************************************************
 *                    Structures
 ******************************************************/

/******************************************************
 *               Static Function Declarations
 ******************************************************/

/******************************************************
 *               Variables Definitions
 ******************************************************/

extern bf_cmd_description_t cmd_description[CHIP_CMD_NUM];

/******************************************************
 *               Function Definitions
 ******************************************************/

/* BF16 command primitives */
uint8_t spi_command_init(bf_command_t* command, const uint8_t depth,
		const bf_chip_address_t chip_address, const bf_cmd_code_t cmd_code,
		const uint8_t data_length, const uint8_t* tx);
uint8_t spi_command_exec(spi_channel_id_t spi_channel, bf_command_t* command, uint32_t* nonces);

/* data preparation routines */
uint8_t gen_clock_data(uint8_t clock, uint8_t prescaler, uint8_t data[4]);
uint8_t gen_task_data(uint32_t* midstate, uint32_t merkle, uint32_t ntime,
		uint32_t nbits, uint32_t mask, uint8_t* task);
uint32_t gen_mask(uint32_t nonce, uint8_t nbits);

/* SPI BCM250 primitives */
uint8_t create_channel(spi_channel_id_t spi_channel, uint8_t* channel_path, uint8_t channel_length);
uint8_t destroy_channel(spi_channel_id_t spi_channel, uint8_t depth);

/* SPI BTC16 primitives  */
void    spi_emit_reset(spi_channel_id_t spi_channel);
uint8_t send_toggle(spi_channel_id_t spi_channel, uint8_t depth,
		bf_chip_address_t chip_address);
uint8_t set_clock(spi_channel_id_t spi_channel, uint8_t depth,
		bf_chip_address_t chip_address, uint8_t clock);

/* cmd buffer primitives */
int8_t  cmd_buffer_init(bf_cmd_buffer_t* cmd_buffer);
int8_t  cmd_buffer_deinit(bf_cmd_buffer_t* cmd_buffer);
int8_t  cmd_buffer_clear(bf_cmd_buffer_t* cmd_buffer);
int8_t  cmd_buffer_push(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address, const bf_chip_address_t src_address,
		const bf_works_t work, const uint32_t id,
		const bf_cmd_code_t cmd_code, const uint8_t data_length, const uint8_t* tx);
int8_t  cmd_buffer_pop(bf_cmd_buffer_t* cmd_buffer, bf_cmd_status_t* cmd_status, uint32_t* nonces);
int8_t  cmd_buffer_exec(spi_channel_id_t spi_channel, bf_cmd_buffer_t* cmd_buffer);

int8_t  cmd_buffer_push_create_channel(bf_cmd_buffer_t* cmd_buffer, uint8_t* channel_path,
		uint8_t channel_length);
int8_t  cmd_buffer_push_destroy_channel(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth);

int8_t  cmd_buffer_push_send_toggle(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address);
int8_t  cmd_buffer_push_set_clock(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address, uint8_t clock);
int8_t  cmd_buffer_push_set_mask(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address, uint8_t mask);

char*   get_cmd_description(bf_cmd_code_t cmd_code);

/* dynamic work list primitives */
bf_list_t* workd_list_init(void);
int8_t workd_list_deinit(bf_list_t* list, struct cgpu_info *bitfury);
int8_t workd_list_push(bf_list_t* list, bf_workd_t* work);
int8_t workd_list_pop(bf_list_t* list, struct cgpu_info *bitfury);
int8_t workd_list_remove(bf_list_t* list, bf_works_t* work);

/* nonce list primitives */
bf_list_t* nonce_list_init(void);
int8_t nonce_list_deinit(bf_list_t* list);
int8_t nonce_list_push(bf_list_t* list, uint32_t nonce);
uint32_t nonce_list_pop(bf_list_t* list);

/* noncework list primitives */
bf_list_t* noncework_list_init(void);
int8_t noncework_list_deinit(bf_list_t* list);
int8_t noncework_list_push(bf_list_t* list, bf_chip_address_t chip_address,
		bf_chip_address_t src_address, bf_works_t cwork, bf_works_t owork, uint32_t nonce);
int8_t noncework_list_pop(bf_list_t* list);

bf_list_t* renoncework_list_init(void);
int8_t renoncework_list_deinit(bf_list_t* list);
int8_t renoncework_list_push(bf_list_t* list, bf_chip_address_t src_address, uint32_t nonce);
int8_t renoncework_list_pop(bf_list_t* list);
	
/* renonce list primitives */
bf_list_t* renonce_list_init(void);
int8_t renonce_list_deinit(bf_list_t* list);
int8_t renonce_list_push(bf_list_t* list, uint32_t id, uint32_t nonce, bf_chip_address_t src_address,
		bf_works_t cwork, bf_works_t owork);
int8_t renonce_list_pop(bf_list_t* list);
int8_t renonce_list_remove(bf_list_t* list, bf_data_t* rdata);

uint8_t find_nonces(uint32_t* curr_nonces, uint32_t* prev_nonces, uint32_t* valid_nonces);
bool match_nonce(uint32_t nonce, uint32_t mask, uint8_t nbits);

#endif /* BF16_BITFURY16_H */

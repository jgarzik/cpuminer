#include <arpa/inet.h>

#include "miner.h"

#include "bf16-bitfury16.h"
#include "bf16-communication.h"
#include "bf16-ctrldevice.h"

#include "math.h"
#include "sha2.h"
#include "driver-bitfury16.h"

//#define FLIP_BITS

bf_cmd_description_t cmd_description[CHIP_CMD_NUM] = {
	{
		.cmd_code        = CHIP_CMD_TASK_STATUS,
		.cmd_description = "CHIP_CMD_TASK_STATUS"
	},
	{
		.cmd_code        = CHIP_CMD_TASK_WRITE,
		.cmd_description = "CHIP_CMD_TASK_WRITE"
	},
	{
		.cmd_code        = CHIP_CMD_TASK_SWITCH,
		.cmd_description = "CHIP_CMD_TASK_SWITCH"
	},
	{
		.cmd_code        = CHIP_CMD_READ_NONCE,
		.cmd_description = "CHIP_CMD_READ_NONCE"
	},
	{
		.cmd_code        = CHIP_CMD_SET_CLOCK,
		.cmd_description = "CHIP_CMD_SET_CLOCK"
	},
	{
		.cmd_code        = CHIP_CMD_TOGGLE,
		.cmd_description = "CHIP_CMD_TOGGLE"
	},
	{
		.cmd_code        = CHIP_CMD_SET_MASK,
		.cmd_description = "CHIP_CMD_SET_MASK"
	},
	{
		.cmd_code        = CHIP_CMD_CREATE_CHANNEL,
		.cmd_description = "CHIP_CMD_CREATE_CHANNEL"
	}
};

char* get_cmd_description(bf_cmd_code_t cmd_code)
{
	uint8_t i;

	for (i = 0; i < CHIP_CMD_NUM; i++)
		if (cmd_description[i].cmd_code == cmd_code)
			return cmd_description[i].cmd_description;

	return NULL;
}

static void shift_bits(uint8_t* data, uint8_t size, uint8_t nbits)
{
	uint8_t i;
	uint8_t bytes = nbits / 8;
	uint8_t bits  = nbits % 8;

	for (i = 0; i < size; i++) {
		data[i] = (data[i + bytes] << bits);
		uint8_t minor = (data[i + bytes + 1] >> (8 - bits));
		data[i] |= minor;
	}
}

static uint8_t extra_bytes(uint8_t depth)
{
	return (depth * 3) / 8 + 1;
}

static uint8_t analyze_rx_data(bf_command_t* command, bf_cmd_status_t* cmd_status, uint32_t* nonces)
{
	uint8_t i;
	uint8_t res = 0;

	if (command->cmd_code & CHIP_CMD_READ_NONCE) {
		shift_bits(command->rx, 49 + 2 + extra_bytes(command->depth), command->depth * 3);

		command->status	   = command->rx[0];
		cmd_status->status = command->rx[0];
		command->checksum             = command->rx[1];
		cmd_status->checksum_expected = command->checksum;

		if (nonces == NULL)
			return 1;

		uint32_t nonce = 0x00000000;

		/* fill nonces buffer */
		for (i = 0; i <= 48; i++) {
			if ((i % 4 == 0) && (i != 0)) {
				if ((nonce & 0x0fffffff) != 0x0fffffff)
					nonce ^= 0xaaaaaaaa;
				nonces[i/4 - 1] = nonce;

				if (i == 48)
					break;

				nonce = 0x00000000;
			}

			nonce |= (command->rx[i + 2] << 8*(4 - (i%4) - 1));
		}

#if 0
		char data[128];
		memset(data, 0, sizeof(data));
		for (i = 0; i < 49 + 2 + extra_bytes(command->depth); i++)
			sprintf(data, "%s%02x", data, command->rx[i]);
		applog(LOG_DEBUG, "BF16: RX <- [%s]", data);
#endif

		for (i = 0; i < 48; i ++)
			command->nonce_checksum += command->rx[i + 2];
		command->nonce_checksum += command->rx[1];

		cmd_status->nonce_checksum_expected = command->nonce_checksum;

		if (command->checksum != command->rx[1]) {
			command->checksum_error       = true;
			cmd_status->checksum_error    = true;
			cmd_status->checksum_received = command->rx[50];
			res = 1;
#if 0
			applog(LOG_ERR, "Checksum mismatch: received [%02x] expected [%02x]", command->rx[1], 0x04);
#endif
		} else {
			command->checksum_error    = false;
			cmd_status->checksum_error = false;
		}

		if ((command->nonce_checksum != command->rx[50]) &&
			(command->nonce_checksum != command->rx[50] + 1)) {
			command->nonce_checksum_error       = true;
			cmd_status->nonce_checksum_error    = true;
			cmd_status->nonce_checksum_received = command->rx[50];

			res += 2;
#if 0
			applog(LOG_ERR, "Nonce checksum mismatch: received [%02x] expected [%02x]", command->rx[50], checksum);
#endif
		} else {
			command->nonce_checksum_error    = false;
			cmd_status->nonce_checksum_error = false;
		}
	} else {
		shift_bits(command->rx, 2 + extra_bytes(command->depth), command->depth * 3);

		if (opt_bf16_test_chip != NULL) {
			char data[16];
			memset(data, 0, sizeof(data));
			for (i = 0; i < 2 + extra_bytes(command->depth); i++)
				sprintf(data, "%s%02x", data, command->rx[i]);
			applog(LOG_NOTICE, "BF16: RX <- [%s]", data);
		}

		command->status	     = command->rx[0];
		cmd_status->status   = command->rx[0];
		uint8_t cmd_checksum = command->rx[1];
		cmd_status->checksum_expected = command->checksum;

#if 0
		applog(LOG_DEBUG, "Command checksum: [%02x]", cmd_checksum);
		applog(LOG_DEBUG, "Command status:   [%02x]", command->status);
#endif

		if ((command->checksum != cmd_checksum) &&
		    (command->checksum != cmd_checksum + 1)) {
			command->checksum_error       = true;
			cmd_status->checksum_error    = true;
			cmd_status->checksum_received = cmd_checksum;
			res = 1;

			if (opt_bf16_test_chip != NULL) {
				applog(LOG_ERR, "BF16: checksum mismatch: received [%02x] expected [%02x]",
						cmd_checksum, command->checksum);
			}
		} else {
			command->checksum_error    = false;
			cmd_status->checksum_error = false;
		}
	}

	return res;
}

/* SPI BTC250 primitives */
uint8_t create_channel(spi_channel_id_t spi_channel, uint8_t* channel_path, uint8_t channel_length)
{
	int res = 0;

	uint8_t* channel_path_buff = cgcalloc(channel_length, sizeof(uint8_t));
	cg_memcpy(channel_path_buff, channel_path, channel_length);

	res = device_spi_transfer(spi_channel, channel_path_buff, channel_length);

	free(channel_path_buff);

	return res;
}

uint8_t destroy_channel(spi_channel_id_t spi_channel, uint8_t depth)
{
	bf_command_t chip_command;
	bf_chip_address_t chip_address = { 0x00, 0x00, 0x0f };

	/* send command to 0x0f address */
	spi_command_init(&chip_command, depth, chip_address, CHIP_CMD_TASK_SWITCH, 0, NULL);
	return spi_command_exec(spi_channel, &chip_command, NULL);
}

/* SPI BTC16 primitives  */
void spi_emit_reset(spi_channel_id_t spi_channel)
{
	device_ctrl_transfer(spi_channel, 1, F_RST);

	uint8_t data[2] = { 0x00, 0x00 };
	device_spi_transfer(spi_channel, data, sizeof(data));

	device_ctrl_transfer(spi_channel, 0, F_RST);
}

uint8_t send_toggle(spi_channel_id_t spi_channel, uint8_t depth, bf_chip_address_t chip_address)
{
	bf_command_t chip_command;
	uint8_t toggle[4] = { 0xa5, 0x00, 0x00, 0x02 };

	spi_command_init(&chip_command, depth, chip_address, CHIP_CMD_TOGGLE, 3, toggle);
	return spi_command_exec(spi_channel, &chip_command, NULL);
}

uint8_t set_clock(spi_channel_id_t spi_channel, uint8_t depth,
		bf_chip_address_t chip_address, uint8_t clock)
{
	bf_command_t chip_command;
	uint8_t clock_buf[4];

	memset(clock_buf, 0, 4);
	gen_clock_data(clock, 1, clock_buf);
	spi_command_init(&chip_command, depth, chip_address, CHIP_CMD_SET_CLOCK, 3, clock_buf);
	return spi_command_exec(spi_channel, &chip_command, NULL);
}

/* cmd buffer primitives */
int8_t cmd_buffer_init(bf_cmd_buffer_t* cmd_buffer)
{
	if (cmd_buffer == NULL)
		return -1;

	cmd_buffer->cmd_list = cgmalloc(sizeof(bf_list_t));
	cmd_buffer->cmd_list->head  = NULL;
	cmd_buffer->cmd_list->tail  = NULL;
	cmd_buffer->cmd_list->count = 0;

	cmd_buffer->tx_buffer = cgcalloc(CMD_BUFFER_LEN, sizeof(uint8_t));
	cmd_buffer->rx_buffer = cgcalloc(CMD_BUFFER_LEN, sizeof(uint8_t));

	memset(cmd_buffer->tx_buffer, 0, CMD_BUFFER_LEN);
	memset(cmd_buffer->rx_buffer, 0, CMD_BUFFER_LEN);

	cmd_buffer->free_bytes = CMD_BUFFER_LEN;
	cmd_buffer->tx_offset  = 0;
	cmd_buffer->rx_offset  = 0;
	cmd_buffer->status     = EMPTY;

	return 0;
}

int8_t cmd_buffer_deinit(bf_cmd_buffer_t* cmd_buffer)
{
	if (cmd_buffer == NULL)
		return -1;

	/* free cmd buffer */
	while (cmd_buffer->cmd_list->head != NULL) {
		bf_data_t* cdata = cmd_buffer->cmd_list->head;
		LIST_POP_HEAD(cmd_buffer->cmd_list);
		free(cdata->data);
		free(cdata);
	}

	free(cmd_buffer->cmd_list);

	/* free RX/TX buffer */
	free(cmd_buffer->tx_buffer);
	free(cmd_buffer->rx_buffer);

	cmd_buffer->free_bytes = CMD_BUFFER_LEN;
	cmd_buffer->tx_offset  = 0;
	cmd_buffer->rx_offset  = 0;
	cmd_buffer->status     = EMPTY;

	return 0;
}

int8_t cmd_buffer_clear(bf_cmd_buffer_t* cmd_buffer)
{
	if (cmd_buffer == NULL)
		return -1;

	/* release cmd buffer data memory */
	while (cmd_buffer->cmd_list->head != NULL) {
		bf_data_t* cdata = cmd_buffer->cmd_list->head;
		LIST_POP_HEAD(cmd_buffer->cmd_list);
		free(cdata->data);
		free(cdata);
	}

	cmd_buffer->cmd_list->count = 0;

	/* clear RX/TX buffer */
	memset(cmd_buffer->tx_buffer, 0, CMD_BUFFER_LEN);
	memset(cmd_buffer->rx_buffer, 0, CMD_BUFFER_LEN);

	cmd_buffer->free_bytes = CMD_BUFFER_LEN;
	cmd_buffer->tx_offset  = 0;
	cmd_buffer->rx_offset  = 0;
	cmd_buffer->status     = EMPTY;

	return 0;
}

int8_t cmd_buffer_push(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address, const bf_chip_address_t src_address,
		const bf_works_t work, const uint32_t id,
		const bf_cmd_code_t cmd_code, const uint8_t data_length, const uint8_t* tx)
{
	uint8_t res = 0;

	if (cmd_buffer == NULL)
		return -1;

	if (cmd_buffer->status == EXECUTED)
		return -2;

	bf_command_t command;
	memset(&command, 0, sizeof(bf_command_t));

	uint8_t buff[192];
	memset(buff, 0, sizeof(buff));

	if (cmd_code != CHIP_CMD_CREATE_CHANNEL) {
		res = spi_command_init(&command, depth, chip_address, cmd_code, data_length, tx);
		if (res != 0)
			return res;
	}

	/* init structure */
	bf_data_t* cdata = cgmalloc(sizeof(bf_data_t));
	cdata->data = cgmalloc(sizeof(bf_cmd_t));
	cdata->next = NULL;
	cdata->prev = NULL;

	cg_memcpy(&CMD(cdata)->chip_address, &chip_address, sizeof(bf_chip_address_t));
	cg_memcpy(&CMD(cdata)->src_address,  &src_address,  sizeof(bf_chip_address_t));
	cg_memcpy(&CMD(cdata)->work,         &work,         sizeof(bf_works_t));
	CMD(cdata)->id       = id;
	CMD(cdata)->depth    = command.depth;
	CMD(cdata)->checksum = command.checksum;
	CMD(cdata)->cmd_code = cmd_code;

	if (CMD(cdata)->cmd_code & CHIP_CMD_READ_NONCE)
		CMD(cdata)->data_length = command.data_length + 49 + 2 + extra_bytes(command.depth);
	else if (CMD(cdata)->cmd_code == CHIP_CMD_CREATE_CHANNEL)
		CMD(cdata)->data_length = data_length;
	else
		CMD(cdata)->data_length = command.data_length + 2 + extra_bytes(command.depth);

	if (cmd_buffer->free_bytes < CMD(cdata)->data_length) {
		/* not enough TX/RX buffer space available */
		free(cdata->data);
		free(cdata);
		return -3;
	}

	/* init send buffer */
	if (cmd_code != CHIP_CMD_CREATE_CHANNEL) {
		cg_memcpy(buff, command.tx, command.data_length);
		cg_memcpy(cmd_buffer->tx_buffer + cmd_buffer->tx_offset, buff, CMD(cdata)->data_length);
	} else
		cg_memcpy(cmd_buffer->tx_buffer + cmd_buffer->tx_offset, tx, CMD(cdata)->data_length);

#if 0
	uint16_t i;
	char data[384];
	memset(data, 0, sizeof(data));
	for (i = 0; i < command.data_length; i++)
		sprintf(data, "%s%02x", data, command.tx[i]);
	applog(LOG_DEBUG, "BF16: TX -> [%s]", data);
#endif

	cmd_buffer->tx_offset  += CMD(cdata)->data_length;
	cmd_buffer->free_bytes -= CMD(cdata)->data_length;

	/* add cmd to buffer */
	LIST_PUSH_TAIL(cmd_buffer->cmd_list, cdata);	
	cmd_buffer->cmd_list->count++;

	return 0;
}

int8_t cmd_buffer_push_send_toggle(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address)
{
	bf_works_t work;
	uint8_t toggle[4] = { 0xa5, 0x00, 0x00, 0x02 };

	return cmd_buffer_push(cmd_buffer, depth, chip_address, chip_address,
			work, 0, CHIP_CMD_TOGGLE, 3, toggle);
}

int8_t cmd_buffer_push_set_clock(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address, uint8_t clock)
{
	bf_works_t work;
	uint8_t clock_buf[4];

	memset(clock_buf, 0, 4);
	gen_clock_data(clock, 1, clock_buf);

	return cmd_buffer_push(cmd_buffer, depth, chip_address, chip_address,
			work, 0, CHIP_CMD_SET_CLOCK, 3, clock_buf);
}

int8_t cmd_buffer_push_set_mask(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth,
		const bf_chip_address_t chip_address, uint8_t mask)
{
	uint8_t i;
	bf_works_t work;
	uint8_t noncemask[4];

	for (i = 0; i < 4; i++)
		noncemask[i] = (mask >> (8*(4 - i - 1))) & 0xff;

	return cmd_buffer_push(cmd_buffer, depth, chip_address, chip_address,
			work, 0, CHIP_CMD_SET_MASK, 3, noncemask);
}

int8_t cmd_buffer_push_create_channel(bf_cmd_buffer_t* cmd_buffer,
		uint8_t* channel_path, uint8_t channel_length)
{
	bf_works_t work;
	bf_chip_address_t chip_address = { 0x00, 0x00, 0x00 };

	return cmd_buffer_push(cmd_buffer, 0, chip_address, chip_address,
			work, 0, CHIP_CMD_CREATE_CHANNEL, channel_length, channel_path);
}

int8_t cmd_buffer_push_destroy_channel(bf_cmd_buffer_t* cmd_buffer, const uint8_t depth)
{
	bf_works_t work;
	bf_chip_address_t chip_address = { 0x00, 0x00, 0x0f };

	return cmd_buffer_push(cmd_buffer, depth, chip_address, chip_address,
			work, 0, CHIP_CMD_TASK_SWITCH, 0, NULL);
}

bool match_nonce(uint32_t nonce, uint32_t mask, uint8_t nbits)
{
	uint32_t fixed_mask = (uint32_t)(pow(2, nbits) - 1);
	return ((nonce & fixed_mask) == (mask & fixed_mask));
}

uint8_t find_nonces(uint32_t* curr_nonces, uint32_t* prev_nonces, uint32_t* valid_nonces)
{
	uint8_t i, j;
	uint8_t found  = 0;
	uint8_t nonces = 0;
	uint8_t diff = 0;

	uint32_t found_nonces[12];
	memset(found_nonces, 0, sizeof(found_nonces));

	for (i = 0; i < 12; i++) {
		if (((curr_nonces[i] & 0x0fffffff) != 0x0fffffff) &&
			(curr_nonces[i] != prev_nonces[i])) {
			found_nonces[found++] = curr_nonces[i];
		}

		if (((curr_nonces[i] & 0x0fffffff) == 0x0fffffff) &&
			(curr_nonces[i] != prev_nonces[i]))
			diff++;
	}

	for (i = 0; i < found; i++) {
		if (found_nonces[i] == 0x00000000)
			continue;

		for (j = i; j < found; j++) {
			if ((j != i) && (found_nonces[i] == found_nonces[j]))
				found_nonces[j] = 0x00000000;
		}

		valid_nonces[nonces++] = found_nonces[i];
	}

	return nonces;
}

int8_t cmd_buffer_pop(bf_cmd_buffer_t* cmd_buffer, bf_cmd_status_t* cmd_status, uint32_t* nonces)
{
	if (cmd_buffer == NULL)
		return -1;

	if (cmd_buffer->status != EXECUTED)
		return -2;

	if (cmd_buffer->cmd_list->head == NULL)
		return -3;

	uint8_t buff[192];
	bf_command_t chip_command;
	memset(buff, 0, sizeof(buff));
	memset(chip_command.rx, 0, sizeof(chip_command.rx));

	/* extract command from list */
	bf_data_t* cdata = cmd_buffer->cmd_list->head;

	chip_command.cmd_code             = CMD(cdata)->cmd_code;
	chip_command.depth                = CMD(cdata)->depth;
	chip_command.data_length          = CMD(cdata)->data_length;
	chip_command.status               = 0;
	chip_command.checksum             = CMD(cdata)->checksum;
	chip_command.nonce_checksum       = 0;
	chip_command.checksum_error       = false;
	chip_command.nonce_checksum_error = false;

	/* extract chip return data */
	cg_memcpy(buff, cmd_buffer->rx_buffer + cmd_buffer->rx_offset, CMD(cdata)->data_length);
	cmd_buffer->rx_offset += CMD(cdata)->data_length;
	if (CMD(cdata)->cmd_code & CHIP_CMD_READ_NONCE) {
		cg_memcpy(chip_command.rx, buff + CMD(cdata)->data_length - (49 + 2 + extra_bytes(chip_command.depth)),
				49 + 2 + extra_bytes(chip_command.depth));
		memset(nonces, 0, 12 * sizeof(uint32_t));
		analyze_rx_data(&chip_command, cmd_status, nonces);
	} else if (CMD(cdata)->cmd_code == CHIP_CMD_CREATE_CHANNEL) {
		cg_memcpy(chip_command.rx, buff, CMD(cdata)->data_length);
	} else {
		cg_memcpy(chip_command.rx, buff + CMD(cdata)->data_length - (2 + extra_bytes(chip_command.depth)),
				2 + extra_bytes(chip_command.depth));
		analyze_rx_data(&chip_command, cmd_status, NULL);
	}

	/* prepare cmd_status */
	cg_memcpy(&cmd_status->chip_address, &CMD(cdata)->chip_address, sizeof(bf_chip_address_t));
	cg_memcpy(&cmd_status->src_address,  &CMD(cdata)->src_address,  sizeof(bf_chip_address_t));
	cg_memcpy(&cmd_status->work,         &CMD(cdata)->work,         sizeof(bf_works_t));
	cmd_status->id       = CMD(cdata)->id;
	cmd_status->cmd_code = CMD(cdata)->cmd_code;

	/* push memory back to free cmd list */
	LIST_POP_HEAD(cmd_buffer->cmd_list);
	cmd_buffer->cmd_list->count--;
	free(cdata->data);
	free(cdata);

	return 0;
}

int8_t cmd_buffer_exec(spi_channel_id_t spi_channel, bf_cmd_buffer_t* cmd_buffer)
{
	if (cmd_buffer == NULL)
		return -1;

	if (cmd_buffer->status == TX_READY) {
		device_spi_txrx(spi_channel, cmd_buffer->tx_buffer, cmd_buffer->rx_buffer, cmd_buffer->tx_offset);
		cmd_buffer->status = EXECUTED;
	} else
		return -2;

	return 0;
}

/* BF16 command primitives */
uint8_t gen_clock_data(uint8_t clock, uint8_t prescaler, uint8_t data[4])
{
	uint8_t i;
	uint32_t data32 = 0x00000000;

	if (clock > 0x3f)
		return -1;

	if ((prescaler != 0) && (prescaler != 1))
		return -1;

	uint32_t magic_const = 0x38;
	uint32_t prescaler1 = prescaler;
	uint32_t clock1 = clock;
	uint32_t prescaler2 = prescaler;
	uint32_t clock2 = clock;

	magic_const <<= 20;

	if (prescaler == 1) {
		prescaler1 <<= 19;
		prescaler2 <<= 12;
	}

	clock1 <<= 13;
	clock2 <<= 6;

	data32 = magic_const | prescaler1 | clock1 | prescaler2 | clock2;
	for (i = 0; i < 4; i++)
		data[i] = (data32 >> (8*(4 - i - 1))) & 0xff;

	return 0;
}

#ifdef FLIP_BITS
static uint32_t flip_bits(uint32_t data, uint8_t nbits)
{
	uint32_t ret = 0x00000000;
	uint8_t i;

	for (i = 0; i < nbits; i++)
		ret |= (((data >> i) & 0x1) << (nbits - (i + 1)));

	return ret;
}
#endif

uint32_t gen_mask(uint32_t nonce, uint8_t nbits)
{
	uint32_t mask = 0x00000000;

	uint32_t mask_code = (nbits << 16);

	/* highest 16 bits of nonce counter */
	uint32_t nonce_code = (nonce & 0x0007ffff);
#ifdef FLIP_BITS
	uint32_t nonce_cntr = flip_bits(nonce_code, 19);
	nonce_code = (flip_bits(nonce_cntr - 2, 19) ^ 0xaaaaaaaa);
	mask = (mask_code | (nonce_code & (uint32_t)(pow(2, nbits) - 1)));
#else
	nonce_code = ((nonce_code ^ 0xaaaaaaaa) & (uint32_t)(pow(2, nbits) - 1));
	mask = (mask_code | nonce_code);
#endif

	return mask;
}

void ms3steps16(uint32_t* p, uint32_t* w, uint32_t* task)
{
	uint32_t a, b, c, d, e, f, g, h, new_e, new_a;
	uint8_t i;

	a = p[0];
	b = p[1];
	c = p[2];
	d = p[3];
	e = p[4];
	f = p[5];
	g = p[6];
	h = p[7];
	for (i = 0; i < 3; i++) {
		new_e = w[i] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) + d;
		new_a = w[i] + sha256_k[i] + h + CH(e,f,g) + SHA256_F2(e) +
		        SHA256_F1(a) + MAJ(a,b,c);
		d = c;
		c = b;
		b = a;
		a = new_a;
		h = g;
		g = f;
		f = e;
		e = new_e;
	}

	task[18] = ntohl(a ^ 0xaaaaaaaa);
	task[17] = ntohl(b ^ 0xaaaaaaaa);
	task[16] = ntohl(c ^ 0xaaaaaaaa);
	task[15] = ntohl(d ^ 0xaaaaaaaa);
	task[11] = ntohl(e ^ 0xaaaaaaaa);
	task[10] = ntohl(f ^ 0xaaaaaaaa);
	task[9]  = ntohl(g ^ 0xaaaaaaaa);
	task[8]  = ntohl(h ^ 0xaaaaaaaa);
}

uint8_t gen_task_data(uint32_t* midstate, uint32_t merkle, uint32_t ntime,
		uint32_t nbits, uint32_t mask, uint8_t* task)
{
	uint8_t  i;
	uint32_t tmp;
	uint32_t w[3];

	w[0] = merkle;
	w[1] = ntime;
	w[2] = nbits;

	for (i = 0; i < 8; i++) {
		tmp = midstate[i];
		tmp ^= 0xaaaaaaaa;
		tmp = ntohl(tmp);
		cg_memcpy(task + i*4, &tmp, sizeof(tmp));
	}

	ms3steps16(midstate, w, (uint32_t*)task);

	for (i = 0; i < 3; i++) {
		tmp = w[i];
		tmp ^= 0xaaaaaaaa;
		tmp = ntohl(tmp);
		cg_memcpy(task + (12 + i)*4, &tmp, sizeof(tmp));
	}

	mask = ntohl(mask);
	cg_memcpy(task + 19*4, &mask, sizeof(mask));

	return 0;
}

uint8_t spi_command_init(bf_command_t* command, const uint8_t depth,
		const bf_chip_address_t chip_address, const bf_cmd_code_t cmd_code,
		const uint8_t data_length, const uint8_t* tx)
{
	uint8_t i;

	memset(command->tx, 0, sizeof(command->tx));
	memset(command->rx, 0, sizeof(command->rx));

	command->tx[0] = 0x01;
	command->data_length = 1;

	if ((chip_address.chip_id > 10) && (chip_address.chip_id != 0x0f))
		return 1;
	else {
		cg_memcpy(&command->chip_address, &chip_address, sizeof(bf_chip_address_t));
		command->tx[1] = (command->chip_address.chip_id << 4);
		command->data_length++;
	}

	command->depth = depth;

	command->cmd_code = cmd_code;
	command->tx[2] = command->cmd_code;
	command->data_length++;

	if (data_length <= 79) {
		command->tx[3] = data_length;
		command->data_length++;
	} else
		return 1;

	/* fill TX data */
	if (data_length == 0) {
		command->tx[4] = 0x00;
		command->data_length++;
	} else if (tx != NULL) {
		cg_memcpy(command->tx + 4, tx, data_length + 1);
		command->data_length += (data_length + 1);
	} else
		return 1;

	/* calculate checksum */
	command->checksum = 0;
	command->nonce_checksum = 0;
	for (i = 2; i < command->data_length; i++)
		command->checksum += command->tx[i];

	command->checksum_error       = false;
	command->nonce_checksum_error = false;

	return 0;
}

uint8_t spi_command_exec(spi_channel_id_t spi_channel, bf_command_t* command, uint32_t* nonces)
{
	uint8_t buff[192];
	uint8_t res = 0;
	bf_cmd_status_t cmd_status;

	if (command->cmd_code & CHIP_CMD_READ_NONCE) {
		memset(buff, 0x00, command->data_length + 49 + 2 + extra_bytes(command->depth));
		cg_memcpy(buff, command->tx, command->data_length);
		device_spi_transfer(spi_channel, buff, command->data_length + 49 + 2 + extra_bytes(command->depth));
		cg_memcpy(command->rx, buff + command->data_length, 49 + 2 + extra_bytes(command->depth));

#if 0
		uint16_t i;
		char data[256];
		memset(data, 0, sizeof(data));
		for (i = 0; i < command->data_length; i++)
			sprintf(data, "%s%02x", data, command->tx[i]);
		applog(LOG_DEBUG, "BF16: TX -> [%s]", data);
#endif

		return analyze_rx_data(command, &cmd_status, nonces);
	} else {
		memset(buff, 0x00, command->data_length + 2 + extra_bytes(command->depth));
		cg_memcpy(buff, command->tx, command->data_length);
		device_spi_transfer(spi_channel, buff, command->data_length + 2 + extra_bytes(command->depth));
		cg_memcpy(command->rx, buff + command->data_length, 2 + extra_bytes(command->depth));

		if (opt_bf16_test_chip != NULL) {
			uint16_t i;
			char data[256];
			memset(data, 0, sizeof(data));
			for (i = 0; i < command->data_length; i++)
				sprintf(data, "%s%02x", data, command->tx[i]);
			applog(LOG_NOTICE, "BF16: TX -> [%s]", data);
		}

		return analyze_rx_data(command, &cmd_status, nonces);
	}

	return res;
}

/* dynamic work list primitives */
bf_list_t* workd_list_init(void)
{
	bf_list_t* list = cgmalloc(sizeof(bf_list_t));
	list->head = NULL;
	list->tail = NULL;
	list->count = 0;
	pthread_mutex_init(&list->lock, NULL);

	return list;
}

int8_t workd_list_deinit(bf_list_t* list, struct cgpu_info *bitfury)
{
	if (list == NULL)
		return -1;

	/* free work list */
	L_LOCK(list);
	while (list->head != NULL) {
		bf_data_t* wdata = list->head;
		LIST_POP_HEAD(list);

		if (WORKD(wdata)->rolled)
			free_work(WORKD(wdata)->work);
		else
			work_completed(bitfury, WORKD(wdata)->work);

		free(wdata);
	}
	L_UNLOCK(list);

	pthread_mutex_destroy(&list->lock);
	list->count = 0;
	free(list);

	return 0;
}

int8_t workd_list_push(bf_list_t* list, bf_workd_t* work)
{
	if ((list == NULL) || (work == NULL))
		return -1;

	bf_data_t* wdata = cgmalloc(sizeof(bf_data_t));
	wdata->data = work;
	wdata->next = NULL;
	wdata->prev = NULL;

	LIST_PUSH_TAIL(list, wdata);
	list->count++;

	return 0;
}

int8_t workd_list_pop(bf_list_t* list, struct cgpu_info *bitfury)
{
	if (list == NULL)
		return -1;

	bf_data_t* wdata = list->head;
	if (wdata != NULL) {
		LIST_POP_HEAD(list);	

		if (WORKD(wdata)->rolled)
			free_work(WORKD(wdata)->work);
		else
			work_completed(bitfury, WORKD(wdata)->work);

		list->count--;
		free(wdata->data);
		free(wdata);
	} else
		return -1;

	return 0;
}

int8_t workd_list_remove(bf_list_t* list, bf_works_t* works)
{
	if (list == NULL)
		return -1;

	bf_data_t* wdata = list->head;
	if (wdata != NULL) {
		LIST_POP_HEAD(list);	

		cg_memcpy(&works->work,    WORKD(wdata)->work,     sizeof(struct work));
		cg_memcpy(&works->payload, &WORKD(wdata)->payload, sizeof(bf_payload_t));

		list->count--;
		free(wdata);
	} else
		return -1;

	return 0;
}

/* nonces list primitives */
bf_list_t* nonce_list_init(void)
{
	bf_list_t* list = cgmalloc(sizeof(bf_list_t));
	list->head = NULL;
	list->tail = NULL;
	list->count = 0;
	pthread_mutex_init(&list->lock, NULL);

	return list;
}

int8_t nonce_list_deinit(bf_list_t* list)
{
	if (list == NULL)
		return -1;

	/* free nonce list */
	L_LOCK(list);
	while (list->head != NULL) {
		bf_data_t* ndata = list->head;
		LIST_POP_HEAD(list);
		free(ndata->data);
		free(ndata);
	}
	L_UNLOCK(list);

	pthread_mutex_destroy(&list->lock);
	list->count = 0;
	free(list);
	
	return 0;
}

int8_t nonce_list_push(bf_list_t* list, uint32_t nonce)
{
	if (list == NULL)
		return -1;

	/* find nonce duplicates */
	bf_data_t* ndata = list->head;
	while (ndata != NULL) {
		if (NONCE(ndata)->nonce == nonce)
			return -1;
		ndata = ndata->next;
	}

	ndata = cgmalloc(sizeof(bf_data_t));
	ndata->data = cgmalloc(sizeof(bf_nonce_t));
	NONCE(ndata)->nonce = nonce;
	ndata->next = NULL;
	ndata->prev = NULL;

	LIST_PUSH_TAIL(list, ndata);
	list->count++;

	return 0;
}

uint32_t nonce_list_pop(bf_list_t* list)
{
	uint32_t nonce = 0;

	bf_data_t* ndata = list->head;
	if (ndata != NULL) {
		nonce = NONCE(ndata)->nonce;
		LIST_POP_HEAD(list);	
		list->count--;
		free(ndata->data);
		free(ndata);
	} else
		return -1;

	return nonce;
}

/* renoncework list primitives */
bf_list_t* renoncework_list_init(void)
{
	bf_list_t* list = cgmalloc(sizeof(bf_list_t));
	list->head = NULL;
	list->tail = NULL;
	list->count = 0;
	pthread_mutex_init(&list->lock, NULL);

	return list;
}

int8_t renoncework_list_deinit(bf_list_t* list)
{
	if (list == NULL)
		return -1;

	/* free renoncework list */
	L_LOCK(list);
	while (list->head != NULL) {
		bf_data_t* rnwdata = list->head;
		LIST_POP_HEAD(list);

		free(rnwdata->data);
		free(rnwdata);
	}
	L_UNLOCK(list);

	pthread_mutex_destroy(&list->lock);
	list->count = 0;
	free(list);

	return 0;
}

int8_t renoncework_list_push(bf_list_t* list, bf_chip_address_t src_address, uint32_t nonce)
{
	if (list == NULL)
		return -1;

	bf_data_t* rnwdata = cgmalloc(sizeof(bf_data_t));
	rnwdata->data = cgmalloc(sizeof(bf_renoncework_t));
	rnwdata->next = NULL;
	rnwdata->prev = NULL;

	cg_memcpy(&RENONCEWORK(rnwdata)->src_address, &src_address, sizeof(bf_chip_address_t));
	RENONCEWORK(rnwdata)->nonce = nonce;

	LIST_PUSH_TAIL(list, rnwdata);
	list->count++;

	return 0;
}

int8_t renoncework_list_pop(bf_list_t* list)
{
	if (list == NULL)
		return -1;

	bf_data_t* rnwdata = list->head;
	if (rnwdata != NULL) {
		LIST_POP_HEAD(list);	
		list->count--;
		free(rnwdata->data);
		free(rnwdata);
	} else
		return -1;

	return 0;
}

/* noncework list primitives */
bf_list_t* noncework_list_init(void)
{
	bf_list_t* list = cgmalloc(sizeof(bf_list_t));
	list->head = NULL;
	list->tail = NULL;
	list->count = 0;
	pthread_mutex_init(&list->lock, NULL);

	return list;
}

int8_t noncework_list_deinit(bf_list_t* list)
{
	if (list == NULL)
		return -1;

	/* free noncework list */
	L_LOCK(list);
	while (list->head != NULL) {
		bf_data_t* nwdata = list->head;
		LIST_POP_HEAD(list);

		free(nwdata->data);
		free(nwdata);
	}
	L_UNLOCK(list);

	pthread_mutex_destroy(&list->lock);
	list->count = 0;
	free(list);

	return 0;
}

int8_t noncework_list_push(bf_list_t* list, bf_chip_address_t chip_address,
		bf_chip_address_t src_address, bf_works_t cwork, bf_works_t owork, uint32_t nonce)
{
	if (list == NULL)
		return -1;

	bf_data_t* nwdata = cgmalloc(sizeof(bf_data_t));
	nwdata->data = cgmalloc(sizeof(bf_noncework_t));
	nwdata->next = NULL;
	nwdata->prev = NULL;

	cg_memcpy(&NONCEWORK(nwdata)->cwork, &cwork, sizeof(bf_works_t));
	cg_memcpy(&NONCEWORK(nwdata)->owork, &owork, sizeof(bf_works_t));
	cg_memcpy(&NONCEWORK(nwdata)->chip_address, &chip_address, sizeof(bf_chip_address_t));
	cg_memcpy(&NONCEWORK(nwdata)->src_address,  &src_address,  sizeof(bf_chip_address_t));
	NONCEWORK(nwdata)->nonce = nonce;

	LIST_PUSH_TAIL(list, nwdata);
	list->count++;

	return 0;
}

int8_t noncework_list_pop(bf_list_t* list)
{
	if (list == NULL)
		return -1;

	bf_data_t* nwdata = list->head;
	if (nwdata != NULL) {
		LIST_POP_HEAD(list);	
		list->count--;
		free(nwdata->data);
		free(nwdata);
	} else
		return -1;

	return 0;
}

/* renonce list primitives */
bf_list_t* renonce_list_init(void)
{
	bf_list_t* list = cgmalloc(sizeof(bf_list_t));
	list->head = NULL;
	list->tail = NULL;
	list->count = 0;
	pthread_mutex_init(&list->lock, NULL);

	return list;
}

int8_t renonce_list_deinit(bf_list_t* list)
{
	if (list == NULL)
		return -1;

	/* free renonce list */
	L_LOCK(list);
	while (list->head != NULL) {
		bf_data_t* rdata = list->head;
		LIST_POP_HEAD(list);

		free(rdata->data);
		free(rdata);
	}
	L_UNLOCK(list);

	pthread_mutex_destroy(&list->lock);
	list->count = 0;
	free(list);

	return 0;
}

int8_t renonce_list_push(bf_list_t* list, uint32_t id, uint32_t nonce, bf_chip_address_t src_address,
		bf_works_t cwork, bf_works_t owork)
{
	if (list == NULL)
		return -1;

	bf_data_t* rdata = cgmalloc(sizeof(bf_data_t));
	rdata->data = cgmalloc(sizeof(bf_renonce_t));
	rdata->next = NULL;
	rdata->prev = NULL;

	cg_memcpy(&RENONCE(rdata)->cwork, &cwork, sizeof(bf_works_t));
	cg_memcpy(&RENONCE(rdata)->owork, &owork, sizeof(bf_works_t));
	cg_memcpy(&RENONCE(rdata)->src_address, &src_address, sizeof(bf_chip_address_t));
	RENONCE(rdata)->id    = id;
	RENONCE(rdata)->nonce = nonce;
	RENONCE(rdata)->stage = RENONCE_STAGE0;
	RENONCE(rdata)->sent      = false;
	RENONCE(rdata)->received  = false;
	RENONCE(rdata)->match     = false;

	LIST_PUSH_TAIL(list, rdata);
	list->count++;

	return 0;
}

int8_t renonce_list_pop(bf_list_t* list)
{
	if (list == NULL)
		return -1;

	bf_data_t* rdata = list->head;
	if (rdata != NULL) {
		LIST_POP_HEAD(list);	
		list->count--;
		free(rdata->data);
		free(rdata);
	} else
		return -1;

	return 0;
}

int8_t renonce_list_remove(bf_list_t* list, bf_data_t* rdata)
{
	if (list == NULL)
		return -1;

	if (rdata != NULL) {
		LIST_REMOVE(list, rdata);
		list->count--;
		free(rdata->data);
		free(rdata);
	} else
		return -1;

	return 0;
}

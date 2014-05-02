#ifndef TRIMPOT_MPC4X_H
#define TRIMPOT_MPC4X_H

#include <stdint.h>
#include <stdbool.h>


struct mcp4x {
	uint16_t (*get_wiper)(struct mcp4x *me, uint8_t id);
	bool (*set_wiper)(struct mcp4x *me, uint8_t id, uint16_t w);
	void (*exit)(struct mcp4x *me);
	uint8_t addr;
	int file;
};

/* constructor */
extern struct mcp4x *mcp4x_init(uint8_t addr);

#endif /* TRIMPOT_MPC4X_H */

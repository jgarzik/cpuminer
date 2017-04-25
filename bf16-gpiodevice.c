#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bf16-gpiodevice.h"
#include "driver-bitfury16.h"

#include "miner.h"

typedef struct {
	unsigned int*   oe_addr;  // R/W, bit==1 -> input, bit==0 -> output,
	unsigned int*   data_out; // R/W
	unsigned int*   data_set; // W/O
	unsigned int*   data_clr; // W/O
	unsigned int*   data_in;  // R/O
} gpio_attr_t;

typedef struct {
	unsigned int* data_control;
} ctrl_attr_t;

typedef struct {
	gpio_attr_t gpio0;
	gpio_attr_t gpio1;
	gpio_attr_t gpio2;
	gpio_attr_t gpio3;
	ctrl_attr_t* ctrl;
} mmap_attr_t;

static mmap_attr_t mmap_base;
char *gpio_device_name  = "/dev/mem";

int8_t gpio_write_ctrl(gpio_rq_t* rq)
{
	if (rq->gpioIndex < MAX_GPIO_INDEX && rq->regIndex < MAX_REGISTER_INDEX) {
		gpio_attr_t* gpio_addr = (void*) &mmap_base;
		gpio_attr_t gpio = gpio_addr[rq->gpioIndex];

		unsigned int** addr = (void*) &gpio;
		*addr[rq->regIndex] = rq->data;

		return 0;
	}

	return -1;
}

int gpio_read_ctrl(gpio_rq_t* rq)
{
	if (rq->gpioIndex < MAX_GPIO_INDEX && rq->regIndex < MAX_REGISTER_INDEX) {
		gpio_attr_t* gpio_addr = (void*) &mmap_base;
		gpio_attr_t gpio = gpio_addr[rq->gpioIndex];

		unsigned int** addr = (void*) &gpio;
		rq->data = *addr[rq->regIndex];
		return 0;
	}

	return -1;
}

void* map_gpio(gpio_attr_t* gpioAttr, void* gpio)
{
	gpioAttr->oe_addr  = gpio + GPIO_OE;
	gpioAttr->data_out = gpio + GPIO_DATAOUT;
	gpioAttr->data_set = gpio + GPIO_SETDATAOUT;
	gpioAttr->data_clr = gpio + GPIO_CLEARDATAOUT;
	gpioAttr->data_in  = gpio + GPIO_DATAIN;

	return gpio;
}

void* mmap_open(const char *device, uint32_t base, uint32_t size)
{
	int fd = open(device, O_RDWR | O_SYNC);
	if (fd < 0)
		quit(1, "Failed to open /dev/mem: %s", strerror(errno));

	void* mmap_addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
	if (mmap_addr == MAP_FAILED)
		quit(1, "Failed to mmap GPIO [%08x]: %s", base, strerror(errno));

	close(fd);

	return mmap_addr;
}

int8_t gpio_init(device_t* attr, char *device, uint16_t size)
{
	attr->device = device;
	attr->mode = 0;
	attr->speed = 0;
	attr->bits = 0;
	attr->size = size;
	attr->rx = malloc(size);
	attr->tx = malloc(size);

	map_gpio(&mmap_base.gpio0, mmap_open(device, GPIO0_START_ADDR, GPIO_SIZE));
	map_gpio(&mmap_base.gpio1, mmap_open(device, GPIO1_START_ADDR, GPIO_SIZE));
	map_gpio(&mmap_base.gpio2, mmap_open(device, GPIO2_START_ADDR, GPIO_SIZE));
	map_gpio(&mmap_base.gpio3, mmap_open(device, GPIO3_START_ADDR, GPIO_SIZE));

	return 0;
}


void gpio_release(device_t *attr)
{
	free(attr->rx);
	free(attr->tx);
}

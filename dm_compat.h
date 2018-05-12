#ifndef _DM_COMPAT_H_
#define _DM_COMPAT_H_

#include "miner.h"

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <asm/ioctls.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <ctype.h>
#include <limits.h>
#include <linux/watchdog.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <linux/spi/spidev.h>
#include <linux/types.h>


#define NUMARGS(...)  ((int)(sizeof((int[]){(int)__VA_ARGS__})/sizeof(int)))

#define FSCANF(STREAM, FORMAT, ...) \
	do { \
		if (unlikely(fscanf((STREAM), (FORMAT), __VA_ARGS__) != NUMARGS(__VA_ARGS__))) { \
			applog(LOG_ERR, "Failed to fscanf %d args from %s %s line %d", \
			       NUMARGS(__VA_ARGS__), __FILE__, __func__, __LINE__); \
		} \
	} while (0)

#define FREAD(PTR, SIZE, NMEMB, STREAM) \
       do { \
	       if (unlikely(fread((PTR), (SIZE), (NMEMB), (STREAM)) != NMEMB)) \
		       applog(LOG_ERR, "Failed to fread size %d nmemb %d from %s %s line %d", \
		       (SIZE), (NMEMB), __FILE__, __func__, __LINE__); \
       } while (0)

#define FWRITE(PTR, SIZE, NMEMB, STREAM) \
       do { \
	       if (unlikely(fwrite((PTR), (SIZE), (NMEMB), (STREAM)) != NMEMB)) \
		       applog(LOG_ERR, "Failed to fwrite size %d nmemb %d from %s %s line %d", \
		       (SIZE), (NMEMB), __FILE__, __func__, __LINE__); \
       } while (0)


#define WRITE(FILDES, BUF, NBYTE) \
	do { \
		int ret = write((FILDES), (BUF), (NBYTE)); \
		if (unlikely(ret != (int)(NBYTE))) { \
			if (ret == -1) { \
				applog(LOG_ERR, "Failed to write size %d from %s %s line %d with errno %d:%s", \
				       (NBYTE), __FILE__, __func__, __LINE__, errno, strerror(errno)); \
			} else { \
				applog(LOG_WARNING, "Failed to write size %d from %s %s line %d", \
				       (NBYTE),  __FILE__, __func__, __LINE__); \
			} \
		} \
	} while (0)


#define READ(FILDES, BUF, NBYTE) \
	do { \
		int ret = read((FILDES), (BUF), (NBYTE)); \
		if (unlikely(ret != (int)(NBYTE))) { \
			if (ret == -1) { \
				applog(LOG_ERR, "Failed to read size %d from %s %s line %d with errno %d:%s", \
				       (NBYTE), __FILE__, __func__, __LINE__, errno, strerror(errno)); \
			} else { \
				applog(LOG_WARNING, "Failed to read size %d from %s %s line %d", \
				       (NBYTE),  __FILE__, __func__, __LINE__); \
			} \
		} \
	} while (0)


/* MCOMPAT_CHAIN */

typedef struct MCOMPAT_CHAIN_TAG{
	//
	bool (*power_on)(unsigned char);
	//
	bool (*power_down)(unsigned char);
	//
	bool (*hw_reset)(unsigned char);
	//
	bool (*power_on_all)(void);
	//
	bool (*power_down_all)(void);
}MCOMPAT_CHAIN_T;



void init_mcompat_chain(void);
void exit_mcompat_chain(void);

void register_mcompat_chain(MCOMPAT_CHAIN_T * ops);


bool mcompat_chain_power_on(unsigned char chain_id);

bool mcompat_chain_power_down(unsigned char chain_id);

bool mcompat_chain_hw_reset(unsigned char chain_id);


/* MCOMPAT_FAN */

extern int g_temp_hi_thr;
extern int g_temp_lo_thr;
extern int g_temp_start_thr;
extern int g_dangerous_temp;
extern int g_work_temp;

typedef struct {
	int final_temp_avg;
	int final_temp_hi;
	int final_temp_lo;
	int temp_highest[3];
	int temp_lowest[3];
}mcompat_temp_s;


typedef struct {
	int temp_hi_thr;
	int temp_lo_thr;
	int temp_start_thr;
	int dangerous_stat_temp;
	int work_temp;
	int default_fan_speed;
}mcompat_temp_config_s;


typedef struct {
	int fd;
	int last_valid_temp;

	int speed;
	int last_fan_speed;
	int last_fan_temp;
	mcompat_temp_s * mcompat_temp;
	int temp_average;
	int temp_highest;
	int temp_lowest;
}mcompat_fan_temp_s;


extern void mcompat_fan_temp_init(unsigned char fan_id,mcompat_temp_config_s temp_config);
extern void mcompat_fan_speed_set(unsigned char fan_id, int speed);
extern void mcompat_fan_speed_update_hub(mcompat_fan_temp_s *fan_temp);


/* MCOMPAT_CMD */

typedef struct MCOMPAT_CMD_TAG{
	//
	void (*set_speed)(unsigned char, int);
	//
	bool (*cmd_reset)(unsigned char, unsigned char, unsigned char *, unsigned char *);
	//
	int (*cmd_bist_start)(unsigned char, unsigned char);
	//
	bool (*cmd_bist_collect)(unsigned char, unsigned char);
	//
	bool (*cmd_bist_fix)(unsigned char, unsigned char);
	//
	bool (*cmd_write_register)(unsigned char, unsigned char, unsigned char *, int);
	//
	bool (*cmd_read_register)(unsigned char, unsigned char, unsigned char *, int);
	//
	bool (*cmd_read_write_reg0d)(unsigned char, unsigned char, unsigned char *, int, unsigned char *);
	//
	bool (*cmd_write_job)(unsigned char, unsigned char, unsigned char *, int);
	//
	bool (*cmd_read_result)(unsigned char, unsigned char, unsigned char *, int);
	//
	bool (*cmd_auto_nonce)(unsigned char, int, int);
	//
	bool (*cmd_read_nonce)(unsigned char, unsigned char *, int);

	bool (*cmd_get_temp)(mcompat_fan_temp_s *temp_ctrl);
}MCOMPAT_CMD_T;



void init_mcompat_cmd(void);
void exit_mcompat_cmd(void);

void register_mcompat_cmd(MCOMPAT_CMD_T * cmd_ops_p);


/* MCOMPAT_GPIO */

typedef struct MCOMPAT_GPIO_TAG{
	//
	void (*set_power_en)(unsigned char, int);
	//
	void (*set_start_en)(unsigned char, int);
	//
	bool (*set_reset)(unsigned char, int);
	//
	void (*set_led)(unsigned char, int);
	//
	int (*get_plug)(unsigned char);
	//
	bool (*set_vid)(unsigned char, int);
	//
	void (*set_green_led)(int mode);
	//
	void (*set_red_led)(int mode);
	//
	int (*get_button)(void);
}MCOMPAT_GPIO_T;



void init_mcompat_gpio(void);
void exit_mcompat_gpio(void);

void register_mcompat_gpio(MCOMPAT_GPIO_T * ops);

/* MCOMPAT_GPIO_I2C */

#define _SCL_PIN                (0)
#define _SDA_PIN                (1)

void mcompat_gpio_i2c_init(void);
void mcompat_gpio_i2c_deinit(void);

void mcompat_gpio_i2c_send_byte(uint8_t data);
uint8_t mcompat_gpio_i2c_recv_byte(void);

bool mcompat_gpio_i2c_send_buf(uint8_t *buf, uint8_t buf_len, uint8_t dev_addr, uint16_t reg_addr);
bool mcompat_gpio_i2c_recv_buf(uint8_t *buf, uint8_t buf_len, uint8_t dev_addr, uint16_t reg_addr);


/* MCOMPAT_PWM */

typedef struct MCOMPAT_PWM_TAG{
	//
	void (*set_pwm)(unsigned char, int, int);
}MCOMPAT_PWM_T;



void init_mcompat_pwm(void);
void exit_mcompat_pwm(void);

void register_mcompat_pwm(MCOMPAT_PWM_T * ops);

/* MCOMPAT_TEMP */

typedef struct _c_temp
{
	short tmp_lo;       // lowest temperature
	short tmp_hi;       // highest temperature
	short tmp_avg;      // average temperature
	bool optimal;       // temp considered in optimal range
} c_temp;

extern int  mcompat_temp_to_centigrade(int temp);
extern bool mcompat_get_chain_temp(unsigned char chain_id, c_temp *chain_tmp);
extern void mcompat_get_chip_temp(int chain_id, int *chip_temp);

/* MCOMPAT_WATCHDOG */

#define MCOMPAT_WATCHDOG_DEV               ("/dev/watchdog0")

void mcompat_watchdog_keep_alive(void);

void mcompat_watchdog_open(void);

void mcompat_watchdog_set_timeout(int timeout);

void mcompat_watchdog_close(void);


/* MCOMPAT_LIB */

#define MCOMPAT_LIB_MINER_TYPE_FILE             ("/tmp/type")
#define MCOMPAT_LIB_HARDWARE_VERSION_FILE       ("/tmp/hwver")

#define MCOMPAT_LIB_HARDWARE_VERSION_G9         (9)
#define MCOMPAT_LIB_HARDWARE_VERSION_G19        (19)
#define MCOMPAT_LIB_HARDWARE_VERSION_ERR        (-1)

#define MCOMPAT_LIB_MINER_TYPE_T1               (1)
#define MCOMPAT_LIB_MINER_TYPE_T2               (2)
#define MCOMPAT_LIB_MINER_TYPE_T3               (3)
#define MCOMPAT_LIB_MINER_TYPE_T4               (4)
#define MCOMPAT_LIB_MINER_TYPE_D11              (5)
#define MCOMPAT_LIB_MINER_TYPE_D12              (6)
#define MCOMPAT_LIB_MINER_TYPE_ERR              (-1)

#define MCOMPAT_LIB_VID_VID_TYPE                (0)
#define MCOMPAT_LIB_VID_GPIO_I2C_TYPE           (1)
#define MCOMPAT_LIB_VID_UART_TYPE               (2)
#define MCOMPAT_LIB_VID_I2C_TYPE                (3)
#define MCOMPAT_LIB_VID_ERR_TYPE                (-1)

#define REG_LENGTH      (12)
#define VID_MAX			(31)
#define VID_MIN			(0)


int mcompat_get_shell_cmd_rst(char *cmd, char *result, int size);

int misc_call_api(char *command, char *host, short int port);

bool misc_tcp_is_ok(char *host, short int port);

char *misc_trim(char *str);

int misc_get_board_version(void);

int misc_get_miner_type(void);

int misc_get_vid_type(void);

void misc_system(const char *cmd, char *rst_buf, int buf_size);

void mcompat_configure_tvsensor(int chain_id, int chip_id, bool is_tsensor);

void  mcompat_cfg_tsadc_divider(int chain_id,unsigned int pll_clk);

void mcompat_get_chip_volt(int chain_id, int *chip_volt);

int mcompat_find_chain_vid(int chain_id, int chip_num, int vid_start, double volt_target);

double mcompat_get_average_volt(int *volt, int size);


/* MCOMPAT_DRV */

#define PLATFORM_ZYNQ_SPI_G9    (0x01)
#define PLATFORM_ZYNQ_SPI_G19   (0x02)
#define PLATFORM_ZYNQ_HUB_G9    (0x03)
#define PLATFORM_ZYNQ_HUB_G19   (0x04)
#define PLATFORM_SOC            (0x10)
#define PLATFORM_ORANGE_PI      (0x20)

#define SPI_SPEED_390K          (0)
#define SPI_SPEED_781K          (1)
#define SPI_SPEED_1562K         (2)
#define SPI_SPEED_3125K         (3)
#define SPI_SPEED_6250K         (4)
#define SPI_SPEED_9960K         (5)

#define MCOMPAT_LOG_DEBUG            (1)
#define MCOMPAT_LOG_INFO             (2)
#define MCOMPAT_LOG_NOTICE           (3)
#define MCOMPAT_LOG_WARNING          (4)
#define MCOMPAT_LOG_ERR              (5)
#define MCOMPAT_LOG_CRIT             (6)
#define MCOMPAT_LOG_ALERT            (7)
#define MCOMPAT_LOG_EMERG            (8)


extern void sys_platform_debug_init(int debug_level);

extern bool sys_platform_init(int platform, int miner_type, int chain_num, int chip_num);

extern bool sys_platform_exit();


extern bool mcompat_set_spi_speed(unsigned char chain_id, int index);

extern bool mcompat_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out);

extern int mcompat_cmd_bist_start(unsigned char chain_id, unsigned char chip_id);

extern bool mcompat_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id);

extern bool mcompat_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id);

extern bool mcompat_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

extern bool mcompat_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

extern bool mcompat_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out);

extern bool mcompat_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len);

extern bool mcompat_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len);

extern bool mcompat_cmd_auto_nonce(unsigned char chain_id, int mode, int len);

extern bool mcompat_cmd_read_nonce(unsigned char chain_id, unsigned char *res, int len);

extern bool mcompat_cmd_get_temp(mcompat_fan_temp_s * fan_temp);

extern bool mcompat_get_chain_temp(unsigned char chain_id, c_temp *chain_tmp);

extern void mcompat_set_power_en(unsigned char chain_id, int val);

extern void mcompat_set_start_en(unsigned char chain_id, int val);

extern bool mcompat_set_reset(unsigned char chain_id, int val);

extern void mcompat_set_led(unsigned char chain_id, int val);

extern bool mcompat_set_vid(unsigned char chain_id, int val);

extern bool mcompat_set_vid_by_step(unsigned char chain_id, int start_vid, int target_vid);

extern void mcompat_set_pwm(unsigned char fan_id, int frequency, int duty);

extern int mcompat_get_plug(unsigned char chain_id);

extern int mcompat_get_button(void);

extern void mcompat_set_green_led(int mode);

extern void mcompat_set_red_led(int mode);

extern bool mcompat_chain_power_on(unsigned char chain_id);

extern bool mcompat_chain_power_down(unsigned char chain_id);

extern bool mcompat_chain_hw_reset(unsigned char chain_id);

extern bool mcompat_chain_power_on_all(void);

extern bool mcompat_chain_power_down_all(void);


#define MCOMPAT_CONFIG_MAX_CHAIN_NUM               (8)
#define MCOMPAT_CONFIG_MAX_CHIP_NUM                (80)
#define MCOMPAT_CONFIG_MAX_JOB_LEN                 (92)
#define MCOMPAT_CONFIG_MAX_CMD_LENGTH              (256)

#define MAGIC_NUM                               (100)

#define CMD_BIST_START                          (0x01)
#define CMD_BIST_COLLECT                        (0x0b)
#define CMD_BIST_FIX                            (0x03)
#define CMD_RESET                               (0x04)
#define CMD_RESETBC                             (0x05)
#define CMD_WRITE_JOB                           (0x07)
#define CMD_WRITE_JOB_T1                        (0x0c)
#define CMD_READ_RESULT                         (0x08)
#define CMD_WRITE_REG                           (0x09)
#define CMD_READ_REG                            (0x0a)
#define CMD_WRITE_REG0d                         (0x0d)
#define CMD_POWER_ON                            (0x02)
#define CMD_POWER_OFF                           (0x06)
#define CMD_POWER_RESET                         (0x0c)

#define RESP_READ_REG                           (0x1a)

#define CMD_ADDR_BROADCAST                      (0x00)
#define CMD_HL                                  (2)
#define CMD_RESET_DL                            (4)
#define CMD_RESET_TL                            (CMD_HL + CMD_RESET_DL)


#define ASIC_MCOMPAT_FAN_PWM_STEP            (5)
#define ASIC_MCOMPAT_FAN_PWM_DUTY_MAX        (100)
#define ASIC_MCOMPAT_FAN_PWM_FREQ_TARGET     (20000)
#define ASIC_MCOMPAT_FAN_PWM_FREQ            (20000)
#define FAN_CNT                           ( 2 )
#define ASIC_MCOMPAT_FAN_TEMP_MAX_THRESHOLD  (100)
#define ASIC_MCOMPAT_FAN_TEMP_UP_THRESHOLD   (55)
#define ASIC_MCOMPAT_FAN_TEMP_DOWN_THRESHOLD (35)

#define MCOMPAT_VID_UART_PATH                        ("/dev/ttyPS1")


#define MCOMPAT_CONFIG_CMD_MAX_LEN                 (MCOMPAT_CONFIG_MAX_JOB_LEN + MCOMPAT_CONFIG_MAX_CHAIN_NUM * 2 * 2)
#define MCOMPAT_CONFIG_CMD_RST_MAX_LEN             (MCOMPAT_CONFIG_CMD_MAX_LEN)

/* SPI */
#define MCOMPAT_CONFIG_SPI_DEFAULT_CS_LINE         (0)
#define MCOMPAT_CONFIG_SPI_DEFAULT_MODE            (SPI_MODE_1)
#define MCOMPAT_CONFIG_SPI_DEFAULT_SPEED           (1500000)
#define MCOMPAT_CONFIG_SPI_DEFAULT_BITS_PER_WORD   (8)
/* GPIO */
#define MCOMPAT_CONFIG_CHAIN0_POWER_EN_GPIO        (872)
#define MCOMPAT_CONFIG_CHAIN1_POWER_EN_GPIO        (873)
#define MCOMPAT_CONFIG_CHAIN2_POWER_EN_GPIO        (874)
#define MCOMPAT_CONFIG_CHAIN3_POWER_EN_GPIO        (875)
#define MCOMPAT_CONFIG_CHAIN4_POWER_EN_GPIO        (876)
#define MCOMPAT_CONFIG_CHAIN5_POWER_EN_GPIO        (877)
#define MCOMPAT_CONFIG_CHAIN6_POWER_EN_GPIO        (878)
#define MCOMPAT_CONFIG_CHAIN7_POWER_EN_GPIO        (879)
#define MCOMPAT_CONFIG_CHAIN0_START_EN_GPIO        (854)
#define MCOMPAT_CONFIG_CHAIN1_START_EN_GPIO        (856)
#define MCOMPAT_CONFIG_CHAIN2_START_EN_GPIO        (858)
#define MCOMPAT_CONFIG_CHAIN3_START_EN_GPIO        (860)
#define MCOMPAT_CONFIG_CHAIN4_START_EN_GPIO        (862)
#define MCOMPAT_CONFIG_CHAIN5_START_EN_GPIO        (864)
#define MCOMPAT_CONFIG_CHAIN6_START_EN_GPIO        (866)
#define MCOMPAT_CONFIG_CHAIN7_START_EN_GPIO        (868)
#define MCOMPAT_CONFIG_CHAIN0_RESET_GPIO           (855)
#define MCOMPAT_CONFIG_CHAIN1_RESET_GPIO           (857)
#define MCOMPAT_CONFIG_CHAIN2_RESET_GPIO           (859)
#define MCOMPAT_CONFIG_CHAIN3_RESET_GPIO           (861)
#define MCOMPAT_CONFIG_CHAIN4_RESET_GPIO           (863)
#define MCOMPAT_CONFIG_CHAIN5_RESET_GPIO           (865)
#define MCOMPAT_CONFIG_CHAIN6_RESET_GPIO           (867)
#define MCOMPAT_CONFIG_CHAIN7_RESET_GPIO           (869)
#define MCOMPAT_CONFIG_CHAIN0_LED_GPIO             (881)
#define MCOMPAT_CONFIG_CHAIN1_LED_GPIO             (882)
#define MCOMPAT_CONFIG_CHAIN2_LED_GPIO             (883)
#define MCOMPAT_CONFIG_CHAIN3_LED_GPIO             (884)
#define MCOMPAT_CONFIG_CHAIN4_LED_GPIO             (885)
#define MCOMPAT_CONFIG_CHAIN5_LED_GPIO             (886)
#define MCOMPAT_CONFIG_CHAIN6_LED_GPIO             (887)
#define MCOMPAT_CONFIG_CHAIN7_LED_GPIO             (888)
#define MCOMPAT_CONFIG_CHAIN0_PLUG_GPIO            (896)
#define MCOMPAT_CONFIG_CHAIN1_PLUG_GPIO            (897)
#define MCOMPAT_CONFIG_CHAIN2_PLUG_GPIO            (898)
#define MCOMPAT_CONFIG_CHAIN3_PLUG_GPIO            (899)
#define MCOMPAT_CONFIG_CHAIN4_PLUG_GPIO            (900)
#define MCOMPAT_CONFIG_CHAIN5_PLUG_GPIO            (901)
#define MCOMPAT_CONFIG_CHAIN6_PLUG_GPIO            (902)
#define MCOMPAT_CONFIG_CHAIN7_PLUG_GPIO            (903)

#define MCOMPAT_CONFIG_B9_GPIO                     (906 + 51)
#define MCOMPAT_CONFIG_A10_GPIO                    (906 + 37)

extern int g_platform;
extern int g_miner_type;
extern int g_chain_num;
extern int g_chip_num;


/* ZYNQ_GPIO */

extern void zynq_gpio_init(int pin, int dir);

extern void zynq_gpio_exit(int pin);

extern int zynq_gpio_read(int pin);


/* ZYNQ_PWM */

#define SYSFS_PWM_DEV       ("/dev/pwmgen0.0")

#define IOCTL_SET_PWM_FREQ(x)   _IOR(MAGIC_NUM, (2*x), char *)
#define IOCTL_SET_PWM_DUTY(x)   _IOR(MAGIC_NUM, (2*x+1), char *)


extern void zynq_set_pwm(unsigned char fan_id, int frequency, int duty);

extern int zynq_gpio_g19_vid_set(int chain_id, int level);


/* ZYNQ_SPI */

typedef struct ZYNQ_SPI_TAG{
	int             fd;
	pthread_mutex_t lock;
}ZYNQ_SPI_T;

void zynq_spi_init(ZYNQ_SPI_T *spi, int bus);

void zynq_spi_exit(ZYNQ_SPI_T *spi);

void zynq_spi_read(ZYNQ_SPI_T *spi, uint8_t *rxbuf, int len);

void zynq_spi_write(ZYNQ_SPI_T *spi, uint8_t *txbuf, int len);


void zynq_set_spi_speed(int speed);



/* ZYNQ_VID */

#define SYSFS_VID_DEV       ("/dev/vidgen0.0")

#define IOCTL_SET_VAL_0     _IOR(MAGIC_NUM, 0, char *)
#define IOCTL_SET_VALUE_0   _IOR(MAGIC_NUM, 0, char *)
#define IOCTL_SET_CHAIN_0   _IOR(MAGIC_NUM, 1, char *)

typedef struct ZYNQ_VID_TAG{
	int             fd;
	pthread_mutex_t lock;
}ZYNQ_VID_T;

extern int zynq_gpio_g9_vid_set(int level);

extern int zynq_gpio_g19_vid_set(int chain_id, int level);


/* HUB_HARDWARE */

#define _MAX_MEM_RANGE              (0x10000)

void hub_hardware_init(void);
void hub_hardware_deinit(void);


/* HUB_VID */

#define I2C_DEVICE_NAME     "/dev/i2c-0"
#define I2C_SLAVE_ADDR      0x01

bool hub_set_vid(uint8_t chan_id, int vol);

bool set_timeout_on_i2c(int time);



/* DRV_HUB */

#define PAGE_SIZE   ((size_t)getpagesize())*2
#define PAGE_MASK   ((uint32_t) (long)~(PAGE_SIZE - 1))


/* Definition for CPU ID */
#define XPAR_CPU_ID 0

/* Definitions for peripheral PS7_CORTEXA9_0 */
#define XPAR_PS7_CORTEXA9_0_CPU_CLK_FREQ_HZ 666666687


/******************************************************************/

/* Canonical definitions for peripheral PS7_CORTEXA9_0 */
#define XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ 666666687


/******************************************************************/

#define STDIN_BASEADDRESS 0xE0001000
#define STDOUT_BASEADDRESS 0xE0001000

/******************************************************************/


/* Definitions for peripheral PS7_DDR_0 */
#define XPAR_PS7_DDR_0_S_AXI_BASEADDR 0x00100000
#define XPAR_PS7_DDR_0_S_AXI_HIGHADDR 0x1FFFFFFF


/******************************************************************/

/* Definitions for driver DEVCFG */
#define XPAR_XDCFG_NUM_INSTANCES 1

/* Definitions for peripheral PS7_DEV_CFG_0 */
#define XPAR_PS7_DEV_CFG_0_DEVICE_ID 0
#define XPAR_PS7_DEV_CFG_0_BASEADDR 0xF8007000
#define XPAR_PS7_DEV_CFG_0_HIGHADDR 0xF80070FF


/******************************************************************/

/* Canonical definitions for peripheral PS7_DEV_CFG_0 */
#define XPAR_XDCFG_0_DEVICE_ID XPAR_PS7_DEV_CFG_0_DEVICE_ID
#define XPAR_XDCFG_0_BASEADDR 0xF8007000
#define XPAR_XDCFG_0_HIGHADDR 0xF80070FF


/******************************************************************/

/* Definitions for driver DMAPS */
#define XPAR_XDMAPS_NUM_INSTANCES 2

/* Definitions for peripheral PS7_DMA_NS */
#define XPAR_PS7_DMA_NS_DEVICE_ID 0
#define XPAR_PS7_DMA_NS_BASEADDR 0xF8004000
#define XPAR_PS7_DMA_NS_HIGHADDR 0xF8004FFF


/* Definitions for peripheral PS7_DMA_S */
#define XPAR_PS7_DMA_S_DEVICE_ID 1
#define XPAR_PS7_DMA_S_BASEADDR 0xF8003000
#define XPAR_PS7_DMA_S_HIGHADDR 0xF8003FFF


/******************************************************************/

/* Canonical definitions for peripheral PS7_DMA_NS */
#define XPAR_XDMAPS_0_DEVICE_ID XPAR_PS7_DMA_NS_DEVICE_ID
#define XPAR_XDMAPS_0_BASEADDR 0xF8004000
#define XPAR_XDMAPS_0_HIGHADDR 0xF8004FFF

/* Canonical definitions for peripheral PS7_DMA_S */
#define XPAR_XDMAPS_1_DEVICE_ID XPAR_PS7_DMA_S_DEVICE_ID
#define XPAR_XDMAPS_1_BASEADDR 0xF8003000
#define XPAR_XDMAPS_1_HIGHADDR 0xF8003FFF


/******************************************************************/

/* Definitions for driver EMACPS */
#define XPAR_XEMACPS_NUM_INSTANCES 1

/* Definitions for peripheral PS7_ETHERNET_0 */
#define XPAR_PS7_ETHERNET_0_DEVICE_ID 0
#define XPAR_PS7_ETHERNET_0_BASEADDR 0xE000B000
#define XPAR_PS7_ETHERNET_0_HIGHADDR 0xE000BFFF
#define XPAR_PS7_ETHERNET_0_ENET_CLK_FREQ_HZ 125000000
#define XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV0 1
#define XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV1 1
#define XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV0 1
#define XPAR_PS7_ETHERNET_0_ENET_SLCR_100MBPS_DIV1 5
#define XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV0 1
#define XPAR_PS7_ETHERNET_0_ENET_SLCR_10MBPS_DIV1 50


/******************************************************************/

/* Canonical definitions for peripheral PS7_ETHERNET_0 */
#define XPAR_XEMACPS_0_DEVICE_ID XPAR_PS7_ETHERNET_0_DEVICE_ID
#define XPAR_XEMACPS_0_BASEADDR 0xE000B000
#define XPAR_XEMACPS_0_HIGHADDR 0xE000BFFF
#define XPAR_XEMACPS_0_ENET_CLK_FREQ_HZ 125000000
#define XPAR_XEMACPS_0_ENET_SLCR_1000Mbps_DIV0 1
#define XPAR_XEMACPS_0_ENET_SLCR_1000Mbps_DIV1 1
#define XPAR_XEMACPS_0_ENET_SLCR_100Mbps_DIV0 1
#define XPAR_XEMACPS_0_ENET_SLCR_100Mbps_DIV1 5
#define XPAR_XEMACPS_0_ENET_SLCR_10Mbps_DIV0 1
#define XPAR_XEMACPS_0_ENET_SLCR_10Mbps_DIV1 50


/******************************************************************/

/* Definitions for driver FANS_CTRL */
#define XPAR_FANS_CTRL_NUM_INSTANCES 1

/* Definitions for peripheral FANS_CTRL_0 */
#define XPAR_FANS_CTRL_0_DEVICE_ID 0
#define XPAR_FANS_CTRL_0_S00_AXI_BASEADDR 0x43C00000
#define XPAR_FANS_CTRL_0_S00_AXI_HIGHADDR 0x43C0FFFF


/******************************************************************/


/* Definitions for peripheral PS7_AFI_0 */
#define XPAR_PS7_AFI_0_S_AXI_BASEADDR 0xF8008000
#define XPAR_PS7_AFI_0_S_AXI_HIGHADDR 0xF8008FFF


/* Definitions for peripheral PS7_AFI_1 */
#define XPAR_PS7_AFI_1_S_AXI_BASEADDR 0xF8009000
#define XPAR_PS7_AFI_1_S_AXI_HIGHADDR 0xF8009FFF


/* Definitions for peripheral PS7_AFI_2 */
#define XPAR_PS7_AFI_2_S_AXI_BASEADDR 0xF800A000
#define XPAR_PS7_AFI_2_S_AXI_HIGHADDR 0xF800AFFF


/* Definitions for peripheral PS7_AFI_3 */
#define XPAR_PS7_AFI_3_S_AXI_BASEADDR 0xF800B000
#define XPAR_PS7_AFI_3_S_AXI_HIGHADDR 0xF800BFFF


/* Definitions for peripheral PS7_DDRC_0 */
#define XPAR_PS7_DDRC_0_S_AXI_BASEADDR 0xF8006000
#define XPAR_PS7_DDRC_0_S_AXI_HIGHADDR 0xF8006FFF


/* Definitions for peripheral PS7_GLOBALTIMER_0 */
#define XPAR_PS7_GLOBALTIMER_0_S_AXI_BASEADDR 0xF8F00200
#define XPAR_PS7_GLOBALTIMER_0_S_AXI_HIGHADDR 0xF8F002FF


/* Definitions for peripheral PS7_GPV_0 */
#define XPAR_PS7_GPV_0_S_AXI_BASEADDR 0xF8900000
#define XPAR_PS7_GPV_0_S_AXI_HIGHADDR 0xF89FFFFF


/* Definitions for peripheral PS7_INTC_DIST_0 */
#define XPAR_PS7_INTC_DIST_0_S_AXI_BASEADDR 0xF8F01000
#define XPAR_PS7_INTC_DIST_0_S_AXI_HIGHADDR 0xF8F01FFF


/* Definitions for peripheral PS7_IOP_BUS_CONFIG_0 */
#define XPAR_PS7_IOP_BUS_CONFIG_0_S_AXI_BASEADDR 0xE0200000
#define XPAR_PS7_IOP_BUS_CONFIG_0_S_AXI_HIGHADDR 0xE0200FFF


/* Definitions for peripheral PS7_L2CACHEC_0 */
#define XPAR_PS7_L2CACHEC_0_S_AXI_BASEADDR 0xF8F02000
#define XPAR_PS7_L2CACHEC_0_S_AXI_HIGHADDR 0xF8F02FFF


/* Definitions for peripheral PS7_OCMC_0 */
#define XPAR_PS7_OCMC_0_S_AXI_BASEADDR 0xF800C000
#define XPAR_PS7_OCMC_0_S_AXI_HIGHADDR 0xF800CFFF


/* Definitions for peripheral PS7_PL310_0 */
#define XPAR_PS7_PL310_0_S_AXI_BASEADDR 0xF8F02000
#define XPAR_PS7_PL310_0_S_AXI_HIGHADDR 0xF8F02FFF


/* Definitions for peripheral PS7_PMU_0 */
#define XPAR_PS7_PMU_0_S_AXI_BASEADDR 0xF8891000
#define XPAR_PS7_PMU_0_S_AXI_HIGHADDR 0xF8891FFF
#define XPAR_PS7_PMU_0_PMU1_S_AXI_BASEADDR 0xF8893000
#define XPAR_PS7_PMU_0_PMU1_S_AXI_HIGHADDR 0xF8893FFF


/* Definitions for peripheral PS7_RAM_0 */
#define XPAR_PS7_RAM_0_S_AXI_BASEADDR 0x00000000
#define XPAR_PS7_RAM_0_S_AXI_HIGHADDR 0x0003FFFF


/* Definitions for peripheral PS7_RAM_1 */
#define XPAR_PS7_RAM_1_S_AXI_BASEADDR 0xFFFC0000
#define XPAR_PS7_RAM_1_S_AXI_HIGHADDR 0xFFFFFFFF


/* Definitions for peripheral PS7_SCUC_0 */
#define XPAR_PS7_SCUC_0_S_AXI_BASEADDR 0xF8F00000
#define XPAR_PS7_SCUC_0_S_AXI_HIGHADDR 0xF8F000FC


/* Definitions for peripheral PS7_SLCR_0 */
#define XPAR_PS7_SLCR_0_S_AXI_BASEADDR 0xF8000000
#define XPAR_PS7_SLCR_0_S_AXI_HIGHADDR 0xF8000FFF


/* Definitions for peripheral PS7_SMCC_0 */
#define XPAR_PS7_SMCC_0_S_AXI_BASEADDR 0xE000E000
#define XPAR_PS7_SMCC_0_S_AXI_HIGHADDR 0xE100EFFF


/******************************************************************/

/* Definitions for driver GPIOPS */
#define XPAR_XGPIOPS_NUM_INSTANCES 1

/* Definitions for peripheral PS7_GPIO_0 */
#define XPAR_PS7_GPIO_0_DEVICE_ID 0
#define XPAR_PS7_GPIO_0_BASEADDR 0xE000A000
#define XPAR_PS7_GPIO_0_HIGHADDR 0xE000AFFF


/******************************************************************/

/* Canonical definitions for peripheral PS7_GPIO_0 */
#define XPAR_XGPIOPS_0_DEVICE_ID XPAR_PS7_GPIO_0_DEVICE_ID
#define XPAR_XGPIOPS_0_BASEADDR 0xE000A000
#define XPAR_XGPIOPS_0_HIGHADDR 0xE000AFFF


/******************************************************************/

/* Definitions for driver MCOMPAT_SPI_WRAPPER */
#define XPAR_MCOMPAT_SPI_WRAPPER_NUM_INSTANCES 1

/* Definitions for peripheral MCOMPAT_SPI_WRAPPER_0 */
#define XPAR_MCOMPAT_SPI_WRAPPER_0_DEVICE_ID 0
#define XPAR_MCOMPAT_SPI_WRAPPER_0_S00_AXI_BASEADDR 0x43C30000
#define XPAR_MCOMPAT_SPI_WRAPPER_0_S00_AXI_HIGHADDR 0x43C3FFFF


/******************************************************************/

/* Definitions for driver NANDPS */
#define XPAR_XNANDPS_NUM_INSTANCES 1

/* Definitions for peripheral PS7_NAND_0 */
#define XPAR_PS7_NAND_0_DEVICE_ID 0
#define XPAR_PS7_NAND_0_BASEADDR 0xE1000000
#define XPAR_PS7_NAND_0_HIGHADDR 0xE1000FFF
#define XPAR_PS7_NAND_0_NAND_CLK_FREQ_HZ 100000000
#define XPAR_PS7_NAND_0_SMC_BASEADDR 0xE000E000
#define XPAR_PS7_NAND_0_NAND_WIDTH 8


/******************************************************************/

/* Canonical definitions for peripheral PS7_NAND_0 */
#define XPAR_XNANDPS_0_DEVICE_ID XPAR_PS7_NAND_0_DEVICE_ID
#define XPAR_XNANDPS_0_CPU_BASEADDR 0xE1000000
#define XPAR_XNANDPS_0_CPU_HIGHADDR 0xE1000FFF
#define XPAR_XNANDPS_0_NAND_CLK_FREQ_HZ 100000000
#define XPAR_XNANDPS_0_SMC_BASEADDR 0xE000E000
#define XPAR_XNANDPS_0_NAND_WIDTH 8


/******************************************************************/

/* Definitions for driver READ_DNA */
#define XPAR_READ_DNA_NUM_INSTANCES 1

/* Definitions for peripheral READ_DNA_0 */
#define XPAR_READ_DNA_0_DEVICE_ID 0
#define XPAR_READ_DNA_0_S00_AXI_BASEADDR 0x43C20000
#define XPAR_READ_DNA_0_S00_AXI_HIGHADDR 0x43C2FFFF


/******************************************************************/

/* Definitions for driver SCUGIC */
#define XPAR_XSCUGIC_NUM_INSTANCES 1U

/* Definitions for peripheral PS7_SCUGIC_0 */
#define XPAR_PS7_SCUGIC_0_DEVICE_ID 0U
#define XPAR_PS7_SCUGIC_0_BASEADDR 0xF8F00100U
#define XPAR_PS7_SCUGIC_0_HIGHADDR 0xF8F001FFU
#define XPAR_PS7_SCUGIC_0_DIST_BASEADDR 0xF8F01000U


/******************************************************************/

/* Canonical definitions for peripheral PS7_SCUGIC_0 */
#define XPAR_SCUGIC_0_DEVICE_ID 0U
#define XPAR_SCUGIC_0_CPU_BASEADDR 0xF8F00100U
#define XPAR_SCUGIC_0_CPU_HIGHADDR 0xF8F001FFU
#define XPAR_SCUGIC_0_DIST_BASEADDR 0xF8F01000U


/******************************************************************/

/* Definitions for driver SCUTIMER */
#define XPAR_XSCUTIMER_NUM_INSTANCES 1

/* Definitions for peripheral PS7_SCUTIMER_0 */
#define XPAR_PS7_SCUTIMER_0_DEVICE_ID 0
#define XPAR_PS7_SCUTIMER_0_BASEADDR 0xF8F00600
#define XPAR_PS7_SCUTIMER_0_HIGHADDR 0xF8F0061F


/******************************************************************/

/* Canonical definitions for peripheral PS7_SCUTIMER_0 */
#define XPAR_XSCUTIMER_0_DEVICE_ID XPAR_PS7_SCUTIMER_0_DEVICE_ID
#define XPAR_XSCUTIMER_0_BASEADDR 0xF8F00600
#define XPAR_XSCUTIMER_0_HIGHADDR 0xF8F0061F


/******************************************************************/

/* Definitions for driver SCUWDT */
#define XPAR_XSCUWDT_NUM_INSTANCES 1

/* Definitions for peripheral PS7_SCUWDT_0 */
#define XPAR_PS7_SCUWDT_0_DEVICE_ID 0
#define XPAR_PS7_SCUWDT_0_BASEADDR 0xF8F00620
#define XPAR_PS7_SCUWDT_0_HIGHADDR 0xF8F006FF


/******************************************************************/

/* Canonical definitions for peripheral PS7_SCUWDT_0 */
#define XPAR_SCUWDT_0_DEVICE_ID XPAR_PS7_SCUWDT_0_DEVICE_ID
#define XPAR_SCUWDT_0_BASEADDR 0xF8F00620
#define XPAR_SCUWDT_0_HIGHADDR 0xF8F006FF


/******************************************************************/

/* Definitions for driver SDPS */
#define XPAR_XSDPS_NUM_INSTANCES 1

/* Definitions for peripheral PS7_SD_0 */
#define XPAR_PS7_SD_0_DEVICE_ID 0
#define XPAR_PS7_SD_0_BASEADDR 0xE0100000
#define XPAR_PS7_SD_0_HIGHADDR 0xE0100FFF
#define XPAR_PS7_SD_0_SDIO_CLK_FREQ_HZ 100000000
#define XPAR_PS7_SD_0_HAS_CD 0
#define XPAR_PS7_SD_0_HAS_WP 0
#define XPAR_PS7_SD_0_BUS_WIDTH 0
#define XPAR_PS7_SD_0_MIO_BANK 0
#define XPAR_PS7_SD_0_HAS_EMIO 0


/******************************************************************/

/* Canonical definitions for peripheral PS7_SD_0 */
#define XPAR_XSDPS_0_DEVICE_ID XPAR_PS7_SD_0_DEVICE_ID
#define XPAR_XSDPS_0_BASEADDR 0xE0100000
#define XPAR_XSDPS_0_HIGHADDR 0xE0100FFF
#define XPAR_XSDPS_0_SDIO_CLK_FREQ_HZ 100000000
#define XPAR_XSDPS_0_HAS_CD 0
#define XPAR_XSDPS_0_HAS_WP 0
#define XPAR_XSDPS_0_BUS_WIDTH 0
#define XPAR_XSDPS_0_MIO_BANK 0
#define XPAR_XSDPS_0_HAS_EMIO 0


/******************************************************************/

/* Definitions for driver UARTPS */
#define XPAR_XUARTPS_NUM_INSTANCES 1

/* Definitions for peripheral PS7_UART_1 */
#define XPAR_PS7_UART_1_DEVICE_ID 0
#define XPAR_PS7_UART_1_BASEADDR 0xE0001000
#define XPAR_PS7_UART_1_HIGHADDR 0xE0001FFF
#define XPAR_PS7_UART_1_UART_CLK_FREQ_HZ 100000000
#define XPAR_PS7_UART_1_HAS_MODEM 0


/******************************************************************/

/* Canonical definitions for peripheral PS7_UART_1 */
#define XPAR_XUARTPS_0_DEVICE_ID XPAR_PS7_UART_1_DEVICE_ID
#define XPAR_XUARTPS_0_BASEADDR 0xE0001000
#define XPAR_XUARTPS_0_HIGHADDR 0xE0001FFF
#define XPAR_XUARTPS_0_UART_CLK_FREQ_HZ 100000000
#define XPAR_XUARTPS_0_HAS_MODEM 0


/******************************************************************/

/* Definitions for driver USBPS */
#define XPAR_XUSBPS_NUM_INSTANCES 1

/* Definitions for peripheral PS7_USB_0 */
#define XPAR_PS7_USB_0_DEVICE_ID 0
#define XPAR_PS7_USB_0_BASEADDR 0xE0002000
#define XPAR_PS7_USB_0_HIGHADDR 0xE0002FFF


/******************************************************************/

/* Canonical definitions for peripheral PS7_USB_0 */
#define XPAR_XUSBPS_0_DEVICE_ID XPAR_PS7_USB_0_DEVICE_ID
#define XPAR_XUSBPS_0_BASEADDR 0xE0002000
#define XPAR_XUSBPS_0_HIGHADDR 0xE0002FFF


/******************************************************************/

/* Definitions for driver VID_LED_BUZZER_CTRL */
#define XPAR_VID_LED_BUZZER_CTRL_NUM_INSTANCES 1

/* Definitions for peripheral VID_LED_BUZZER_CTRL_0 */
#define XPAR_VID_LED_BUZZER_CTRL_0_DEVICE_ID 0
#define XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_BASEADDR 0x43C10000
#define XPAR_VID_LED_BUZZER_CTRL_0_S00_AXI_HIGHADDR 0x43C1FFFF


/******************************************************************/

/* Definitions for driver XADCPS */
#define XPAR_XADCPS_NUM_INSTANCES 1

/* Definitions for peripheral PS7_XADC_0 */
#define XPAR_PS7_XADC_0_DEVICE_ID 0
#define XPAR_PS7_XADC_0_BASEADDR 0xF8007100
#define XPAR_PS7_XADC_0_HIGHADDR 0xF8007120


/******************************************************************/

/* Canonical definitions for peripheral PS7_XADC_0 */
#define XPAR_XADCPS_0_DEVICE_ID XPAR_PS7_XADC_0_DEVICE_ID
#define XPAR_XADCPS_0_BASEADDR 0xF8007100
#define XPAR_XADCPS_0_HIGHADDR 0xF8007120

#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG0_OFFSET 0
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG1_OFFSET 4
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG2_OFFSET 8
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG3_OFFSET 12
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG4_OFFSET 16
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG5_OFFSET 20
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG6_OFFSET 24
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG7_OFFSET 28
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG8_OFFSET 32
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG9_OFFSET 36
#define VID_LED_BUZZER_CTRL_S00_AXI_SLV_REG10_OFFSET 40

#define FANS_CTRL_S00_AXI_SLV_REG0_OFFSET 0
#define FANS_CTRL_S00_AXI_SLV_REG1_OFFSET 4
#define FANS_CTRL_S00_AXI_SLV_REG2_OFFSET 8
#define FANS_CTRL_S00_AXI_SLV_REG3_OFFSET 12
#define FANS_CTRL_S00_AXI_SLV_REG4_OFFSET 16
#define FANS_CTRL_S00_AXI_SLV_REG5_OFFSET 20
#define FANS_CTRL_S00_AXI_SLV_REG6_OFFSET 24
#define FANS_CTRL_S00_AXI_SLV_REG7_OFFSET 28
#define FANS_CTRL_S00_AXI_SLV_REG8_OFFSET 32
#define FANS_CTRL_S00_AXI_SLV_REG9_OFFSET 36
#define FANS_CTRL_S00_AXI_SLV_REG10_OFFSET 40
#define FANS_CTRL_S00_AXI_SLV_REG11_OFFSET 44

#define AUTO_CMD0A_REG0_ADDR    0x0160
#define AUTO_CMD0A_REG1_ADDR    0x0164
#define AUTO_CMD0A_REG2_ADDR    0x0168
#define AUTO_CMD0A_REG3_ADDR    0x016c
#define AUTO_CMD0A_REG4_ADDR    0x0170
#define AUTO_CMD0A_REG5_ADDR    0x0174
#define AUTO_CMD0A_REG6_ADDR    0x0178
#define AUTO_CMD0A_REG7_ADDR    0x017c


#define LED_ON                  0
#define LED_OFF                 1
#define LED_BLING_ON            2
#define LED_BLING_OFF           3


#define SPI_RESET_REG       0x1200
#define SPI_BASEADDR_GAP    0x200
#define SPI_AXIBASE         0x43C30000

#define XST_SUCCESS                     0L
#define XST_FAILURE                     1L
#define XST_DEVICE_NOT_FOUND            2L
#define XST_DEVICE_BLOCK_NOT_FOUND      3L
#define XST_INVALID_VERSION             4L
#define XST_DEVICE_IS_STARTED           5L
#define XST_DEVICE_IS_STOPPED           6L
#define XST_FIFO_ERROR                  7L
#define XST_CRC_ERROR                   8L

#define MAIN_CFG_REG0_ADDR      0x00
#define MAIN_CFG_REG1_ADDR      0x04
#define MAIN_CFG_REG2_ADDR      0x08
#define MAIN_CFG_REG3_ADDR      0x0c
#define CMD_CTRL_REG0_ADDR      0x10
#define CMD_CTRL_REG1_ADDR      0x14
#define CMD_CTRL_REG2_ADDR      0x18
#define CMD_CTRL_REG3_ADDR      0x1c
#define CMD_READ_REG0_ADDR      0x20
#define CMD_READ_REG1_ADDR      0x24
#define CMD_READ_REG2_ADDR      0x28
#define CMD_READ_REG3_ADDR      0x2c
#define CMD_READ_REG4_ADDR      0x30
#define CMD_READ_REG5_ADDR      0x34
#define CMD_READ_REG6_ADDR      0x38
#define CMD_READ_REG7_ADDR      0x3c
#define CMD_READ_REG8_ADDR      0x40
#define CMD_READ_REG9_ADDR      0x44
#define CMD_READ_REGA_ADDR      0x48
#define CMD_READ_REGB_ADDR      0x4c
#define CMD_READ_REGC_ADDR      0x50
#define CMD_READ_REGD_ADDR      0x54
#define CMD_READ_REGE_ADDR      0x58
#define CMD_READ_REGF_ADDR      0x5c
#define CMD_WRITE_HEAD_ADDR     0x60
#define CMD_WRITE_REG01_ADDR    0x64
#define CMD_WRITE_REG02_ADDR    0x68
#define CMD_WRITE_REG03_ADDR    0x6c
#define CMD_WRITE_REG04_ADDR    0x70
#define CMD_WRITE_REG05_ADDR    0x74
#define CMD_WRITE_REG06_ADDR    0x78
#define CMD_WRITE_REG07_ADDR    0x7c
#define CMD_WRITE_REG08_ADDR    0x80
#define CMD_WRITE_REG09_ADDR    0x84
#define CMD_WRITE_REG0A_ADDR    0x88
#define CMD_WRITE_REG0B_ADDR    0x8c
#define CMD_WRITE_REG0C_ADDR    0x90
#define CMD_WRITE_REG0D_ADDR    0x94
#define CMD_WRITE_REG0E_ADDR    0x98
#define CMD_WRITE_REG0F_ADDR    0x9c
#define CMD_WRITE_REG10_ADDR    0xa0
#define CMD_WRITE_REG11_ADDR    0xa4
#define CMD_WRITE_REG12_ADDR    0xa8
#define CMD_WRITE_REG13_ADDR    0xac
#define CMD_WRITE_REG14_ADDR    0xb0
#define CMD_WRITE_REG15_ADDR    0xb4
#define CMD_WRITE_REG16_ADDR    0xb8
#define CMD_WRITE_REG17_ADDR    0xbc
#define CMD_WRITE_REG18_ADDR    0xc0
#define CMD_WRITE_REG19_ADDR    0xc4
#define CMD_WRITE_REG1A_ADDR    0xc8
#define CMD_WRITE_REG1B_ADDR    0xcc
#define CMD_WRITE_REG1C_ADDR    0xd0
#define CMD_WRITE_REG1D_ADDR    0xd4
#define CMD_WRITE_REG1E_ADDR    0xd8
#define CMD_WRITE_REG1F_ADDR    0xdc
#define CMD_WRITE_REG20_ADDR    0xe0
#define CMD_WRITE_REG21_ADDR    0xe4
#define CMD_WRITE_REG22_ADDR    0xe8
#define CMD_WRITE_REG23_ADDR    0xec
#define CMD_WRITE_REG24_ADDR    0xf0
#define CMD_WRITE_REG25_ADDR    0xf4
#define CMD_WRITE_REG26_ADDR    0xf8
#define CMD_WRITE_REG27_ADDR    0xfc

#define SPI_BUFFER_SIZE     170
#define SPI_TX_BUF_SIZE     (((CMD_WRITE_REG27_ADDR-CMD_WRITE_HEAD_ADDR) / 4) + 1) * 2
#define SPI_RX_BUF_SIZE     (((CMD_READ_REGF_ADDR-CMD_READ_REG0_ADDR) / 4) + 1) * 2

#define CHK_HY              0x00000080
#define CHK_H1              0x00000040
#define CHK_LN              0x00000020
#define CHK_CMD             0x00000004
#define CHK_ID              0x00000008


#define SPI_BYPASS_EN       0x00001000
#define SPI_BYPASS_END      0x00002000


#define REORDER16(arg)      ((uint16_t)(arg<<8 | arg>>8))


#define SYSTEM_LINUX


void hub_init(void);

void hub_deinit(void);

int Xil_SPI_In32(uint32_t phyaddr);

void Xil_SPI_Out32(uint32_t phyaddr, uint32_t val);


extern bool rece_queue_has_nonce(uint8_t spi_id, uint32_t timeout_us);

extern void read_nonce_buffer(uint8_t spi_id, uint8_t* buf8, uint32_t len_cfg);

extern int send_job_queue(uint8_t spi_id, uint8_t* tx_buf8, uint8_t* rx_buf8, uint32_t len_cfg, uint32_t last_job);

extern int do_spi_cmd(uint8_t spi_id, uint8_t* tx_buf8, uint8_t* rx_buf8, uint32_t len_cfg);

int hub_spi_init(uint8_t spi_id, uint8_t chip_num);

void hub_spi_reset(uint8_t spi_id);

void hub_spi_clean_chain(uint32_t spi_id);

void hub_set_spi_speed(uint8_t spi_id, int select);

void hub_set_power_en(uint8_t chain_id, int value);

void hub_set_start_en(uint8_t chain_id, int value);

bool hub_set_reset(uint8_t chain_id, int value);

void hub_set_led(uint8_t chain_id, int mode);

bool hub_set_vid(uint8_t chain_id, int vid);
void hub_set_vid_vid(uint8_t chain_id, int vid);
void hub_set_vid_uart_select(uint8_t spi_id);

void hub_set_pwm(uint8_t fan_id, int frequency, int duty);

int hub_get_plug(uint8_t chain_id);

void hub_set_green_led(int mode);

void hub_set_red_led(int mode);

int hub_get_button(void);

int enable_auto_nonce(uint8_t chain_id, uint16_t cmd08_cmd, uint32_t len_cfg);

int disable_auto_nonce(uint8_t chain_id);

void enable_auto_cmd0a(uint8_t spi_id, uint32_t threshold, uint32_t msb, uint32_t lsb, uint32_t large_en, uint32_t mode );//mode : 1 only cmd0a;0 cmd08 follows cmd0a

void disable_auto_cmd0a(uint8_t spi_id, uint32_t threshold, uint32_t msb, uint32_t lsb, uint32_t large_en, uint32_t mode );//mode : 1 only cmd0a;0 cmd08 follows cmd0a

void init_hub_gpio(void);



//#define XPAR_MCOMPAT_PERIPHERAL_0_S00_AXI_BASEADDR 0x43C00000

#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG0_OFFSET 0
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG1_OFFSET 4
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG2_OFFSET 8
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG3_OFFSET 12
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG4_OFFSET 16
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG5_OFFSET 20
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG6_OFFSET 24
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG7_OFFSET 28
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG8_OFFSET 32
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG9_OFFSET 36
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG10_OFFSET 40
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG11_OFFSET 44
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG12_OFFSET 48
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG13_OFFSET 52
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG14_OFFSET 56
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG15_OFFSET 60
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG16_OFFSET 64
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG17_OFFSET 68
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG18_OFFSET 72
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG19_OFFSET 76
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG20_OFFSET 80
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG21_OFFSET 84
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG22_OFFSET 88
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG23_OFFSET 92
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG24_OFFSET 96
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG25_OFFSET 100
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG26_OFFSET 104
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG27_OFFSET 108
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG28_OFFSET 112
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG29_OFFSET 116
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG30_OFFSET 120
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG31_OFFSET 124
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG32_OFFSET 128
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG33_OFFSET 132
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG34_OFFSET 136
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG35_OFFSET 140
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG36_OFFSET 144
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG37_OFFSET 148
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG38_OFFSET 152
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG39_OFFSET 156
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG40_OFFSET 160
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG41_OFFSET 164
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG42_OFFSET 168
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG43_OFFSET 172
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG44_OFFSET 176
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG45_OFFSET 180
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG46_OFFSET 184
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG47_OFFSET 188
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG48_OFFSET 192
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG49_OFFSET 196
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG50_OFFSET 200
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG51_OFFSET 204
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG52_OFFSET 208
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG53_OFFSET 212
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG54_OFFSET 216
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG55_OFFSET 220
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG56_OFFSET 224
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG57_OFFSET 228
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG58_OFFSET 232
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG59_OFFSET 236
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG60_OFFSET 240
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG61_OFFSET 244
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG62_OFFSET 248
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG63_OFFSET 252
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG64_OFFSET 256
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG65_OFFSET 260
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG66_OFFSET 264
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG67_OFFSET 268
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG68_OFFSET 272
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG69_OFFSET 276
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG70_OFFSET 280
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG71_OFFSET 284
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG72_OFFSET 288
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG73_OFFSET 292
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG74_OFFSET 296
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG75_OFFSET 300
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG76_OFFSET 304
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG77_OFFSET 308
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG78_OFFSET 312
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG79_OFFSET 316
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG80_OFFSET 320
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG81_OFFSET 324
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG82_OFFSET 328
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG83_OFFSET 332
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG84_OFFSET 336
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG85_OFFSET 340
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG86_OFFSET 344
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG87_OFFSET 348
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG88_OFFSET 352
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG89_OFFSET 356
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG90_OFFSET 360
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG91_OFFSET 364
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG92_OFFSET 368
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG93_OFFSET 372
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG94_OFFSET 376
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG95_OFFSET 380
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG96_OFFSET 384
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG97_OFFSET 388
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG98_OFFSET 392
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG99_OFFSET 396
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG100_OFFSET 400
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG101_OFFSET 404
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG102_OFFSET 408
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG103_OFFSET 412
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG104_OFFSET 416
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG105_OFFSET 420
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG106_OFFSET 424
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG107_OFFSET 428
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG108_OFFSET 432
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG109_OFFSET 436
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG110_OFFSET 440
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG111_OFFSET 444
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG112_OFFSET 448
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG113_OFFSET 452
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG114_OFFSET 456
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG115_OFFSET 460
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG116_OFFSET 464
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG117_OFFSET 468
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG118_OFFSET 472
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG119_OFFSET 476
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG120_OFFSET 480
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG121_OFFSET 484
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG122_OFFSET 488
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG123_OFFSET 492
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG124_OFFSET 496
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG125_OFFSET 500
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG126_OFFSET 504
#define MCOMPAT_PERIPHERAL_S00_AXI_SLV_REG127_OFFSET 508

/* DRV_OPI */

#define AX_CMD_SYNC_HEAD	0xA55A
#define CUSTOM_SYNC_HEAD	0x6996

#define OPI_SPI_TIMEOUT     100

#define OPI_STATUS_SUC      0
#define OPI_STATUS_EOR      1

#define OPI_SET_POWER_EN    (0x11)
#define OPI_SET_STARR_EN    (0x12)
#define OPI_SET_RESET       (0x13)
#define OPI_SET_LED         (0x14)
#define OPI_GET_PLUG        (0x15)
#define OPI_SET_VID         (0x16)
#define OPI_SET_PWM         (0x17)
#define OPI_SET_SPI_SPEED   (0x18)
#define OPI_POWER_ON        (0x21)
#define OPI_POWER_DOWN      (0x22)
#define OPI_POWER_RESET     (0x23)



#define OPI_HI_BYTE(a)      ((uint8_t)(((a) >> 8) & 0xff))
#define OPI_LO_BYTE(a)      ((uint8_t)(((a) >> 0) & 0xff))

#define OPI_MAKE_WORD(a, b)	(uint16_t)((((a) & 0xff) << 8) | (( (a) & 0xff) << 0))


bool opi_spi_read_write(uint8_t chain_id, uint8_t *txbuf, uint8_t *rxbuf, int len);

bool opi_send_command(uint8_t chain_id, uint8_t cmd, uint8_t chip_id, uint8_t *buff, int len);

bool opi_poll_result(uint8_t chain_id, uint8_t cmd, uint8_t chip_id, uint8_t *buff, int len);


void opi_set_power_en(unsigned char chain_id, int val);

void opi_set_start_en(unsigned char chain_id, int val);

bool opi_set_reset(unsigned char chain_id, int val);

void opi_set_led(unsigned char chain_id, int val);

int opi_get_plug(unsigned char chain_id);

bool opi_set_vid(unsigned char chain_id, int vid);

void opi_set_pwm(unsigned char fan_id, int frequency, int duty);


bool opi_chain_power_on(unsigned char chain_id);

bool opi_chain_power_down(unsigned char chain_id);

bool opi_chain_hw_reset(unsigned char chain_id);

bool opi_chain_power_on_all(void);

bool opi_chain_power_down_all(void);


void opi_set_spi_speed(unsigned char chain_id, int index);


/* DRV_SPI */

void spi_send_data(ZYNQ_SPI_T *spi, unsigned char *buf, int len);
void spi_recv_data(ZYNQ_SPI_T *spi, unsigned char *buf, int len);

void spi_send_data_in_word(ZYNQ_SPI_T *spi, unsigned char *buf, int len);
void spi_recv_data_in_word(ZYNQ_SPI_T *spi, unsigned char *buf, int len);

bool spi_send_command(ZYNQ_SPI_T *spi, unsigned char cmd, unsigned char chip_id, unsigned char *buff, int len);
bool spi_poll_result(ZYNQ_SPI_T *spi, unsigned char cmd, unsigned char chip_id, unsigned char *buff, int len);


void init_spi_gpio(int chain_num);
void exit_spi_gpio(int chain_num);


void spi_set_power_en(unsigned char chain_id, int val);
void spi_set_start_en(unsigned char chain_id, int val);
bool spi_set_reset(unsigned char chain_id, int val);
void spi_set_led(unsigned char chain_id, int val);
bool spi_set_vid(unsigned char chain_id, int vid);
int spi_get_plug(unsigned char chain_id);

void spi_set_spi_speed(unsigned char chain_id, int index);


/* DRV_ZYNQ */

bool zynq_chain_power_on(unsigned char chain_id);

bool zynq_chain_power_down(unsigned char chain_id);

bool zynq_chain_hw_reset(unsigned char chain_id);

bool zynq_chain_power_on_all(void);

bool zynq_chain_power_down_all(void);


/* HUB_CMD */

bool init_hub_cmd(int chain_num, int chip_num);

bool exit_hub_cmd(int chain_num);

bool hub_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out);

int hub_cmd_bist_start(unsigned char chain_id, unsigned char chip_id);

bool hub_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id);

bool hub_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id);

bool hub_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

bool hub_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

bool hub_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out);

bool hub_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len);

bool hub_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len);

bool hub_cmd_auto_nonce(unsigned char chain_id, int mode, int len);

bool hub_cmd_read_nonce(unsigned char chain_id, unsigned char *res, int len);

//bool hub_cmd_get_temp(mcompat_fan_temp_s *fan_temp_ctrl);

bool hub_get_chain_temp(unsigned char chain_id, short tmp_hi, short tmp_lo, short tmp_avg);


/* OPI_CMD */

bool init_opi_cmd(void);

bool exit_opi_cmd(void);

bool opi_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out);

int opi_cmd_bist_start(unsigned char chain_id, unsigned char chip_id);

bool opi_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id);

bool opi_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id);

bool opi_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

bool opi_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

bool opi_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out);

bool opi_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len);

bool opi_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len);


/* SPI_CMD */

bool init_spi_cmd(int chain_num);

bool exit_spi_cmd(int chain_num);

bool spi_cmd_reset(unsigned char chain_id, unsigned char chip_id, unsigned char *in, unsigned char *out);

int spi_cmd_bist_start(unsigned char chain_id, unsigned char chip_id);

bool spi_cmd_bist_collect(unsigned char chain_id, unsigned char chip_id);

bool spi_cmd_bist_fix(unsigned char chain_id, unsigned char chip_id);

bool spi_cmd_write_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

bool spi_cmd_read_register(unsigned char chain_id, unsigned char chip_id, unsigned char *reg, int len);

bool spi_cmd_read_write_reg0d(unsigned char chain_id, unsigned char chip_id, unsigned char *in, int len, unsigned char *out);

bool spi_cmd_read_result(unsigned char chain_id, unsigned char chip_id, unsigned char *res, int len);

bool spi_cmd_write_job(unsigned char chain_id, unsigned char chip_id, unsigned char *job, int len);


/* UTIL */

unsigned short CRC16_2(unsigned char* pchMsg, unsigned short wDataLen);


/* OPI_H3 */

#define SW_PORTC_IO_BASE 0x01c20800
#define GPIO_BANK(pin)	((pin) >> 5)
#define GPIO_CFG_INDEX(pin)	(((pin) & 0x1F) >> 3)
#define GPIO_CFG_OFFSET(pin)	((((pin) & 0x1F) & 0x7) << 2)
#define GPIO_NUM(pin)	((pin) & 0x1F)
#define GPIO_PUL_INDEX(pin)	(((pin) & 0x1F )>> 4)
#define GPIO_PUL_OFFSET(pin)	(((pin) & 0x0F) << 1)

struct sunxi_gpio {
	unsigned int cfg[4];
	unsigned int dat;
	unsigned int drv[2];
	unsigned int pull[2];
};

struct sunxi_gpio_int {
	unsigned int cfg[3];
	unsigned int ctl;
	unsigned int sta;
	unsigned int deb;
};

struct sunxi_gpio_reg {
	struct sunxi_gpio gpio_bank[9];
	unsigned char res[0xbc];
	struct sunxi_gpio_int gpio_int;
};

typedef struct
{
	uint8_t mode;
	uint8_t bits;
	uint32_t speed;
	uint16_t delay;
} spi_config_t;

#define PA12 12
#define PA11 11
#define PA6  6
#define PA0  0
#define PA1  1
#define PA3  3
#define PC0  64
#define PC1  65
#define PC2  66
#define PA19 19
#define PA7  7
#define PA8  8
#define PA9  9
#define PA10 10
#define PA20 20

#define PA13 13
#define PA14 14
#define PD14 110
#define PC4  68
#define PC7  71
#define PA2  2
#define PC3  67
#define PA21 21
#define PA18 18
#define PG8  200
#define PG9  201
#define PG6  198
#define PG7  199


#define PA15 15
#define PL10 362
#define PL3 355

#define _3   12
#define _5   11
#define _7   6
#define _8   13
#define _10  14
#define _11  0
#define _12  110
#define _13  1
#define _15  3
#define _16  68
#define _18  71
#define _19  64
#define _21  65
#define _22  2
#define _23  66
#define _24  67
#define _26  21
#define _27  19
#define _28  18
#define _29  7
#define _31  8
#define _32  200
#define _33  9
#define _35  10
#define _36  201
#define _37  20
#define _38  198
#define _40  199

#define STATUS_LED 15
#define POWER_LED  362
#define POWER_KEY 355

#define HIGH 1
#define LOW 0

#define INPUT 0
#define OUTPUT 1

#define PUTDOWM 2
#define PUTUP 1


int gpio_init(void);
int gpio_setcfg(unsigned int pin, unsigned int p1);
int gpio_getcfg(unsigned int pin);
int gpio_output(unsigned int pin, unsigned int p1);
int gpio_pullup(unsigned int pin, unsigned int p1);
int gpio_input(unsigned int pin);
int i2c_open(char *dev, uint8_t address);
int i2c_close(int fd);
int i2c_send(int fd, uint8_t *buf, uint8_t num_bytes);
int i2c_read(int fd, uint8_t *buf, uint8_t num_bytes);
int spi_open(char *dev, spi_config_t config);
int spi_close(int fd);
int spi_xfer(int fd, uint8_t *tx_buf, uint8_t tx_len, uint8_t *rx_buf, uint8_t rx_len);
int spi_read(int fd, uint8_t *rx_buf, uint8_t rx_len);
int spi_write(int fd, uint8_t *tx_buffer, uint8_t tx_len);
void delay(unsigned int howLong);


/* OPI_SPI */

#define PIN_SPI_A0		_11
#define PIN_SPI_A1		_12
#define PIN_SPI_A2		_13

#define PIN_SPI_E1		_22


#define SPI_DEVICE_TEMPLATE		"/dev/spidev%d.%d"
#define DEFAULT_SPI_BUS			1
#define DEFAULT_SPI_CS_LINE		0
#define DEFAULT_SPI_MODE		SPI_MODE_1
#define DEFAULT_SPI_BITS_PER_WORD	16
#define DEFAULT_SPI_SPEED		1500000
#define DEFAULT_SPI_DELAY_USECS		0


struct spi_config {
	int bus;
	int cs_line;
	uint8_t mode;
	uint32_t speed;
	uint8_t bits;
	uint16_t delay;
};

struct spi_ctx {
	int fd;
	int power_en;
	int start_en;
	int reset;
	int led;
	int plug;
	int id;
	struct spi_config config;
};


void opi_spi_init(void);

void opi_spi_exit(void);

//void opi_set_spi_speed(uint32_t speed);

bool opi_spi_transfer(uint8_t id, uint16_t *txbuf, uint16_t *rxbuf, int len);

#endif /* _DM_COMPAT_H_ */

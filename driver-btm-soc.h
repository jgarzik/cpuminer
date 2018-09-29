/*
 * Copyright 2016-2017 Fazio Bai <yang.bai@bitmain.com>
 * Copyright 2016-2017 Clement Duan <kai.duan@bitmain.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef __DRIVER_BTM_SOC_H__
#define __DRIVER_BTM_SOC_H__

#include "config.h"
//FPGA rgister Address Map
#define HARDWARE_VERSION                (0x00000000/sizeof(int))
#define FAN_SPEED                       (0x00000004/sizeof(int))
#define HASH_ON_PLUG                    (0x00000008/sizeof(int))
#define BUFFER_SPACE                    (0x0000000c/sizeof(int))
#define RETURN_NONCE                    (0x00000010/sizeof(int))
#define NONCE_NUMBER_IN_FIFO            (0x00000018/sizeof(int))
#define NONCE_FIFO_INTERRUPT            (0x0000001c/sizeof(int))
#define TEMPERATURE_0_3                 (0x00000020/sizeof(int))
#define TEMPERATURE_4_7                 (0x00000024/sizeof(int))
#define TEMPERATURE_8_11                (0x00000028/sizeof(int))
#define TEMPERATURE_12_15               (0x0000002c/sizeof(int))
#define IIC_COMMAND                     (0x00000030/sizeof(int))
#define RESET_HASHBOARD_COMMAND         (0x00000034/sizeof(int))
#define BMC_CMD_COUNTER                 (0x00000038/sizeof(int))
#define TW_WRITE_COMMAND                (0x00000040/sizeof(int))
#define QN_WRITE_DATA_COMMAND           (0x00000080/sizeof(int))
#define FAN_CONTROL                     (0x00000084/sizeof(int))
#define TIME_OUT_CONTROL                (0x00000088/sizeof(int))
#define TICKET_MASK_FPGA                (0x0000008c/sizeof(int))
#define HASH_COUNTING_NUMBER_FPGA       (0x00000090/sizeof(int))
#define SNO                             (0x00000094/sizeof(int))
#define BC_WRITE_COMMAND                (0x000000c0/sizeof(int))
#define BC_COMMAND_BUFFER               (0x000000c4/sizeof(int))
#define FPGA_CHIP_ID_ADDR               (0x000000f0/sizeof(int))
#define CRC_ERROR_CNT_ADDR              (0x000000f8/sizeof(int))
#define DHASH_ACC_CONTROL               (0x00000100/sizeof(int))
#define COINBASE_AND_NONCE2_LENGTH      (0x00000104/sizeof(int))
#define WORK_NONCE_2                    (0x00000108/sizeof(int))
#define NONCE2_AND_JOBID_STORE_ADDRESS  (0x00000110/sizeof(int))
#define MERKLE_BIN_NUMBER               (0x00000114/sizeof(int))
#define JOB_START_ADDRESS               (0x00000118/sizeof(int))
#define JOB_LENGTH                      (0x0000011c/sizeof(int))
#define JOB_DATA_READY                  (0x00000120/sizeof(int))
#define JOB_ID                          (0x00000124/sizeof(int))
#define BLOCK_HEADER_VERSION            (0x00000130/sizeof(int))
#define TIME_STAMP                      (0x00000134/sizeof(int))
#define TARGET_BITS                     (0x00000138/sizeof(int))
#define PRE_HEADER_HASH                 (0x00000140/sizeof(int))

//FPGA registers bit map
//QN_WRITE_DATA_COMMAND
#define RESET_HASH_BOARD                (1 << 31)
#define RESET_ALL                       (1 << 23)
#define CHAIN_ID(id)                    (id << 16)
#define RESET_FPGA                      (1 << 15)
#define RESET_TIME(time)                (time << 0)
#define TIME_OUT_VALID                  (1 << 31)
//RETURN_NONCE
#define WORK_ID_OR_CRC                  (1 << 31)
#define WORK_ID_OR_CRC_VALUE(value)     ((value >> 16) & 0x7fff)
#define NONCE_INDICATOR                 (1 << 7)
#define CHAIN_NUMBER(value)             (value & 0xf)
#define REGISTER_DATA_CRC(value)        ((value >> 24) & 0x7f)
//BC_WRITE_COMMAND
#define BC_COMMAND_BUFFER_READY         (1 << 31)
#define BC_COMMAND_EN_CHAIN_ID          (1 << 23)
#define BC_COMMAND_EN_NULL_WORK         (1 << 22)
//NONCE2_AND_JOBID_STORE_ADDRESS
#define JOB_ID_OFFSET                   (0x0/sizeof(int))
#define HEADER_VERSION_OFFSET           (0x4/sizeof(int))
#define NONCE2_L_OFFSET                 (0x8/sizeof(int))
#define NONCE2_H_OFFSET                 (0xc/sizeof(int))
#define MIDSTATE_OFFSET                 0x20
//DHASH_ACC_CONTROL
#define VIL_MODE                        (1 << 15)
#define VIL_MIDSTATE_NUMBER(value)      ((value & 0x0f) << 8)
#define NEW_BLOCK                       (1 << 7)
#define RUN_BIT                         (1 << 6)
#define OPERATION_MODE                  (1 << 5)
//NONCE_FIFO_INTERRUPT
#define FLUSH_NONCE3_FIFO               (1 << 16)


//ASIC macro define
//ASIC register address
#define SOC_VERSION              1
#define CHIP_ADDRESS            0x0
#define GOLDEN_NONCE_COUNTER    0x8
#define PLL_PARAMETER           0xc
#define START_NONCE_OFFSET      0x10
#define HASH_COUNTING_NUMBER    0x14
#define TICKET_MASK             0x18
#define MISC_CONTROL            0x1c
#define GENERAL_I2C_COMMAND     0X20

//ASIC command
#define SET_ADDRESS             0x1
#define SET_PLL_DIVIDER2        0x2
#define PATTERN_CONTROL         0x3
#define GET_STATUS              0x4
#define CHAIN_INACTIVE          0x5
#define SET_BAUD_OPS            0x6
#define SET_PLL_DIVIDER1        0x7
#define SET_CONFIG              0x8
#define COMMAND_FOR_ALL         0x80
//other ASIC macro define
#define MAX_BAUD_DIVIDER        26
#define DEFAULT_BAUD_DIVIDER    26
#define VIL_COMMAND_TYPE        (0x02 << 5)
#define VIL_ALL                 (0x01 << 4)
#define PAT                     (0x01 << 7)
#define GRAY                    (0x01 << 6)
#define INV_CLKO                (0x01 << 5)
#define LPD                     (0x01 << 4)
#define GATEBCLK                (0x01 << 7)
#define RFS                     (0x01 << 6)
#define MMEN                    (0x01 << 7)
#define TFS(x)                  ((x & 0x03) << 5)


// Pic
#define PIC_FLASH_POINTER_START_ADDRESS_H   0x03
#define PIC_FLASH_POINTER_START_ADDRESS_L   0x00
#define PIC_FLASH_POINTER_END_ADDRESS_H     0x0f
#define PIC_FLASH_POINTER_END_ADDRESS_L     0x7f
#define PIC_FLASH_LENGTH                    (((unsigned int)PIC_FLASH_POINTER_END_ADDRESS_H<<8 + PIC_FLASH_POINTER_END_ADDRESS_L) - ((unsigned int)PIC_FLASH_POINTER_START_ADDRESS_H<<8 + PIC_FLASH_POINTER_START_ADDRESS_L) + 1)
#define PIC_FLASH_SECTOR_LENGTH             32
#define PIC_SOFTWARE_VERSION_LENGTH         1
#define PIC_VOLTAGE_TIME_LENGTH             6
#define PIC_COMMAND_1                       0x55
#define PIC_COMMAND_2                       0xaa
#define SET_PIC_FLASH_POINTER               0x01
#define SEND_DATA_TO_IIC                    0x02    // just send data into pic's cache
#define READ_DATA_FROM_IIC                  0x03
#define ERASE_IIC_FLASH                     0x04    // erase 32 bytes one time
#define WRITE_DATA_INTO_PIC                 0x05    // tell pic write data into flash from cache
#define JUMP_FROM_LOADER_TO_APP             0x06
#define RESET_PIC                           0x07
#define GET_PIC_FLASH_POINTER               0x08
#define ERASE_PIC_APP_PROGRAM               0x09
#define SET_VOLTAGE                         0x10
#define SET_VOLTAGE_TIME                    0x11
#define SET_HASH_BOARD_ID                   0x12
#define GET_HASH_BOARD_ID                   0x13
#define SET_HOST_MAC_ADDRESS                0x14
#define ENABLE_VOLTAGE                      0x15
#define SEND_HEART_BEAT                     0x16
#define GET_PIC_SOFTWARE_VERSION            0x17
#define GET_VOLTAGE                         0x18
#define GET_DATE                            0x19
#define GET_WHICH_MAC                       0x20
#define GET_MAC                             0x21
#define WR_TEMP_OFFSET_VALUE                0x22
#define RD_TEMP_OFFSET_VALUE                0x23

//diff freq
#define PIC_FLASH_POINTER_FREQ_START_ADDRESS_H   0x0F
#define PIC_FLASH_POINTER_FREQ_START_ADDRESS_L   0xA0
#define PIC_FLASH_POINTER_FREQ_END_ADDRESS_H     0x0f
#define PIC_FLASH_POINTER_FREQ_END_ADDRESS_L     0xDF
#define FREQ_MAGIC                               0x7D

// BAD CORE NUM
#define PIC_FLASH_POINTER_BADCORE_START_ADDRESS_H   0x0F
#define PIC_FLASH_POINTER_BADCORE_START_ADDRESS_L   0x80
#define PIC_FLASH_POINTER_BADCORE_END_ADDRESS_H     0x0f
#define PIC_FLASH_POINTER_BADCORE_END_ADDRESS_L     0x9F
#define BADCORE_MAGIC                            0x23   // magic number for bad core num

#define HEART_BEAT_TIME_GAP                 10      // 10s
#define IIC_READ                            (1 << 25)
#define IIC_WRITE                           (~IIC_READ)
#define IIC_REG_ADDR_VALID                  (1 << 24)
//#define IIC_ADDR_HIGH_4_BIT                   (0x0A << 20)
#define IIC_CHAIN_NUMBER(x)                 ((x & 0x0f) << 16)
#define IIC_REG_ADDR(x)                     ((x & 0xff) << 8)

// AT24C02
#define AT24C02_ADDRESS     0x50
#define EEPROM_LENGTH       256
#define HASH_ID_ADDR        0x80
#define VOLTAGE_ADDR        0x90
#define SENSOR_OFFSET_ADDR  0x98
#define VOLTAGE_SET_TIME    0xA0
#define VOLTAGE_SET_TIME    0xA0
#define FREQ_BADCORE_ADDR   0x00    // 128 bytes   0 - 0x7F

//other FPGA macro define
#define TOTAL_LEN                       0x160
#define FPGA_MEM_TOTAL_LEN              (16*1024*1024)  // 16M bytes
#define HARDWARE_VERSION_VALUE          0xC501
#define NONCE2_AND_JOBID_STORE_SPACE    (2*1024*1024)   // 2M bytes
#define NONCE2_AND_JOBID_STORE_SPACE_ORDER  9           // for 2M bytes space
#define JOB_STORE_SPACE                 (1 << 16)       // for 64K bytes space
#define JOB_START_SPACE                 (1024*8)        // 8K bytes
#define JOB_START_ADDRESS_ALIGN         32              // JOB_START_ADDRESS need 32 bytes aligned
#define NONCE2_AND_JOBID_ALIGN          64              // NONCE2_AND_JOBID_STORE_SPACE need 64 bytes aligned
#define MAX_TIMEOUT_VALUE               0x1ffff         // defined in TIME_OUT_CONTROL
#define MAX_NONCE_NUMBER_IN_FIFO        0x1ff           // 511 nonce
#define NONCE_DATA_LENGTH               4               // 4 bytes
#define REGISTER_DATA_LENGTH            4               // 4 bytes
#define TW_WRITE_COMMAND_LEN            52
#define TW_WRITE_COMMAND_LEN_VIL        52
#define NEW_BLOCK_MARKER                0x11
#define NORMAL_BLOCK_MARKER             0x01

// ATTENTION: if MEM size is changed, must change this micro definition too!!!   use MAX size (BYTE) - 16 MB as FPGA start memory address
#define PHY_MEM_NONCE2_JOBID_ADDRESS_XILINX_1GB         ((1024-16)*1024*1024)
#define PHY_MEM_NONCE2_JOBID_ADDRESS_XILINX_512MB       ((512-16)*1024*1024)        // XILINX use 512MB memory
#define PHY_MEM_NONCE2_JOBID_ADDRESS_XILINX_256MB       ((256-16)*1024*1024)        // XILINX use 256MB memory

#define PHY_MEM_NONCE2_JOBID_ADDRESS_C5         ((1024-16)*1024*1024)
extern unsigned int PHY_MEM_NONCE2_JOBID_ADDRESS;

#define PHY_MEM_JOB_START_ADDRESS_1     (PHY_MEM_NONCE2_JOBID_ADDRESS + NONCE2_AND_JOBID_STORE_SPACE)
#define PHY_MEM_JOB_START_ADDRESS_2     (PHY_MEM_JOB_START_ADDRESS_1 + JOB_STORE_SPACE)

//#include "miner_type.h"     // use setminertype to define miner type in this file instead of belows!!!
//#define R4        // if defined , for R4  63 chips
//#define S9_PLUS   // if defined , for T9  57 chips
//#define S9_63     // if defined , for S9  63 chips
//#define T9_18     // if defined , for T9+  18 chips

#define RESET_KEEP_TIME     3   // keep reset signal for 1 secnods
#undef USE_OPENCORE_ONEBYONE    // if defined, we will use open core one by one, do 114 times on open core for each chain!  but NOT WORKS!??
#undef ENABLE_REGISTER_CRC_CHECK    //if defined, will drop the register buffer with crc error!
#define REBOOT_TEST_ONCE_1HOUR  //if defined, will check hashrate after 1 hour, and reboot only once
#define ENABLE_FINAL_TEST_WITHOUT_REBOOT    // when REBOOT_TEST_ONCE_1HOUR enabeld and this defined, the miner will not reboot after test for 1 hours, then we can save time. test system will treat these miners as good with green color.
#define DISABLE_FINAL_TEST      //if defined, it will set rebootTestNum=0 and restartNum=2 to indicate the the miner fw is in normal user mode , not test mode
#define DISABLE_SHOWX_ENABLE_XTIMES // if defined, will disable x show on web UI, but will enable x times counter in 1 mins
#define FASTER_TESTPATTEN   // will use 9% timeout to test patten
#undef USE_OPENCORE_TWICE
#define ENABLE_REINIT_WHEN_TESTFAILED       //if defined, when test failed on patten, we set a flag in file, and will use highest voltage according to the limit rules of power and hashrate.
#define RESET_HASHBOARD_TIME            15
#define ENABLE_CHECK_PIC_FLASH_ADDR     // if enabled, will check PIC FLASH ADDR value , set and read back to compare from PIC
#define ENABLE_RESTORE_PIC_APP          // if enabled, will restore PIC APP when the version is not correct!!!

#ifdef R4
#define USE_N_OFFSET_FIX_TEMP   // if defined, we will use n and offset to fix temp value
#define EXTEND_TEMP_MODE        // if defined, we set temp value area from -64 to 191 as extended temp
#define ENABLE_HIGH_VOLTAGE_OPENCORE

#define PIC_VERSION                     0x03

#define CHAIN_ASIC_NUM                  63

#define R4_MAX_VOLTAGE_C5                   890
#define R4_MAX_VOLTAGE_XILINX               910

#define FIX_BAUD_VALUE                  1
#define UPRATE_PERCENT                  1   // means we need reserved more 1% rate

#define HIGHEST_VOLTAGE_LIMITED_HW      940 //measn the largest voltage, hw can support
#define USE_NEW_RESET_FPGA
#undef USE_PREINIT_OPENCORE // if defined, we will open core at first ,then get asicnum and do other init process
#endif

#ifdef S9_PLUS
#define ENABLE_HIGH_VOLTAGE_OPENCORE

#define S9_PLUS_VOLTAGE2    //if defined, then it support S9+ new board with new voltage controller

#define PIC_VERSION                     0x03

#define CHAIN_ASIC_NUM                  57
#define USE_N_OFFSET_FIX_TEMP   // if defined, we will use n and offset to fix temp value
#define EXTEND_TEMP_MODE        // if defined, we set temp value area from -64 to 191 as extended temp
#define HIGHEST_VOLTAGE_LIMITED_HW      970 //measn the largest voltage, hw can support
#define FIX_BAUD_VALUE                  1
#define UPRATE_PERCENT                  2   // means we need reserved more 2% rate
#define USE_NEW_RESET_FPGA
#undef USE_PREINIT_OPENCORE // if defined, we will open core at first ,then get asicnum and do other init process
#endif

#ifdef S9_63
#define ENABLE_HIGH_VOLTAGE_OPENCORE

#define PIC_VERSION                     0x03

#define CHAIN_ASIC_NUM                  63
#define USE_N_OFFSET_FIX_TEMP   // if defined, we will use n and offset to fix temp value
#define EXTEND_TEMP_MODE        // if defined, we set temp value area from -64 to 191 as extended temp

#define FIX_BAUD_VALUE                  1
#define UPRATE_PERCENT                  1   // means we need reserved more 1% rate
#define HIGHEST_VOLTAGE_LIMITED_HW      940 //measn the largest voltage, hw can support
#define USE_NEW_RESET_FPGA
#undef USE_PREINIT_OPENCORE // if defined, we will open core at first ,then get asicnum and do other init process
#endif

#ifdef T9_18
#define ENABLE_HIGH_VOLTAGE_OPENCORE    // T9+ use this , will cause error on chips, because the voltage changing need a long time to balance

#define PIC_VERSION                     0x03

#define CHAIN_ASIC_NUM                  18
#define USE_N_OFFSET_FIX_TEMP   // if defined, we will use n and offset to fix temp value
#define EXTEND_TEMP_MODE        // if defined, we set temp value area from -64 to 191 as extended temp

#define FIX_BAUD_VALUE                  1
#define UPRATE_PERCENT                  2   // means we need reserved more 2% rate
#define HIGHEST_VOLTAGE_LIMITED_HW      930 //measn the largest voltage, hw can support
#define USE_NEW_RESET_FPGA
#undef USE_PREINIT_OPENCORE // if defined, we will open core at first ,then get asicnum and do other init process
#endif

#ifdef USE_PREINIT_OPENCORE
#define ENABLE_SET_TICKETMASK_BEFORE_TESTPATTEN     // a bug, do not know reason: asic ticket mask > 0 even after reset asic!!!
#else
#undef ENABLE_SET_TICKETMASK_BEFORE_TESTPATTEN      // a bug, do not know reason: asic ticket mask > 0 even after reset asic!!!
#endif

#define ASIC_TYPE           1387    // 1385 or  1387
#define CHIP_ADDR_INTERVAL  4   // fix chip address interval = 4 
#define DEFAULT_BAUD_VALUE  26
#define ASIC_CORE_NUM       114

#define BM1387_CORE_NUM     ASIC_CORE_NUM

// macro define about miner
#define BITMAIN_MAX_CHAIN_NUM           16
#define BITMAIN_MAX_FAN_NUM             8               // FPGA just can supports 8 fan
#define BITMAIN_DEFAULT_ASIC_NUM        64              // max support 64 ASIC on 1 HASH board
#define MIDSTATE_LEN                    32
#define DATA2_LEN                       12
#define MAX_RETURNED_NONCE_NUM          10
#define PREV_HASH_LEN                   32
#define MERKLE_BIN_LEN                  32
#define INIT_CONFIG_TYPE                0x51
#define STATUS_DATA_TYPE                0xa1
#define SEND_JOB_TYPE                   0x52
#define READ_JOB_TYPE                   0xa2
#define CHECK_SYSTEM_TIME_GAP           10000           // 10s
//fan

// BELOW IS ALL FOR DEBUG !!! normally all must be undefined!!!
#undef DEBUG_FORCE_REINIT   //defined to force to reinit with higher voltage
#undef DEBUG_KEEP_USE_PIC_VOLTAGE_WITHOUT_CHECKING_VOLTAGE_OF_SEARCHFREQ    // if defined, will read pic voltage at first , and use this voltage in mining as working voltage, ignore the backup voltage of search freq
#undef DEBUG_ENABLE_I2C_TIMEOUT_PROCESS // if defined, sw will process I2C timeout, but normally FPGA will process timeout, SW do not need this
#undef DEBUG_PRINT_T9_PLUS_PIC_HEART_INFO   // if defined, used to debug T9+ bug: pic heart cmd failed!
#undef DEBUG_PIC_UPGRADE        // if defined, we will force to write PIC program data once!
#undef DEBUG_KEEP_REBOOT_EVERY_ONE_HOUR     // if defined, keep reboot every one hour!!!  this is for R4 
#undef DEBUG_NOT_CHECK_FAN_NUM      // if defined, we will ignore fan number checking, will keep run even without any fan!!!
#undef DEBUG_WITHOUT_FREQ_VOLTAGE_LIMIT // if defined, we will not limit freq according to voltage!

#undef DEBUG_DOWN_VOLTAGE_TEST
#ifdef DEBUG_DOWN_VOLTAGE_TEST
#define DEBUG_DOWN_VOLTAGE_VALUE    10  // means down 0.1 V 
#endif

#undef DEBUG_XILINX_NONCE_NOTENOUGH // will disable mutex lock on read temp and send work, but will disable one chain's read temp
#ifdef DEBUG_XILINX_NONCE_NOTENOUGH
#define DISABLE_REG_CHAIN_INDEX     5   //disable which chain's read register
#endif

#undef DEBUG_OPENCORE_TWICE
#undef ENABLE_REINIT_MINING // if defined, will enable hashrate check in mining, and re-init if low hashrate.  
#undef DEBUG_REINIT // reinit per 2mins and will not do pre heat patten test
#undef DEBUG_REBOOT // reboot every 30mins, for test
#undef DEBUG_218_FAN_FULLSPEED  //for debug on 218, full speed on fan
#undef DISABLE_TEMP_PROTECT
#undef TWO_CHIP_TEMP_S9
#undef SHOW_BOTTOM_TEMP
#undef KEEP_TEMPFAN_LOG // if defined, will not clear old temp fan log info
#undef HIGH_TEMP_TEST_S9    //if defined, will use 120 degree as the high temp
#undef CAPTURE_PATTEN

#define CHECK_RT_IDEAL_RATE_PERCENT     85  // RT rate / ideal rate >= 85% will be OK, or need re init

typedef enum
{
    TEMP_POS_LOCAL=0,
    TEMP_POS_MIDDLE,
    TEMP_POS_BOTTOM,
    TEMP_POS_NUM=4, // always the last one, to identify the number of temp , must 4 bytes alignment
} TEMP_POSITION;

#ifdef R4
#define PWM_T   0   // 0 local temp,  1 middle temp,  2 bottom,  as above!!!

#define MIN_FAN_NUM                     1
#define MAX_FAN_SPEED                   3000
#define TEMP_INTERVAL                   2

// below are used for R4 on using one app to support C5 and XILINX board
extern int MIN_PWM_PERCENT;
extern int MID_PWM_PERCENT;
extern int MAX_PWM_PERCENT;
extern int MAX_TEMP;
extern int MAX_FAN_TEMP;
extern int MID_FAN_TEMP;
extern int MIN_FAN_TEMP;
extern int MAX_PCB_TEMP;
extern int MAX_FAN_PCB_TEMP;

#if PWM_T == 1
#define MIN_PWM_PERCENT_C5              20
#define MID_PWM_PERCENT_C5              60
#define MAX_PWM_PERCENT_C5              100
#define MAX_TEMP_C5                     125
#define MAX_FAN_TEMP_C5                 110
#define MID_FAN_TEMP_C5                 90
#define MIN_FAN_TEMP_C5                 60
#define MAX_PCB_TEMP_C5                 100 //  use middle to control fan, but use pcb temp to check to stop or not!
#define MAX_FAN_PCB_TEMP_C5             85  //90 use middle to control fan, but use pcb temp to check to stop or not!

#define MIN_PWM_PERCENT_XILINX          20
#define MID_PWM_PERCENT_XILINX          60
#define MAX_PWM_PERCENT_XILINX          100
#define MAX_TEMP_XILINX                 125
#define MAX_FAN_TEMP_XILINX             110
#define MID_FAN_TEMP_XILINX             90
#define MIN_FAN_TEMP_XILINX             60
#define MAX_PCB_TEMP_XILINX             100 //  use middle to control fan, but use pcb temp to check to stop or not!
#define MAX_FAN_PCB_TEMP_XILINX         85  //90 use middle to control fan, but use pcb temp to check to stop or not!
#else
#define MIN_PWM_PERCENT_C5              50
#define MID_PWM_PERCENT_C5              90
#define MAX_PWM_PERCENT_C5              100
#define MAX_TEMP_C5                     90
#define MAX_FAN_TEMP_C5                 75
#define MID_FAN_TEMP_C5                 65
#define MIN_FAN_TEMP_C5                 25
#define MAX_PCB_TEMP_C5                 90  //  use middle to control fan, but use pcb temp to check to stop or not!
#define MAX_FAN_PCB_TEMP_C5             85  //90 use middle to control fan, but use pcb temp to check to stop or not!

#define MIN_PWM_PERCENT_XILINX          30
#define MID_PWM_PERCENT_XILINX          70
#define MAX_PWM_PERCENT_XILINX          100
#define MAX_TEMP_XILINX                 90
#define MAX_FAN_TEMP_XILINX             75
#define MID_FAN_TEMP_XILINX             65
#define MIN_FAN_TEMP_XILINX             25
#define MAX_PCB_TEMP_XILINX             90  //  use middle to control fan, but use pcb temp to check to stop or not!
#define MAX_FAN_PCB_TEMP_XILINX         85  //90 use middle to control fan, but use pcb temp to check to stop or not!
#endif

#define TEMP_INTERVAL                   2

#define MID_PWM_ADJUST_FACTOR           ((MAX_PWM_PERCENT-MID_PWM_PERCENT)/(MAX_FAN_TEMP-MID_FAN_TEMP))
#define PWM_ADJUST_FACTOR               ((MID_PWM_PERCENT-MIN_PWM_PERCENT)/(MID_FAN_TEMP-MIN_FAN_TEMP))
#else
// below is for S9
#define PWM_T   1   // 0 local temp,  1 middle temp,  2 bottom,  as above!!!

#define MIN_FAN_NUM                     2
#define MAX_FAN_SPEED                   6000
#if PWM_T == 1
#define MIN_PWM_PERCENT                 0
#define MAX_PWM_PERCENT                 100

#ifdef HIGH_TEMP_TEST_S9
#define MAX_TEMP                        135 //125     135      145          release:135
#define MAX_FAN_TEMP                    120 // 115    125      135          release:120
#define MIN_FAN_TEMP                    70  //65       75        85            release:70
#define MAX_PCB_TEMP                    105 //100       105      110        release:105
#define MAX_FAN_PCB_TEMP                95  //95       100        105        release:95
#define MIN_FAN_PCB_TEMP                45  // Attention: MAX_FAN_PCB_TEMP - MIN_FAN_PCB_TEMP  =  MAX_FAN_TEMP - MIN_FAN_TEMP
#else
#ifdef TWO_CHIP_TEMP_S9
#define MAX_TEMP                        135 //125     135      145          release:135
#define MAX_FAN_TEMP                    120 // 115    125      135          release:120
#define MIN_FAN_TEMP                    70  //65       75        85            release:70
#define MAX_PCB_TEMP                    105 //100       105      110        release:105
#define MAX_FAN_PCB_TEMP                95  //95       100        105        release:95
#define MIN_FAN_PCB_TEMP                45  // Attention: MAX_FAN_PCB_TEMP - MIN_FAN_PCB_TEMP  =  MAX_FAN_TEMP - MIN_FAN_TEMP
#else
#define MAX_TEMP                        125 //125     135      145          release:125
#define MAX_FAN_TEMP                    90  // 115    125      135          release:115
#define MIN_FAN_TEMP                    40  //65       75        85            release:65
#define MAX_PCB_TEMP                    90  //100       105      110        release:95
#define MAX_FAN_PCB_TEMP                75  //95       100        105        release:85
#define MIN_FAN_PCB_TEMP                25  // Attention: MAX_FAN_PCB_TEMP - MIN_FAN_PCB_TEMP  =  MAX_FAN_TEMP - MIN_FAN_TEMP
#endif
#endif
#else
#define MIN_PWM_PERCENT                 20
#define MAX_PWM_PERCENT                 100
#define MAX_TEMP                        90
#define MAX_FAN_TEMP                    75
#define MIN_FAN_TEMP                    35
#define MAX_PCB_TEMP                    90  //  use middle to control fan, but use pcb temp to check to stop or not!
#endif
#define TEMP_INTERVAL                   2
#define PWM_ADJUST_FACTOR               ((MAX_PWM_PERCENT-MIN_PWM_PERCENT)/(MAX_FAN_TEMP-MIN_FAN_TEMP))
#endif

#ifdef HIGH_TEMP_TEST_S9
#define MIN_TEMP_CONTINUE_DOWN_FAN      110 // release: 90
#define MAX_TEMP_NEED_UP_FANSTEP        120 // release: 100   if temp is higher than 100, then we need make fan much faster
#else
#define MIN_TEMP_CONTINUE_DOWN_FAN      80  // release: 90
#define MAX_TEMP_NEED_UP_FANSTEP        85  // release: 100   if temp is higher than 100, then we need make fan much faster
#endif

#define PWM_SCALE                       50  //50:   1M=1us,      20KHz??
//25:   40KHz

#define PWM_ADJ_SCALE                   9/10
//use for hash test
#define TEST_DHASH 0
#define DEVICE_DIFF 8
//use for status check

#define MAX_TEMPCHIP_NUM        8   // support 8 chip has temp

#define MIN_FREQ                4   // 8:300M   6:250M      4:200M
#define MAX_FREQ                100 //850M
#define MAX_SW_TEMP_OFFSET      -15
#define BMMINER_VERSION         3   // 3 for auto freq,  1 or 2 for normal ( the old version is 0)

// for c5, bmminer will detect board type and use it.
#define RED_LED_DEV_C5 "/sys/class/leds/hps_led2/brightness"
#define GREEN_LED_DEV_C5 "/sys/class/leds/hps_led0/brightness"

// for xilinx, bmminer will detect board type and use it.
#define RED_LED_DEV_XILINX "/sys/class/gpio/gpio943/value"
#define GREEN_LED_DEV_XILINX "/sys/class/gpio/gpio944/value"

// S9 , T9,  R4    PIC PROGRAM
#define PIC_PROGRAM "/etc/config/hash_s8_app.txt"

// T9+  PIC PROGRAM
#define DSPIC33EP16GS202_PIC_PROGRAM "/etc/config/dsPIC33EP16GS202_app.txt"


#define TIMESLICE 60

#ifdef T9_18
#define IIC_ADDR_HIGH_4_BIT                 (0x04 << 20)
#define EEPROM_ADDR_HIGH_4_BIT              (0x0A << 20)
#define IIC_SELECT(x)                       ((x & 0x03) << 26)

unsigned int get_iic();
unsigned char set_iic(unsigned int data);
unsigned char T9_plus_write_pic_iic(bool read, bool reg_addr_valid, unsigned char reg_addr, unsigned char which_iic, unsigned char data);
int dsPIC33EP16GS202_jump_to_app_from_loader(unsigned char which_iic);
#else
#define IIC_ADDR_HIGH_4_BIT                 (0x0A << 20)
#endif


struct init_config
{
    uint8_t     token_type;
    uint8_t     version;
    uint16_t    length;
    uint8_t     reset                   :1;
    uint8_t     fan_eft                 :1;
    uint8_t     timeout_eft             :1;
    uint8_t     frequency_eft           :1;
    uint8_t     voltage_eft             :1;
    uint8_t     chain_check_time_eft    :1;
    uint8_t     chip_config_eft         :1;
    uint8_t     hw_error_eft            :1;
    uint8_t     beeper_ctrl             :1;
    uint8_t     temp_ctrl               :1;
    uint8_t     chain_freq_eft          :1;
    uint8_t     reserved1               :5;
    uint8_t     reserved2[2];
    uint8_t     chain_num;
    uint8_t     asic_num;
    uint8_t     fan_pwm_percent;
    uint8_t     temperature;
    uint16_t    frequency;
    uint8_t     voltage[2];
    uint8_t     chain_check_time_integer;
    uint8_t     chain_check_time_fractions;
    uint8_t     timeout_data_integer;
    uint8_t     timeout_data_fractions;
    uint32_t    reg_data;
    uint8_t     chip_address;
    uint8_t     reg_address;
    uint16_t    chain_min_freq;
    uint16_t    chain_max_freq;
    uint16_t    crc;
} __attribute__((packed, aligned(4)));



struct bitmain_soc_info
{
    cglock_t update_lock;

    uint8_t     data_type;
    uint8_t     version;
    uint16_t    length;
    uint8_t     chip_value_eft  :1;
    uint8_t     reserved1       :7;
    uint8_t     chain_num;
    uint16_t    reserved2;
    uint8_t     fan_num;
    uint8_t     temp_num;
    uint8_t     reserved3[2];
    uint32_t    fan_exist;
    uint32_t    temp_exist;
    uint16_t    diff;
    uint16_t    reserved4;
    uint32_t    reg_value;
    uint32_t    chain_asic_exist[BITMAIN_MAX_CHAIN_NUM][BITMAIN_DEFAULT_ASIC_NUM/32];
    uint32_t    chain_asic_status[BITMAIN_MAX_CHAIN_NUM][BITMAIN_DEFAULT_ASIC_NUM/32];
    uint8_t     chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
    uint8_t     temp[BITMAIN_MAX_CHAIN_NUM];
    uint8_t     fan_speed_value[BITMAIN_MAX_FAN_NUM];
    uint16_t    freq[BITMAIN_MAX_CHAIN_NUM];
    struct thr_info *thr;
    pthread_t read_nonce_thr;
    pthread_mutex_t lock;

    struct init_config soc_config;
    int pool_no;
    struct pool pool0;
    struct pool pool1;
    struct pool pool2;
    uint32_t pool0_given_id;
    uint32_t pool1_given_id;
    uint32_t pool2_given_id;

    uint16_t    crc;
} __attribute__((packed, aligned(4)));

struct part_of_job
{
    uint8_t     token_type;             // buf[0]
    uint8_t     version;
    uint16_t    reserved;
    uint32_t    length;                 // buf[1]
    uint8_t     pool_nu;                // buf[2]
    uint8_t     new_block       :1;
    uint8_t     asic_diff_valid :1;
    uint8_t     reserved1       :6;
    uint8_t     asic_diff;
    uint8_t     reserved2[1];
    uint32_t    job_id;                 // buf[3]
    uint32_t    bbversion;              // buf[4]
    uint8_t     prev_hash[32];          // buf[5] - buf[12]
    uint32_t    ntime;                  // buf[13]
    uint32_t    nbit;                   // buf[14]
    uint16_t    coinbase_len;           // buf[15]
    uint16_t    nonce2_offset;
    uint16_t    nonce2_bytes_num;       // 4 or 8 bytes // buf[16]
    uint16_t    merkles_num;
    uint64_t    nonce2_start_value; //nonce2 start calculate value. // buf[17] - buf[18]
};
//uint8_t   coinbase                        //this is variable
//uint8_t   merkle_bin[32] * merkles_num
//uint16_t  crc

struct nonce_content
{
    uint32_t    job_id;
    uint32_t    work_id;
    uint32_t    header_version;
    uint64_t    nonce2;
    uint32_t    nonce3;
    uint32_t    chain_num;
    uint8_t     midstate[MIDSTATE_LEN];
} __attribute__((packed, aligned(4)));

struct nonce
{
    uint8_t     token_type;
    uint8_t     version;
    uint16_t    length;
    uint16_t    valid_nonce_num;
    struct nonce_content nonce_cont[MAX_RETURNED_NONCE_NUM];
    uint16_t    crc;
} __attribute__((packed, aligned(4)));

struct all_parameters
{

    unsigned int    *current_job_start_address;
    unsigned int    pwm_value;
    unsigned int    chain_exist[BITMAIN_MAX_CHAIN_NUM];
    unsigned int    timeout;
    unsigned int    fan_exist_map;
    unsigned int    temp_sensor_map;
    unsigned int    nonce_error;
    unsigned int    chain_asic_exist[BITMAIN_MAX_CHAIN_NUM][8];
    unsigned int    chain_asic_status[BITMAIN_MAX_CHAIN_NUM][8];
    signed char     chain_asic_temp_num[BITMAIN_MAX_CHAIN_NUM]; // the real number of temp chip
    unsigned char   TempChipType[BITMAIN_MAX_CHAIN_NUM][MAX_TEMPCHIP_NUM];
    unsigned char   TempChipAddr[BITMAIN_MAX_CHAIN_NUM][MAX_TEMPCHIP_NUM];  // each temp chip's address: chip index*4, index start from 0
    int16_t         chain_asic_temp[BITMAIN_MAX_CHAIN_NUM][MAX_TEMPCHIP_NUM][TEMP_POS_NUM]; // 4 kinds of temp
    int16_t         chain_asic_maxtemp[BITMAIN_MAX_CHAIN_NUM][TEMP_POS_NUM];    // 4 kinds of temp
    int16_t         chain_asic_mintemp[BITMAIN_MAX_CHAIN_NUM][TEMP_POS_NUM];    // 4 kinds of temp
    int8_t          chain_asic_iic[CHAIN_ASIC_NUM];
    uint32_t        chain_hw[BITMAIN_MAX_CHAIN_NUM];
    uint64_t        chain_asic_nonce[BITMAIN_MAX_CHAIN_NUM][BITMAIN_DEFAULT_ASIC_NUM];
    char            chain_asic_status_string[BITMAIN_MAX_CHAIN_NUM][BITMAIN_DEFAULT_ASIC_NUM+8];

    unsigned long long int total_nonce_num;

    unsigned char   fan_exist[BITMAIN_MAX_FAN_NUM];
    unsigned int    fan_speed_value[BITMAIN_MAX_FAN_NUM];
    int             temp[BITMAIN_MAX_CHAIN_NUM];
    uint8_t         chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
    unsigned char   check_bit;
    unsigned char   pwm_percent;
    unsigned char   chain_num;
    unsigned char   fan_num;
    unsigned char   temp_num;
    unsigned int    fan_speed_top1;
    int             temp_top1[TEMP_POS_NUM];
    int             temp_low1[TEMP_POS_NUM];
    int             temp_top1_last;
    unsigned char   corenum;
    unsigned char   addrInterval;
    unsigned char   max_asic_num_in_one_chain;
    unsigned char   baud;
    unsigned char   diff;
    uint8_t         fan_eft;
    uint8_t         fan_pwm;

    unsigned short int  frequency;
    char frequency_t[10];
    unsigned short int  freq[BITMAIN_MAX_CHAIN_NUM];
} __attribute__((packed, aligned(4)));



struct nonce_buf
{
    unsigned int p_wr;
    unsigned int p_rd;
    unsigned int nonce_num;
    struct nonce_content nonce_buffer[MAX_NONCE_NUMBER_IN_FIFO];
} __attribute__((packed, aligned(4)));

struct reg_content
{
    unsigned int reg_value;
    unsigned char crc;
    unsigned char chain_number;
} __attribute__((packed, aligned(4)));

struct reg_buf
{
    unsigned int p_wr;
    unsigned int p_rd;
    unsigned int reg_value_num;
    struct reg_content reg_buffer[MAX_NONCE_NUMBER_IN_FIFO];
} __attribute__((packed, aligned(4)));

struct freq_pll
{
    const char *freq;
    unsigned int fildiv1;
    unsigned int fildiv2;
    unsigned int vilpll;
};

#define Swap32(l) (((l) >> 24) | (((l) & 0x00ff0000) >> 8) | (((l) & 0x0000ff00) << 8) | ((l) << 24))


struct vil_work
{
    uint8_t type;       // Bit[7:5]: Type,fixed 0x01.   Bit[4:0]:Reserved
    uint8_t length;     // data length, from Byte0 to the end.
    uint8_t wc_base;    // Bit[7]: Reserved.    Bit[6:0]: Work count base, muti-Midstate, each Midstate corresponding work count increase one by one.
    uint8_t mid_num;    // Bit[7:3]: Reserved   Bit[2:0]: MSN, midstate num,now support 1,2,4.
    //uint32_t sno;       // SPAT mode??Start Nonce Number    Normal mode??Reserved.
    uint8_t midstate[32];
    uint8_t data2[12];
};

struct vil_work_1387
{
    uint8_t work_type;
    uint8_t chain_id;
    uint8_t reserved1[2];
    uint32_t work_count;
    uint8_t data[12];
    uint8_t midstate[32];
};


static struct freq_pll freq_pll_1385[] =
{
    {"100",0x020040, 0x0420, 0x200241},
    {"125",0x028040, 0x0420, 0x280241},
    {"150",0x030040, 0x0420, 0x300241},
    {"175",0x038040, 0x0420, 0x380241},
    {"200",0x040040, 0x0420, 0x400241},
    {"225",0x048040, 0x0420, 0x480241},
    {"250",0x050040, 0x0420, 0x500241},
    {"275",0x058040, 0x0420, 0x580241},
    {"300",0x060040, 0x0420, 0x600241},
    {"325",0x068040, 0x0420, 0x680241},
    {"350",0x070040, 0x0420, 0x700241},
    {"375",0x078040, 0x0420, 0x780241},
    {"400",0x080040, 0x0420, 0x800241},
    {"404",0x061040, 0x0320, 0x610231},
    {"406",0x041040, 0x0220, 0x410221},
    {"408",0x062040, 0x0320, 0x620231},
    {"412",0x042040, 0x0220, 0x420221},
    {"416",0x064040, 0x0320, 0x640231},
    {"418",0x043040, 0x0220, 0x430221},
    {"420",0x065040, 0x0320, 0x650231},
    {"425",0x044040, 0x0220, 0x440221},
    {"429",0x067040, 0x0320, 0x670231},
    {"431",0x045040, 0x0220, 0x450221},
    {"433",0x068040, 0x0320, 0x680231},
    {"437",0x046040, 0x0220, 0x460221},
    {"441",0x06a040, 0x0320, 0x6a0231},
    {"443",0x047040, 0x0220, 0x470221},
    {"445",0x06b040, 0x0320, 0x6b0231},
    {"450",0x048040, 0x0220, 0x480221},
    {"454",0x06d040, 0x0320, 0x6d0231},
    {"456",0x049040, 0x0220, 0x490221},
    {"458",0x06e040, 0x0320, 0x6e0231},
    {"462",0x04a040, 0x0220, 0x4a0221},
    {"466",0x070040, 0x0320, 0x700231},
    {"468",0x04b040, 0x0220, 0x4b0221},
    {"470",0x071040, 0x0320, 0x710231},
    {"475",0x04c040, 0x0220, 0x4c0221},
    {"479",0x073040, 0x0320, 0x730231},
    {"481",0x04d040, 0x0220, 0x4d0221},
    {"483",0x074040, 0x0320, 0x740231},
    {"487",0x04e040, 0x0220, 0x4e0221},
    {"491",0x076040, 0x0320, 0x760231},
    {"493",0x04f040, 0x0220, 0x4f0221},
    {"495",0x077040, 0x0320, 0x770231},
    {"500",0x050040, 0x0220, 0x500221},
    {"504",0x079040, 0x0320, 0x790231},
    {"506",0x051040, 0x0220, 0x510221},
    {"508",0x07a040, 0x0320, 0x7a0231},
    {"512",0x052040, 0x0220, 0x520221},
    {"516",0x07c040, 0x0320, 0x7c0231},
    {"518",0x053040, 0x0220, 0x530221},
    {"520",0x07d040, 0x0320, 0x7d0231},
    {"525",0x054040, 0x0220, 0x540221},
    {"529",0x07f040, 0x0320, 0x7f0231},
    {"531",0x055040, 0x0220, 0x550221},
    {"533",0x080040, 0x0320, 0x800231},
    {"537",0x056040, 0x0220, 0x560221},
    {"543",0x057040, 0x0220, 0x570221},
    {"550",0x058040, 0x0220, 0x580221},
    {"556",0x059040, 0x0220, 0x590221},
    {"562",0x05a040, 0x0220, 0x5a0221},
    {"568",0x05b040, 0x0220, 0x5b0221},
    {"575",0x05c040, 0x0220, 0x5c0221},
    {"581",0x05d040, 0x0220, 0x5d0221},
    {"587",0x05e040, 0x0220, 0x5e0221},
    {"593",0x05f040, 0x0220, 0x5f0221},
    {"600",0x060040, 0x0220, 0x600221},
    {"606",0x061040, 0x0220, 0x610221},
    {"612",0x062040, 0x0220, 0x620221},
    {"618",0x063040, 0x0220, 0x630221},
    {"625",0x064040, 0x0220, 0x640221},
    {"631",0x065040, 0x0220, 0x650221},
    {"637",0x066040, 0x0220, 0x660221},
    {"643",0x067040, 0x0220, 0x670221},
    {"650",0x068040, 0x0220, 0x680221},
    {"656",0x069040, 0x0220, 0x690221},
    {"662",0x06a040, 0x0220, 0x6a0221},
    {"668",0x06b040, 0x0220, 0x6b0221},
    {"675",0x06c040, 0x0220, 0x6c0221},
    {"681",0x06d040, 0x0220, 0x6d0221},
    {"687",0x06e040, 0x0220, 0x6e0221},
    {"693",0x06f040, 0x0220, 0x6f0221},
    {"700",0x070040, 0x0220, 0x700221},
    {"706",0x071040, 0x0220, 0x710221},
    {"712",0x072040, 0x0220, 0x720221},
    {"718",0x073040, 0x0220, 0x730221},
    {"725",0x074040, 0x0220, 0x740221},
    {"731",0x075040, 0x0220, 0x750221},
    {"737",0x076040, 0x0220, 0x760221},
    {"743",0x077040, 0x0220, 0x770221},
    {"750",0x078040, 0x0220, 0x780221},
    {"756",0x079040, 0x0220, 0x790221},
    {"762",0x07a040, 0x0220, 0x7a0221},
    {"768",0x07b040, 0x0220, 0x7b0221},
    {"775",0x07c040, 0x0220, 0x7c0221},
    {"781",0x07d040, 0x0220, 0x7d0221},
    {"787",0x07e040, 0x0220, 0x7e0221},
    {"793",0x07f040, 0x0220, 0x7f0221},
    {"800",0x080040, 0x0220, 0x800221},
    {"825",0x042040, 0x0120, 0x420211},
    {"850",0x044040, 0x0120, 0x440211},
    {"875",0x046040, 0x0120, 0x460211},
    {"900",0x048040, 0x0120, 0x480211},
    {"925",0x04a040, 0x0120, 0x4a0211},
    {"950",0x04c040, 0x0120, 0x4c0211},
    {"975",0x04e040, 0x0120, 0x4e0211},
    {"1000",0x050040, 0x0120, 0x500211},
    {"1025",0x052040, 0x0120, 0x520211},
    {"1050",0x054040, 0x0120, 0x540211},
    {"1075",0x056040, 0x0120, 0x560211},
    {"1100",0x058040, 0x0120, 0x580211},
    {"1125",0x05a040, 0x0120, 0x5a0211},
    {"1150",0x05c040, 0x0120, 0x5c0211},
    {"1175",0x05e040, 0x0120, 0x5e0211},
};

extern bool opt_bitmain_fan_ctrl;
extern bool opt_bitmain_new_cmd_type_vil;
extern bool opt_fixed_freq;
extern bool opt_pre_heat;
extern int opt_bitmain_fan_pwm;
extern int opt_bitmain_soc_freq;
extern int opt_bitmain_soc_voltage;
extern int ADD_FREQ;
extern int ADD_FREQ1;
extern int fpga_version;
extern int opt_multi_version;

int getChainExistFlag(int chainIndex);
unsigned char get_pic_voltage(unsigned char chain);
int getVolValueFromPICvoltage(unsigned char vol_pic);
unsigned char getPICvoltageFromValue(int vol_value);
void set_pic_voltage(unsigned char chain, unsigned char voltage);
int getChainAsicNum(int chainIndex);
int readRebootTestNum();
int send_job(unsigned char *buf);


#endif


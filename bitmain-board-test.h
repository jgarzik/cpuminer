/*
 * Copyright 2016-2017 Fazio Bai <yang.bai@bitmain.com>
 * Copyright 2016-2017 Clement Duan <kai.duan@bitmain.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */
#ifndef __BITMAIN_BOARD_TEST_H__
#define __BITMAIN_BOARD_TEST_H__
#include <stdbool.h>

#define MAX_ASIC_NUM 128
#define MAX_WORK 5000

#define FIL 0x1
#define VIL 0x0

struct configuration
{
    bool AutoStart;
    bool Gray;
    int NonceMask; //???
    int DataCount;
    int PassCount1;
    int PassCount2;
    int PassCount3;
    int Freq;
    int Timeout;
    bool Regulate;
    int Value;
    int ReadIntervalTimeout;
    int AddrInterval;
    int CoreNum;
    int AsicNum;
    int UseFreqPIC;
    int TestMode;
    int CheckChain;
    int CommandMode;
    int AsicType;
    int ValidNonce1;
    int ValidNonce2;
    int ValidNonce3;
    unsigned int Pic;
    unsigned int Voltage1;
    unsigned int Voltage2;
    unsigned int Voltage3;
    unsigned int final_voltage1;
    unsigned int final_voltage2;
    unsigned int final_voltage3;
    unsigned int freq_gap;
    int OpenCoreGap;
    int checktemp;
    unsigned int IICPic;
    unsigned int OpenCoreNum1;
    unsigned int OpenCoreNum2;
    unsigned int OpenCoreNum3;
    unsigned int OpenCoreNum4;
    unsigned int dac;
    unsigned int GetTempFrom;
    unsigned int TempSel;
    unsigned int TempSensor1;
    unsigned int TempSensor2;
    unsigned int TempSensor3;
    unsigned int TempSensor4;
    signed char DefaultTempOffset;
    int freq_e;
    int freq_m;
    int freq_a;
    int freq_t;
    int force_freq;
    int UseConfigVol;
    int StartSensor;
    int StartTemp;
    int year;
    int month;
    int date;
    int hour;
    int minute;
    int second;
};

struct _CONFIG
{
    int dataCount;
    int passCount1;
    int passCount2;
    int passCount3;
    int core;
    int freq;
    int timeout;
    int baud;
    bool regulate;
    int value;
    int addrInterval;
    int asicNum;
    int testMode;
    int CommandMode;
    int AsicType;
    int ValidNonce1;
    int ValidNonce2;
    int ValidNonce3;
    unsigned int Pic;
    unsigned int Voltage1;
    unsigned int Voltage2;
    unsigned int Voltage3;
    int OpenCoreGap;
    int checktemp;
    unsigned int IICPic;
    int UseFreqPIC;
    unsigned int freq_gap;
    unsigned int OpenCoreNum1;
    unsigned int OpenCoreNum2;
    unsigned int OpenCoreNum3;
    unsigned int OpenCoreNum4;
    unsigned int dac;
    unsigned int GetTempFrom;
    unsigned int TempSel;
    unsigned char TempSensor1;
    unsigned char TempSensor2;
    unsigned char TempSensor3;
    unsigned char TempSensor4;
    signed char DefaultTempOffset;
    int freq_e;
    int freq_m;
    int freq_a;
    int freq_t;
    int force_freq;
    int UseConfigVol;
    unsigned char StartSensor;
    signed char StartTemp;
    int year;
    int month;
    int date;
    int hour;
    int minute;
    int second;
};

struct testpatten_work
{
    int     id;
    uint32_t nonce; /* For devices that hash sole work */
    unsigned char data[12];
    unsigned char   midstate[32];
};

struct testpatten_cgpu_info
{
    FILE * fps[MAX_ASIC_NUM];

    pthread_t receive_id, show_id, pic_heart_beat_id, read_temp,freq_id;
    pthread_t send_id[BITMAIN_MAX_CHAIN_NUM];
    int device_fd;
    int lcd_fd;

    char workdataPathPrefix[64];
    struct testpatten_work *works[MAX_ASIC_NUM];
    //int work_array[MAX_ASIC_NUM]; //work number of every asic
    uint32_t results[MAX_ASIC_NUM][MAX_WORK];
    int result_array[MAX_ASIC_NUM]; //return nonce number of every asic
    int subid[MAX_ASIC_NUM];
    int min_work_subid;
    int index;
    int valid_nonce;
    int err_nonce;
    int repeated_nonce;

    int start_key_fd;
    int red_led_fd;
    int green_led_fd;
    int beep_fd;

    unsigned int    real_asic_num;
    unsigned int    asicNum;
    unsigned int    core_num;
    int freq_e;
    int freq_m;
    int freq_a;
    int freq_t;
    int AsicType;
    unsigned int    chain_num;
    unsigned short int  frequency;
    unsigned int    CommandMode;    // 1:fil  0:vil
    unsigned int    chain_exist[BITMAIN_MAX_CHAIN_NUM];
    unsigned int    timeout;
    unsigned char   chain_asic_num[BITMAIN_MAX_CHAIN_NUM];
    unsigned int   addrInterval;
    unsigned char   baud;
    unsigned short int  freq[BITMAIN_MAX_CHAIN_NUM];
    unsigned int max_asic_num_in_one_chain;
    unsigned char temp_sel;
    unsigned char rfs;
    unsigned char tfs;
    signed char T1_offset_value;
    signed char T2_offset_value;
    signed char T3_offset_value;
    signed char T4_offset_value;
};

#define PRE_HEAT_TEST_COUNT     2

#undef CHECK_ALLNONCE_ADD_VOLTAGE_USERMODE //if defined, when in user mode (restartNum>0), then check nonce num in test patten, if failed, add 0.1V voltage , must according to hashrate

#define RETRY_FREQ_INDEX    12  // 400M, if search base freq < 400M, will switch to use RETRY_VOLTAGE to search again.
#define REBOOT_TEST_NUM     2   // save into file

#define ENABLE_SEARCH_LOGFILE   //enable log info into kernel info web page.
#define LOG_CHIPS_CORE_DETAIL   // if enabled , will show details nonce number info for chips and cores, open for debug

#define TEST_MODE_OK_NUM    2   // if 8xPatten test mode test OK counter >= TEST_MODE_OK_NUM, then this board is OK,  3 

#define SEARCH_FREQ_CHANCE_NUM  2   // give each board 2 chances to search freq, the first failed, we can add voltage  SEARCH_VOLTAGE_ADD_STEP to search for next chance
#define SEARCH_VOLTAGE_ADD_STEP 30  // means, each chance will add 0.3V to search freq again.

#define SEARCH_BASEFREQ_PATTEN_NUM  912
#define SEARCH_BASEFREQ_NONCE_NUM   (SEARCH_BASEFREQ_PATTEN_NUM*CHAIN_ASIC_NUM)

#define SEARCH_FREQ_PATTEN_NUM      114
#define SEARCH_FREQ_NONCE_NUM       (SEARCH_FREQ_PATTEN_NUM*CHAIN_ASIC_NUM)

#define TESTMODE_PATTEN_NUM         912
#define TESTMODE_NONCE_NUM          (TESTMODE_PATTEN_NUM*CHAIN_ASIC_NUM)

#define DEFAULT_TEMP_OFFSET     -70

#define FOR_LOOP_CHAIN  for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)

#define LOWEST_FREQ_INDEX   4   // 8:300M       6:250M      4:200M
#define HIGHEST_FREQ_INDEX  100 // 850M:100 700M:82  668M:77

#define SEND_WAIT_TIMEOUT   120 // unit is 100ms
#define RECV_WAIT_TIMEOUT   20 // unit is 100ms

#define NOBOARD_RETRY_COUNT 3

#ifdef ALLOW_KPERCENT_8xPATTEN
static void fix_result_byPercent(int chainIndex);
#endif

static int calculate_core_number(unsigned int actual_core_number);

#endif

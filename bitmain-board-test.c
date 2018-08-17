/*
 * Copyright 2016-2017 Fazio Bai <yang.bai@bitmain.com>
 * Copyright 2016-2017 Clement Duan <kai.duan@bitmain.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */
#include "config.h"
#include <assert.h>

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <math.h>

#ifndef WIN32
#include <sys/select.h>
#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#else
#include "compat.h"
#include <windows.h>
#include <io.h>
#endif

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include <zlib.h>

#include "elist.h"
#include "miner.h"


#include "util.h"
#include "driver-btm-soc.h"

#include "bitmain-board-test.h"

// below are defined in driver-btm-c5.c
extern pthread_mutex_t iic_mutex;
extern bool isChainAllCoresOpened[BITMAIN_MAX_CHAIN_NUM];
extern bool someBoardUpVoltage;
extern int lowest_testOK_temp[BITMAIN_MAX_CHAIN_NUM];
extern int LOWEST_TEMP_DOWN_FAN;
extern int chain_badcore_num[BITMAIN_MAX_CHAIN_NUM][256];
extern pthread_mutex_t opencore_readtemp_mutex;
extern unsigned int *axi_fpga_addr;             // defined in driver-btm-c5.c
extern unsigned int *fpga_mem_addr;             //defined in driver-btm-c5.c
extern int fd_fpga_mem;                                // fpga memory
extern unsigned int *nonce2_jobid_address;      // the value should be filled in NONCE2_AND_JOBID_STORE_ADDRESS

extern void open_core_one_chain(int chainIndex, bool nullwork_enable);
extern void insert_reg_data(unsigned int *buf);
extern int GetTotalRate();
extern int getVoltageLimitedFromHashrate(int hashrate_GHz);
extern bool isChainEnough();
extern void set_PWM(unsigned char pwm_percent);
extern int get_nonce_number_in_fifo(void);
extern int get_return_nonce(unsigned int *buf);
extern int get_nonce_fifo_interrupt(void);
extern void set_nonce_fifo_interrupt(unsigned int value);
extern void set_TW_write_command(unsigned int *value);
extern void set_TW_write_command_vil(unsigned int *value);
extern int get_buffer_space(void);
extern int get_freqvalue_by_index(int index);
extern int getChainAsicFreqIndex(int chainIndex, int asicIndex);
extern int get_hash_on_plug(void);


////////// below is only used inside of this file !!! so all static!/////////////
static bool chain_need_opencore[BITMAIN_MAX_CHAIN_NUM]= {false};
static bool StartSendFlag[BITMAIN_MAX_CHAIN_NUM];

static int chain_DataCount[BITMAIN_MAX_CHAIN_NUM];
static int chain_ValidNonce[BITMAIN_MAX_CHAIN_NUM];
static int chain_PassCount[BITMAIN_MAX_CHAIN_NUM];

static int chain_vol_value[BITMAIN_MAX_CHAIN_NUM];  // the searching vol
static int chain_vol_final[BITMAIN_MAX_CHAIN_NUM];  // the final vol, need saved in PIC
static int chain_vol_added[BITMAIN_MAX_CHAIN_NUM];  // how many vol added , recorded in PIC

static int last_result[BITMAIN_MAX_CHAIN_NUM][256];
static int last_result_opencore[BITMAIN_MAX_CHAIN_NUM][256];

static int result = 0;
static bool search_freq_result[BITMAIN_MAX_CHAIN_NUM];  // set true as default

static struct testpatten_cgpu_info cgpu;
static volatile bool gBegin_get_nonce = false;

static unsigned int send_work_num[BITMAIN_MAX_CHAIN_NUM];

static int asic_nonce_num[BITMAIN_MAX_CHAIN_NUM][256];
static int asic_core_nonce_num[BITMAIN_MAX_CHAIN_NUM][256][256];  // 1st: which asic, 2nd: which core
static int last_nonce_num[BITMAIN_MAX_CHAIN_NUM];
static int repeated_nonce_num[BITMAIN_MAX_CHAIN_NUM];
static uint32_t repeated_nonce_id[BITMAIN_MAX_CHAIN_NUM][256];
static int valid_nonce_num[BITMAIN_MAX_CHAIN_NUM];    // all the received nonce in one test
static int err_nonce_num[BITMAIN_MAX_CHAIN_NUM];
static int total_valid_nonce_num=0;

static volatile bool start_receive = false;

static int testModeOKCounter[BITMAIN_MAX_CHAIN_NUM];

static struct configuration Conf;  //store information that read from Config.ini
static struct _CONFIG conf;        //store the information that handled from Config.ini

static bool ExitFlag=false;
static bool receiveExit;
static bool sendExit[BITMAIN_MAX_CHAIN_NUM];

static void writeLogFile(char *logstr);
static int calculate_asic_number(unsigned int actual_asic_number);
static int calculate_core_number(unsigned int actual_core_number);


#define CONFIG_FILE "/etc/config/Config.ini"
#define FORCE_FREQ_FILE "/etc/config/forcefreq.txt"
#define LAST_FORCE_FREQ_FILE    "/etc/config/last_forcefreq.txt"

static bool last_all_pass(int chainIndex)
{
    int i = 0;
    for(i=0; i<CHAIN_ASIC_NUM; i++)
        if (!last_result[chainIndex][i])
            return false;
    return true;
}

static bool last_all_core_opened(int chainIndex)
{
    int i = 0;
    for(i=0; i<CHAIN_ASIC_NUM; i++)
        if (!last_result_opencore[chainIndex][i])
            return false;
    return true;
}

static bool isAllChainChipCoreOpened()
{
    int i;
    FOR_LOOP_CHAIN
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        if(!last_all_core_opened(i))
        {
            return false;
        }
    }

    return true;
}

static int load_testpatten_work(int id, int count)
{
    struct testpatten_work * new_work;
    int subid = 0;
    unsigned long DataLen=MAX_WORK*48;  // midstate + data + nonce = 48 bytes
    unsigned char *workData;
    unsigned char *zipData;
    unsigned long zipLen;

    workData=(unsigned char *)malloc(DataLen);

    fseek(cgpu.fps[id],0,SEEK_END);
    zipLen = ftell(cgpu.fps[id]);
    fseek(cgpu.fps[id],0,SEEK_SET);

    zipData=(unsigned char *)malloc(zipLen);
    zipLen=fread(zipData,1,zipLen,cgpu.fps[id]);

    uncompress(workData,&DataLen,zipData,zipLen);
    free(zipData);

    cgpu.works[id] = (struct testpatten_work *)malloc(count * sizeof(struct testpatten_work));
    if(NULL == cgpu.works[id])
    {
        applog(LOG_ERR, "malloc struct testpatten_work err\n");
        return 0;
    }

    while(subid*48<DataLen)
    {
        if(subid >= count)
            break;

        new_work = cgpu.works[id] + subid;

        memcpy((uint8_t *)(&new_work->nonce) ,workData+subid*48+44, 4);
        new_work->nonce = htonl(new_work->nonce);

        memcpy(new_work->midstate ,workData+subid*48, 32);
        memcpy(new_work->data ,workData+subid*48+32, 12);

        new_work->id = subid;
        subid++;
    }
    free(workData);
    return subid;
}

static int read_config()
{
    FILE * file;
    int forceFreq,forceFlag;
    struct configuration *m_conf = &Conf;
    char str[1024] = {0};
    char * temp;
    int offset = 0, starttemp = 0;
    int i;
    file = fopen(CONFIG_FILE, "r");
    char logstr[1024];

    while(fgets(str, sizeof(str) - 1 , file))
    {
        if(str[0] == '#' || str[1] == '#')
            continue;

        if((temp = strstr(str, "TestDir="))!=NULL)
        {
            temp += 8;
            for(i = 0; i < 64; i++)
            {
                cgpu.workdataPathPrefix[i] = *temp++;
                //printf("%c", *temp);
                if(*temp == '\n' || *temp == '\r')
                    break;
            }
            i++;
            cgpu.workdataPathPrefix[i] = '\0';
            printf("workdataPathPrefix:%s\n", cgpu.workdataPathPrefix);
        }
        else if((temp = strstr(str, "DataCount="))!=NULL)
        {
            temp += 10;
            sscanf(temp, "%d", &m_conf->DataCount);
        }
        else if((temp = strstr(str, "PassCount1="))!=NULL)
        {
            temp += 11;
            sscanf(temp, "%d", &m_conf->PassCount1);
        }
        else if((temp = strstr(str, "PassCount2="))!=NULL)
        {
            temp += 11;
            sscanf(temp, "%d", &m_conf->PassCount2);
        }
        else if((temp = strstr(str, "PassCount3="))!=NULL)
        {
            temp += 11;
            sscanf(temp, "%d", &m_conf->PassCount3);
        }
        else if((temp = strstr(str, "Freq="))!=NULL)
        {
            temp += 5;
            sscanf(temp, "%d", &m_conf->Freq);

            m_conf->force_freq=0;
        }
        else if((temp = strstr(str, "freq_e="))!=NULL)
        {
            temp += 7;
            sscanf(temp, "%d", &m_conf->freq_e);
        }
        else if((temp = strstr(str, "UseConfigVol="))!=NULL)
        {
            temp += 13;
            sscanf(temp, "%d", &m_conf->UseConfigVol);
        }
        else if((temp = strstr(str, "freq_m="))!=NULL)
        {
            temp += 7;
            sscanf(temp, "%d", &m_conf->freq_m);
        }
        else if((temp = strstr(str, "freq_a="))!=NULL)
        {
            temp += 7;
            sscanf(temp, "%d", &m_conf->freq_a);
        }
        else if((temp = strstr(str, "freq_t="))!=NULL)
        {
            temp += 7;
            sscanf(temp, "%d", &m_conf->freq_t);
        }
        else if((temp = strstr(str, "force_freq="))!=NULL)
        {
            temp += 11;
            //    sscanf(temp, "%d", &m_conf->force_freq);
        }
        else if((temp = strstr(str, "Timeout="))!=NULL)
        {
            temp +=8;
            sscanf(temp, "%d", &m_conf->Timeout);
        }
        else if((temp = strstr(str, "UseFreqPIC="))!=NULL)
        {
            temp += 11;
            sscanf(temp , "%d", &m_conf->UseFreqPIC);
        }
        else if((temp = strstr(str, "TestMode="))!=NULL)
        {
            temp += 9;
            sscanf(temp, "%d", &m_conf->TestMode);
        }
        else if((temp = strstr(str, "CheckChain="))!=NULL)
        {
            temp += 11;
            sscanf(temp, "%d", &m_conf->CheckChain);
        }
        else if((temp = strstr(str, "CommandMode="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%d", &m_conf->CommandMode);
        }
        else if((temp = strstr(str, "ValidNonce1="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%d", &m_conf->ValidNonce1);
        }
        else if((temp = strstr(str, "ValidNonce2="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%d", &m_conf->ValidNonce2);
        }
        else if((temp = strstr(str, "ValidNonce3="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%d", &m_conf->ValidNonce3);
        }
        else if((temp = strstr(str, "Pic_VOLTAGE="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%d", &m_conf->Pic);
        }
        else if((temp = strstr(str, "Voltage1="))!=NULL)
        {
            temp += 9;
            sscanf(temp, "%d", &m_conf->Voltage1);
        }
        else if((temp = strstr(str, "Voltage2="))!=NULL)
        {
            temp += 9;
            sscanf(temp, "%d", &m_conf->Voltage2);
        }
        else if((temp = strstr(str, "Voltage3="))!=NULL)
        {
            temp += 9;
            sscanf(temp, "%d", &m_conf->Voltage3);
        }
        else if((temp = strstr(str, "final_voltage1="))!=NULL)
        {
            temp += 15;
            sscanf(temp, "%ud", &m_conf->final_voltage1);
        }
        else if((temp = strstr(str, "final_voltage2="))!=NULL)
        {
            temp += 15;
            sscanf(temp, "%ud", &m_conf->final_voltage2);
        }
        else if((temp = strstr(str, "final_voltage3="))!=NULL)
        {
            temp += 15;
            sscanf(temp, "%ud", &m_conf->final_voltage3);
        }
        else if((temp = strstr(str, "freq_gap="))!=NULL)
        {
            temp += 9;
            sscanf(temp, "%ud", &m_conf->freq_gap);
        }
        else if((temp = strstr(str, "OpenCoreGap="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%d", &m_conf->OpenCoreGap);
        }
        else if((temp = strstr(str, "CheckTemp="))!=NULL)
        {
            temp += 10;
            sscanf(temp, "%d", &m_conf->checktemp);
        }
        else if((temp = strstr(str, "IICPic="))!=NULL)
        {
            temp += 7;
            sscanf(temp, "%d", &m_conf->IICPic);
        }
        else if((temp = strstr(str, "Open_Core_Num1="))!=NULL)
        {
            temp += 15;
            sscanf(temp, "%ud", &m_conf->OpenCoreNum1);
        }
        else if((temp = strstr(str, "Open_Core_Num2="))!=NULL)
        {
            temp += 15;
            sscanf(temp, "%ud", &m_conf->OpenCoreNum2);
        }
        else if((temp = strstr(str, "Open_Core_Num3="))!=NULL)
        {
            temp += 15;
            sscanf(temp, "%ud", &m_conf->OpenCoreNum3);
        }
        else if((temp = strstr(str, "Open_Core_Num4="))!=NULL)
        {
            temp += 15;
            sscanf(temp, "%ud", &m_conf->OpenCoreNum4);
        }
        else if((temp = strstr(str, "DAC="))!=NULL)
        {
            temp += 4;
            sscanf(temp, "%ud", &m_conf->dac);
        }
        else if((temp = strstr(str, "GetTempFrom="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%ud", &m_conf->GetTempFrom);
        }
        else if((temp = strstr(str, "TempSel="))!=NULL)
        {
            temp += 8;
            sscanf(temp, "%ud", &m_conf->TempSel);
        }
        else if((temp = strstr(str, "TempSensor1="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%ud", &m_conf->TempSensor1);
        }
        else if((temp = strstr(str, "TempSensor2="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%ud", &m_conf->TempSensor2);
        }
        else if((temp = strstr(str, "TempSensor3="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%ud", &m_conf->TempSensor3);
        }
        else if((temp = strstr(str, "TempSensor4="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%ud", &m_conf->TempSensor4);
        }
        else if((temp = strstr(str, "DefaultTempOffset="))!=NULL)
        {
            temp += 18;
            sscanf(temp, "%d", &offset);
            if(offset < 0)
            {
                offset -= 2*offset;
                m_conf->DefaultTempOffset = (signed char)offset;
                m_conf->DefaultTempOffset -= 2*m_conf->DefaultTempOffset;
                //printf("~~~~~~~~~ m_conf->DefaultTempOffset = %d\n", m_conf->DefaultTempOffset);
            }
            else
            {
                m_conf->DefaultTempOffset = offset;
                //printf("~~~~~~~~~ m_conf->DefaultTempOffset = %d\n", m_conf->DefaultTempOffset);
            }
        }
        else if((temp = strstr(str, "year="))!=NULL)
        {
            temp += 5;
            sscanf(temp, "%d", &m_conf->year);
            //printf("year = %d\n", m_conf->year);
        }
        else if((temp = strstr(str, "month="))!=NULL)
        {
            temp += 6;
            sscanf(temp, "%d", &m_conf->month);
        }
        else if((temp = strstr(str, "date="))!=NULL)
        {
            temp += 5;
            sscanf(temp, "%d", &m_conf->date);
        }
        else if((temp = strstr(str, "hour="))!=NULL)
        {
            temp += 5;
            sscanf(temp, "%d", &m_conf->hour);
        }
        else if((temp = strstr(str, "minute="))!=NULL)
        {
            temp += 7;
            sscanf(temp, "%d", &m_conf->minute);
        }
        else if((temp = strstr(str, "second="))!=NULL)
        {
            temp += 7;
            sscanf(temp, "%d", &m_conf->second);
        }
        else if((temp = strstr(str, "StartSensor="))!=NULL)
        {
            temp += 12;
            sscanf(temp, "%d", &m_conf->StartSensor);
        }
        else if((temp = strstr(str, "StartTemp="))!=NULL)
        {
            temp += 10;
            sscanf(temp, "%d", &m_conf->StartTemp);
            sscanf(temp, "%d", &starttemp);
            if(starttemp < 0)
            {
                starttemp -= 2*starttemp;
                m_conf->StartTemp = (signed char)starttemp;
                m_conf->StartTemp -= 2*m_conf->StartTemp;
                //printf("~~~~~~~~~ m_conf->DefaultTempOffset = %d\n", m_conf->DefaultTempOffset);
            }
            else
            {
                m_conf->StartTemp = starttemp;
                //printf("~~~~~~~~~ m_conf->DefaultTempOffset = %d\n", m_conf->DefaultTempOffset);
            }
        }
    }

    m_conf->AsicNum=CHAIN_ASIC_NUM;
    m_conf->AsicType=ASIC_TYPE;
    m_conf->CoreNum=ASIC_CORE_NUM;
    return 0;
}

static int process_config()
{
    uint32_t rBaudrate;
    int temp_corenum = 0;

    conf.CommandMode = Conf.CommandMode;

    conf.TempSel = Conf.TempSel;

    conf.GetTempFrom = Conf.GetTempFrom;

    if(Conf.CommandMode == FIL)
    {
        if(conf.GetTempFrom == 1)   // read temp from asic
        {
            applog(LOG_ERR, "Can't get temperature from ASIC in FIL mode!\n");
            return -1;
        }
    }

    if(Conf.CommandMode == VIL)
    {
        if(conf.GetTempFrom == 1)   // read temp from asic
        {
            cgpu.temp_sel = Conf.TempSel;
            cgpu.rfs = 1;
            cgpu.tfs = 3;
            //printf("cgpu.temp_sel = %d, cgpu.rfs = %d, cgpu.tfs = %d\n", cgpu.temp_sel, cgpu.rfs, cgpu.tfs);

            if(Conf.TempSensor1 + Conf.TempSensor2 + Conf.TempSensor3 + Conf.TempSensor4)
            {
                conf.TempSensor1 = Conf.TempSensor1;
                conf.TempSensor2 = Conf.TempSensor2;
                conf.TempSensor3 = Conf.TempSensor3;
                conf.TempSensor4 = Conf.TempSensor4;
                conf.DefaultTempOffset = Conf.DefaultTempOffset;
                cgpu.T1_offset_value = Conf.DefaultTempOffset;
                cgpu.T2_offset_value = Conf.DefaultTempOffset;
                cgpu.T3_offset_value = Conf.DefaultTempOffset;
                cgpu.T4_offset_value = Conf.DefaultTempOffset;
                conf.StartSensor = Conf.StartSensor;
                conf.StartTemp = Conf.StartTemp;
            }
            else
            {
                applog(LOG_ERR, "Must set temperature sensor address!\n");
                return -1;
            }
        }
    }

    conf.AsicType = ASIC_TYPE;

    conf.core = Conf.CoreNum;

    conf.freq_e = Conf.freq_e;

    conf.freq_m = Conf.freq_m;

    conf.freq_a = Conf.freq_a;

    conf.freq_t = Conf.freq_t;

    conf.force_freq = Conf.force_freq;

    conf.UseConfigVol = Conf.UseConfigVol;

    conf.OpenCoreNum1 = Conf.OpenCoreNum1;

    conf.OpenCoreNum2 = Conf.OpenCoreNum2;

    conf.OpenCoreNum3 = Conf.OpenCoreNum3;

    conf.OpenCoreNum4 = Conf.OpenCoreNum4;

    conf.asicNum = calculate_asic_number(CHAIN_ASIC_NUM);

    conf.addrInterval = Conf.AddrInterval = CHIP_ADDR_INTERVAL;

    temp_corenum = calculate_core_number(conf.core);

    conf.testMode = Conf.TestMode;

    conf.ValidNonce1 = Conf.ValidNonce1;

    conf.ValidNonce2 = Conf.ValidNonce2;

    conf.ValidNonce3 = Conf.ValidNonce3;

    conf.Pic = Conf.Pic;

    conf.IICPic = Conf.IICPic;

    conf.dac= Conf.dac;

    conf.Voltage1 = Conf.Voltage1;

    conf.Voltage2 = Conf.Voltage2;

    conf.Voltage3 = Conf.Voltage3;

    conf.OpenCoreGap = Conf.OpenCoreGap;

    conf.checktemp = Conf.checktemp;

    if(ASIC_TYPE==1385 || ASIC_TYPE == 1387)
    {
        conf.freq = Conf.Freq;
    }
    else
    {
        printf("%s: ASIC_TYPE = %d, but it is not correct!\n", __FUNCTION__, ASIC_TYPE);
    }

    conf.year = Conf.year;
    conf.month = Conf.month;
    conf.date = Conf.date;
    conf.hour = Conf.hour;
    conf.minute = Conf.minute;
    conf.second = Conf.second;

    if(Conf.Timeout <= 0)
        conf.timeout = 0x1000000/temp_corenum*conf.addrInterval/Conf.Freq*95/100;
    else
        conf.timeout = Conf.Timeout;

    rBaudrate = 1000000 * 5/3 / conf.timeout * (64*8);//64*8 need send bit, ratio=2/3
    conf.baud = 25000000/rBaudrate/8 - 1;
    if(conf.baud > DEFAULT_BAUD_VALUE)
    {
        conf.baud = DEFAULT_BAUD_VALUE;
    }
    else if(conf.baud <= 0)
    {
        applog(LOG_ERR, "$$$$Config argument Baudrate:%d err\n", conf.baud);
        return -1;
    }

    if(Conf.DataCount > MAX_WORK || Conf.DataCount <= 0)
    {
        applog(LOG_ERR, "$$$$Config argument DataCount:%d err\n", Conf.DataCount);
    }
    else
        conf.dataCount = Conf.DataCount;

    if(Conf.PassCount1 > conf.dataCount || Conf.PassCount1 < 0)
    {
        applog(LOG_ERR, "$$$$Config argument DataCount:%d err\n", Conf.DataCount);
    }
    else
        conf.passCount1 = Conf.PassCount1;

    if(Conf.PassCount2 > conf.dataCount || Conf.PassCount2 < 0)
    {
        applog(LOG_ERR, "$$$$Config argument DataCount:%d err\n", Conf.DataCount);
    }
    else
        conf.passCount2 = Conf.PassCount2;

    if(Conf.PassCount3 > conf.dataCount || Conf.PassCount3 < 0)
    {
        applog(LOG_ERR, "$$$$Config argument DataCount:%d err\n", Conf.DataCount);
    }
    else
        conf.passCount3 = Conf.PassCount3;

    return 0;
}

static void print_config()
{
    const struct configuration *m_conf = &Conf;
    printf("\n\nRead Config.ini\n");
    printf("DataCount:%d\n", m_conf->DataCount);
    printf("PassCount1:%d\n", m_conf->PassCount1);
    printf("PassCount2:%d\n", m_conf->PassCount2);
    printf("PassCount3:%d\n", m_conf->PassCount3);
    printf("Freq:%d\n", m_conf->Freq);
    printf("Timeout:%d\n", m_conf->Timeout);
    printf("OpenCoreGap:%d\n", m_conf->OpenCoreGap);
    printf("CheckTemp:%d\n", m_conf->checktemp);
    printf("CoreNum:%d\n", m_conf->CoreNum);
    printf("freq_e:%d\n", m_conf->freq_e);
    printf("AsicNum:%d\n", m_conf->AsicNum);
    printf("TestMode:%d\n", m_conf->TestMode);
    printf("CheckChain:%d\n", m_conf->CheckChain);
    printf("CommandMode:%d\n", m_conf->CommandMode);
    printf("AsicType:%d\n", m_conf->AsicType);
    printf("ValidNonce1:%d\n", m_conf->ValidNonce1);
    printf("ValidNonce2:%d\n", m_conf->ValidNonce2);
    printf("ValidNonce3:%d\n", m_conf->ValidNonce3);
    printf("Pic:%ud\n", m_conf->Pic);
    printf("IICPic:%ud\n", m_conf->IICPic);
    printf("dac = %ud\n", m_conf->dac);
    printf("Voltage1:%ud\n", m_conf->Voltage1);
    printf("Voltage2:%ud\n", m_conf->Voltage2);
    printf("Voltage3:%ud\n", m_conf->Voltage3);
    printf("OpenCoreNum1 = %ud = 0x%x\n", m_conf->OpenCoreNum1, m_conf->OpenCoreNum1);
    printf("OpenCoreNum2 = %ud = 0x%x\n", m_conf->OpenCoreNum2, m_conf->OpenCoreNum2);
    printf("OpenCoreNum3 = %ud = 0x%x\n", m_conf->OpenCoreNum3, m_conf->OpenCoreNum3);
    printf("OpenCoreNum4 = %ud = 0x%x\n", m_conf->OpenCoreNum4, m_conf->OpenCoreNum4);
    printf("GetTempFrom:%d\n", m_conf->GetTempFrom);
    printf("TempSel:%d\n", m_conf->TempSel);
    printf("TempSensor1:%d\n", m_conf->TempSensor1);
    printf("TempSensor2:%d\n", m_conf->TempSensor2);
    printf("TempSensor3:%d\n", m_conf->TempSensor3);
    printf("TempSensor4:%d\n", m_conf->TempSensor4);
    printf("DefaultTempOffset:%d\n", m_conf->DefaultTempOffset);
    printf("StartSensor:%d\n", m_conf->StartSensor);
    printf("StartTemp:%d\n", m_conf->StartTemp);
    printf("year:%04d\n", m_conf->year);
    printf("month:%02d\n", m_conf->month);
    printf("date:%02d\n", m_conf->date);
    printf("hour:%02d\n", m_conf->hour);
    printf("minute:%02d\n", m_conf->minute);
    printf("second:%02d\n", m_conf->second);

    printf("\n\n");
}

static void print_CONFIG(void)
{
    const struct _CONFIG *m_conf = &conf;
    printf("\n\nparameter processed after Reading Config.ini\n");
    printf("DataCount:%d\n", m_conf->dataCount);
    printf("PassCount1:%d\n", m_conf->passCount1);
    printf("PassCount2:%d\n", m_conf->passCount2);
    printf("PassCount3:%d\n", m_conf->passCount3);
    printf("Freq:%d\n", m_conf->freq);
    printf("Timeout:%d\n", m_conf->timeout);
    printf("OpenCoreGap:%d\n", m_conf->OpenCoreGap);
    printf("CheckTemp:%d\n", m_conf->checktemp);
    printf("CoreNum:%d\n", m_conf->core);
    printf("AsicNum:%d\n", m_conf->asicNum);
    printf("TestMode:%d\n", m_conf->testMode);
    printf("CommandMode:%d\n", m_conf->CommandMode);
    printf("AsicType:%d\n", m_conf->AsicType);
    printf("ValidNonce1:%d\n", m_conf->ValidNonce1);
    printf("ValidNonce2:%d\n", m_conf->ValidNonce2);
    printf("ValidNonce3:%d\n", m_conf->ValidNonce3);
    printf("Pic:%ud\n", m_conf->Pic);
    printf("IICPic:%ud\n", m_conf->IICPic);
    printf("dac:%ud\n", m_conf->dac);
    printf("Voltage1:%ud\n", m_conf->Voltage1);
    printf("Voltage2:%ud\n", m_conf->Voltage2);
    printf("Voltage3:%ud\n", m_conf->Voltage3);
    printf("OpenCoreNum1 = %ud = 0x%x\n", m_conf->OpenCoreNum1, m_conf->OpenCoreNum1);
    printf("OpenCoreNum2 = %ud = 0x%x\n", m_conf->OpenCoreNum2, m_conf->OpenCoreNum2);
    printf("OpenCoreNum3 = %ud = 0x%x\n", m_conf->OpenCoreNum3, m_conf->OpenCoreNum3);
    printf("OpenCoreNum4 = %ud = 0x%x\n", m_conf->OpenCoreNum4, m_conf->OpenCoreNum4);
    printf("GetTempFrom:%d\n", m_conf->GetTempFrom);
    printf("TempSel:%d\n", m_conf->TempSel);
    printf("TempSensor1:%d\n", m_conf->TempSensor1);
    printf("TempSensor2:%d\n", m_conf->TempSensor2);
    printf("TempSensor3:%d\n", m_conf->TempSensor3);
    printf("TempSensor4:%d\n", m_conf->TempSensor4);
    printf("DefaultTempOffset:%d\n", m_conf->DefaultTempOffset);
    printf("StartSensor:%d\n", m_conf->StartSensor);
    printf("StartTemp:%d\n", m_conf->StartTemp);
    printf("year:%04d\n", m_conf->year);
    printf("month:%02d\n", m_conf->month);
    printf("date:%02d\n", m_conf->date);
    printf("hour:%02d\n", m_conf->hour);
    printf("minute:%02d\n", m_conf->minute);
    printf("second:%02d\n", m_conf->second);
    printf("\n\n");
}

static int get_works()
{
    char strFilePath[64] = {0};
    int i, j, record, loop=0;
    unsigned int OpenCoreNum1 = conf.OpenCoreNum1;
    unsigned int OpenCoreNum2 = conf.OpenCoreNum2;
    unsigned int OpenCoreNum3 = conf.OpenCoreNum3;
    unsigned int OpenCoreNum4 = conf.OpenCoreNum4;
    //getcwd(Path, 128);
    //applog(LOG_DEBUG, "Path:%s\n", Path);

    //printf("%s: loop = %d\n", __FUNCTION__, loop);

    if(CHAIN_ASIC_NUM == 1)
    {
        for(j=0; j < 32; j++)
        {
            if(OpenCoreNum1 & 0x00000001)
            {
                loop++;
            }
            OpenCoreNum1 = OpenCoreNum1 >> 1;

            if(OpenCoreNum2 & 0x00000001)
            {
                loop++;
            }
            OpenCoreNum2 = OpenCoreNum2 >> 1;

            if(OpenCoreNum3 & 0x00000001)
            {
                loop++;
            }
            OpenCoreNum3 = OpenCoreNum3 >> 1;

            if(OpenCoreNum4 & 0x00000001)
            {
                loop++;
            }
            OpenCoreNum4 = OpenCoreNum4 >> 1;
        }

        printf("%s: loop = %d\n", __FUNCTION__, loop);
    }
    else
    {
        loop = conf.asicNum;
    }

    j=0;
    OpenCoreNum1 = conf.OpenCoreNum1;
    OpenCoreNum2 = conf.OpenCoreNum2;
    OpenCoreNum3 = conf.OpenCoreNum3;
    OpenCoreNum4 = conf.OpenCoreNum4;

    for(i = 0; i < loop; i++)
    {
        if(CHAIN_ASIC_NUM == 1)
        {
            for(; j < 128; j++)
            {
                if(j < 32)
                {
                    if(OpenCoreNum1 & 0x00000001)
                    {
                        sprintf(strFilePath, "%s%02i.bin", cgpu.workdataPathPrefix, j+1);
                        printf("dir:%s\n", strFilePath);
                        OpenCoreNum1 = OpenCoreNum1 >> 1;
                        j++;
                        break;
                    }
                    else
                    {
                        OpenCoreNum1 = OpenCoreNum1 >> 1;
                    }
                }
                else if((j >= 32) && (j < 64))
                {
                    if(OpenCoreNum2 & 0x00000001)
                    {
                        sprintf(strFilePath, "%s%02i.bin", cgpu.workdataPathPrefix, j+1);
                        printf("dir:%s\n", strFilePath);
                        OpenCoreNum2 = OpenCoreNum2 >> 1;
                        j++;
                        break;
                    }
                    else
                    {
                        OpenCoreNum2 = OpenCoreNum2 >> 1;
                    }
                }
                else if((j >= 64) && (j < 96))
                {
                    if(OpenCoreNum3 & 0x00000001)
                    {
                        sprintf(strFilePath, "%s%02i.bin", cgpu.workdataPathPrefix, j+1);
                        printf("dir:%s\n", strFilePath);
                        OpenCoreNum3 = OpenCoreNum3 >> 1;
                        j++;
                        break;
                    }
                    else
                    {
                        OpenCoreNum3 = OpenCoreNum3 >> 1;
                    }
                }
                else
                {
                    if(OpenCoreNum4 & 0x00000001)
                    {
                        sprintf(strFilePath, "%s%02i.bin", cgpu.workdataPathPrefix, j+1);
                        printf("dir:%s\n", strFilePath);
                        OpenCoreNum4 = OpenCoreNum4 >> 1;
                        j++;
                        break;
                    }
                    else
                    {
                        OpenCoreNum4 = OpenCoreNum4 >> 1;
                    }
                }
            }
        }
        else
        {
            sprintf(strFilePath, "%s%02i.bin", cgpu.workdataPathPrefix, i+1);
            //applog(LOG_DEBUG, "dir:%s\n", strFilePath);
        }

        cgpu.fps[i] = fopen(strFilePath, "rb");
        if(NULL == cgpu.fps[i])
        {
            applog(LOG_ERR, "Open test file %s error\n", strFilePath);
            return -1;
        }
        cgpu.subid[i] = load_testpatten_work(i, MAX_WORK);
        //applog(LOG_DEBUG, "asic[%d] get work %d\n", i, cgpu.subid[i]);
        fclose(cgpu.fps[i]);
    }

    cgpu.min_work_subid = cgpu.subid[0];
    record = 0;
    for(i = 0; i < loop; i++)
    {
        if(cgpu.min_work_subid > cgpu.subid[i])
        {
            cgpu.min_work_subid = cgpu.subid[i];
            record = i;
        }
    }
    applog(LOG_DEBUG, "min work minertest[%d]:%d\n\n\n", record, cgpu.min_work_subid);
    if(conf.dataCount > cgpu.min_work_subid)
    {
        applog(LOG_ERR, "$$$$dataCount=%d, but min work subid=%d\n",
               conf.dataCount, cgpu.min_work_subid);
        return -1;
    }
    return 0;
}


static int configMiner()
{
    int ret;

    read_config();
    print_config();
    ret = process_config();
    if(ret < 0) return -EFAULT;

    print_CONFIG();
    ret = get_works();
    if(ret < 0) return -EFAULT;
    return 0;
}

static int calculate_asic_number(unsigned int actual_asic_number)
{
    int i = 0;
    if(actual_asic_number == 1)
    {
        i = 1;
    }
    else if(actual_asic_number == 2)
    {
        i = 2;
    }
    else if((actual_asic_number > 2) && (actual_asic_number <= 4))
    {
        i = 4;
    }
    else if((actual_asic_number > 4) && (actual_asic_number <= 8))
    {
        i = 8;
    }
    else if((actual_asic_number > 8) && (actual_asic_number <= 16))
    {
        i = 16;
    }
    else if((actual_asic_number > 16) && (actual_asic_number <= 32))
    {
        i = 32;
    }
    else if((actual_asic_number > 32) && (actual_asic_number <= 64))
    {
        i = 64;
    }
    else if((actual_asic_number > 64) && (actual_asic_number <= 128))
    {
        i = 128;
    }
    else
    {
        applog(LOG_DEBUG,"actual_asic_number = %d, but it is error\n", actual_asic_number);
        return -1;
    }
    return i;
}

static int calculate_core_number(unsigned int actual_core_number)
{
    int i = 0;
    if(actual_core_number == 1)
    {
        i = 1;
    }
    else if(actual_core_number == 2)
    {
        i = 2;
    }
    else if((actual_core_number > 2) && (actual_core_number <= 4))
    {
        i = 4;
    }
    else if((actual_core_number > 4) && (actual_core_number <= 8))
    {
        i = 8;
    }
    else if((actual_core_number > 8) && (actual_core_number <= 16))
    {
        i = 16;
    }
    else if((actual_core_number > 16) && (actual_core_number <= 32))
    {
        i = 32;
    }
    else if((actual_core_number > 32) && (actual_core_number <= 64))
    {
        i = 64;
    }
    else if((actual_core_number > 64) && (actual_core_number <= 128))
    {
        i = 128;
    }
    else
    {
        applog(LOG_DEBUG,"actual_core_number = %d, but it is error\n", actual_core_number);
        return -1;
    }
    return i;
}

static int get_result(int chainIndex, int passCount, int validnonce)
{
    char logstr[1024];
    int ret = 3;
    int i, j=0, loop=0, m, n;
    unsigned int OpenCoreNum1 = conf.OpenCoreNum1;
    unsigned int OpenCoreNum2 = conf.OpenCoreNum2;
    unsigned int OpenCoreNum3 = conf.OpenCoreNum3;
    unsigned int OpenCoreNum4 = conf.OpenCoreNum4;

    printf("\n------------------------------------------------------------------------------------------------------\n");
    if(conf.CommandMode)
    {
        printf("Command mode is FIL\n");
    }
    else
    {
        printf("Command mode is VIL\n");
    }

    if(cgpu.real_asic_num == 1)
    {
        printf("Open core number : Conf.OpenCoreNum1 = %ud = 0x%x\n", Conf.OpenCoreNum1, Conf.OpenCoreNum1);
        printf("Open core number : Conf.OpenCoreNum2 = %ud = 0x%x\n", Conf.OpenCoreNum2, Conf.OpenCoreNum2);
        printf("Open core number : Conf.OpenCoreNum3 = %ud = 0x%x\n", Conf.OpenCoreNum3, Conf.OpenCoreNum3);
        printf("Open core number : Conf.OpenCoreNum4 = %ud = 0x%x\n", Conf.OpenCoreNum4, Conf.OpenCoreNum4);
        loop = Conf.CoreNum;
    }
    else
    {
        loop = cgpu.real_asic_num;
    }
    sprintf(logstr,"require nonce number:%d\n", passCount);
    writeLogFile(logstr);

    sprintf(logstr,"require validnonce number:%d\n", validnonce);
    writeLogFile(logstr);

    for(i = 0; i < loop; i++)
    {
#ifdef LOG_CHIPS_CORE_DETAIL
        if(cgpu.real_asic_num == 1)
        {
            sprintf(logstr,"core[%02d]=%02d\t", i, asic_nonce_num[chainIndex][i]);
            writeLogFile(logstr);
        }
        else
        {
            sprintf(logstr,"asic[%02d]=%02d\t", i, asic_nonce_num[chainIndex][i]);
            writeLogFile(logstr);
        }

        if(i % 8 == 7)
        {
            sprintf(logstr,"\n");
            writeLogFile(logstr);
        }
#endif
        if(cgpu.real_asic_num == 1)
        {
            for(; j < 128; j++)
            {
                if(j < 32)
                {
                    if(OpenCoreNum1 & 0x00000001)
                    {
                        if(asic_nonce_num[chainIndex][j] < passCount)
                        {
                            ret = (~0x00000001) & ret;
                        }
                        OpenCoreNum1 = OpenCoreNum1 >> 1;
                    }
                    else
                    {
                        OpenCoreNum1 = OpenCoreNum1 >> 1;
                    }
                }
                else if((j >= 32) && (j < 64))
                {
                    if(OpenCoreNum2 & 0x00000001)
                    {
                        if(asic_nonce_num[chainIndex][j] < passCount)
                        {
                            ret = (~0x00000001) & ret;
                        }
                        OpenCoreNum2 = OpenCoreNum2 >> 1;
                    }
                    else
                    {
                        OpenCoreNum2 = OpenCoreNum2 >> 1;
                    }
                }
                else if((j >= 64) && (j < 96))
                {
                    if(OpenCoreNum3 & 0x00000001)
                    {
                        if(asic_nonce_num[chainIndex][j] < passCount)
                        {
                            ret = (~0x00000001) & ret;
                        }
                        OpenCoreNum3 = OpenCoreNum3 >> 1;
                    }
                    else
                    {
                        OpenCoreNum3 = OpenCoreNum3 >> 1;
                    }
                }
                else
                {
                    if(OpenCoreNum4 & 0x00000001)
                    {
                        if(asic_nonce_num[chainIndex][j] < passCount)
                        {
                            ret = (~0x00000001) & ret;
                        }
                        OpenCoreNum4 = OpenCoreNum4 >> 1;
                    }
                    else
                    {
                        OpenCoreNum4 = OpenCoreNum4 >> 1;
                    }
                }
            }
        }
        else
        {
            if(asic_nonce_num[chainIndex][i] < passCount)
            {
                ret = (~0x00000001) & ret;
            }
        }
    }

    if((Conf.StartSensor > 0) && (cgpu.real_asic_num != 1))
    {
        n = passCount/Conf.CoreNum;
#ifdef LOG_CHIPS_CORE_DETAIL
        sprintf(logstr,"\n\n\nBelow ASIC's core didn't receive all the nonce, they should receive %d nonce each!\n\n", n);
        writeLogFile(logstr);
#endif
        for(i = 0; i < loop; i++)
        {
            int opened_core_num=0;
            for(m=0; m<Conf.CoreNum; m++)
            {
                if(asic_core_nonce_num[chainIndex][i][m]>0) // we only check core is open
                    opened_core_num++;
            }

            if(opened_core_num >= Conf.CoreNum-chain_badcore_num[chainIndex][i])
                last_result_opencore[chainIndex][i]=1;
            else last_result_opencore[chainIndex][i]=0;

            if(asic_nonce_num[chainIndex][i] < passCount-chain_badcore_num[chainIndex][i]*n)
                last_result[chainIndex][i]=0;
            else last_result[chainIndex][i]=1;

#ifdef LOG_CHIPS_CORE_DETAIL
            if(asic_nonce_num[chainIndex][i] < passCount)
            {
                sprintf(logstr,"asic[%02d]=%02d\n", i, asic_nonce_num[chainIndex][i]);
                writeLogFile(logstr);

                for(m=0; m<Conf.CoreNum; m++)
                {
                    if(asic_core_nonce_num[chainIndex][i][m] != n)
                    {
                        sprintf(logstr,"core[%03d]=%d\t", m, asic_core_nonce_num[chainIndex][i][m]);
                        writeLogFile(logstr);
                    }
                }
                sprintf(logstr,"\n\n");
                writeLogFile(logstr);
            }
#endif
        }
    }

    sprintf(logstr,"\n\n");
    writeLogFile(logstr);

    for(i = 0; i < loop; i++)
    {
        sprintf(logstr,"freq[%02d]=%d\t", i, get_freqvalue_by_index(getChainAsicFreqIndex(chainIndex,i)));
        writeLogFile(logstr);

        if(i % 8 == 7)
        {
            sprintf(logstr,"\n");
            writeLogFile(logstr);
        }
    }

    sprintf(logstr,"\n\n");
    writeLogFile(logstr);

    if(valid_nonce_num[chainIndex] < validnonce)
    {
        ret = (~0x00000001) & ret;
    }

    sprintf(logstr,"total valid nonce number:%d\n", valid_nonce_num[chainIndex]);
    writeLogFile(logstr);
    sprintf(logstr,"total send work number:%d\n", send_work_num[chainIndex]);
    writeLogFile(logstr);
    sprintf(logstr,"require valid nonce number:%d\n", validnonce);
    writeLogFile(logstr);

    sprintf(logstr,"repeated_nonce_num:%d\n", repeated_nonce_num[chainIndex]);
    writeLogFile(logstr);
    sprintf(logstr,"err_nonce_num:%d\n", err_nonce_num[chainIndex]);
    writeLogFile(logstr);
    sprintf(logstr,"last_nonce_num:%d\n", last_nonce_num[chainIndex]);
    writeLogFile(logstr);
    return ret;
}



////////////////////////////////////TEST PATTEN MAIN.C /////////////////////////////////
static int reset_work_data(void)
{
    int i, j;
    for(i =0 ; i < conf.asicNum; i++)
    {
        for(j = 0; j < conf.dataCount; j++)
        {
            cgpu.results[i][j] = 0;
        }
        cgpu.result_array[i] = 0;
    }
    cgpu.index = 0;
    cgpu.valid_nonce = 0;
    cgpu.err_nonce = 0;
    cgpu.repeated_nonce = 0;
    return 0;
}

static int cgpu_init(void)
{
    int ret = 0;
    memset(&cgpu, 0, sizeof(struct testpatten_cgpu_info));
    return 0;
}

static int send_func_all()
{
    int which_asic[BITMAIN_MAX_CHAIN_NUM];
    int i,j;
    unsigned int work_fifo_ready = 0;
    int index[BITMAIN_MAX_CHAIN_NUM];
    struct testpatten_work * works, *work;
    unsigned char data_fil[TW_WRITE_COMMAND_LEN] = {0xff};
    unsigned char data_vil[TW_WRITE_COMMAND_LEN_VIL] = {0xff};
    struct vil_work_1387 work_vil_1387;
    unsigned int buf[TW_WRITE_COMMAND_LEN/sizeof(unsigned int)]= {0};
    unsigned int buf_vil[TW_WRITE_COMMAND_LEN_VIL/sizeof(unsigned int)]= {0};
    int chainIndex;
    char logstr[1024];
    bool isSendOver=false;
    int wait_counter=0;
    bool sendStartFlag[BITMAIN_MAX_CHAIN_NUM];

    for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
    {
        index[i]=0;
        which_asic[i]=0;
        sendStartFlag[i]=StartSendFlag[i];
    }

    while(!isSendOver)
    {
        // send work
        for(chainIndex=0; chainIndex<BITMAIN_MAX_CHAIN_NUM; chainIndex++)
        {
            if(cgpu.chain_exist[chainIndex] == 0 || (!sendStartFlag[chainIndex]))
                continue;

            while(which_asic[chainIndex] < CHAIN_ASIC_NUM)
            {
                work_fifo_ready = get_buffer_space();
                if(work_fifo_ready & (0x1 << chainIndex))   // work fifo is not full, we can send work
                {
                    wait_counter=0; // clear wait fifo counter

                    if(cgpu.CommandMode)    // fil mode
                    {
                        memset(buf, 0x0, TW_WRITE_COMMAND_LEN/sizeof(unsigned int));

                        // get work for sending to asic
                        works = cgpu.works[which_asic[chainIndex]]; // which ASIC
                        work = works + index[chainIndex];      // which test data for the ASIC

                        // parse work data
                        memset(data_fil, 0x0, TW_WRITE_COMMAND_LEN);
                        data_fil[0] = NORMAL_BLOCK_MARKER;
                        data_fil[1] = chainIndex | 0x80; //set chain id and enable it
                        for(i=0; i<MIDSTATE_LEN; i++)
                        {
                            data_fil[i+4] = work->midstate[i];
                        }
                        for(i=0; i<DATA2_LEN; i++)
                        {
                            data_fil[i+40] = work->data[i];
                        }

                        // send work
                        //printf("\n");
                        for(j=0; j<TW_WRITE_COMMAND_LEN/sizeof(unsigned int); j++)
                        {
                            buf[j] = (data_fil[4*j + 0] << 24) | (data_fil[4*j + 1] << 16) | (data_fil[4*j + 2] << 8) | data_fil[4*j + 3];
                            if(j==9)
                            {
                                buf[j] = index[chainIndex];
                            }
                            //applog(LOG_DEBUG,"%s: buf[%d] = 0x%08x\n", __FUNCTION__, j, buf[j]);
                        }

#ifndef DEBUG_XILINX_NONCE_NOTENOUGH
                        pthread_mutex_lock(&opencore_readtemp_mutex);
#endif
                        set_TW_write_command(buf);

#ifndef DEBUG_XILINX_NONCE_NOTENOUGH
                        pthread_mutex_unlock(&opencore_readtemp_mutex);
#endif
                        which_asic[chainIndex]++;
                    }
                    else    // vil mode
                    {
                        if(ASIC_TYPE == 1387)
                        {
                            //printf("\n--- send work\n");
                            memset(buf_vil, 0x0, TW_WRITE_COMMAND_LEN_VIL/sizeof(unsigned int));

                            works = cgpu.works[which_asic[chainIndex]]; // which ASIC
                            work = works + index[chainIndex];      // which test data for the ASIC

                            // parse work data
                            memset(&work_vil_1387, 0, sizeof(struct vil_work_1387));
                            work_vil_1387.work_type = NORMAL_BLOCK_MARKER;
                            work_vil_1387.chain_id = 0x80 | chainIndex;
                            work_vil_1387.reserved1[0]= 0;
                            work_vil_1387.reserved1[1]= 0;
                            work_vil_1387.work_count = index[chainIndex];
                            for(i=0; i<DATA2_LEN; i++)
                            {
                                work_vil_1387.data[i] = work->data[i];
                            }
                            for(i=0; i<MIDSTATE_LEN; i++)
                            {
                                work_vil_1387.midstate[i] = work->midstate[i];
                            }

                            // send work
                            buf_vil[0] = (work_vil_1387.work_type << 24) | (work_vil_1387.chain_id << 16) | (work_vil_1387.reserved1[0] << 8) | work_vil_1387.reserved1[1];
                            buf_vil[1] = work_vil_1387.work_count;
                            for(j=2; j<DATA2_LEN/sizeof(int)+2; j++)
                            {
                                buf_vil[j] = (work_vil_1387.data[4*(j-2) + 0] << 24) | (work_vil_1387.data[4*(j-2) + 1] << 16) | (work_vil_1387.data[4*(j-2) + 2] << 8) | work_vil_1387.data[4*(j-2) + 3];
                            }
                            for(j=5; j<MIDSTATE_LEN/sizeof(unsigned int)+5; j++)
                            {
                                buf_vil[j] = (work_vil_1387.midstate[4*(j-5) + 0] << 24) | (work_vil_1387.midstate[4*(j-5) + 1] << 16) | (work_vil_1387.midstate[4*(j-5) + 2] << 8) | work_vil_1387.midstate[4*(j-5) + 3];;
                            }

#ifndef DEBUG_XILINX_NONCE_NOTENOUGH
                            pthread_mutex_lock(&opencore_readtemp_mutex);
#endif
                            set_TW_write_command_vil(buf_vil);

#ifndef DEBUG_XILINX_NONCE_NOTENOUGH
                            pthread_mutex_unlock(&opencore_readtemp_mutex);
#endif
                            which_asic[chainIndex]++;

                        }
                        else
                        {
                            //printf("\n--- send work\n");
                            memset(buf_vil, 0x0, TW_WRITE_COMMAND_LEN_VIL/sizeof(unsigned int));
                            // get work for sending to asic
                            //work_fil = (struct testpatten_work *)((void *)cgpu.works[which_asic] + index*sizeof(struct testpatten_work));
                            works = cgpu.works[which_asic[chainIndex]]; // which ASIC
                            work = works + index[chainIndex];      // which test data for the ASIC

                            // parse work data
                            memset(data_vil, 0x00, TW_WRITE_COMMAND_LEN_VIL);
                            data_vil[0] = NORMAL_BLOCK_MARKER;
                            data_vil[1] = chainIndex | 0x80; //set chain id and enable it
                            data_vil[4] = 0x01 << 5;                // type
                            data_vil[5] = sizeof(struct vil_work);  // length
                            data_vil[6] = index[chainIndex];               // wc_base / work_id
                            data_vil[7] = 0x01;                     // mid_num

                            for(i=0; i<MIDSTATE_LEN; i++)
                            {
                                data_vil[i+8] = work->midstate[i];
                            }
                            for(i=0; i<DATA2_LEN; i++)
                            {
                                data_vil[i+40] = work->data[i];
                            }

                            // send work
                            for(j=0; j<TW_WRITE_COMMAND_LEN_VIL/sizeof(unsigned int); j++)
                            {
                                buf_vil[j] = (data_vil[4*j + 0] << 24) | (data_vil[4*j + 1] << 16) | (data_vil[4*j + 2] << 8) | data_vil[4*j + 3];
                                //printf("%s: buf_vil[%d] = 0x%08x\n", __FUNCTION__, j, buf_vil[j]);
                            }

#ifndef DEBUG_XILINX_NONCE_NOTENOUGH
                            pthread_mutex_lock(&opencore_readtemp_mutex);
#endif
                            set_TW_write_command_vil(buf_vil);

#ifndef DEBUG_XILINX_NONCE_NOTENOUGH
                            pthread_mutex_unlock(&opencore_readtemp_mutex);
#endif
                            which_asic[chainIndex]++;
                        }
                    }
                    send_work_num[chainIndex]++;
                    //printf("%s: send_work_num = %d\n", __FUNCTION__, send_work_num);
                }
                else    //work fifo is full, wait for 1ms
                {
                    wait_counter++;
                    break;
                }
            }

            if(which_asic[chainIndex] >= CHAIN_ASIC_NUM)
            {
                which_asic[chainIndex]=0;   // then send from chip[0] ....
                index[chainIndex]++;    // switch to next work
                if(index[chainIndex] >= chain_DataCount[chainIndex])
                    sendStartFlag[chainIndex]=false;
            }

            if(wait_counter>2000)
            {
                // timeout on wait for fifo ready
                sprintf(logstr,"Fatal Error: send work timeout\n");
                writeLogFile(logstr);
                break;
            }
        }
        usleep(5000);

        isSendOver=true;
        for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
        {
            if(cgpu.chain_exist[i] == 0 || (!StartSendFlag[i]))
                continue;

            if(index[i] < chain_DataCount[i])
            {
                isSendOver=false;
            }
        }
    }

    for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
    {
        if(cgpu.chain_exist[i] == 0 || (!StartSendFlag[i]))
            continue;

        StartSendFlag[i]=false; // when send over , must set this flag to false!!!
        sprintf(logstr,"get send work num :%d on Chain[%d]\n", send_work_num[i],i);
        writeLogFile(logstr);

        sendExit[i]=true;
    }

    return 0;
}

static uint32_t last_nonce[BITMAIN_MAX_CHAIN_NUM], llast_nonce[BITMAIN_MAX_CHAIN_NUM];
static unsigned int work_id[BITMAIN_MAX_CHAIN_NUM];
static unsigned int m_nonce[BITMAIN_MAX_CHAIN_NUM];
static void *receive_func(void *arg)
{
    unsigned int j=0, n=0, nonce_number = 0, read_loop=0;
    unsigned int buf[2] = {0,0};

    uint8_t which_asic_nonce = 0;
    uint8_t which_core_nonce = 0;
    uint8_t whose_nonce = 0, nonce_index=0;
    unsigned int OpenCoreNum1 = conf.OpenCoreNum1;
    unsigned int OpenCoreNum2 = conf.OpenCoreNum2;
    unsigned int OpenCoreNum3 = conf.OpenCoreNum3;
    unsigned int OpenCoreNum4 = conf.OpenCoreNum4;
    char logstr[1024];
    int chainIndex;

    memset(repeated_nonce_id, 0xff, sizeof(repeated_nonce_id));
    memset(last_nonce,0x00,sizeof(last_nonce));
    memset(llast_nonce,0x00,sizeof(llast_nonce));
    memset(work_id,0x00,sizeof(work_id));
    memset(m_nonce,0x00,sizeof(m_nonce));

    while(!ExitFlag)
    {
        if(!start_receive)
        {
            j=0;
            n=0;
            nonce_number = 0;
            read_loop=0;
            buf[0]=0;
            buf[1]=0;

            which_asic_nonce = 0;
            which_core_nonce = 0;
            whose_nonce = 0;
            nonce_index=0;
            OpenCoreNum1 = conf.OpenCoreNum1;
            OpenCoreNum2 = conf.OpenCoreNum2;
            OpenCoreNum3 = conf.OpenCoreNum3;
            OpenCoreNum4 = conf.OpenCoreNum4;

            memset(repeated_nonce_id, 0xff, sizeof(repeated_nonce_id));
            memset(last_nonce,0x00,sizeof(last_nonce));
            memset(llast_nonce,0x00,sizeof(llast_nonce));
            memset(work_id,0x00,sizeof(work_id));
            memset(m_nonce,0x00,sizeof(m_nonce));

            usleep(100000);
            continue;
        }

        read_loop = 0;

        nonce_number = get_nonce_number_in_fifo() & MAX_NONCE_NUMBER_IN_FIFO;
        //applog(LOG_DEBUG,"%s: --- nonce_number = %d\n", __FUNCTION__, nonce_number);
        if(nonce_number>0)
        {
            read_loop = nonce_number;
            //applog(LOG_DEBUG,"%s: read_loop = %d\n", __FUNCTION__, read_loop);

            for(j=0; j<read_loop; j++)
            {
                get_return_nonce(buf);
                //printf("%s: buf[0] = 0x%08x\n", __FUNCTION__, buf[0]);
                //printf("%s: buf[1] = 0x%08x\n", __FUNCTION__, buf[1]);
                if(buf[0] & WORK_ID_OR_CRC) //nonce
                {
                    if(gBegin_get_nonce)
                    {
                        if(buf[0] & NONCE_INDICATOR)
                        {
                            chainIndex=CHAIN_NUMBER(buf[0]);
                            if(chainIndex<0 || chainIndex>=BITMAIN_MAX_CHAIN_NUM)
                            {
                                sprintf(logstr,"Error chain index of nonce!!!\n");
                                writeLogFile(logstr);
                                continue;
                            }

                            if(cgpu.CommandMode)    // fil mode
                            {
                                work_id[chainIndex] = (buf[0] >> 16) & 0x00007fff;
                            }
                            else    // vil mode
                            {
                                if(ASIC_TYPE == 1387)
                                {
                                    work_id[chainIndex] = (buf[0] >> 16) & 0x00007fff;
                                }
                                else
                                {
                                    work_id[chainIndex] = (buf[0] >> 24) & 0x0000007f;
                                }
                            }

                            if((buf[1] == last_nonce[chainIndex]) || (buf[1] == llast_nonce[chainIndex]))
                            {
                                last_nonce_num[chainIndex]++;
                                continue;
                            }

                            if(cgpu.real_asic_num == 1)
                            {
                                if(conf.core <= 64)
                                {
                                    which_core_nonce = (buf[1] & 0x0000003f);
                                    whose_nonce = which_core_nonce;
                                }
                                else if((conf.core <= 128) && (conf.core > 64))
                                {
                                    which_core_nonce = (buf[1] & 0x0000007f);
                                    if(which_core_nonce <= 56)
                                    {
                                        whose_nonce = which_core_nonce;
                                    }
                                    else if((which_core_nonce >= 64) && (which_core_nonce < 128))
                                    {
                                        whose_nonce = which_core_nonce - 7;
                                    }
                                }
                                else
                                {
                                    printf("%s: conf.core = %d, but it is error\n", __FUNCTION__, conf.core);
                                }
                                nonce_index = 0;
                                OpenCoreNum1 = conf.OpenCoreNum1;
                                OpenCoreNum2 = conf.OpenCoreNum2;
                                OpenCoreNum3 = conf.OpenCoreNum3;
                                OpenCoreNum4 = conf.OpenCoreNum4;

                                for(n=0; n<whose_nonce; n++)
                                {
                                    if(n < 32)
                                    {
                                        if(OpenCoreNum1 & 0x00000001)
                                        {
                                            nonce_index++;
                                            OpenCoreNum1 = OpenCoreNum1 >> 1;
                                        }
                                        else
                                        {
                                            OpenCoreNum1 = OpenCoreNum1 >> 1;
                                        }
                                    }
                                    else if((n >= 32) && (n < 64))
                                    {
                                        if(OpenCoreNum2 & 0x00000001)
                                        {
                                            nonce_index++;
                                            OpenCoreNum2 = OpenCoreNum2 >> 1;
                                        }
                                        else
                                        {
                                            OpenCoreNum2 = OpenCoreNum2 >> 1;
                                        }
                                    }
                                    else if((n >= 64) && (n < 96))
                                    {
                                        if(OpenCoreNum3 & 0x00000001)
                                        {
                                            nonce_index++;
                                            OpenCoreNum3 = OpenCoreNum3 >> 1;
                                        }
                                        else
                                        {
                                            OpenCoreNum3 = OpenCoreNum3 >> 1;
                                        }
                                    }
                                    else
                                    {
                                        if(OpenCoreNum4 & 0x00000001)
                                        {
                                            nonce_index++;
                                            OpenCoreNum4 = OpenCoreNum4 >> 1;
                                        }
                                        else
                                        {
                                            OpenCoreNum4 = OpenCoreNum4 >> 1;
                                        }
                                    }
                                }
                                //printf("%s: nonce_index = 0x%08x\n", __FUNCTION__, nonce_index);
                            }
                            else
                            {
                                if(CHIP_ADDR_INTERVAL != 0)
                                {
                                    which_asic_nonce = (buf[1] >> 24) / CHIP_ADDR_INTERVAL;
                                    if(which_asic_nonce >= CHAIN_ASIC_NUM)
                                    {
                                        continue;
                                    }
                                    //printf("%s: which_asic = %d\n", __FUNCTION__, which_asic);
                                }
                                else
                                {
                                    //printf("CHIP_ADDR_INTERVAL==0, default=4\n");
                                    which_asic_nonce = (buf[1] >> 24) / 4;
                                    if(which_asic_nonce >= conf.asicNum)
                                    {
                                        continue;
                                    }
                                }
                                whose_nonce = which_asic_nonce;
                                nonce_index = which_asic_nonce;
                            }
                            //printf("%s: whose_nonce = 0x%08x\n", __FUNCTION__, whose_nonce);

                            llast_nonce[chainIndex] = last_nonce[chainIndex];
                            last_nonce[chainIndex] = buf[1];

                            if(work_id[chainIndex]>=MAX_WORK)
                                continue;

                            m_nonce[chainIndex] = (cgpu.works[nonce_index] + work_id[chainIndex])->nonce;
                            //printf("%s: m_nonce = 0x%08x\n", __FUNCTION__, m_nonce);

                            if(buf[1] == m_nonce[chainIndex])
                            {
                                //printf("%s: repeated_nonce_id[which_asic] = 0x%08x\n", __FUNCTION__, repeated_nonce_id[which_asic]);

                                if(work_id[chainIndex] != repeated_nonce_id[chainIndex][whose_nonce])
                                {
                                    repeated_nonce_id[chainIndex][whose_nonce] = work_id[chainIndex];
                                    asic_nonce_num[chainIndex][whose_nonce]++;
                                    valid_nonce_num[chainIndex]++;

                                    total_valid_nonce_num++;    // used to check and wait all nonce back...

                                    if(cgpu.real_asic_num != 1)
                                    {
                                        if(conf.core <= 64)
                                        {
                                            which_core_nonce = (buf[1] & 0x0000003f);
                                        }
                                        else if((conf.core <= 128) && (conf.core > 64))
                                        {
                                            which_core_nonce = (buf[1] & 0x0000007f);
                                            if(which_core_nonce <= 56)
                                            {
                                                which_core_nonce = which_core_nonce;
                                            }
                                            else if((which_core_nonce >= 64) && (which_core_nonce < 128))
                                            {
                                                which_core_nonce = which_core_nonce - 7;
                                            }
                                        }
                                        else
                                        {
                                            printf("%s: conf.core = %d, but it is error\n", __FUNCTION__, conf.core);
                                        }
                                        asic_core_nonce_num[chainIndex][whose_nonce][which_core_nonce]++;
                                    }

                                }
                                else
                                {
                                    repeated_nonce_num[chainIndex]++;
                                    //printf("repeat nonce 0x%08x\n", buf[1]);
                                }
                            }
                            else
                            {
                                err_nonce_num[chainIndex]++;
                                //printf("error nonce 0x%08x\n", buf[1]);
                            }
                            //printf("\n");
                        }
                    }
                }
                else    //reg value
                {
                    insert_reg_data(buf);   // insert to driver-btm-c5.c reg buffer
                }
            }
        }
        else usleep(1000);
    }

    receiveExit=true;
    return 0;
}

static bool doTestBoard(int test_times)
{
    int i, freq_index = 0;
    char logstr[1024];
    int wait_count=0;
    int last_send_num;
    int last_recv_num;
    int vol_value, vol_pic, vol_value_limited;
    bool result_flag=true;

    memset(asic_nonce_num, 0, sizeof(asic_nonce_num));
    memset(asic_core_nonce_num, 0, sizeof(asic_core_nonce_num));
    memset(repeated_nonce_id, 0xff, sizeof(repeated_nonce_id));
    memset(err_nonce_num, 0, sizeof(err_nonce_num));
    memset(last_nonce_num, 0, sizeof(last_nonce_num));
    memset(repeated_nonce_num, 0, sizeof(repeated_nonce_num));
    memset(valid_nonce_num, 0, sizeof(valid_nonce_num));
    memset(send_work_num, 0, sizeof(send_work_num));

    total_valid_nonce_num=0;

    start_receive=true;

    FOR_LOOP_CHAIN
    {
        cgpu.chain_exist[i] = getChainExistFlag(i);
    }

#ifndef T9_18
    sprintf(logstr,"Check voltage total rate=%d\n",GetTotalRate());
    writeLogFile(logstr);

    for(i=0; i < BITMAIN_MAX_CHAIN_NUM; i++)  // here must use i from 0 in for loop, because we use j to get the index as config file's voltage value
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        pthread_mutex_lock(&iic_mutex);
        vol_pic=get_pic_voltage(i);
        pthread_mutex_unlock(&iic_mutex);

        vol_value = getVolValueFromPICvoltage(vol_pic);

        chain_vol_value[i]=(vol_value/10)*10;   // must record current voltage!!!

        vol_value_limited=getVoltageLimitedFromHashrate(GetTotalRate());

        sprintf(logstr,"get PIC voltage=%d [%d] on chain[%d], check: must be < %d\n",chain_vol_value[i],vol_pic,i,vol_value_limited);
        writeLogFile(logstr);

        if(chain_vol_value[i] > vol_value_limited)  // we will set voltage to the highest voltage for the last chance on test patten
        {
            chain_vol_value[i]=vol_value_limited;
            sprintf(logstr,"will set the voltage limited on chain[%d], change voltage=%d\n",i,chain_vol_value[i]);
            writeLogFile(logstr);

            vol_pic=getPICvoltageFromValue(chain_vol_value[i]);
            sprintf(logstr,"now set pic voltage=%d on chain[%d]\n",vol_pic,i);
            writeLogFile(logstr);

            pthread_mutex_lock(&iic_mutex);
            set_pic_voltage(i, vol_pic);
            pthread_mutex_unlock(&iic_mutex);

            someBoardUpVoltage=true;
        }
    }
#endif

    reset_work_data();

    cgpu.CommandMode = 0;
    cgpu.AsicType = ASIC_TYPE;
    cgpu.asicNum = conf.asicNum;
    cgpu.real_asic_num = CHAIN_ASIC_NUM;
    cgpu.core_num = conf.core;

    pthread_mutex_lock(&opencore_readtemp_mutex);
    FOR_LOOP_CHAIN
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        cgpu.chain_asic_num[i]=getChainAsicNum(i);

        if(chain_need_opencore[i])
        {
            sprintf(logstr,"do open core on Chain[%d]...\n",i);
            writeLogFile(logstr);

            open_core_one_chain(i,true);

            sprintf(logstr,"Done open core on Chain[%d]!\n",i);
            writeLogFile(logstr);
        }
    }
    pthread_mutex_unlock(&opencore_readtemp_mutex);

    // before the first time for sending work, reset the FPGA's nonce fifo
    if(!gBegin_get_nonce)
    {
        //printf("\n--- clear nonce fifo before send work\n");
        printf("clement2 set_nonce_fifo_interrupt\n");
        set_nonce_fifo_interrupt(get_nonce_fifo_interrupt() | FLUSH_NONCE3_FIFO);
        gBegin_get_nonce = true;
    }

    FOR_LOOP_CHAIN
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        sprintf(logstr,"start send works on chain[%d]\n",i);
        writeLogFile(logstr);

        StartSendFlag[i]=true;
    }

    send_func_all();

    for(i=0; i < BITMAIN_MAX_CHAIN_NUM; i++)
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        sprintf(logstr,"wait recv nonce on chain[%d]\n",i);
        writeLogFile(logstr);

        last_recv_num=0;
        wait_count=0;
        while(wait_count < RECV_WAIT_TIMEOUT && valid_nonce_num[i]<chain_ValidNonce[i])
        {
            if(last_recv_num!=valid_nonce_num[i])
            {
                wait_count=0;
                last_recv_num=valid_nonce_num[i];
            }
            else wait_count++;

            usleep(100000);
        }
    }

    gBegin_get_nonce=false;
    start_receive=false;

    FOR_LOOP_CHAIN
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        sprintf(logstr,"get nonces on chain[%d]\n",i);
        writeLogFile(logstr);

        result = get_result(i, chain_PassCount[i], chain_ValidNonce[i]);
    }

    result_flag=true;

    FOR_LOOP_CHAIN
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        if(last_all_core_opened(i))
        {
            sprintf(logstr,"chain[%d]: All chip cores are opened OK!\n",i);
            writeLogFile(logstr);

            chain_need_opencore[i]=false;

            isChainAllCoresOpened[i]=true;
        }
        else
        {
            sprintf(logstr,"chain[%d]: some chip cores are not opened FAILED!\n",i);
            writeLogFile(logstr);

            chain_need_opencore[i]=true;    // next time , force to re-open core again if open core failed!

            isChainAllCoresOpened[i]=false;
        }

        if(last_all_pass(i))
        {
            sprintf(logstr,"Test Patten on chain[%d]: OK!\n",i);
            writeLogFile(logstr);
        }
        else
        {
            result_flag=false;

            sprintf(logstr,"Test Patten on chain[%d]: FAILED!\n",i);
            writeLogFile(logstr);

#ifndef T9_18
#ifdef CHECK_ALLNONCE_ADD_VOLTAGE_USERMODE
            if(readRestartNum()>0 && isChainEnough())   // up voltage is not suitable for T9+
            {
                sprintf(logstr,"Try to add voltage on chain[%d]...\n",i);
                writeLogFile(logstr);

                vol_value=getVoltageLimitedFromHashrate(GetTotalRate());

                if(test_times>=PRE_HEAT_TEST_COUNT-1 && chain_vol_value[i]<vol_value)   // we will set voltage to the highest voltage for the last chance on test patten
                {
                    chain_vol_value[i]=vol_value;
                    sprintf(logstr,"will set the voltage limited on chain[%d], change voltage=%d\n",i,chain_vol_value[i]);
                    writeLogFile(logstr);

                    vol_pic=getPICvoltageFromValue(chain_vol_value[i]);
                    sprintf(logstr,"now set pic voltage=%d on chain[%d]\n",vol_pic,i);
                    writeLogFile(logstr);

                    pthread_mutex_lock(&iic_mutex);
                    set_pic_voltage(i, vol_pic);
                    pthread_mutex_unlock(&iic_mutex);

                    someBoardUpVoltage=true;
                }
                else if(chain_vol_value[i]+10<=vol_value)
                {
                    chain_vol_value[i]+=10;
                    sprintf(logstr,"Can add 0.1V on chain[%d], change voltage=%d\n",i,chain_vol_value[i]);
                    writeLogFile(logstr);

                    vol_pic=getPICvoltageFromValue(chain_vol_value[i]);
                    sprintf(logstr,"now set pic voltage=%d on chain[%d]\n",vol_pic,i);
                    writeLogFile(logstr);

                    pthread_mutex_lock(&iic_mutex);
                    set_pic_voltage(i, vol_pic);
                    pthread_mutex_unlock(&iic_mutex);

                    someBoardUpVoltage=true;
                }
            }
#endif
#endif
            search_freq_result[i]=false;
        }
    }

    return result_flag;
}

static bool showLogToKernelLog=true;
static void writeLogFile(char *logstr)
{
    if(showLogToKernelLog)
        writeInitLogFile(logstr);
}

static int init_once=1;

bool clement_doTestBoard(bool showlog)
{
    int run_count = 0;
    int ret, i,j,k;
    char logstr[1024];
    bool doOnce=true;
    int wait_count;
    int rebootTestNum;  // for searching process, to reboot 3times to check hashrate.
    int restartMinerNum;    // the number of chances to reboot miner, for sometime hashrate is low when first startup.
    bool result_flag;

    showLogToKernelLog=showlog;

    if(init_once>0)
    {
        ret = cgpu_init();
        if(ret < 0)
        {
            printf("cgpu_init Error!\n");
            return false;
        }

        ret = configMiner();
        if(ret < 0)
        {
            printf("configMiner Error!\n");
            return false;
        }

        init_once=0;

        printf("single board test start\n");

        Conf.DataCount=conf.dataCount=TESTMODE_PATTEN_NUM;  // fixed to 114
        Conf.PassCount1=conf.passCount1=TESTMODE_PATTEN_NUM;
        Conf.PassCount2=conf.passCount2=TESTMODE_PATTEN_NUM;
        Conf.PassCount3=conf.passCount3=TESTMODE_PATTEN_NUM;
        Conf.ValidNonce1=conf.ValidNonce1=TESTMODE_NONCE_NUM;
        Conf.ValidNonce2=conf.ValidNonce2=TESTMODE_NONCE_NUM;
        Conf.ValidNonce3=conf.ValidNonce3=TESTMODE_NONCE_NUM;

        ExitFlag=false;

        receiveExit=false;
        pthread_create(&cgpu.receive_id, NULL, receive_func, &cgpu);

        for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
        {
            StartSendFlag[i]=false;
        }
    }

    for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
    {
        testModeOKCounter[i]=0;
        for(j=0; j<256; j++)
        {
            last_result[i][j] = 0 ;
            last_result_opencore[i][j] = 0 ;
        }

        // force to test all boards at any time
        chain_vol_value[i]=0;
        chain_vol_final[i]=0;
        chain_vol_added[i]=0;
        search_freq_result[i]=true;

        chain_DataCount[i]=TESTMODE_PATTEN_NUM; // when seaching base freq, we use 8*144 patten on chip
        chain_ValidNonce[i]=TESTMODE_NONCE_NUM;
        chain_PassCount[i]=TESTMODE_PATTEN_NUM;

        chain_need_opencore[i]=false;   // init must be false, because chip cores are opened in driver-btm-c5.c
    }

    k=0;
    do
    {
        k++;

        sprintf(logstr,"do heat board 8xPatten for %d times\n",k);
        writeLogFile(logstr);

        for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
        {
            for(j=0; j<256; j++)
            {
                last_result[i][j] = 0 ;
                last_result_opencore[i][j] = 0 ;
            }

            // force to test all boards at any time
            search_freq_result[i]=true;

            chain_DataCount[i]=TESTMODE_PATTEN_NUM; // when seaching base freq, we use 8*144 patten on chip
            chain_ValidNonce[i]=TESTMODE_NONCE_NUM;
            chain_PassCount[i]=TESTMODE_PATTEN_NUM;
        }

        result_flag=doTestBoard(k);

        for(i=0; i < BITMAIN_MAX_CHAIN_NUM; i++)
        {
            if(cgpu.chain_exist[i]==0)
                continue;

            if(search_freq_result[i])
            {
                testModeOKCounter[i]++;
            }
        }

        if(result_flag) // if test paten OK, we stop preheat
            break;
    }
    while(k<PRE_HEAT_TEST_COUNT);

    if(isAllChainChipCoreOpened())
    {
        result_flag=true;
        someBoardUpVoltage=false;   // if all chip core opened, then we do not re-init again!!!
    }
    else
    {
        result_flag=false;
        someBoardUpVoltage=true;
    }

    set_PWM(100);   // when exit preheat, set full speed of fan
    return result_flag;
}

bool clement_doTestBoardOnce(bool showlog)
{
    int run_count = 0;
    int ret, i,j;
    char logstr[1024];
    bool doOnce=true;
    int wait_count;
    int rebootTestNum;  // for searching process, to reboot 3times to check hashrate.
    int restartMinerNum;    // the number of chances to reboot miner, for sometime hashrate is low when first startup.

    showLogToKernelLog=showlog;

    if(init_once>0)
    {
        ret = cgpu_init();
        if(ret < 0)
        {
            printf("cgpu_init Error!\n");
            return false;
        }

        ret = configMiner();
        if(ret < 0)
        {
            printf("configMiner Error!\n");
            return false;
        }

        init_once=0;

        printf("single board test start\n");

        Conf.DataCount=conf.dataCount=TESTMODE_PATTEN_NUM;  // fixed to 114
        Conf.PassCount1=conf.passCount1=TESTMODE_PATTEN_NUM;
        Conf.PassCount2=conf.passCount2=TESTMODE_PATTEN_NUM;
        Conf.PassCount3=conf.passCount3=TESTMODE_PATTEN_NUM;
        Conf.ValidNonce1=conf.ValidNonce1=TESTMODE_NONCE_NUM;
        Conf.ValidNonce2=conf.ValidNonce2=TESTMODE_NONCE_NUM;
        Conf.ValidNonce3=conf.ValidNonce3=TESTMODE_NONCE_NUM;

        ExitFlag=false;

        receiveExit=false;
        pthread_create(&cgpu.receive_id, NULL, receive_func, &cgpu);

        for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
        {
            StartSendFlag[i]=false;
        }
    }

    for(i=0; i<BITMAIN_MAX_CHAIN_NUM; i++)
    {
        testModeOKCounter[i]=0;
        for(j=0; j<256; j++)
        {
            last_result[i][j] = 0 ;
            last_result_opencore[i][j] = 0 ;
        }

        // force to test all boards at any time
        chain_vol_value[i]=0;
        chain_vol_final[i]=0;
        chain_vol_added[i]=0;
        search_freq_result[i]=true;

        chain_DataCount[i]=TESTMODE_PATTEN_NUM; // when seaching base freq, we use 8*144 patten on chip
        chain_ValidNonce[i]=TESTMODE_NONCE_NUM;
        chain_PassCount[i]=TESTMODE_PATTEN_NUM;

        chain_need_opencore[i]=false;   // init must be false, because chip cores are opened in driver-btm-c5.c
    }

    doTestBoard(0);

    for(i=0; i < BITMAIN_MAX_CHAIN_NUM; i++)
    {
        if(cgpu.chain_exist[i]==0)
            continue;

        if(search_freq_result[i])
        {
            testModeOKCounter[i]++;
        }
    }

    set_PWM(100);   // when exit preheat, set full speed of fan
    return true;
}



/*
 * Copyright (c) 2014-2015 Jens Kuske <jenskuske@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vencoder.h"
#include <sys/time.h>
#include <time.h>
#include <memoryAdapter.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include "jsonload.h"
#include "h264enc.h"


// #define DO_LATENCY_TEST
 
#define MSG(x) fprintf(stderr, "h264enc: " x "\n")
//#define PMSG(x) fprintf(stderr, "Pete: " x "\n"); fflush(stderr)
#define PMSG(x)
#define ROI_NUM 2

#define KBITS_MAX 64000

#define BITRATE_MULT 1024

struct h264enc_internal {
    VencHeaderData          sps_pps_data;
    VencH264Param           h264Param;
    VencSuperFrameConfig    sSuperFrameCfg;
    VencH264SVCSkip         SVCSkip; // set SVC and skip_frame
    VencCyclicIntraRefresh  sIntraRefresh;
    VencROIConfig           sRoiConfig[ROI_NUM];
    VeProcSet               sVeProcInfo;
    VencSmartFun            sH264Smart;
    int ParamSliceHeight;
    int FastEnc;
    
    VencBaseConfig baseConfig;
    VencAllocateBufferParam bufferParam;
    VideoEncoder* pVideoEnc;
    VencInputBuffer *inputBuffers;
    VencOutputBuffer outputBuffer;
    bool outputBufferHasData;
    
    unsigned int vbv_size;
    unsigned int num_buffers;
    unsigned char **buffer_pointers;
    int UsedBuf;
    
    long long pts;
};


static h264enc H264Enc = {0};
static bool Initialised = false;
static bool FirstInitDone = false;


#define NUM_CONFIG_ELEMS 52
JsonToConfigElem ConfigElems[NUM_CONFIG_ELEMS]  = 
{
    { "vbv_size", &H264Enc.vbv_size, sizeof(H264Enc.vbv_size),"%d" },
    { "nProfile", &H264Enc.h264Param.sProfileLevel.nProfile, sizeof(H264Enc.h264Param.sProfileLevel.nProfile),"%d" },
    { "nLevel", &H264Enc.h264Param.sProfileLevel.nLevel, sizeof(H264Enc.h264Param.sProfileLevel.nLevel),"%d" },
    { "sQPRange.nMinqp", &H264Enc.h264Param.sQPRange.nMinqp, sizeof(H264Enc.h264Param.sQPRange.nMinqp),"%d" },
    { "sQPRange.nMaxqp", &H264Enc.h264Param.sQPRange.nMaxqp, sizeof(H264Enc.h264Param.sQPRange.nMaxqp),"%d" },
    { "bEntropyCodingCABAC", &H264Enc.h264Param.bEntropyCodingCABAC, sizeof(H264Enc.h264Param.bEntropyCodingCABAC),"%d" },
    { "nMaxKeyInterval", &H264Enc.h264Param.nMaxKeyInterval , sizeof(H264Enc.h264Param.nMaxKeyInterval ),"%d" },
    { "nCodingMode", &H264Enc.h264Param.nCodingMode, sizeof(H264Enc.h264Param.nCodingMode),"%d" },
    { "bLongRefEnable", &H264Enc.h264Param.bLongRefEnable, sizeof(H264Enc.h264Param.bLongRefEnable),"%d" },
    { "nLongRefPoc", &H264Enc.h264Param.nLongRefPoc, sizeof(H264Enc.h264Param.nLongRefPoc),"%d" },

    { "bUseGopCtrlEn", &H264Enc.h264Param.sGopParam.bUseGopCtrlEn, sizeof(H264Enc.h264Param.sGopParam.bUseGopCtrlEn),"%d" },
    { "eGopMode", &H264Enc.h264Param.sGopParam.eGopMode, sizeof(H264Enc.h264Param.sGopParam.eGopMode),"%d" },
    { "nVirtualIFrameInterval", &H264Enc.h264Param.sGopParam.nVirtualIFrameInterval, sizeof(H264Enc.h264Param.sGopParam.nVirtualIFrameInterval),"%d" },
    { "nSpInterval", &H264Enc.h264Param.sGopParam.nSpInterval, sizeof(H264Enc.h264Param.sGopParam.nSpInterval),"%d" },
    { "sRefParam.bAdvancedRefEn", &H264Enc.h264Param.sGopParam.sRefParam.bAdvancedRefEn, sizeof(H264Enc.h264Param.sGopParam.sRefParam.bAdvancedRefEn),"%d" },
    { "sRefParam.bRefBaseEn", &H264Enc.h264Param.sGopParam.sRefParam.bRefBaseEn, sizeof(H264Enc.h264Param.sGopParam.sRefParam.bRefBaseEn),"%d" },
    
    { "sSuperFrameCfg.eSuperFrameMode", &H264Enc.sSuperFrameCfg.eSuperFrameMode, sizeof(H264Enc.sSuperFrameCfg.eSuperFrameMode),"%d" },
    { "sSuperFrameCfg.nMaxIFrameBits", &H264Enc.sSuperFrameCfg.nMaxIFrameBits, sizeof(H264Enc.sSuperFrameCfg.nMaxIFrameBits),"%d" },
    { "sSuperFrameCfg.nMaxPFrameBits", &H264Enc.sSuperFrameCfg.nMaxPFrameBits, sizeof(H264Enc.sSuperFrameCfg.nMaxPFrameBits),"%d" },
    
    { "SVCSkip.nTemporalSVC", &H264Enc.SVCSkip.nTemporalSVC, sizeof(H264Enc.SVCSkip.nTemporalSVC),"%d" },
    { "SVCSkip.nSkipFrame", &H264Enc.SVCSkip.nSkipFrame, sizeof(H264Enc.SVCSkip.nSkipFrame),"%d" },
    { "SVCSkip.bEnableLayerRatio", &H264Enc.SVCSkip.bEnableLayerRatio, sizeof(H264Enc.SVCSkip.bEnableLayerRatio),"%d" },
    { "SVCSkip.nLayerRatio[0]", &H264Enc.SVCSkip.nLayerRatio[0], sizeof(H264Enc.SVCSkip.nLayerRatio[0]),"%d" },
    { "SVCSkip.nLayerRatio[1]", &H264Enc.SVCSkip.nLayerRatio[1], sizeof(H264Enc.SVCSkip.nLayerRatio[1]),"%d" },
    { "SVCSkip.nLayerRatio[2]", &H264Enc.SVCSkip.nLayerRatio[2], sizeof(H264Enc.SVCSkip.nLayerRatio[2]),"%d" },
    { "SVCSkip.nLayerRatio[3]", &H264Enc.SVCSkip.nLayerRatio[3], sizeof(H264Enc.SVCSkip.nLayerRatio[3]),"%d" },
    
    { "sRoiConfig[0].bEnable", &H264Enc.sRoiConfig[0].bEnable , sizeof(H264Enc.sRoiConfig[0].bEnable ),"%d" },
    { "sRoiConfig[0].index", &H264Enc.sRoiConfig[0].index , sizeof(H264Enc.sRoiConfig[0].index ),"%d" },
    { "sRoiConfig[0].nQPoffset", &H264Enc.sRoiConfig[0].nQPoffset , sizeof(H264Enc.sRoiConfig[0].nQPoffset ),"%d" },
    { "sRoiConfig[0].roi_abs_flag", &H264Enc.sRoiConfig[0].roi_abs_flag , sizeof(H264Enc.sRoiConfig[0].roi_abs_flag ),"%d" },
    { "sRoiConfig[0].sRect.nLeft", &H264Enc.sRoiConfig[0].sRect.nLeft , sizeof(H264Enc.sRoiConfig[0].sRect.nLeft ),"%d" },
    { "sRoiConfig[0].sRect.nTop", &H264Enc.sRoiConfig[0].sRect.nTop , sizeof(H264Enc.sRoiConfig[0].sRect.nTop ),"%d" },
    { "sRoiConfig[0].sRect.nWidth", &H264Enc.sRoiConfig[0].sRect.nWidth , sizeof(H264Enc.sRoiConfig[0].sRect.nWidth ),"%d" },
    { "sRoiConfig[0].sRect.nHeight", &H264Enc.sRoiConfig[0].sRect.nHeight , sizeof(H264Enc.sRoiConfig[0].sRect.nHeight ),"%d" },
    
    { "sRoiConfig[1].bEnable", &H264Enc.sRoiConfig[1].bEnable , sizeof(H264Enc.sRoiConfig[0].bEnable ),"%d" },
    { "sRoiConfig[1].index", &H264Enc.sRoiConfig[1].index , sizeof(H264Enc.sRoiConfig[0].index ),"%d" },
    { "sRoiConfig[1].nQPoffset", &H264Enc.sRoiConfig[1].nQPoffset , sizeof(H264Enc.sRoiConfig[0].nQPoffset ),"%d" },
    { "sRoiConfig[1].roi_abs_flag", &H264Enc.sRoiConfig[1].roi_abs_flag , sizeof(H264Enc.sRoiConfig[0].roi_abs_flag ),"%d" },
    { "sRoiConfig[1].sRect.nLeft", &H264Enc.sRoiConfig[1].sRect.nLeft , sizeof(H264Enc.sRoiConfig[0].sRect.nLeft ),"%d" },
    { "sRoiConfig[1].sRect.nTop", &H264Enc.sRoiConfig[1].sRect.nTop , sizeof(H264Enc.sRoiConfig[0].sRect.nTop ),"%d" },
    { "sRoiConfig[1].sRect.nWidth", &H264Enc.sRoiConfig[1].sRect.nWidth , sizeof(H264Enc.sRoiConfig[0].sRect.nWidth ),"%d" },
    { "sRoiConfig[1].sRect.nHeight", &H264Enc.sRoiConfig[1].sRect.nHeight , sizeof(H264Enc.sRoiConfig[0].sRect.nHeight ),"%d" },
    
    { "sVeProcInfo.bProcEnable", &H264Enc.sVeProcInfo.bProcEnable , sizeof(H264Enc.sVeProcInfo.bProcEnable ),"%d" },
    { "sVeProcInfo.nProcFreq", &H264Enc.sVeProcInfo.nProcFreq , sizeof(H264Enc.sVeProcInfo.nProcFreq ),"%d" },
    { "sVeProcInfo.nStatisBitRateTime", &H264Enc.sVeProcInfo.nStatisBitRateTime , sizeof(H264Enc.sVeProcInfo.nStatisBitRateTime ),"%d" },
    { "sVeProcInfo.nStatisFrRateTime", &H264Enc.sVeProcInfo.nStatisFrRateTime , sizeof(H264Enc.sVeProcInfo.nStatisFrRateTime ),"%d" },
    
    { "sH264Smart.smart_fun_en", &H264Enc.sH264Smart.smart_fun_en , sizeof(H264Enc.sH264Smart.smart_fun_en),"%d" },
    { "sH264Smart.img_bin_en", &H264Enc.sH264Smart.img_bin_en, sizeof(H264Enc.sH264Smart.img_bin_en),"%d" },
    { "sH264Smart.img_bin_th", &H264Enc.sH264Smart.img_bin_th, sizeof(H264Enc.sH264Smart.img_bin_th),"%d" },
    { "sH264Smart.shift_bits", &H264Enc.sH264Smart.shift_bits, sizeof(H264Enc.sH264Smart.shift_bits),"%d" },
    
    { "ParamSliceHeight", &H264Enc.ParamSliceHeight, sizeof(H264Enc.ParamSliceHeight),"%d" },
    { "FastEnc", &H264Enc.FastEnc, sizeof(H264Enc.FastEnc),"%d" },
};



#ifdef DO_LATENCY_TEST
static long long GetNowUs()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000000 + now.tv_usec;
}

bool LEDFrame = false;
bool DetectLEDFrame(unsigned char *Data, size_t Len);
void SetLED(int LED, bool On);

#define NUM_TIME_STEPS 6
static uint32_t FrameCTime = 0;
static uint32_t FrameTimes[NUM_TIME_STEPS] = {0};
#endif



void init_svc_skip(VencH264SVCSkip *SVCSkip)
{
    SVCSkip->nTemporalSVC =  NO_T_SVC;
    switch(SVCSkip->nTemporalSVC)
    {
        case T_LAYER_4:
            SVCSkip->nSkipFrame = SKIP_8;
            break;
        case T_LAYER_3:
            SVCSkip->nSkipFrame = SKIP_4;
            break;
        case T_LAYER_2:
            SVCSkip->nSkipFrame = SKIP_2;
            break;
        default:
            SVCSkip->nSkipFrame = NO_SKIP;
            break;
    }
}

void init_roi(VencROIConfig *sRoiConfig)
{
    sRoiConfig[0].bEnable = 0;
    sRoiConfig[0].index = 0;
    sRoiConfig[0].nQPoffset = 10;
    sRoiConfig[0].sRect.nLeft = 300;
    sRoiConfig[0].sRect.nTop = 150;
    sRoiConfig[0].sRect.nWidth = 680;
    sRoiConfig[0].sRect.nHeight = 420;

    sRoiConfig[1].bEnable = 0;
    sRoiConfig[1].index = 1;
    sRoiConfig[1].nQPoffset = 10;
    sRoiConfig[1].sRect.nLeft = 320;
    sRoiConfig[1].sRect.nTop = 180;
    sRoiConfig[1].sRect.nWidth = 320;
    sRoiConfig[1].sRect.nHeight = 180;
}

void init_enc_proc_info(VeProcSet *ve_proc_set)
{
    ve_proc_set->bProcEnable = 0;
    ve_proc_set->nProcFreq = 3;
}

void initH264ParamsDefault(h264enc *h264_func)
{
    memset(h264_func, 0, sizeof(h264enc));

    //init h264Param
    h264_func->h264Param.bEntropyCodingCABAC = 1;
//    h264_func->h264Param.nBitrate = 10 * 1024 * 1024; 
    h264_func->h264Param.nFramerate = 60;
    h264_func->h264Param.nCodingMode = VENC_FRAME_CODING;
    h264_func->h264Param.nMaxKeyInterval = 600; 
    h264_func->h264Param.sProfileLevel.nProfile = VENC_H264ProfileMain; //VENC_H264ProfileHigh; / VENC_H264ProfileBaseline / VENC_H264ProfileMain
    h264_func->h264Param.sProfileLevel.nLevel = VENC_H264Level41; // VENC_H264Level51;
    h264_func->h264Param.sQPRange.nMinqp = 10;
    h264_func->h264Param.sQPRange.nMaxqp = 50;
    h264_func->h264Param.bLongRefEnable = 0;
    h264_func->h264Param.nLongRefPoc = 0;

    h264_func->sIntraRefresh.bEnable = 1;
    h264_func->sIntraRefresh.nBlockNumber = 8;
    
    h264_func->sH264Smart.img_bin_en = 0;
    h264_func->sH264Smart.img_bin_th = 27;
    h264_func->sH264Smart.shift_bits = 2;
    h264_func->sH264Smart.smart_fun_en = 0;
}

int initH264Func(h264enc *h264_func)
{
   
    //init VencH264SVCSkip
    init_svc_skip(&h264_func->SVCSkip);

    //init VencROIConfig
    init_roi(h264_func->sRoiConfig);

    //init proc info
    init_enc_proc_info(&h264_func->sVeProcInfo);


    return 0;
}


void h264enc_free(h264enc *c)
{
    c = &H264Enc;
    
    /* Rather than get rid of our venc instance when the pipeline pauses, we now keep it for when it restarts */
    /* However input buffers may have changed, so we refresh them */
    #if 0
    if(c->pVideoEnc)
    {
        VideoEncDestroy(c->pVideoEnc);
    }
    c->pVideoEnc = NULL;

    
    if(H264Enc.baseConfig.memops)
    {
        CdcMemClose(H264Enc.baseConfig.memops);
    }
    #endif
    
    free(H264Enc.inputBuffers);
    free(H264Enc.buffer_pointers);
    
    Initialised = false;
}

unsigned char **h264_get_buffers(h264enc *c)
{
    c = &H264Enc;
    return c->buffer_pointers;
}

h264enc *h264enc_new(const struct h264enc_params *p, int num_buffers)
{
    bool result;
    
    PMSG("h264enc_new()");
    
    if(!FirstInitDone)
    {
        initH264ParamsDefault(&H264Enc);

        H264Enc.h264Param.nBitrate = p->bitrate * BITRATE_MULT; 
        H264Enc.sIntraRefresh.nBlockNumber = p->keyframe_interval; // we are using rolling i-frame (intra-refresh), so rather than set keyframe interval we set nBlocknumber    
        H264Enc.h264Param.nMaxKeyInterval = 300; 
       
        fprintf(stderr, "bitrate=%d, qp=%d, keyframe=%d\n", p->bitrate, p->qp, p->keyframe_interval);
                
        memset(&(H264Enc.baseConfig), 0 ,sizeof(VencBaseConfig));
        memset(&(H264Enc.bufferParam), 0 ,sizeof(VencAllocateBufferParam));
        H264Enc.baseConfig.memops = MemAdapterGetOpsS();
        if (H264Enc.baseConfig.memops == NULL)
        {
            MSG("MemAdapterGetOpsS failed\n");
            return false;
        }

        
        CdcMemOpen(H264Enc.baseConfig.memops);

    
    H264Enc.baseConfig.nInputWidth= p->width;
    H264Enc.baseConfig.nInputHeight = p->height;
    H264Enc.baseConfig.nStride = p->width;
    H264Enc.baseConfig.nDstWidth = p->width;
    H264Enc.baseConfig.nDstHeight = p->height;
    
    H264Enc.baseConfig.eInputFormat = VENC_PIXEL_YUV420SP;
    //H264Enc.baseConfig.eInputFormat = VENC_PIXEL_YUYV422;
    
    H264Enc.bufferParam.nSizeY = (H264Enc.baseConfig.nInputWidth*H264Enc.baseConfig.nInputHeight * 2);
    H264Enc.bufferParam.nSizeC = 0; //H264Enc.baseConfig.nInputWidth*H264Enc.baseConfig.nInputHeight/2;
    
    H264Enc.bufferParam.nBufferNum = num_buffers; // DQ_BUF requires four buffers
        }
    if(!FirstInitDone)
    {
        H264Enc.pVideoEnc = VideoEncCreate(VENC_CODEC_H264);
        result = initH264Func(&H264Enc);
        if(result)
        {
            MSG("initH264Func error, return \n");
            return false;
        }
         H264Enc.vbv_size = 12*1024*1024;
    
    
    }
    
   
    /* Having set everything to defaults, we try to load the json config. If we can't we use defaults */
    LoadJsonConfig("/usr/local/share/openhd/video/sunxisrc_h264.json", ConfigElems, NUM_CONFIG_ELEMS);
    VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamH264Param, &(H264Enc.h264Param));
    
    fprintf(stderr, "Pete bitrate=%d, qp=%d, keyframe=%d\n", H264Enc.h264Param.nBitrate, H264Enc.h264Param.sQPRange.nMaxqp, H264Enc.h264Param.nMaxKeyInterval);
    
    if(!FirstInitDone)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamSetVbvSize, &H264Enc.vbv_size);
        
        VideoEncInit(H264Enc.pVideoEnc, &(H264Enc.baseConfig));
    }
    
     if(H264Enc.sIntraRefresh.bEnable)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamH264CyclicIntraRefresh,
                                 &H264Enc.sIntraRefresh);
        printf("Enabling intra refresh\n");
    }

    if(1 == H264Enc.h264Param.bEntropyCodingCABAC)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamH264EntropyCodingCABAC,
                                 &H264Enc.h264Param.bEntropyCodingCABAC);
        printf("Enabling Entropy CABAC\n");
    }
    
    if(H264Enc.sRoiConfig[0].bEnable)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamROIConfig, &H264Enc.sRoiConfig[0]);
        printf("Enabling ROI0\n");
    }
    if(H264Enc.sRoiConfig[1].bEnable)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamROIConfig, &H264Enc.sRoiConfig[1]);
        printf("Enabling ROI1\n");
    }
    
    if(H264Enc.sVeProcInfo.bProcEnable)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamProcSet,
                                 &H264Enc.sVeProcInfo);
        printf("Enabling bProc\n");
    }
    if(H264Enc.sH264Smart.smart_fun_en)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamSmartFuntion,
                                 &H264Enc.sH264Smart);
        printf("Enabling smartfun\n");
    }
    
    if(H264Enc.ParamSliceHeight)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamSliceHeight,
                                 &H264Enc.ParamSliceHeight);
        printf("Enabling slicing. Using %d slices of %d pixels\n", p->width/ H264Enc.ParamSliceHeight, H264Enc.ParamSliceHeight);
    } 
    
    if(H264Enc.FastEnc)
    {
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamFastEnc,
                                 &H264Enc.FastEnc);
        printf("Enabling FastEnc\n");
    }


        
        //VENC_IndexParamH264SVCSkip

        
        // Todo: Superframe (VENC_IndexParamSuperFrameConfig) and GOP aren't checked and enabled 
        
        
    VideoEncGetParameter(H264Enc.pVideoEnc, VENC_IndexParamH264SPSPPS, &(H264Enc.sps_pps_data));
    
    AllocInputBuffer(H264Enc.pVideoEnc, &(H264Enc.bufferParam));
    
    H264Enc.num_buffers = num_buffers;
    H264Enc.inputBuffers = malloc(num_buffers  * sizeof(VencInputBuffer));
    H264Enc.buffer_pointers = malloc(num_buffers * sizeof(unsigned char *));
    if(!H264Enc.inputBuffers || !H264Enc.buffer_pointers)
    {
        printf("%s Could not allocate memory for input buffers\n", __func__);
        return false;
    }
    
    for(int i = 0 ; i < num_buffers; i ++)
    {
        GetOneAllocInputBuffer(H264Enc.pVideoEnc, &(H264Enc.inputBuffers[i]));
        H264Enc.buffer_pointers[i] = H264Enc.inputBuffers[i].pAddrVirY;
        printf("Set buf %d to %p\n", i, H264Enc.buffer_pointers[i]);
        /* Don't return the buffer, we want them all queued ready for the camera to use */
    }
    Initialised = true;
    FirstInitDone = true;
    
    PMSG("h264enc_new() complete");
	return &H264Enc;
}

void h264_set_bitrate(unsigned int Kbits)
{
    if(Kbits < 1000)
        Kbits = 1000;
    if(Kbits > KBITS_MAX)
        Kbits = KBITS_MAX;
    H264Enc.h264Param.nBitrate = Kbits * BITRATE_MULT;
    if(Initialised)
    {
        printf("h264 : Dynamically setting bitrate to %dK\n", Kbits);
        VideoEncSetParameter(H264Enc.pVideoEnc, VENC_IndexParamH264Param, &(H264Enc.h264Param));
    }
    else
    {
        printf("Set bitrate on unopened encoder, will use %dK once open\n", Kbits);
    }
}

int h264enc_get_initial_bytestream_length(h264enc *c)
{
    
    c = &H264Enc;
    if(! c )
    {
        printf("h264enc_get_initial_bytestream_length not initialised\n");
        return 0;
    }
    VideoEncGetParameter(c->pVideoEnc, VENC_IndexParamH264SPSPPS, &(c->sps_pps_data));
    return c->sps_pps_data.nLength;
}

void *h264enc_get_intial_bytestream_buffer(h264enc *c)
{
    
    c = &H264Enc;
    if(! c )
    {
        printf("h264enc_get_intial_bytestream_buffer not initialised\n");
        return NULL;
    }
   // fprintf(stderr, "Returning initial buffer of %d bytes\n", c->sps_pps_data.nLength);
    return c->sps_pps_data.pBuffer;
}

void h264enc_set_input_buffer(h264enc *c, void *Dat, size_t Len)
{
    PMSG("h264enc_set_input_buffer()");

    c = &H264Enc;
    if(! c )
    {
        printf("h264enc_set_input_buffer not initialised\n");
        return;
    }
    #ifdef DO_LATENCY_TEST
    LEDFrame = DetectLEDFrame(Dat, Len);
    
    if(true == LEDFrame)
    {
        FrameCTime = 0;
        FrameTimes[FrameCTime ++] = GetNowUs();
        SetLED(0, true);
    }
    #endif
    //GetOneAllocInputBuffer(c->pVideoEnc, &(c->inputBuffer));
    
    bool FoundBuf = false;
    int CTab = 0;
    while((false == FoundBuf) && (CTab < c->num_buffers))
    {
        if(c->buffer_pointers[CTab] == Dat)
        {
            FoundBuf = true;
        }
        else
        {
            CTab ++;
        }
    }
    
    if(false == FoundBuf)
    {
        printf("Could not find buffer %p\n", Dat);
        return;
    }
    c->UsedBuf = CTab;
    
    {
        static int StartFrames = 480;
        static bool First = true;
        static int ActualKeyframes = 0;
        
        if(First)
        {
            ActualKeyframes = H264Enc.h264Param.nMaxKeyInterval;
            First = false;
        }
        
        if(StartFrames)
        {
            StartFrames --;
            H264Enc.h264Param.nMaxKeyInterval = 2;
            
            memset(Dat, 0, 1280 * 720);
            memset(Dat + 1280 * 720, 128, 1280 * 720 / 2);
            
            /* If we've finished our startup sequence, restore the original keyframe interval */
            if(StartFrames == 0)
            {
                H264Enc.h264Param.nMaxKeyInterval = ActualKeyframes;
            }
        }
    }
    
    int Offset = (H264Enc.baseConfig.nInputWidth * H264Enc.baseConfig.nInputHeight); 
    c->inputBuffers[CTab].pAddrPhyC = c->inputBuffers[CTab].pAddrPhyY + Offset;
    c->inputBuffers[CTab].pAddrVirC = c->inputBuffers[CTab].pAddrVirY + Offset;
    
    FlushCacheAllocInputBuffer(c->pVideoEnc, &c->inputBuffers[CTab]);
    c->pts += 1000/c->h264Param.nFramerate;
    c->inputBuffers[CTab].nPts = c->pts;
    
    int Ret = AddOneInputBuffer(c->pVideoEnc, &c->inputBuffers[CTab]);
    if(Ret != 0)
    {
        printf("AddOneinputBuffer returned %d\n", Ret);
    }
    
 //   fprintf(stderr, "DataLen = %d, w x h =%d, Copylen=%d\n", Len, 1280*720, DatLen);
	#ifdef DO_LATENCY_TEST
    if(true == LEDFrame)
    {
        FrameTimes[FrameCTime ++] = GetNowUs();
    }
    #endif
}

void h264enc_done_outputbuffer(h264enc *c)
{
    
    c = &H264Enc;
    if(! c || false == c->outputBufferHasData)
    {
        printf("h264enc_done_outputbuffer not initialised\n");
        return ;
    }
    FreeOneBitStreamFrame(c->pVideoEnc, &c->outputBuffer);
    c->outputBufferHasData = false;
    #ifdef DO_LATENCY_TEST
    if(true == LEDFrame)
    {
        FrameTimes[FrameCTime ++] = GetNowUs();
        fprintf(stderr, "Frame time: Start[%d], Inp=%d, Enc=%d, Tot=%d\n", (unsigned int) FrameTimes[0], (unsigned int)(FrameTimes[1]-FrameTimes[0]), (unsigned int)(FrameTimes[3]-FrameTimes[2]), (unsigned int)(FrameTimes[4]-FrameTimes[0]));
        LEDFrame = false;
        SetLED(0, false);
        SetLED(1, false);
    }
    #endif
}

void *h264enc_get_bytestream_buffer(const h264enc *c, int stream)
{
    c = &H264Enc;
    if(! c || false == c->outputBufferHasData)
    {
        printf("h264enc_get_bytestream_buffer not initialised\n");
        return NULL;
    }
    PMSG("h264enc_get_bytestream_buffer()");
    if(stream == 0)
    {
        return c->outputBuffer.pData0;
    }
    else
    {
        return c->outputBuffer.pData1;
    }
}

unsigned int h264enc_is_keyframe(const h264enc *c)
{
    c = &H264Enc;
    if(! c || false == c->outputBufferHasData)
    {
        printf("h264enc_is_keyframe not initialised\n");
        return 0;
    }
    
    if(c->outputBuffer.nFlag & VENC_BUFFERFLAG_KEYFRAME)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

unsigned int h264enc_get_bytestream_length(const h264enc *c, int stream)
{
    c = &H264Enc;
    //fprintf(stderr, "Pete: h264enc_get_bytestream_length()=%d (%d)\n", c->outputBuffer.nSize0, c->outputBuffer.nSize1);
    if(! c || false == c->outputBufferHasData)
    {
        printf("h264enc_get_bytestream_length not initialised\n");
        return 0;
    }
    if(stream == 0)
    {
        
        return c->outputBuffer.nSize0;
    }
    else
    {
        return c->outputBuffer.nSize1;
    }
}

int h264enc_encode_picture(h264enc *c)
{
    c = &H264Enc;
    int result;
    #ifdef DO_LATENCY_TEST
    if(true == LEDFrame)
    {
        FrameTimes[FrameCTime ++] = GetNowUs();
    }
    #endif
    
    if(! c )
    {
        printf("h264enc_encode_picture not initialised\n");
        return 0;
    }
    int Ret = VideoEncodeOneFrame(c->pVideoEnc);
    
    if(Ret != 0)
    {
        printf("VideoEncodeOneFrame returned %d\n" , Ret);
    }
    if(c->UsedBuf >= 0)
    {
        AlreadyUsedInputBuffer(c->pVideoEnc,&c->inputBuffers[c->UsedBuf]);
        ReturnOneAllocInputBuffer(c->pVideoEnc, &c->inputBuffers[c->UsedBuf]); // Are these two necessary?
        GetOneAllocInputBuffer(c->pVideoEnc, &(c->inputBuffers[c->UsedBuf]));
        c->UsedBuf = -1;
    }
    else
    {
        printf("No buffer to free\n");
    }
    result = GetOneBitstreamFrame(c->pVideoEnc, &c->outputBuffer);
    if(result != 0)
    {
        printf("h264enc_encode_picture() Could not get result buffer, GetOneBitstreamFrame returned %d\n", result);
        return 0;
    }
    c->outputBufferHasData = true;
    PMSG("h264enc_encode_picture() complete");
    #if 0
    { static int FileNum = 0;
        char Filename[30];
        sprintf(Filename, "Frm%04d.raw", FileNum ++);
        FILE *fp = fopen(Filename, "wb");
        if(fp)
        {
            fwrite(c->outputBuffer.pData0, c->outputBuffer.nSize0, 1, fp);
            if(c->outputBuffer.nSize1)
            {
                printf("Frm %d, nSize1=%d\n", FileNum - 1, c->outputBuffer.nSize1);
                fwrite(c->outputBuffer.pData1, c->outputBuffer.nSize1, 1, fp);
            }
            fclose(fp);
        }
    }
    #endif
    #ifdef DO_LATENCY_TEST
    if(true == LEDFrame)
    {
        FrameTimes[FrameCTime ++] = GetNowUs();
    }
    #endif
	return 1;
}



#ifdef DO_LATENCY_TEST
#define NUM_LEDS 3
static const char *LEDS[NUM_LEDS] = 
{  
    "100",
    "101",
    "104"
};
FILE *LEDFiles[NUM_LEDS] = {NULL};

char LEDSetStr[50];
void SetLED(int LED, bool On)
{
    static bool Initd = false;
    
    if(!Initd)
    {
        FILE *fp = fopen("/sys/class/gpio/export", "w");
        if(!fp)
        {
            printf("Could not init LEDs\n");
        }
        else
        {
            for(int i = 0; i < NUM_LEDS; i++)
            {
                fprintf(fp, "%s\n", LEDS[i]);
                fflush(fp);
                
                sprintf(LEDSetStr, "/sys/class/gpio/gpio%s/value", LEDS[i]);
                printf("Opening %s\n", LEDSetStr);
                LEDFiles[i] = fopen(LEDSetStr, "w");
                if(!LEDFiles[i])
                {
                    printf("Could not open LED %s, file %s\n", LEDS[i], LEDSetStr);
                }
                
                fprintf(LEDFiles[LED], "1\n");
                fflush(LEDFiles[LED]);
            }
            fclose(fp);
        }
        Initd = true;
    }
    
    if((LED < NUM_LEDS))
    {
        if(LEDFiles[LED] == NULL)
        {
            printf("file for %s not open\n", LEDS[LED]);
        }
        else
        {
            if(On)
                fprintf(LEDFiles[LED], "0\n");
            else
                fprintf(LEDFiles[LED], "1\n");
            fflush(LEDFiles[LED]);
        }
    }
    else
    {
        printf("Invalid LED\n");
    }
}


#define WIDTH 1280
#define HEIGHT 720
bool DetectLEDFrame(unsigned char *Data, size_t Len)
{
    static int Count = 0;
    uint64_t TotLum = 0;
    uint32_t AvLum;
    int WindowSize = 8; // -WindowSize to +WindowSize
    int StartY = (HEIGHT) - WindowSize; // Changed from HEIGHT / 2 to give best possible results
    int StartX = (WIDTH / 2) - WindowSize;

    /* Make sure we can't retrigger within 0.16s */
    if(Count <= 10)
    {
        Count ++;
        return false;
    }
    for(int y = StartY; y < (StartY + WindowSize); y ++)
    {
        for(int x = StartX; x < (StartX + WindowSize); x ++)
        {
            int Elem = y * WIDTH + x;
            if(Elem >= Len)
            {
                printf("DetectLEDFrame() Invalid Elem %d, y=%d, x=%d\n", Elem, y, x);
            }
            else
            {
               TotLum += *(Data + Elem);
            }
        }
    }
    AvLum = TotLum / (WindowSize * WindowSize);
    //printf("AvLum=%d\n", AvLum);
    if(AvLum > 128)
    {
        Count = 0;
        
        /* Optionally add a small white & black rectangle to the top left of the frame */
        for(int y = 0; y<12; y ++)
        {
            for(int x = 0; x < 12; x += 2)
            {
                Data[y * WIDTH + x] = 0;
                Data[y * WIDTH + x + 1] = 255;
            }
        }
        return true;
    }
    return false;
}

#endif

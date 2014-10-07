/*
 * Copyright (C) 2010-2010 NXP Software
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Reverb"
#define ARRAY_SIZE(array) (sizeof array / sizeof array[0])
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "EffectReverb.h"
// from Reverb/lib
#include "LVREV.h"

// effect_handle_t interface implementation for reverb
extern "C" const struct effect_interface_s gReverbInterface;

#define LVM_ERROR_CHECK(LvmStatus, callingFunc, calledFunc){\
        if (LvmStatus == LVREV_NULLADDRESS){\
            ALOGV("\tLVREV_ERROR : Parameter error - "\
                    "null pointer returned by %s in %s\n\n\n\n", callingFunc, calledFunc);\
        }\
        if (LvmStatus == LVREV_INVALIDNUMSAMPLES){\
            ALOGV("\tLVREV_ERROR : Parameter error - "\
                    "bad number of samples returned by %s in %s\n\n\n\n", callingFunc, calledFunc);\
        }\
        if (LvmStatus == LVREV_OUTOFRANGE){\
            ALOGV("\tLVREV_ERROR : Parameter error - "\
                    "out of range returned by %s in %s\n", callingFunc, calledFunc);\
        }\
    }

// Namespaces
namespace android {
namespace {

/************************************************************************************/
/*                                                                                  */
/* Preset definitions                                                               */
/*                                                                                  */
/************************************************************************************/

const static t_reverb_settings sReverbPresets[] = {
        // REVERB_PRESET_NONE: values are unused
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        // REVERB_PRESET_SMALLROOM
        {-400, -600, 1100, 830, -400, 5, 500, 10, 1000, 1000},
        // REVERB_PRESET_MEDIUMROOM
        {-400, -600, 1300, 830, -1000, 20, -200, 20, 1000, 1000},
        // REVERB_PRESET_LARGEROOM
        {-400, -600, 1500, 830, -1600, 5, -1000, 40, 1000, 1000},
        // REVERB_PRESET_MEDIUMHALL
        {-400, -600, 1800, 700, -1300, 15, -800, 30, 1000, 1000},
        // REVERB_PRESET_LARGEHALL
        {-400, -600, 1800, 700, -2000, 30, -1400, 60, 1000, 1000},
        // REVERB_PRESET_PLATE
        {-400, -200, 1300, 900, 0, 2, 0, 10, 1000, 750},
};


// NXP SW auxiliary environmental reverb
const effect_descriptor_t gAuxEnvReverbDescriptor = {
        { 0xc2e5d5f0, 0x94bd, 0x4763, 0x9cac, { 0x4e, 0x23, 0x4d, 0x06, 0x83, 0x9e } },
        { 0x4a387fc0, 0x8ab3, 0x11df, 0x8bad, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } },
        EFFECT_CONTROL_API_VERSION,
        EFFECT_FLAG_TYPE_AUXILIARY,
        LVREV_CUP_LOAD_ARM9E,
        LVREV_MEM_USAGE,
        "Auxiliary Environmental Reverb",
        "NXP Software Ltd.",
};

// NXP SW insert environmental reverb
static const effect_descriptor_t gInsertEnvReverbDescriptor = {
        {0xc2e5d5f0, 0x94bd, 0x4763, 0x9cac, {0x4e, 0x23, 0x4d, 0x06, 0x83, 0x9e}},
        {0xc7a511a0, 0xa3bb, 0x11df, 0x860e, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
        EFFECT_CONTROL_API_VERSION,
        EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_FIRST | EFFECT_FLAG_VOLUME_CTRL,
        LVREV_CUP_LOAD_ARM9E,
        LVREV_MEM_USAGE,
        "Insert Environmental Reverb",
        "NXP Software Ltd.",
};

// NXP SW auxiliary preset reverb
static const effect_descriptor_t gAuxPresetReverbDescriptor = {
        {0x47382d60, 0xddd8, 0x11db, 0xbf3a, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
        {0xf29a1400, 0xa3bb, 0x11df, 0x8ddc, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
        EFFECT_CONTROL_API_VERSION,
        EFFECT_FLAG_TYPE_AUXILIARY,
        LVREV_CUP_LOAD_ARM9E,
        LVREV_MEM_USAGE,
        "Auxiliary Preset Reverb",
        "NXP Software Ltd.",
};

// NXP SW insert preset reverb
static const effect_descriptor_t gInsertPresetReverbDescriptor = {
        {0x47382d60, 0xddd8, 0x11db, 0xbf3a, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
        {0x172cdf00, 0xa3bc, 0x11df, 0xa72f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
        EFFECT_CONTROL_API_VERSION,
        EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_FIRST | EFFECT_FLAG_VOLUME_CTRL,
        LVREV_CUP_LOAD_ARM9E,
        LVREV_MEM_USAGE,
        "Insert Preset Reverb",
        "NXP Software Ltd.",
};

// gDescriptors contains pointers to all defined effect descriptor in this library
static const effect_descriptor_t * const gDescriptors[] = {
        &gAuxEnvReverbDescriptor,
        &gInsertEnvReverbDescriptor,
        &gAuxPresetReverbDescriptor,
        &gInsertPresetReverbDescriptor
};

struct ReverbContext{
    const struct effect_interface_s *itfe;
    effect_config_t                 config;
    LVREV_Handle_t                  hInstance;
    int16_t                         SavedRoomLevel;
    int16_t                         SavedHfLevel;
    int16_t                         SavedDecayTime;
    int16_t                         SavedDecayHfRatio;
    int16_t                         SavedReverbLevel;
    int16_t                         SavedDiffusion;
    int16_t                         SavedDensity;
    bool                            bEnabled;
    #ifdef LVM_PCM
    FILE                            *PcmInPtr;
    FILE                            *PcmOutPtr;
    #endif
    LVM_Fs_en                       SampleRate;
    LVM_INT32                       *InFrames32;
    LVM_INT32                       *OutFrames32;
    bool                            auxiliary;
    bool                            preset;
    uint16_t                        curPreset;
    uint16_t                        nextPreset;
    int                             SamplesToExitCount;
    LVM_INT16                       leftVolume;
    LVM_INT16                       rightVolume;
    LVM_INT16                       prevLeftVolume;
    LVM_INT16                       prevRightVolume;
    int                             volumeMode;
};

enum {
    REVERB_VOLUME_OFF,
    REVERB_VOLUME_FLAT,
    REVERB_VOLUME_RAMP,
};

#define REVERB_DEFAULT_PRESET REVERB_PRESET_NONE


#define REVERB_SEND_LEVEL   (0x0C00) // 0.75 in 4.12 format
#define REVERB_UNIT_VOLUME  (0x1000) // 1.0 in 4.12 format

//--- local function prototypes
int  Reverb_init            (ReverbContext *pContext);
void Reverb_free            (ReverbContext *pContext);
int  Reverb_setConfig       (ReverbContext *pContext, effect_config_t *pConfig);
void Reverb_getConfig       (ReverbContext *pContext, effect_config_t *pConfig);
int  Reverb_setParameter    (ReverbContext *pContext, void *pParam, void *pValue);
int  Reverb_getParameter    (ReverbContext *pContext,
                             void          *pParam,
                             size_t        *pValueSize,
                             void          *pValue);
int Reverb_LoadPreset       (ReverbContext   *pContext);

/* Effect Library Interface Implementation */

extern "C" int EffectCreate(const effect_uuid_t *uuid,
                            int32_t             sessionId,
                            int32_t             ioId,
                            effect_handle_t  *pHandle){
    int ret;
    int i;
    int length = sizeof(gDescriptors) / sizeof(const effect_descriptor_t *);
    const effect_descriptor_t *desc;

    if (pHandle == NULL || uuid == NULL){
        return -EINVAL;
    }

    for (i = 0; i < length; i++) {
        desc = gDescriptors[i];
        if (memcmp(uuid, &desc->uuid, sizeof(effect_uuid_t))
                == 0) {
            break;
        }
    }

    if (i == length) {
        return -ENOENT;
    }

    ReverbContext *pContext = new ReverbContext;

    pContext->itfe      = &gReverbInterface;
    pContext->hInstance = NULL;

    pContext->auxiliary = false;
    if ((desc->flags & EFFECT_FLAG_TYPE_MASK) == EFFECT_FLAG_TYPE_AUXILIARY){
        pContext->auxiliary = true;
    }

    pContext->preset = false;
    if (memcmp(&desc->type, SL_IID_PRESETREVERB, sizeof(effect_uuid_t)) == 0) {
        pContext->preset = true;
        // force reloading preset at first call to process()
        pContext->curPreset = REVERB_PRESET_LAST + 1;
        pContext->nextPreset = REVERB_DEFAULT_PRESET;
    }

    ret = Reverb_init(pContext);

    if (ret < 0){
        delete pContext;
        return ret;
    }

    *pHandle = (effect_handle_t)pContext;

    #ifdef LVM_PCM
    pContext->PcmInPtr = NULL;
    pContext->PcmOutPtr = NULL;

    pContext->PcmInPtr  = fopen("/data/tmp/reverb_pcm_in.pcm", "w");
    pContext->PcmOutPtr = fopen("/data/tmp/reverb_pcm_out.pcm", "w");

    if((pContext->PcmInPtr  == NULL)||
       (pContext->PcmOutPtr == NULL)){
       return -EINVAL;
    }
    #endif


    // Allocate memory for reverb process (*2 is for STEREO)
    pContext->InFrames32  = (LVM_INT32 *)malloc(LVREV_MAX_FRAME_SIZE * sizeof(LVM_INT32) * 2);
    pContext->OutFrames32 = (LVM_INT32 *)malloc(LVREV_MAX_FRAME_SIZE * sizeof(LVM_INT32) * 2);

    return 0;
} /* end EffectCreate */

extern "C" int EffectRelease(effect_handle_t handle){
    ReverbContext * pContext = (ReverbContext *)handle;

    if (pContext == NULL){
        return -EINVAL;
    }

    #ifdef LVM_PCM
    fclose(pContext->PcmInPtr);
    fclose(pContext->PcmOutPtr);
    #endif
    free(pContext->InFrames32);
    free(pContext->OutFrames32);
    Reverb_free(pContext);
    delete pContext;
    return 0;
} /* end EffectRelease */

extern "C" int EffectGetDescriptor(const effect_uuid_t *uuid,
                                   effect_descriptor_t *pDescriptor) {
    int i;
    int length = sizeof(gDescriptors) / sizeof(const effect_descriptor_t *);

    if (pDescriptor == NULL || uuid == NULL){
        return -EINVAL;
    }

    for (i = 0; i < length; i++) {
        if (memcmp(uuid, &gDescriptors[i]->uuid, sizeof(effect_uuid_t)) == 0) {
            *pDescriptor = *gDescriptors[i];
            return 0;
        }
    }

    return -EINVAL;
} /* end EffectGetDescriptor */

/* local functions */
#define CHECK_ARG(cond) {                     \
    if (!(cond)) {                            \
        ALOGV("\tLVM_ERROR : Invalid argument: "#cond);      \
        return -EINVAL;                       \
    }                                         \
}

//----------------------------------------------------------------------------
// MonoTo2I_32()
//----------------------------------------------------------------------------
// Purpose:
//  Convert MONO to STEREO
//
//----------------------------------------------------------------------------

void MonoTo2I_32( const LVM_INT32  *src,
                        LVM_INT32  *dst,
                        LVM_INT16 n)
{
   LVM_INT16 ii;
   src += (n-1);
   dst += ((n*2)-1);

   for (ii = n; ii != 0; ii--)
   {
       *dst = *src;
       dst--;

       *dst = *src;
       dst--;
       src--;
   }

   return;
}

//----------------------------------------------------------------------------
// From2iToMono_32()
//----------------------------------------------------------------------------
// Purpose:
//  Convert STEREO to MONO
//
//----------------------------------------------------------------------------

void From2iToMono_32( const LVM_INT32 *src,
                            LVM_INT32 *dst,
                            LVM_INT16 n)
{
   LVM_INT16 ii;
   LVM_INT32 Temp;

   for (ii = n; ii != 0; ii--)
   {
       Temp = (*src>>1);
       src++;

       Temp +=(*src>>1);
       src++;

       *dst = Temp;
       dst++;
   }

   return;
}

static inline int16_t clamp16(int32_t sample)
{
    if ((sample>>15) ^ (sample>>31))
        sample = 0x7FFF ^ (sample>>31);
    return sample;
}

//----------------------------------------------------------------------------
// process()
//----------------------------------------------------------------------------
// Purpose:
// Apply the Reverb
//
// Inputs:
//  pIn:        pointer to stereo/mono 16 bit input data
//  pOut:       pointer to stereo 16 bit output data
//  frameCount: Frames to process
//  pContext:   effect engine context
//  strength    strength to be applied
//
//  Outputs:
//  pOut:       pointer to updated stereo 16 bit output data
//
//----------------------------------------------------------------------------

int process( LVM_INT16     *pIn,
             LVM_INT16     *pOut,
             int           frameCount,
             ReverbContext *pContext){

    LVM_INT16               samplesPerFrame = 1;
    LVREV_ReturnStatus_en   LvmStatus = LVREV_SUCCESS;              /* Function call status */
    LVM_INT16 *OutFrames16;


    // Check that the input is either mono or stereo
    if (pContext->config.inputCfg.channels == AUDIO_CHANNEL_OUT_STEREO) {
        samplesPerFrame = 2;
    } else if (pContext->config.inputCfg.channels != AUDIO_CHANNEL_OUT_MONO) {
        return -EINVAL;
    }

    OutFrames16 = (LVM_INT16 *)pContext->OutFrames32;

    // Check for NULL pointers
    if((pContext->InFrames32 == NULL)||(pContext->OutFrames32 == NULL)){
        return -EINVAL;
    }

    #ifdef LVM_PCM
    fwrite(pIn, frameCount*sizeof(LVM_INT16)*samplesPerFrame, 1, pContext->PcmInPtr);
    fflush(pContext->PcmInPtr);
    #endif

    if (pContext->preset && pContext->nextPreset != pContext->curPreset) {
        Reverb_LoadPreset(pContext);
    }



    // Convert to Input 32 bits
    if (pContext->auxiliary) {
        for(int i=0; i<frameCount*samplesPerFrame; i++){
            pContext->InFrames32[i] = (LVM_INT32)pIn[i]<<8;
        }
    } else {
        // insert reverb input is always stereo
        for (int i = 0; i < frameCount; i++) {
            pContext->InFrames32[2*i] = (pIn[2*i] * REVERB_SEND_LEVEL) >> 4; // <<8 + >>12
            pContext->InFrames32[2*i+1] = (pIn[2*i+1] * REVERB_SEND_LEVEL) >> 4; // <<8 + >>12
        }
    }

    if (pContext->preset && pContext->curPreset == REVERB_PRESET_NONE) {
        memset(pContext->OutFrames32, 0, frameCount * sizeof(LVM_INT32) * 2); //always stereo here
    } else {
        if(pContext->bEnabled == LVM_FALSE && pContext->SamplesToExitCount > 0) {
            memset(pContext->InFrames32,0,frameCount * sizeof(LVM_INT32) * samplesPerFrame);
        }

        /* Process the samples, producing a stereo output */
        LvmStatus = LVREV_Process(pContext->hInstance,      /* Instance handle */
                                  pContext->InFrames32,     /* Input buffer */
                                  pContext->OutFrames32,    /* Output buffer */
                                  frameCount);              /* Number of samples to read */
    }

    LVM_ERROR_CHECK(LvmStatus, "LVREV_Process", "process")
    if(LvmStatus != LVREV_SUCCESS) return -EINVAL;

    // Convert to 16 bits
    if (pContext->auxiliary) {
        for (int i=0; i < frameCount*2; i++) { //always stereo here
            OutFrames16[i] = clamp16(pContext->OutFrames32[i]>>8);
        }
    } else {
        for (int i=0; i < frameCount*2; i++) { //always stereo here
            OutFrames16[i] = clamp16((pContext->OutFrames32[i]>>8) + (LVM_INT32)pIn[i]);
        }

        // apply volume with ramp if needed
        if ((pContext->leftVolume != pContext->prevLeftVolume ||
                pContext->rightVolume != pContext->prevRightVolume) &&
                pContext->volumeMode == REVERB_VOLUME_RAMP) {
            LVM_INT32 vl = (LVM_INT32)pContext->prevLeftVolume << 16;
            LVM_INT32 incl = (((LVM_INT32)pContext->leftVolume << 16) - vl) / frameCount;
            LVM_INT32 vr = (LVM_INT32)pContext->prevRightVolume << 16;
            LVM_INT32 incr = (((LVM_INT32)pContext->rightVolume << 16) - vr) / frameCount;

            for (int i = 0; i < frameCount; i++) {
                OutFrames16[2*i] =
                        clamp16((LVM_INT32)((vl >> 16) * OutFrames16[2*i]) >> 12);
                OutFrames16[2*i+1] =
                        clamp16((LVM_INT32)((vr >> 16) * OutFrames16[2*i+1]) >> 12);

                vl += incl;
                vr += incr;
            }

            pContext->prevLeftVolume = pContext->leftVolume;
            pContext->prevRightVolume = pContext->rightVolume;
        } else if (pContext->volumeMode != REVERB_VOLUME_OFF) {
            if (pContext->leftVolume != REVERB_UNIT_VOLUME ||
                pContext->rightVolume != REVERB_UNIT_VOLUME) {
                for (int i = 0; i < frameCount; i++) {
                    OutFrames16[2*i] =
                            clamp16((LVM_INT32)(pContext->leftVolume * OutFrames16[2*i]) >> 12);
                    OutFrames16[2*i+1] =
                            clamp16((LVM_INT32)(pContext->rightVolume * OutFrames16[2*i+1]) >> 12);
                }
            }
            pContext->prevLeftVolume = pContext->leftVolume;
            pContext->prevRightVolume = pContext->rightVolume;
            pContext->volumeMode = REVERB_VOLUME_RAMP;
        }
    }

    #ifdef LVM_PCM
    fwrite(OutFrames16, frameCount*sizeof(LVM_INT16)*2, 1, pContext->PcmOutPtr);
    fflush(pContext->PcmOutPtr);
    #endif

    // Accumulate if required
    if (pContext->config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE){
        for (int i=0; i<frameCount*2; i++){ //always stereo here
            pOut[i] = clamp16((int32_t)pOut[i] + (int32_t)OutFrames16[i]);
        }
    }else{
        memcpy(pOut, OutFrames16, frameCount*sizeof(LVM_INT16)*2);
    }

    return 0;
}    /* end process */

//----------------------------------------------------------------------------
// Reverb_free()
//----------------------------------------------------------------------------
// Purpose: Free all memory associated with the Bundle.
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

void Reverb_free(ReverbContext *pContext){

    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;         /* Function call status */
    LVREV_ControlParams_st    params;                        /* Control Parameters */
    LVREV_MemoryTable_st      MemTab;

    /* Free the algorithm memory */
    LvmStatus = LVREV_GetMemoryTable(pContext->hInstance,
                                   &MemTab,
                                   LVM_NULL);

    LVM_ERROR_CHECK(LvmStatus, "LVM_GetMemoryTable", "Reverb_free")

    for (int i=0; i<LVM_NR_MEMORY_REGIONS; i++){
        if (MemTab.Region[i].Size != 0){
            if (MemTab.Region[i].pBaseAddress != NULL){
                free(MemTab.Region[i].pBaseAddress);
            }
        }
    }
}    /* end Reverb_free */

//----------------------------------------------------------------------------
// Reverb_setConfig()
//----------------------------------------------------------------------------
// Purpose: Set input and output audio configuration.
//
// Inputs:
//  pContext:   effect engine context
//  pConfig:    pointer to effect_config_t structure holding input and output
//      configuration parameters
//
// Outputs:
//
//----------------------------------------------------------------------------

int Reverb_setConfig(ReverbContext *pContext, effect_config_t *pConfig){
    LVM_Fs_en   SampleRate;

    CHECK_ARG(pContext != NULL);
    CHECK_ARG(pConfig != NULL);

    CHECK_ARG(pConfig->inputCfg.samplingRate == pConfig->outputCfg.samplingRate);
    CHECK_ARG(pConfig->inputCfg.format == pConfig->outputCfg.format);
    CHECK_ARG((pContext->auxiliary && pConfig->inputCfg.channels == AUDIO_CHANNEL_OUT_MONO) ||
              ((!pContext->auxiliary) && pConfig->inputCfg.channels == AUDIO_CHANNEL_OUT_STEREO));
    CHECK_ARG(pConfig->outputCfg.channels == AUDIO_CHANNEL_OUT_STEREO);
    CHECK_ARG(pConfig->outputCfg.accessMode == EFFECT_BUFFER_ACCESS_WRITE
              || pConfig->outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE);
    CHECK_ARG(pConfig->inputCfg.format == AUDIO_FORMAT_PCM_16_BIT);

    pContext->config = *pConfig;


    switch (pConfig->inputCfg.samplingRate) {
    case 8000:
        SampleRate = LVM_FS_8000;
        break;
    case 16000:
        SampleRate = LVM_FS_16000;
        break;
    case 22050:
        SampleRate = LVM_FS_22050;
        break;
    case 32000:
        SampleRate = LVM_FS_32000;
        break;
    case 44100:
        SampleRate = LVM_FS_44100;
        break;
    case 48000:
        SampleRate = LVM_FS_48000;
        break;
    default:
        return -EINVAL;
    }

    if (pContext->SampleRate != SampleRate) {

        LVREV_ControlParams_st    ActiveParams;
        LVREV_ReturnStatus_en     LvmStatus = LVREV_SUCCESS;

        /* Get the current settings */
        LvmStatus = LVREV_GetControlParameters(pContext->hInstance,
                                         &ActiveParams);

        LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "Reverb_setConfig")
        if(LvmStatus != LVREV_SUCCESS) return -EINVAL;

        ActiveParams.SampleRate = SampleRate;

        LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);

        LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "Reverb_setConfig")
        if(LvmStatus != LVREV_SUCCESS) return -EINVAL;
        pContext->SampleRate = SampleRate;
    }

    return 0;
}   /* end Reverb_setConfig */

//----------------------------------------------------------------------------
// Reverb_getConfig()
//----------------------------------------------------------------------------
// Purpose: Get input and output audio configuration.
//
// Inputs:
//  pContext:   effect engine context
//  pConfig:    pointer to effect_config_t structure holding input and output
//      configuration parameters
//
// Outputs:
//
//----------------------------------------------------------------------------

void Reverb_getConfig(ReverbContext *pContext, effect_config_t *pConfig)
{
    *pConfig = pContext->config;
}   /* end Reverb_getConfig */

//----------------------------------------------------------------------------
// Reverb_init()
//----------------------------------------------------------------------------
// Purpose: Initialize engine with default configuration
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

int Reverb_init(ReverbContext *pContext){
    int status;

    CHECK_ARG(pContext != NULL);

    if (pContext->hInstance != NULL){
        Reverb_free(pContext);
    }

    pContext->config.inputCfg.accessMode                    = EFFECT_BUFFER_ACCESS_READ;
    if (pContext->auxiliary) {
        pContext->config.inputCfg.channels                  = AUDIO_CHANNEL_OUT_MONO;
    } else {
        pContext->config.inputCfg.channels                  = AUDIO_CHANNEL_OUT_STEREO;
    }

    pContext->config.inputCfg.format                        = AUDIO_FORMAT_PCM_16_BIT;
    pContext->config.inputCfg.samplingRate                  = 44100;
    pContext->config.inputCfg.bufferProvider.getBuffer      = NULL;
    pContext->config.inputCfg.bufferProvider.releaseBuffer  = NULL;
    pContext->config.inputCfg.bufferProvider.cookie         = NULL;
    pContext->config.inputCfg.mask                          = EFFECT_CONFIG_ALL;
    pContext->config.outputCfg.accessMode                   = EFFECT_BUFFER_ACCESS_ACCUMULATE;
    pContext->config.outputCfg.channels                     = AUDIO_CHANNEL_OUT_STEREO;
    pContext->config.outputCfg.format                       = AUDIO_FORMAT_PCM_16_BIT;
    pContext->config.outputCfg.samplingRate                 = 44100;
    pContext->config.outputCfg.bufferProvider.getBuffer     = NULL;
    pContext->config.outputCfg.bufferProvider.releaseBuffer = NULL;
    pContext->config.outputCfg.bufferProvider.cookie        = NULL;
    pContext->config.outputCfg.mask                         = EFFECT_CONFIG_ALL;

    pContext->leftVolume = REVERB_UNIT_VOLUME;
    pContext->rightVolume = REVERB_UNIT_VOLUME;
    pContext->prevLeftVolume = REVERB_UNIT_VOLUME;
    pContext->prevRightVolume = REVERB_UNIT_VOLUME;
    pContext->volumeMode = REVERB_VOLUME_FLAT;

    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;        /* Function call status */
    LVREV_ControlParams_st    params;                         /* Control Parameters */
    LVREV_InstanceParams_st   InstParams;                     /* Instance parameters */
    LVREV_MemoryTable_st      MemTab;                         /* Memory allocation table */
    bool                      bMallocFailure = LVM_FALSE;

    /* Set the capabilities */
    InstParams.MaxBlockSize  = MAX_CALL_SIZE;
    InstParams.SourceFormat  = LVM_STEREO;          // Max format, could be mono during process
    InstParams.NumDelays     = LVREV_DELAYLINES_4;

    /* Allocate memory, forcing alignment */
    LvmStatus = LVREV_GetMemoryTable(LVM_NULL,
                                  &MemTab,
                                  &InstParams);

    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetMemoryTable", "Reverb_init")
    if(LvmStatus != LVREV_SUCCESS) return -EINVAL;

    /* Allocate memory */
    for (int i=0; i<LVM_NR_MEMORY_REGIONS; i++){
        if (MemTab.Region[i].Size != 0){
            MemTab.Region[i].pBaseAddress = malloc(MemTab.Region[i].Size);

            if (MemTab.Region[i].pBaseAddress == LVM_NULL){
                bMallocFailure = LVM_TRUE;
            }
        }
    }

    /* If one or more of the memory regions failed to allocate, free the regions that were
     * succesfully allocated and return with an error
     */
    if(bMallocFailure == LVM_TRUE){
        for (int i=0; i<LVM_NR_MEMORY_REGIONS; i++){
            if (MemTab.Region[i].pBaseAddress != LVM_NULL){
                free(MemTab.Region[i].pBaseAddress);
            }
        }
        return -EINVAL;
    }

    /* Initialise */
    pContext->hInstance = LVM_NULL;

    /* Init sets the instance handle */
    LvmStatus = LVREV_GetInstanceHandle(&pContext->hInstance,
                                        &MemTab,
                                        &InstParams);

    LVM_ERROR_CHECK(LvmStatus, "LVM_GetInstanceHandle", "Reverb_init")
    if(LvmStatus != LVREV_SUCCESS) return -EINVAL;

    /* Set the initial process parameters */
    /* General parameters */
    params.OperatingMode  = LVM_MODE_ON;
    params.SampleRate     = LVM_FS_44100;
    pContext->SampleRate  = LVM_FS_44100;

    if(pContext->config.inputCfg.channels == AUDIO_CHANNEL_OUT_MONO){
        params.SourceFormat   = LVM_MONO;
    } else {
        params.SourceFormat   = LVM_STEREO;
    }

    /* Reverb parameters */
    params.Level          = 0;
    params.LPF            = 23999;
    params.HPF            = 50;
    params.T60            = 1490;
    params.Density        = 100;
    params.Damping        = 21;
    params.RoomSize       = 100;

    pContext->SamplesToExitCount = (params.T60 * pContext->config.inputCfg.samplingRate)/1000;

    /* Saved strength is used to return the exact strength that was used in the set to the get
     * because we map the original strength range of 0:1000 to 1:15, and this will avoid
     * quantisation like effect when returning
     */
    pContext->SavedRoomLevel    = -6000;
    pContext->SavedHfLevel      = 0;
    pContext->bEnabled          = LVM_FALSE;
    pContext->SavedDecayTime    = params.T60;
    pContext->SavedDecayHfRatio = params.Damping*20;
    pContext->SavedDensity      = params.RoomSize*10;
    pContext->SavedDiffusion    = params.Density*10;
    pContext->SavedReverbLevel  = -6000;

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance,
                                         &params);

    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "Reverb_init")
    if(LvmStatus != LVREV_SUCCESS) return -EINVAL;

    return 0;
}   /* end Reverb_init */

//----------------------------------------------------------------------------
// ReverbConvertLevel()
//----------------------------------------------------------------------------
// Purpose:
// Convert level from OpenSL ES format to LVM format
//
// Inputs:
//  level       level to be applied
//
//----------------------------------------------------------------------------

int16_t ReverbConvertLevel(int16_t level){
    static int16_t LevelArray[101] =
    {
       -12000, -4000,  -3398,  -3046,  -2796,  -2603,  -2444,  -2310,  -2194,  -2092,
       -2000,  -1918,  -1842,  -1773,  -1708,  -1648,  -1592,  -1540,  -1490,  -1443,
       -1398,  -1356,  -1316,  -1277,  -1240,  -1205,  -1171,  -1138,  -1106,  -1076,
       -1046,  -1018,  -990,   -963,   -938,   -912,   -888,   -864,   -841,   -818,
       -796,   -775,   -754,   -734,   -714,   -694,   -675,   -656,   -638,   -620,
       -603,   -585,   -568,   -552,   -536,   -520,   -504,   -489,   -474,   -459,
       -444,   -430,   -416,   -402,   -388,   -375,   -361,   -348,   -335,   -323,
       -310,   -298,   -286,   -274,   -262,   -250,   -239,   -228,   -216,   -205,
       -194,   -184,   -173,   -162,   -152,   -142,   -132,   -121,   -112,   -102,
       -92,    -82,    -73,    -64,    -54,    -45,    -36,    -27,    -18,    -9,
       0
    };
    int16_t i;

    for(i = 0; i < 101; i++)
    {
       if(level <= LevelArray[i])
           break;
    }
    return i;
}

//----------------------------------------------------------------------------
// ReverbConvertHFLevel()
//----------------------------------------------------------------------------
// Purpose:
// Convert level from OpenSL ES format to LVM format
//
// Inputs:
//  level       level to be applied
//
//----------------------------------------------------------------------------

int16_t ReverbConvertHfLevel(int16_t Hflevel){
    int16_t i;

    static LPFPair_t LPFArray[97] =
    {   // Limit range to 50 for LVREV parameter range
        {-10000, 50}, { -5000, 50 }, { -4000, 50},  { -3000, 158}, { -2000, 502},
        {-1000, 1666},{ -900, 1897}, { -800, 2169}, { -700, 2496}, { -600, 2895},
        {-500, 3400}, { -400, 4066}, { -300, 5011}, { -200, 6537}, { -100,  9826},
        {-99, 9881 }, { -98, 9937 }, { -97, 9994 }, { -96, 10052}, { -95, 10111},
        {-94, 10171}, { -93, 10231}, { -92, 10293}, { -91, 10356}, { -90, 10419},
        {-89, 10484}, { -88, 10549}, { -87, 10616}, { -86, 10684}, { -85, 10753},
        {-84, 10823}, { -83, 10895}, { -82, 10968}, { -81, 11042}, { -80, 11117},
        {-79, 11194}, { -78, 11272}, { -77, 11352}, { -76, 11433}, { -75, 11516},
        {-74, 11600}, { -73, 11686}, { -72, 11774}, { -71, 11864}, { -70, 11955},
        {-69, 12049}, { -68, 12144}, { -67, 12242}, { -66, 12341}, { -65, 12443},
        {-64, 12548}, { -63, 12654}, { -62, 12763}, { -61, 12875}, { -60, 12990},
        {-59, 13107}, { -58, 13227}, { -57, 13351}, { -56, 13477}, { -55, 13607},
        {-54, 13741}, { -53, 13878}, { -52, 14019}, { -51, 14164}, { -50, 14313},
        {-49, 14467}, { -48, 14626}, { -47, 14789}, { -46, 14958}, { -45, 15132},
        {-44, 15312}, { -43, 15498}, { -42, 15691}, { -41, 15890}, { -40, 16097},
        {-39, 16311}, { -38, 16534}, { -37, 16766}, { -36, 17007}, { -35, 17259},
        {-34, 17521}, { -33, 17795}, { -32, 18081}, { -31, 18381}, { -30, 18696},
        {-29, 19027}, { -28, 19375}, { -27, 19742}, { -26, 20129}, { -25, 20540},
        {-24, 20976}, { -23, 21439}, { -22, 21934}, { -21, 22463}, { -20, 23031},
        {-19, 23643}, { -18, 23999}
    };

    for(i = 0; i < 96; i++)
    {
        if(Hflevel <= LPFArray[i].Room_HF)
            break;
    }
    return LPFArray[i].LPF;
}

//----------------------------------------------------------------------------
// ReverbSetRoomHfLevel()
//----------------------------------------------------------------------------
// Purpose:
// Apply the HF level to the Reverb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//  level       level to be applied
//
//----------------------------------------------------------------------------

void ReverbSetRoomHfLevel(ReverbContext *pContext, int16_t level){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetRoomHfLevel")

    ActiveParams.LPF = ReverbConvertHfLevel(level);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetRoomHfLevel")
    pContext->SavedHfLevel = level;
    return;
}

//----------------------------------------------------------------------------
// ReverbGetRoomHfLevel()
//----------------------------------------------------------------------------
// Purpose:
// Get the level applied to the Revervb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int16_t ReverbGetRoomHfLevel(ReverbContext *pContext){
    int16_t level;

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetRoomHfLevel")

    level = ReverbConvertHfLevel(pContext->SavedHfLevel);

    return pContext->SavedHfLevel;
}

//----------------------------------------------------------------------------
// ReverbSetReverbLevel()
//----------------------------------------------------------------------------
// Purpose:
// Apply the level to the Reverb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//  level       level to be applied
//
//----------------------------------------------------------------------------

void ReverbSetReverbLevel(ReverbContext *pContext, int16_t level){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetReverbLevel")

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (level + pContext->SavedRoomLevel)-LVREV_MAX_REVERB_LEVEL;

    ActiveParams.Level = ReverbConvertLevel(CombinedLevel);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetReverbLevel")

    pContext->SavedReverbLevel = level;
    return;
}

//----------------------------------------------------------------------------
// ReverbGetReverbLevel()
//----------------------------------------------------------------------------
// Purpose:
// Get the level applied to the Revervb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int16_t ReverbGetReverbLevel(ReverbContext *pContext){
    int16_t level;

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetReverbLevel")

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (pContext->SavedReverbLevel + pContext->SavedRoomLevel)-LVREV_MAX_REVERB_LEVEL;

    //CombinedLevel, pContext->SavedReverbLevel, pContext->SavedRoomLevel);
    level = ReverbConvertLevel(CombinedLevel);

    return pContext->SavedReverbLevel;
}

//----------------------------------------------------------------------------
// ReverbSetRoomLevel()
//----------------------------------------------------------------------------
// Purpose:
// Apply the level to the Reverb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//  level       level to be applied
//
//----------------------------------------------------------------------------

void ReverbSetRoomLevel(ReverbContext *pContext, int16_t level){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetRoomLevel")

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (level + pContext->SavedReverbLevel)-LVREV_MAX_REVERB_LEVEL;
    ActiveParams.Level = ReverbConvertLevel(CombinedLevel);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetRoomLevel")

    pContext->SavedRoomLevel = level;
    return;
}

//----------------------------------------------------------------------------
// ReverbGetRoomLevel()
//----------------------------------------------------------------------------
// Purpose:
// Get the level applied to the Revervb. Must first be converted to LVM format
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int16_t ReverbGetRoomLevel(ReverbContext *pContext){
    int16_t level;

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT32                 CombinedLevel;             // Sum of room and reverb level controls

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetRoomLevel")

    // needs to subtract max levels for both RoomLevel and ReverbLevel
    CombinedLevel = (pContext->SavedRoomLevel + pContext->SavedReverbLevel-LVREV_MAX_REVERB_LEVEL);
    level = ReverbConvertLevel(CombinedLevel);

    return pContext->SavedRoomLevel;
}

//----------------------------------------------------------------------------
// ReverbSetDecayTime()
//----------------------------------------------------------------------------
// Purpose:
// Apply the decay time to the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  time        decay to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDecayTime(ReverbContext *pContext, uint32_t time){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDecayTime")

    if (time <= LVREV_MAX_T60) {
        ActiveParams.T60 = (LVM_UINT16)time;
    }
    else {
        ActiveParams.T60 = LVREV_MAX_T60;
    }

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDecayTime")

    pContext->SamplesToExitCount = (ActiveParams.T60 * pContext->config.inputCfg.samplingRate)/1000;
    pContext->SavedDecayTime = (int16_t)time;
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDecayTime()
//----------------------------------------------------------------------------
// Purpose:
// Get the decay time applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

uint32_t ReverbGetDecayTime(ReverbContext *pContext){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDecayTime")

    return (uint32_t)ActiveParams.T60;
}

//----------------------------------------------------------------------------
// ReverbSetDecayHfRatio()
//----------------------------------------------------------------------------
// Purpose:
// Apply the HF decay ratio to the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  ratio       ratio to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDecayHfRatio(ReverbContext *pContext, int16_t ratio){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;   /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDecayHfRatio")

    ActiveParams.Damping = (LVM_INT16)(ratio/20);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDecayHfRatio")

    pContext->SavedDecayHfRatio = ratio;
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDecayHfRatio()
//----------------------------------------------------------------------------
// Purpose:
// Get the HF decay ratio applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int32_t ReverbGetDecayHfRatio(ReverbContext *pContext){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;   /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDecayHfRatio")

    return pContext->SavedDecayHfRatio;
}

//----------------------------------------------------------------------------
// ReverbSetDiffusion()
//----------------------------------------------------------------------------
// Purpose:
// Apply the diffusion to the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  level        decay to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDiffusion(ReverbContext *pContext, int16_t level){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDiffusion")

    ActiveParams.Density = (LVM_INT16)(level/10);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDiffusion")

    pContext->SavedDiffusion = level;
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDiffusion()
//----------------------------------------------------------------------------
// Purpose:
// Get the decay time applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int32_t ReverbGetDiffusion(ReverbContext *pContext){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT16                 Temp;

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDiffusion")

    Temp = (LVM_INT16)(pContext->SavedDiffusion/10);

    return pContext->SavedDiffusion;
}

//----------------------------------------------------------------------------
// ReverbSetDensity()
//----------------------------------------------------------------------------
// Purpose:
// Apply the density level the Reverb.
//
// Inputs:
//  pContext:   effect engine context
//  level        decay to be applied
//
//----------------------------------------------------------------------------

void ReverbSetDensity(ReverbContext *pContext, int16_t level){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbSetDensity")

    ActiveParams.RoomSize = (LVM_INT16)(((level * 99) / 1000) + 1);

    /* Activate the initial settings */
    LvmStatus = LVREV_SetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_SetControlParameters", "ReverbSetDensity")

    pContext->SavedDensity = level;
    return;
}

//----------------------------------------------------------------------------
// ReverbGetDensity()
//----------------------------------------------------------------------------
// Purpose:
// Get the density level applied to the Revervb.
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int32_t ReverbGetDensity(ReverbContext *pContext){

    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */
    LVM_INT16                 Temp;
    /* Get the current settings */
    LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
    LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "ReverbGetDensity")

    Temp = (LVM_INT16)(((pContext->SavedDensity * 99) / 1000) + 1);

    return pContext->SavedDensity;
}

//----------------------------------------------------------------------------
// Reverb_LoadPreset()
//----------------------------------------------------------------------------
// Purpose:
// Load a the next preset
//
// Inputs:
//  pContext         - handle to instance data
//
// Outputs:
//
// Side Effects:
//
//----------------------------------------------------------------------------
int Reverb_LoadPreset(ReverbContext   *pContext)
{
    //TODO: add reflections delay, level and reverb delay when early reflections are
    // implemented
    pContext->curPreset = pContext->nextPreset;

    if (pContext->curPreset != REVERB_PRESET_NONE) {
        const t_reverb_settings *preset = &sReverbPresets[pContext->curPreset];
        ReverbSetRoomLevel(pContext, preset->roomLevel);
        ReverbSetRoomHfLevel(pContext, preset->roomHFLevel);
        ReverbSetDecayTime(pContext, preset->decayTime);
        ReverbSetDecayHfRatio(pContext, preset->decayHFRatio);
        //reflectionsLevel
        //reflectionsDelay
        ReverbSetReverbLevel(pContext, preset->reverbLevel);
        // reverbDelay
        ReverbSetDiffusion(pContext, preset->diffusion);
        ReverbSetDensity(pContext, preset->density);
    }

    return 0;
}


//----------------------------------------------------------------------------
// Reverb_getParameter()
//----------------------------------------------------------------------------
// Purpose:
// Get a Reverb parameter
//
// Inputs:
//  pContext         - handle to instance data
//  pParam           - pointer to parameter
//  pValue           - pointer to variable to hold retrieved value
//  pValueSize       - pointer to value size: maximum size as input
//
// Outputs:
//  *pValue updated with parameter value
//  *pValueSize updated with actual value size
//
//
// Side Effects:
//
//----------------------------------------------------------------------------

int Reverb_getParameter(ReverbContext *pContext,
                        void          *pParam,
                        size_t        *pValueSize,
                        void          *pValue){
    int status = 0;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;
    char *name;
    t_reverb_settings *pProperties;

    if (pContext->preset) {
        if (param != REVERB_PARAM_PRESET || *pValueSize < sizeof(uint16_t)) {
            return -EINVAL;
        }

        *(uint16_t *)pValue = pContext->nextPreset;
        return 0;
    }

    switch (param){
        case REVERB_PARAM_ROOM_LEVEL:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_ROOM_HF_LEVEL:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_DECAY_TIME:
            if (*pValueSize != sizeof(uint32_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case REVERB_PARAM_DECAY_HF_RATIO:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_REFLECTIONS_LEVEL:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_REFLECTIONS_DELAY:
            if (*pValueSize != sizeof(uint32_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case REVERB_PARAM_REVERB_LEVEL:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_REVERB_DELAY:
            if (*pValueSize != sizeof(uint32_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case REVERB_PARAM_DIFFUSION:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_DENSITY:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;
        case REVERB_PARAM_PROPERTIES:
            if (*pValueSize != sizeof(t_reverb_settings)){
                return -EINVAL;
            }
            *pValueSize = sizeof(t_reverb_settings);
            break;

        default:
            return -EINVAL;
    }

    pProperties = (t_reverb_settings *) pValue;

    switch (param){
        case REVERB_PARAM_PROPERTIES:
            pProperties->roomLevel = ReverbGetRoomLevel(pContext);
            pProperties->roomHFLevel = ReverbGetRoomHfLevel(pContext);
            pProperties->decayTime = ReverbGetDecayTime(pContext);
            pProperties->decayHFRatio = ReverbGetDecayHfRatio(pContext);
            pProperties->reflectionsLevel = 0;
            pProperties->reflectionsDelay = 0;
            pProperties->reverbDelay = 0;
            pProperties->reverbLevel = ReverbGetReverbLevel(pContext);
            pProperties->diffusion = ReverbGetDiffusion(pContext);
            pProperties->density = ReverbGetDensity(pContext);

            break;

        case REVERB_PARAM_ROOM_LEVEL:
            *(int16_t *)pValue = ReverbGetRoomLevel(pContext);

            break;
        case REVERB_PARAM_ROOM_HF_LEVEL:
            *(int16_t *)pValue = ReverbGetRoomHfLevel(pContext);

            break;
        case REVERB_PARAM_DECAY_TIME:
            *(uint32_t *)pValue = ReverbGetDecayTime(pContext);

            break;
        case REVERB_PARAM_DECAY_HF_RATIO:
            *(int16_t *)pValue = ReverbGetDecayHfRatio(pContext);

            break;
        case REVERB_PARAM_REVERB_LEVEL:
             *(int16_t *)pValue = ReverbGetReverbLevel(pContext);

            break;
        case REVERB_PARAM_DIFFUSION:
            *(int16_t *)pValue = ReverbGetDiffusion(pContext);

            break;
        case REVERB_PARAM_DENSITY:
            *(uint16_t *)pValue = 0;
            *(int16_t *)pValue = ReverbGetDensity(pContext);
            break;
        case REVERB_PARAM_REFLECTIONS_LEVEL:
            *(uint16_t *)pValue = 0;
        case REVERB_PARAM_REFLECTIONS_DELAY:
            *(uint32_t *)pValue = 0;
        case REVERB_PARAM_REVERB_DELAY:
            *(uint32_t *)pValue = 0;
            break;

        default:
            status = -EINVAL;
            break;
    }

    return status;
} /* end Reverb_getParameter */

//----------------------------------------------------------------------------
// Reverb_setParameter()
//----------------------------------------------------------------------------
// Purpose:
// Set a Reverb parameter
//
// Inputs:
//  pContext         - handle to instance data
//  pParam           - pointer to parameter
//  pValue           - pointer to value
//
// Outputs:
//
//----------------------------------------------------------------------------

int Reverb_setParameter (ReverbContext *pContext, void *pParam, void *pValue){
    int status = 0;
    int16_t level;
    int16_t ratio;
    uint32_t time;
    t_reverb_settings *pProperties;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;

    if (pContext->preset) {
        if (param != REVERB_PARAM_PRESET) {
            return -EINVAL;
        }

        uint16_t preset = *(uint16_t *)pValue;
        if (preset > REVERB_PRESET_LAST) {
            return -EINVAL;
        }
        pContext->nextPreset = preset;
        return 0;
    }

    switch (param){
        case REVERB_PARAM_PROPERTIES:
            pProperties = (t_reverb_settings *) pValue;
            ReverbSetRoomLevel(pContext, pProperties->roomLevel);
            ReverbSetRoomHfLevel(pContext, pProperties->roomHFLevel);
            ReverbSetDecayTime(pContext, pProperties->decayTime);
            ReverbSetDecayHfRatio(pContext, pProperties->decayHFRatio);
            ReverbSetReverbLevel(pContext, pProperties->reverbLevel);
            ReverbSetDiffusion(pContext, pProperties->diffusion);
            ReverbSetDensity(pContext, pProperties->density);
            break;
        case REVERB_PARAM_ROOM_LEVEL:
            level = *(int16_t *)pValue;
            ReverbSetRoomLevel(pContext, level);
           break;
        case REVERB_PARAM_ROOM_HF_LEVEL:
            level = *(int16_t *)pValue;
            ReverbSetRoomHfLevel(pContext, level);
           break;
        case REVERB_PARAM_DECAY_TIME:
            time = *(uint32_t *)pValue;
            ReverbSetDecayTime(pContext, time);
           break;
        case REVERB_PARAM_DECAY_HF_RATIO:
            ratio = *(int16_t *)pValue;
            ReverbSetDecayHfRatio(pContext, ratio);
            break;
         case REVERB_PARAM_REVERB_LEVEL:
            level = *(int16_t *)pValue;
            ReverbSetReverbLevel(pContext, level);
           break;
        case REVERB_PARAM_DIFFUSION:
            ratio = *(int16_t *)pValue;
            ReverbSetDiffusion(pContext, ratio);
            break;
        case REVERB_PARAM_DENSITY:
            ratio = *(int16_t *)pValue;
            ReverbSetDensity(pContext, ratio);
            break;
           break;
        case REVERB_PARAM_REFLECTIONS_LEVEL:
        case REVERB_PARAM_REFLECTIONS_DELAY:
        case REVERB_PARAM_REVERB_DELAY:
            break;
        default:
            break;
    }

    return status;
} /* end Reverb_setParameter */

} // namespace
} // namespace

extern "C" {
/* Effect Control Interface Implementation: Process */
int Reverb_process(effect_handle_t   self,
                                 audio_buffer_t         *inBuffer,
                                 audio_buffer_t         *outBuffer){
    android::ReverbContext * pContext = (android::ReverbContext *) self;
    int    status = 0;

    if (pContext == NULL){
        return -EINVAL;
    }
    if (inBuffer == NULL  || inBuffer->raw == NULL  ||
            outBuffer == NULL || outBuffer->raw == NULL ||
            inBuffer->frameCount != outBuffer->frameCount){
        return -EINVAL;
    }
    /* Process all the available frames, block processing is handled internalLY by the LVM bundle */
    status = process(    (LVM_INT16 *)inBuffer->raw,
                         (LVM_INT16 *)outBuffer->raw,
                                      outBuffer->frameCount,
                                      pContext);

    if (pContext->bEnabled == LVM_FALSE) {
        if (pContext->SamplesToExitCount > 0) {
            pContext->SamplesToExitCount -= outBuffer->frameCount;
        } else {
            status = -ENODATA;
        }
    }

    return status;
}   /* end Reverb_process */

/* Effect Control Interface Implementation: Command */
int Reverb_command(effect_handle_t  self,
                              uint32_t            cmdCode,
                              uint32_t            cmdSize,
                              void                *pCmdData,
                              uint32_t            *replySize,
                              void                *pReplyData){
    android::ReverbContext * pContext = (android::ReverbContext *) self;
    int retsize;
    LVREV_ControlParams_st    ActiveParams;              /* Current control Parameters */
    LVREV_ReturnStatus_en     LvmStatus=LVREV_SUCCESS;     /* Function call status */

    if (pContext == NULL){
        return -EINVAL;
    }

    switch (cmdCode){
        case EFFECT_CMD_INIT:

            if (pReplyData == NULL || *replySize != sizeof(int)){
                return -EINVAL;
            }
            *(int *) pReplyData = 0;
            break;

        case EFFECT_CMD_SET_CONFIG:
            if (pCmdData == NULL ||
                cmdSize != sizeof(effect_config_t) ||
                pReplyData == NULL ||
                *replySize != sizeof(int)) {
                return -EINVAL;
            }
            *(int *) pReplyData = android::Reverb_setConfig(pContext,
                                                            (effect_config_t *) pCmdData);
            break;

        case EFFECT_CMD_GET_CONFIG:
            if (pReplyData == NULL ||
                *replySize != sizeof(effect_config_t)) {
                return -EINVAL;
            }

            android::Reverb_getConfig(pContext, (effect_config_t *)pReplyData);
            break;

        case EFFECT_CMD_RESET:
            Reverb_setConfig(pContext, &pContext->config);
            break;

        case EFFECT_CMD_GET_PARAM:{
            if (pCmdData == NULL ||
                    cmdSize < (int)(sizeof(effect_param_t) + sizeof(int32_t)) ||
                    pReplyData == NULL ||
                    *replySize < (int) (sizeof(effect_param_t) + sizeof(int32_t))){
                return -EINVAL;
            }
            effect_param_t *p = (effect_param_t *)pCmdData;

            memcpy(pReplyData, pCmdData, sizeof(effect_param_t) + p->psize);

            p = (effect_param_t *)pReplyData;

            int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);

            p->status = android::Reverb_getParameter(pContext,
                                                         (void *)p->data,
                                                         (size_t  *)&p->vsize,
                                                          p->data + voffset);

            *replySize = sizeof(effect_param_t) + voffset + p->vsize;

        } break;
        case EFFECT_CMD_SET_PARAM:{

            if (pCmdData == NULL || (cmdSize < (int)(sizeof(effect_param_t) + sizeof(int32_t)))
                    || pReplyData == NULL || *replySize != (int)sizeof(int32_t)) {
                return -EINVAL;
            }

            effect_param_t *p = (effect_param_t *) pCmdData;

            if (p->psize != sizeof(int32_t)){
                return -EINVAL;
            }

            *(int *)pReplyData = android::Reverb_setParameter(pContext,
                                                             (void *)p->data,
                                                              p->data + p->psize);
        } break;

        case EFFECT_CMD_ENABLE:

            if (pReplyData == NULL || *replySize != sizeof(int)){
                return -EINVAL;
            }
            if(pContext->bEnabled == LVM_TRUE){
                 return -EINVAL;
             }
            *(int *)pReplyData = 0;
            pContext->bEnabled = LVM_TRUE;
            /* Get the current settings */
            LvmStatus = LVREV_GetControlParameters(pContext->hInstance, &ActiveParams);
            LVM_ERROR_CHECK(LvmStatus, "LVREV_GetControlParameters", "EFFECT_CMD_ENABLE")
            pContext->SamplesToExitCount =
                    (ActiveParams.T60 * pContext->config.inputCfg.samplingRate)/1000;
            // force no volume ramp for first buffer processed after enabling the effect
            pContext->volumeMode = android::REVERB_VOLUME_FLAT;
            break;
        case EFFECT_CMD_DISABLE:

            if (pReplyData == NULL || *replySize != sizeof(int)){
                return -EINVAL;
            }
            if(pContext->bEnabled == LVM_FALSE){
                 return -EINVAL;
             }
            *(int *)pReplyData = 0;
            pContext->bEnabled = LVM_FALSE;
            break;

        case EFFECT_CMD_SET_VOLUME:
            if (pCmdData == NULL ||
                cmdSize != 2 * sizeof(uint32_t)) {
                return -EINVAL;
            }

            if (pReplyData != NULL) { // we have volume control
                pContext->leftVolume = (LVM_INT16)((*(uint32_t *)pCmdData + (1 << 11)) >> 12);
                pContext->rightVolume = (LVM_INT16)((*((uint32_t *)pCmdData + 1) + (1 << 11)) >> 12);
                *(uint32_t *)pReplyData = (1 << 24);
                *((uint32_t *)pReplyData + 1) = (1 << 24);
                if (pContext->volumeMode == android::REVERB_VOLUME_OFF) {
                    // force no volume ramp for first buffer processed after getting volume control
                    pContext->volumeMode = android::REVERB_VOLUME_FLAT;
                }
            } else { // we don't have volume control
                pContext->leftVolume = REVERB_UNIT_VOLUME;
                pContext->rightVolume = REVERB_UNIT_VOLUME;
                pContext->volumeMode = android::REVERB_VOLUME_OFF;
            }
            break;

        case EFFECT_CMD_SET_DEVICE:
        case EFFECT_CMD_SET_AUDIO_MODE:
            break;

        default:
            return -EINVAL;
    }

    return 0;
}    /* end Reverb_command */

/* Effect Control Interface Implementation: get_descriptor */
int Reverb_getDescriptor(effect_handle_t   self,
                                    effect_descriptor_t *pDescriptor)
{
    android::ReverbContext * pContext = (android::ReverbContext *)self;
    const effect_descriptor_t *desc;

    if (pContext == NULL || pDescriptor == NULL) {
        return -EINVAL;
    }

    if (pContext->auxiliary) {
        if (pContext->preset) {
            desc = &android::gAuxPresetReverbDescriptor;
        } else {
            desc = &android::gAuxEnvReverbDescriptor;
        }
    } else {
        if (pContext->preset) {
            desc = &android::gInsertPresetReverbDescriptor;
        } else {
            desc = &android::gInsertEnvReverbDescriptor;
        }
    }

    *pDescriptor = *desc;

    return 0;
}   /* end Reverb_getDescriptor */

// effect_handle_t interface implementation for Reverb effect
const struct effect_interface_s gReverbInterface = {
    Reverb_process,
    Reverb_command,
    Reverb_getDescriptor,
    NULL,
};    /* end gReverbInterface */

// This is the only symbol that needs to be exported
__attribute__ ((visibility ("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    tag : AUDIO_EFFECT_LIBRARY_TAG,
    version : EFFECT_LIBRARY_API_VERSION,
    name : "Reverb Library",
    implementor : "NXP Software Ltd.",
    create_effect : android::EffectCreate,
    release_effect : android::EffectRelease,
    get_descriptor : android::EffectGetDescriptor,
};

}

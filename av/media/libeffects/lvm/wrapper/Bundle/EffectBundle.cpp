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

#define LOG_TAG "Bundle"
#define ARRAY_SIZE(array) (sizeof array / sizeof array[0])
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "EffectBundle.h"


// effect_handle_t interface implementation for bass boost
extern "C" const struct effect_interface_s gLvmEffectInterface;

static inline int16_t clamp16(int32_t sample)
{
    // check overflow for both positive and negative values:
    // all bits above short range must me equal to sign bit
    if ((sample>>15) ^ (sample>>31))
        sample = 0x7FFF ^ (sample>>31);
    return sample;
}

// Namespaces
namespace android {
namespace {

// Flag to allow a one time init of global memory, only happens on first call ever
int LvmInitFlag = LVM_FALSE;
SessionContext GlobalSessionMemory[LVM_MAX_SESSIONS];
int SessionIndex[LVM_MAX_SESSIONS];

/* local functions */
#define CHECK_ARG(cond) {                     \
    if (!(cond)) {                            \
        return -EINVAL;                       \
    }                                         \
}


// NXP SW BassBoost UUID
const effect_descriptor_t gBassBoostDescriptor = {
        {0x0634f220, 0xddd4, 0x11db, 0xa0fc, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b }},
        {0x8631f300, 0x72e2, 0x11df, 0xb57e, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}, // uuid
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_FIRST | EFFECT_FLAG_DEVICE_IND
        | EFFECT_FLAG_VOLUME_CTRL),
        BASS_BOOST_CUP_LOAD_ARM9E,
        BUNDLE_MEM_USAGE,
        "Dynamic Bass Boost",
        "NXP Software Ltd.",
};

// NXP SW Virtualizer UUID
const effect_descriptor_t gVirtualizerDescriptor = {
        {0x37cc2c00, 0xdddd, 0x11db, 0x8577, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
        {0x1d4033c0, 0x8557, 0x11df, 0x9f2d, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}},
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_LAST | EFFECT_FLAG_DEVICE_IND
        | EFFECT_FLAG_VOLUME_CTRL),
        VIRTUALIZER_CUP_LOAD_ARM9E,
        BUNDLE_MEM_USAGE,
        "Virtualizer",
        "NXP Software Ltd.",
};

// NXP SW Equalizer UUID
const effect_descriptor_t gEqualizerDescriptor = {
        {0x0bed4300, 0xddd6, 0x11db, 0x8f34, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}, // type
        {0xce772f20, 0x847d, 0x11df, 0xbb17, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}, // uuid Eq NXP
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_FIRST | EFFECT_FLAG_VOLUME_CTRL),
        EQUALIZER_CUP_LOAD_ARM9E,
        BUNDLE_MEM_USAGE,
        "Equalizer",
        "NXP Software Ltd.",
};

// NXP SW Volume UUID
const effect_descriptor_t gVolumeDescriptor = {
        {0x09e8ede0, 0xddde, 0x11db, 0xb4f6, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b }},
        {0x119341a0, 0x8469, 0x11df, 0x81f9, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b }}, //uuid VOL NXP
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_LAST | EFFECT_FLAG_VOLUME_CTRL),
        VOLUME_CUP_LOAD_ARM9E,
        BUNDLE_MEM_USAGE,
        "Volume",
        "NXP Software Ltd.",
};

//--- local function prototypes
void LvmGlobalBundle_init      (void);
int  LvmBundle_init            (EffectContext *pContext);
int  LvmEffect_enable          (EffectContext *pContext);
int  LvmEffect_disable         (EffectContext *pContext);
void LvmEffect_free            (EffectContext *pContext);
int  Effect_setConfig          (EffectContext *pContext, effect_config_t *pConfig);
void Effect_getConfig          (EffectContext *pContext, effect_config_t *pConfig);
int  BassBoost_setParameter    (EffectContext *pContext, void *pParam, void *pValue);
int  BassBoost_getParameter    (EffectContext *pContext,
                               void           *pParam,
                               size_t         *pValueSize,
                               void           *pValue);
int  Virtualizer_setParameter  (EffectContext *pContext, void *pParam, void *pValue);
int  Virtualizer_getParameter  (EffectContext *pContext,
                               void           *pParam,
                               size_t         *pValueSize,
                               void           *pValue);
int  Equalizer_setParameter    (EffectContext *pContext, void *pParam, void *pValue);
int  Equalizer_getParameter    (EffectContext *pContext,
                                void          *pParam,
                                size_t        *pValueSize,
                                void          *pValue);
int  Volume_setParameter       (EffectContext *pContext, void *pParam, void *pValue);
int  Volume_getParameter       (EffectContext *pContext,
                                void          *pParam,
                                size_t        *pValueSize,
                                void          *pValue);
int Effect_setEnabled(EffectContext *pContext, bool enabled);

/* Effect Library Interface Implementation */

extern "C" int EffectCreate(const effect_uuid_t *uuid,
                            int32_t             sessionId,
                            int32_t             ioId,
                            effect_handle_t  *pHandle){
    int ret = 0;
    int sessionNo;
    int i;
    EffectContext *pContext = NULL;
    bool newBundle = false;
    SessionContext *pSessionContext;

    if (pHandle == NULL || uuid == NULL){
        ret = -EINVAL;
        goto exit;
    }

    if(LvmInitFlag == LVM_FALSE){
        LvmInitFlag = LVM_TRUE;
        LvmGlobalBundle_init();
    }

    // Find next available sessionNo
    for(i=0; i<LVM_MAX_SESSIONS; i++){
        if((SessionIndex[i] == LVM_UNUSED_SESSION)||(SessionIndex[i] == sessionId)){
            sessionNo       = i;
            SessionIndex[i] = sessionId;
            break;
        }
    }

    if(i==LVM_MAX_SESSIONS){
        ret = -EINVAL;
        goto exit;
    }

    pContext = new EffectContext;

    // If this is the first create in this session
    if(GlobalSessionMemory[sessionNo].bBundledEffectsEnabled == LVM_FALSE){
        GlobalSessionMemory[sessionNo].bBundledEffectsEnabled = LVM_TRUE;
        GlobalSessionMemory[sessionNo].pBundledContext        = new BundledEffectContext;
        newBundle = true;

        pContext->pBundledContext = GlobalSessionMemory[sessionNo].pBundledContext;
        pContext->pBundledContext->SessionNo                = sessionNo;
        pContext->pBundledContext->SessionId                = sessionId;
        pContext->pBundledContext->hInstance                = NULL;
        pContext->pBundledContext->bVolumeEnabled           = LVM_FALSE;
        pContext->pBundledContext->bEqualizerEnabled        = LVM_FALSE;
        pContext->pBundledContext->bBassEnabled             = LVM_FALSE;
        pContext->pBundledContext->bBassTempDisabled        = LVM_FALSE;
        pContext->pBundledContext->bVirtualizerEnabled      = LVM_FALSE;
        pContext->pBundledContext->bVirtualizerTempDisabled = LVM_FALSE;
        pContext->pBundledContext->NumberEffectsEnabled     = 0;
        pContext->pBundledContext->NumberEffectsCalled      = 0;
        pContext->pBundledContext->firstVolume              = LVM_TRUE;
        pContext->pBundledContext->volume                   = 0;

        #ifdef LVM_PCM
        char fileName[256];
        snprintf(fileName, 256, "/data/tmp/bundle_%p_pcm_in.pcm", pContext->pBundledContext);
        pContext->pBundledContext->PcmInPtr = fopen(fileName, "w");
        if (pContext->pBundledContext->PcmInPtr == NULL) {
            ret = -EINVAL;
            goto exit;
        }

        snprintf(fileName, 256, "/data/tmp/bundle_%p_pcm_out.pcm", pContext->pBundledContext);
        pContext->pBundledContext->PcmOutPtr = fopen(fileName, "w");
        if (pContext->pBundledContext->PcmOutPtr == NULL) {
            fclose(pContext->pBundledContext->PcmInPtr);
           pContext->pBundledContext->PcmInPtr = NULL;
           ret = -EINVAL;
           goto exit;
        }
        #endif

        /* Saved strength is used to return the exact strength that was used in the set to the get
         * because we map the original strength range of 0:1000 to 1:15, and this will avoid
         * quantisation like effect when returning
         */
        pContext->pBundledContext->BassStrengthSaved        = 0;
        pContext->pBundledContext->VirtStrengthSaved        = 0;
        pContext->pBundledContext->CurPreset                = PRESET_CUSTOM;
        pContext->pBundledContext->levelSaved               = 0;
        pContext->pBundledContext->bMuteEnabled             = LVM_FALSE;
        pContext->pBundledContext->bStereoPositionEnabled   = LVM_FALSE;
        pContext->pBundledContext->positionSaved            = 0;
        pContext->pBundledContext->workBuffer               = NULL;
        pContext->pBundledContext->frameCount               = -1;
        pContext->pBundledContext->SamplesToExitCountVirt   = 0;
        pContext->pBundledContext->SamplesToExitCountBb     = 0;
        pContext->pBundledContext->SamplesToExitCountEq     = 0;

        for (int i = 0; i < FIVEBAND_NUMBANDS; i++) {
            pContext->pBundledContext->bandGaindB[i] = EQNB_5BandSoftPresets[i];
        }

        ret = LvmBundle_init(pContext);

        if (ret < 0){
            goto exit;
        }
    }
    else{
        pContext->pBundledContext =
                GlobalSessionMemory[sessionNo].pBundledContext;
    }

    pSessionContext = &GlobalSessionMemory[pContext->pBundledContext->SessionNo];

    // Create each Effect
    if (memcmp(uuid, &gBassBoostDescriptor.uuid, sizeof(effect_uuid_t)) == 0){
        // Create Bass Boost
        pSessionContext->bBassInstantiated = LVM_TRUE;
        pContext->pBundledContext->SamplesToExitCountBb = 0;

        pContext->itfe       = &gLvmEffectInterface;
        pContext->EffectType = LVM_BASS_BOOST;
    } else if (memcmp(uuid, &gVirtualizerDescriptor.uuid, sizeof(effect_uuid_t)) == 0){
        // Create Virtualizer
        pSessionContext->bVirtualizerInstantiated=LVM_TRUE;
        pContext->pBundledContext->SamplesToExitCountVirt = 0;

        pContext->itfe       = &gLvmEffectInterface;
        pContext->EffectType = LVM_VIRTUALIZER;
    } else if (memcmp(uuid, &gEqualizerDescriptor.uuid, sizeof(effect_uuid_t)) == 0){
        // Create Equalizer
        pSessionContext->bEqualizerInstantiated = LVM_TRUE;
        pContext->pBundledContext->SamplesToExitCountEq = 0;

        pContext->itfe       = &gLvmEffectInterface;
        pContext->EffectType = LVM_EQUALIZER;
    } else if (memcmp(uuid, &gVolumeDescriptor.uuid, sizeof(effect_uuid_t)) == 0){
        // Create Volume
        pSessionContext->bVolumeInstantiated = LVM_TRUE;

        pContext->itfe       = &gLvmEffectInterface;
        pContext->EffectType = LVM_VOLUME;
    }
    else{
        ret = -EINVAL;
        goto exit;
    }

exit:
    if (ret != 0) {
        if (pContext != NULL) {
            if (newBundle) {
                GlobalSessionMemory[sessionNo].bBundledEffectsEnabled = LVM_FALSE;
                SessionIndex[sessionNo] = LVM_UNUSED_SESSION;
                delete pContext->pBundledContext;
            }
            delete pContext;
        }
        *pHandle = (effect_handle_t)NULL;
    } else {
        *pHandle = (effect_handle_t)pContext;
    }
    return ret;
} /* end EffectCreate */

extern "C" int EffectRelease(effect_handle_t handle){
    EffectContext * pContext = (EffectContext *)handle;

    if (pContext == NULL){
        return -EINVAL;
    }

    SessionContext *pSessionContext = &GlobalSessionMemory[pContext->pBundledContext->SessionNo];

    // Clear the instantiated flag for the effect
    // protect agains the case where an effect is un-instantiated without being disabled
    if(pContext->EffectType == LVM_BASS_BOOST) {
        pSessionContext->bBassInstantiated = LVM_FALSE;
        if(pContext->pBundledContext->SamplesToExitCountBb > 0){
            pContext->pBundledContext->NumberEffectsEnabled--;
        }
        pContext->pBundledContext->SamplesToExitCountBb = 0;
    } else if(pContext->EffectType == LVM_VIRTUALIZER) {
        pSessionContext->bVirtualizerInstantiated = LVM_FALSE;
        if(pContext->pBundledContext->SamplesToExitCountVirt > 0){
            pContext->pBundledContext->NumberEffectsEnabled--;
        }
        pContext->pBundledContext->SamplesToExitCountVirt = 0;
    } else if(pContext->EffectType == LVM_EQUALIZER) {
        pSessionContext->bEqualizerInstantiated =LVM_FALSE;
        if(pContext->pBundledContext->SamplesToExitCountEq > 0){
            pContext->pBundledContext->NumberEffectsEnabled--;
        }
        pContext->pBundledContext->SamplesToExitCountEq = 0;
    } else if(pContext->EffectType == LVM_VOLUME) {
        pSessionContext->bVolumeInstantiated = LVM_FALSE;
        if (pContext->pBundledContext->bVolumeEnabled == LVM_TRUE){
            pContext->pBundledContext->NumberEffectsEnabled--;
        }
    }

    // Disable effect, in this case ignore errors (return codes)
    // if an effect has already been disabled
    Effect_setEnabled(pContext, LVM_FALSE);

    // if all effects are no longer instantiaed free the lvm memory and delete BundledEffectContext
    if ((pSessionContext->bBassInstantiated == LVM_FALSE) &&
            (pSessionContext->bVolumeInstantiated == LVM_FALSE) &&
            (pSessionContext->bEqualizerInstantiated ==LVM_FALSE) &&
            (pSessionContext->bVirtualizerInstantiated==LVM_FALSE))
    {
        #ifdef LVM_PCM
        if (pContext->pBundledContext->PcmInPtr != NULL) {
            fclose(pContext->pBundledContext->PcmInPtr);
            pContext->pBundledContext->PcmInPtr = NULL;
        }
        if (pContext->pBundledContext->PcmOutPtr != NULL) {
            fclose(pContext->pBundledContext->PcmOutPtr);
            pContext->pBundledContext->PcmOutPtr = NULL;
        }
        #endif


        // Clear the SessionIndex
        for(int i=0; i<LVM_MAX_SESSIONS; i++){
            if(SessionIndex[i] == pContext->pBundledContext->SessionId){
                SessionIndex[i] = LVM_UNUSED_SESSION;
                break;
            }
        }

        pSessionContext->bBundledEffectsEnabled = LVM_FALSE;
        pSessionContext->pBundledContext = LVM_NULL;
        LvmEffect_free(pContext);
        if (pContext->pBundledContext->workBuffer != NULL) {
            free(pContext->pBundledContext->workBuffer);
        }
        delete pContext->pBundledContext;
        pContext->pBundledContext = LVM_NULL;
    }
    // free the effect context for current effect
    delete pContext;

    return 0;

} /* end EffectRelease */

extern "C" int EffectGetDescriptor(const effect_uuid_t *uuid,
                                   effect_descriptor_t *pDescriptor) {
    const effect_descriptor_t *desc = NULL;

    if (pDescriptor == NULL || uuid == NULL){
        return -EINVAL;
    }

    if (memcmp(uuid, &gBassBoostDescriptor.uuid, sizeof(effect_uuid_t)) == 0) {
        desc = &gBassBoostDescriptor;
    } else if (memcmp(uuid, &gVirtualizerDescriptor.uuid, sizeof(effect_uuid_t)) == 0) {
        desc = &gVirtualizerDescriptor;
    } else if (memcmp(uuid, &gEqualizerDescriptor.uuid, sizeof(effect_uuid_t)) == 0) {
        desc = &gEqualizerDescriptor;
    } else if (memcmp(uuid, &gVolumeDescriptor.uuid, sizeof(effect_uuid_t)) == 0) {
        desc = &gVolumeDescriptor;
    }

    if (desc == NULL) {
        return  -EINVAL;
    }

    *pDescriptor = *desc;

    return 0;
} /* end EffectGetDescriptor */

void LvmGlobalBundle_init(){
    for(int i=0; i<LVM_MAX_SESSIONS; i++){
        GlobalSessionMemory[i].bBundledEffectsEnabled   = LVM_FALSE;
        GlobalSessionMemory[i].bVolumeInstantiated      = LVM_FALSE;
        GlobalSessionMemory[i].bEqualizerInstantiated   = LVM_FALSE;
        GlobalSessionMemory[i].bBassInstantiated        = LVM_FALSE;
        GlobalSessionMemory[i].bVirtualizerInstantiated = LVM_FALSE;
        GlobalSessionMemory[i].pBundledContext          = LVM_NULL;

        SessionIndex[i] = LVM_UNUSED_SESSION;
    }
    return;
}
//----------------------------------------------------------------------------
// LvmBundle_init()
//----------------------------------------------------------------------------
// Purpose: Initialize engine with default configuration, creates instance
// with all effects disabled.
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

int LvmBundle_init(EffectContext *pContext){
    int status;

    pContext->config.inputCfg.accessMode                    = EFFECT_BUFFER_ACCESS_READ;
    pContext->config.inputCfg.channels                      = AUDIO_CHANNEL_OUT_STEREO;
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

    CHECK_ARG(pContext != NULL);

    if (pContext->pBundledContext->hInstance != NULL){
        LvmEffect_free(pContext);
    }

    LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;          /* Function call status */
    LVM_ControlParams_t     params;                         /* Control Parameters */
    LVM_InstParams_t        InstParams;                     /* Instance parameters */
    LVM_EQNB_BandDef_t      BandDefs[MAX_NUM_BANDS];        /* Equaliser band definitions */
    LVM_HeadroomParams_t    HeadroomParams;                 /* Headroom parameters */
    LVM_HeadroomBandDef_t   HeadroomBandDef[LVM_HEADROOM_MAX_NBANDS];
    LVM_MemTab_t            MemTab;                         /* Memory allocation table */
    bool                    bMallocFailure = LVM_FALSE;

    /* Set the capabilities */
    InstParams.BufferMode       = LVM_UNMANAGED_BUFFERS;
    InstParams.MaxBlockSize     = MAX_CALL_SIZE;
    InstParams.EQNB_NumBands    = MAX_NUM_BANDS;
    InstParams.PSA_Included     = LVM_PSA_ON;

    /* Allocate memory, forcing alignment */
    LvmStatus = LVM_GetMemoryTable(LVM_NULL,
                                  &MemTab,
                                  &InstParams);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

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
    pContext->pBundledContext->hInstance = LVM_NULL;

    /* Init sets the instance handle */
    LvmStatus = LVM_GetInstanceHandle(&pContext->pBundledContext->hInstance,
                                      &MemTab,
                                      &InstParams);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    /* Set the initial process parameters */
    /* General parameters */
    params.OperatingMode          = LVM_MODE_ON;
    params.SampleRate             = LVM_FS_44100;
    params.SourceFormat           = LVM_STEREO;
    params.SpeakerType            = LVM_HEADPHONES;

    pContext->pBundledContext->SampleRate = LVM_FS_44100;

    /* Concert Sound parameters */
    params.VirtualizerOperatingMode   = LVM_MODE_OFF;
    params.VirtualizerType            = LVM_CONCERTSOUND;
    params.VirtualizerReverbLevel     = 100;
    params.CS_EffectLevel             = LVM_CS_EFFECT_NONE;

    /* N-Band Equaliser parameters */
    params.EQNB_OperatingMode     = LVM_EQNB_OFF;
    params.EQNB_NBands            = FIVEBAND_NUMBANDS;
    params.pEQNB_BandDefinition   = &BandDefs[0];

    for (int i=0; i<FIVEBAND_NUMBANDS; i++)
    {
        BandDefs[i].Frequency = EQNB_5BandPresetsFrequencies[i];
        BandDefs[i].QFactor   = EQNB_5BandPresetsQFactors[i];
        BandDefs[i].Gain      = EQNB_5BandSoftPresets[i];
    }

    /* Volume Control parameters */
    params.VC_EffectLevel         = 0;
    params.VC_Balance             = 0;

    /* Treble Enhancement parameters */
    params.TE_OperatingMode       = LVM_TE_OFF;
    params.TE_EffectLevel         = 0;

    /* PSA Control parameters */
    params.PSA_Enable             = LVM_PSA_OFF;
    params.PSA_PeakDecayRate      = (LVM_PSA_DecaySpeed_en)0;

    /* Bass Enhancement parameters */
    params.BE_OperatingMode       = LVM_BE_OFF;
    params.BE_EffectLevel         = 0;
    params.BE_CentreFreq          = LVM_BE_CENTRE_90Hz;
    params.BE_HPF                 = LVM_BE_HPF_ON;

    /* PSA Control parameters */
    params.PSA_Enable             = LVM_PSA_OFF;
    params.PSA_PeakDecayRate      = LVM_PSA_SPEED_MEDIUM;

    /* TE Control parameters */
    params.TE_OperatingMode       = LVM_TE_OFF;
    params.TE_EffectLevel         = 0;

    /* Activate the initial settings */
    LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance,
                                         &params);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    /* Set the headroom parameters */
    HeadroomBandDef[0].Limit_Low          = 20;
    HeadroomBandDef[0].Limit_High         = 4999;
    HeadroomBandDef[0].Headroom_Offset    = 0;
    HeadroomBandDef[1].Limit_Low          = 5000;
    HeadroomBandDef[1].Limit_High         = 24000;
    HeadroomBandDef[1].Headroom_Offset    = 0;
    HeadroomParams.pHeadroomDefinition    = &HeadroomBandDef[0];
    HeadroomParams.Headroom_OperatingMode = LVM_HEADROOM_ON;
    HeadroomParams.NHeadroomBands         = 2;

    LvmStatus = LVM_SetHeadroomParams(pContext->pBundledContext->hInstance,
                                      &HeadroomParams);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    return 0;
}   /* end LvmBundle_init */


//----------------------------------------------------------------------------
// LvmBundle_process()
//----------------------------------------------------------------------------
// Purpose:
// Apply LVM Bundle effects
//
// Inputs:
//  pIn:        pointer to stereo 16 bit input data
//  pOut:       pointer to stereo 16 bit output data
//  frameCount: Frames to process
//  pContext:   effect engine context
//  strength    strength to be applied
//
//  Outputs:
//  pOut:       pointer to updated stereo 16 bit output data
//
//----------------------------------------------------------------------------

int LvmBundle_process(LVM_INT16        *pIn,
                      LVM_INT16        *pOut,
                      int              frameCount,
                      EffectContext    *pContext){

    LVM_ControlParams_t     ActiveParams;                           /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */
    LVM_INT16               *pOutTmp;

    if (pContext->config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_WRITE){
        pOutTmp = pOut;
    }else if (pContext->config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE){
        if (pContext->pBundledContext->frameCount != frameCount) {
            if (pContext->pBundledContext->workBuffer != NULL) {
                free(pContext->pBundledContext->workBuffer);
            }
            pContext->pBundledContext->workBuffer =
                    (LVM_INT16 *)malloc(frameCount * sizeof(LVM_INT16) * 2);
            pContext->pBundledContext->frameCount = frameCount;
        }
        pOutTmp = pContext->pBundledContext->workBuffer;
    }else{
        return -EINVAL;
    }

    #ifdef LVM_PCM
    fwrite(pIn, frameCount*sizeof(LVM_INT16)*2, 1, pContext->pBundledContext->PcmInPtr);
    fflush(pContext->pBundledContext->PcmInPtr);
    #endif

    /* Process the samples */
    LvmStatus = LVM_Process(pContext->pBundledContext->hInstance, /* Instance handle */
                            pIn,                                  /* Input buffer */
                            pOutTmp,                              /* Output buffer */
                            (LVM_UINT16)frameCount,               /* Number of samples to read */
                            0);                                   /* Audo Time */

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    #ifdef LVM_PCM
    fwrite(pOutTmp, frameCount*sizeof(LVM_INT16)*2, 1, pContext->pBundledContext->PcmOutPtr);
    fflush(pContext->pBundledContext->PcmOutPtr);
    #endif

    if (pContext->config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE){
        for (int i=0; i<frameCount*2; i++){
            pOut[i] = clamp16((LVM_INT32)pOut[i] + (LVM_INT32)pOutTmp[i]);
        }
    }
    return 0;
}    /* end LvmBundle_process */

//----------------------------------------------------------------------------
// LvmEffect_enable()
//----------------------------------------------------------------------------
// Purpose: Enable the effect in the bundle
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

int LvmEffect_enable(EffectContext *pContext){
    LVM_ControlParams_t     ActiveParams;                           /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */

    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance,
                                         &ActiveParams);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    if(pContext->EffectType == LVM_BASS_BOOST) {
        ActiveParams.BE_OperatingMode       = LVM_BE_ON;
    }
    if(pContext->EffectType == LVM_VIRTUALIZER) {
        ActiveParams.VirtualizerOperatingMode   = LVM_MODE_ON;
    }
    if(pContext->EffectType == LVM_EQUALIZER) {
        ActiveParams.EQNB_OperatingMode     = LVM_EQNB_ON;
    }

    LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    return 0;
}

//----------------------------------------------------------------------------
// LvmEffect_disable()
//----------------------------------------------------------------------------
// Purpose: Disable the effect in the bundle
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

int LvmEffect_disable(EffectContext *pContext){

    LVM_ControlParams_t     ActiveParams;                           /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */
    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance,
                                         &ActiveParams);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    if(pContext->EffectType == LVM_BASS_BOOST) {
        ActiveParams.BE_OperatingMode       = LVM_BE_OFF;
    }
    if(pContext->EffectType == LVM_VIRTUALIZER) {
        ActiveParams.VirtualizerOperatingMode   = LVM_MODE_OFF;
    }
    if(pContext->EffectType == LVM_EQUALIZER) {
        ActiveParams.EQNB_OperatingMode     = LVM_EQNB_OFF;
    }

    LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    return 0;
}

//----------------------------------------------------------------------------
// LvmEffect_free()
//----------------------------------------------------------------------------
// Purpose: Free all memory associated with the Bundle.
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------

void LvmEffect_free(EffectContext *pContext){
    LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;         /* Function call status */
    LVM_ControlParams_t     params;                        /* Control Parameters */
    LVM_MemTab_t            MemTab;

    /* Free the algorithm memory */
    LvmStatus = LVM_GetMemoryTable(pContext->pBundledContext->hInstance,
                                   &MemTab,
                                   LVM_NULL);

    for (int i=0; i<LVM_NR_MEMORY_REGIONS; i++){
        if (MemTab.Region[i].Size != 0){
            if (MemTab.Region[i].pBaseAddress != NULL){
                free(MemTab.Region[i].pBaseAddress);
            }
        }
    }
}    /* end LvmEffect_free */

//----------------------------------------------------------------------------
// Effect_setConfig()
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

int Effect_setConfig(EffectContext *pContext, effect_config_t *pConfig){
    LVM_Fs_en   SampleRate;

    CHECK_ARG(pContext != NULL);
    CHECK_ARG(pConfig != NULL);

    CHECK_ARG(pConfig->inputCfg.samplingRate == pConfig->outputCfg.samplingRate);
    CHECK_ARG(pConfig->inputCfg.channels == pConfig->outputCfg.channels);
    CHECK_ARG(pConfig->inputCfg.format == pConfig->outputCfg.format);
    CHECK_ARG(pConfig->inputCfg.channels == AUDIO_CHANNEL_OUT_STEREO);
    CHECK_ARG(pConfig->outputCfg.accessMode == EFFECT_BUFFER_ACCESS_WRITE
              || pConfig->outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE);
    CHECK_ARG(pConfig->inputCfg.format == AUDIO_FORMAT_PCM_16_BIT);

    pContext->config = *pConfig;

    switch (pConfig->inputCfg.samplingRate) {
    case 8000:
        SampleRate = LVM_FS_8000;
        pContext->pBundledContext->SamplesPerSecond = 8000*2; // 2 secs Stereo
        break;
    case 16000:
        SampleRate = LVM_FS_16000;
        pContext->pBundledContext->SamplesPerSecond = 16000*2; // 2 secs Stereo
        break;
    case 22050:
        SampleRate = LVM_FS_22050;
        pContext->pBundledContext->SamplesPerSecond = 22050*2; // 2 secs Stereo
        break;
    case 32000:
        SampleRate = LVM_FS_32000;
        pContext->pBundledContext->SamplesPerSecond = 32000*2; // 2 secs Stereo
        break;
    case 44100:
        SampleRate = LVM_FS_44100;
        pContext->pBundledContext->SamplesPerSecond = 44100*2; // 2 secs Stereo
        break;
    case 48000:
        SampleRate = LVM_FS_48000;
        pContext->pBundledContext->SamplesPerSecond = 48000*2; // 2 secs Stereo
        break;
    default:
        return -EINVAL;
    }

    if(pContext->pBundledContext->SampleRate != SampleRate){

        LVM_ControlParams_t     ActiveParams;
        LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;

        /* Get the current settings */
        LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance,
                                         &ActiveParams);

        if(LvmStatus != LVM_SUCCESS) return -EINVAL;

        ActiveParams.SampleRate = SampleRate;

        LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);

        pContext->pBundledContext->SampleRate = SampleRate;
    }

    return 0;
}   /* end Effect_setConfig */

//----------------------------------------------------------------------------
// Effect_getConfig()
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

void Effect_getConfig(EffectContext *pContext, effect_config_t *pConfig)
{
    *pConfig = pContext->config;
}   /* end Effect_getConfig */

//----------------------------------------------------------------------------
// BassGetStrength()
//----------------------------------------------------------------------------
// Purpose:
// get the effect strength currently being used, what is actually returned is the strengh that was
// previously used in the set, this is because the app uses a strength in the range 0-1000 while
// the bassboost uses 1-15, so to avoid a quantisation the original set value is used. However the
// actual used value is checked to make sure it corresponds to the one being returned
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

uint32_t BassGetStrength(EffectContext *pContext){

    LVM_ControlParams_t     ActiveParams;                           /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */
    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance,
                                         &ActiveParams);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    /* Check that the strength returned matches the strength that was set earlier */
    if(ActiveParams.BE_EffectLevel !=
       (LVM_INT16)((15*pContext->pBundledContext->BassStrengthSaved)/1000)){
        return -EINVAL;
    }

    return pContext->pBundledContext->BassStrengthSaved;
}    /* end BassGetStrength */

//----------------------------------------------------------------------------
// BassSetStrength()
//----------------------------------------------------------------------------
// Purpose:
// Apply the strength to the BassBosst. Must first be converted from the range 0-1000 to 1-15
//
// Inputs:
//  pContext:   effect engine context
//  strength    strength to be applied
//
//----------------------------------------------------------------------------

void BassSetStrength(EffectContext *pContext, uint32_t strength){

    pContext->pBundledContext->BassStrengthSaved = (int)strength;

    LVM_ControlParams_t     ActiveParams;              /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance,
                                         &ActiveParams);

    /* Bass Enhancement parameters */
    ActiveParams.BE_EffectLevel    = (LVM_INT16)((15*strength)/1000);
    ActiveParams.BE_CentreFreq     = LVM_BE_CENTRE_90Hz;


    /* Activate the initial settings */
    LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);

}    /* end BassSetStrength */

//----------------------------------------------------------------------------
// VirtualizerGetStrength()
//----------------------------------------------------------------------------
// Purpose:
// get the effect strength currently being used, what is actually returned is the strengh that was
// previously used in the set, this is because the app uses a strength in the range 0-1000 while
// the Virtualizer uses 1-100, so to avoid a quantisation the original set value is used.However the
// actual used value is checked to make sure it corresponds to the one being returned
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

uint32_t VirtualizerGetStrength(EffectContext *pContext){

    LVM_ControlParams_t     ActiveParams;                           /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */

    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);

    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    return pContext->pBundledContext->VirtStrengthSaved;
}    /* end getStrength */

//----------------------------------------------------------------------------
// VirtualizerSetStrength()
//----------------------------------------------------------------------------
// Purpose:
// Apply the strength to the Virtualizer. Must first be converted from the range 0-1000 to 1-15
//
// Inputs:
//  pContext:   effect engine context
//  strength    strength to be applied
//
//----------------------------------------------------------------------------

void VirtualizerSetStrength(EffectContext *pContext, uint32_t strength){
    LVM_ControlParams_t     ActiveParams;              /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;     /* Function call status */

    pContext->pBundledContext->VirtStrengthSaved = (int)strength;

    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance,&ActiveParams);

    /* Virtualizer parameters */
    ActiveParams.CS_EffectLevel             = (int)((strength*32767)/1000);

    /* Activate the initial settings */
    LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
}    /* end setStrength */


//----------------------------------------------------------------------------
// EqualizerLimitBandLevels()
//----------------------------------------------------------------------------
// Purpose: limit all EQ band gains to a value less than 0 dB while
//          preserving the relative band levels.
//
// Inputs:
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------
void EqualizerLimitBandLevels(EffectContext *pContext) {
    LVM_ControlParams_t     ActiveParams;              /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);

    // Apply a volume correction to avoid clipping in the EQ based on 2 factors:
    // - the maximum EQ band gain: the volume correction is such that the total of volume + max
    // band gain is <= 0 dB
    // - the average gain in all bands weighted by their proximity to max gain band.
    int maxGain = 0;
    int avgGain = 0;
    int avgCount = 0;
    for (int i = 0; i < FIVEBAND_NUMBANDS; i++) {
        if (pContext->pBundledContext->bandGaindB[i] >= maxGain) {
            int tmpMaxGain = pContext->pBundledContext->bandGaindB[i];
            int tmpAvgGain = 0;
            int tmpAvgCount = 0;
            for (int j = 0; j < FIVEBAND_NUMBANDS; j++) {
                int gain = pContext->pBundledContext->bandGaindB[j];
                // skip current band and gains < 0 dB
                if (j == i || gain < 0)
                    continue;
                // no need to continue if one band not processed yet has a higher gain than current
                // max
                if (gain > tmpMaxGain) {
                    // force skipping "if (tmpAvgGain >= avgGain)" below as tmpAvgGain is not
                    // meaningful in this case
                    tmpAvgGain = -1;
                    break;
                }

                int weight = 1;
                if (j < (i + 2) && j > (i - 2))
                    weight = 4;
                tmpAvgGain += weight * gain;
                tmpAvgCount += weight;
            }
            if (tmpAvgGain >= avgGain) {
                maxGain = tmpMaxGain;
                avgGain = tmpAvgGain;
                avgCount = tmpAvgCount;
            }
        }
        ActiveParams.pEQNB_BandDefinition[i].Frequency = EQNB_5BandPresetsFrequencies[i];
        ActiveParams.pEQNB_BandDefinition[i].QFactor   = EQNB_5BandPresetsQFactors[i];
        ActiveParams.pEQNB_BandDefinition[i].Gain = pContext->pBundledContext->bandGaindB[i];
    }

    int gainCorrection = 0;
    if (maxGain + pContext->pBundledContext->volume > 0) {
        gainCorrection = maxGain + pContext->pBundledContext->volume;
    }
    if (avgCount) {
        gainCorrection += avgGain/avgCount;
    }

    ActiveParams.VC_EffectLevel  = pContext->pBundledContext->volume - gainCorrection;
    if (ActiveParams.VC_EffectLevel < -96) {
        ActiveParams.VC_EffectLevel = -96;
    }

    /* Activate the initial settings */
    LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);

    if(pContext->pBundledContext->firstVolume == LVM_TRUE){
        LvmStatus = LVM_SetVolumeNoSmoothing(pContext->pBundledContext->hInstance, &ActiveParams);
        pContext->pBundledContext->firstVolume = LVM_FALSE;
    }
}


//----------------------------------------------------------------------------
// EqualizerGetBandLevel()
//----------------------------------------------------------------------------
// Purpose: Retrieve the gain currently being used for the band passed in
//
// Inputs:
//  band:       band number
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------
int32_t EqualizerGetBandLevel(EffectContext *pContext, int32_t band){
    return pContext->pBundledContext->bandGaindB[band] * 100;
}

//----------------------------------------------------------------------------
// EqualizerSetBandLevel()
//----------------------------------------------------------------------------
// Purpose:
//  Sets gain value for the given band.
//
// Inputs:
//  band:       band number
//  Gain:       Gain to be applied in millibels
//  pContext:   effect engine context
//
// Outputs:
//
//---------------------------------------------------------------------------
void EqualizerSetBandLevel(EffectContext *pContext, int band, short Gain){
    int gainRounded;
    if(Gain > 0){
        gainRounded = (int)((Gain+50)/100);
    }else{
        gainRounded = (int)((Gain-50)/100);
    }
    pContext->pBundledContext->bandGaindB[band] = gainRounded;
    pContext->pBundledContext->CurPreset = PRESET_CUSTOM;

    EqualizerLimitBandLevels(pContext);
}

//----------------------------------------------------------------------------
// EqualizerGetCentreFrequency()
//----------------------------------------------------------------------------
// Purpose: Retrieve the frequency being used for the band passed in
//
// Inputs:
//  band:       band number
//  pContext:   effect engine context
//
// Outputs:
//
//----------------------------------------------------------------------------
int32_t EqualizerGetCentreFrequency(EffectContext *pContext, int32_t band){
    int32_t Frequency =0;

    LVM_ControlParams_t     ActiveParams;                           /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */
    LVM_EQNB_BandDef_t      *BandDef;
    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance,
                                         &ActiveParams);

    BandDef   = ActiveParams.pEQNB_BandDefinition;
    Frequency = (int32_t)BandDef[band].Frequency*1000;     // Convert to millibels

    return Frequency;
}

//----------------------------------------------------------------------------
// EqualizerGetBandFreqRange(
//----------------------------------------------------------------------------
// Purpose:
//
// Gets lower and upper boundaries of a band.
// For the high shelf, the low bound is the band frequency and the high
// bound is Nyquist.
// For the peaking filters, they are the gain[dB]/2 points.
//
// Inputs:
//  band:       band number
//  pContext:   effect engine context
//
// Outputs:
//  pLow:       lower band range
//  pLow:       upper band range
//----------------------------------------------------------------------------
int32_t EqualizerGetBandFreqRange(EffectContext *pContext, int32_t band, uint32_t *pLow,
                                  uint32_t *pHi){
    *pLow = bandFreqRange[band][0];
    *pHi  = bandFreqRange[band][1];
    return 0;
}

//----------------------------------------------------------------------------
// EqualizerGetBand(
//----------------------------------------------------------------------------
// Purpose:
//
// Returns the band with the maximum influence on a given frequency.
// Result is unaffected by whether EQ is enabled or not, or by whether
// changes have been committed or not.
//
// Inputs:
//  targetFreq   The target frequency, in millihertz.
//  pContext:    effect engine context
//
// Outputs:
//  pLow:       lower band range
//  pLow:       upper band range
//----------------------------------------------------------------------------
int32_t EqualizerGetBand(EffectContext *pContext, uint32_t targetFreq){
    int band = 0;

    if(targetFreq < bandFreqRange[0][0]){
        return -EINVAL;
    }else if(targetFreq == bandFreqRange[0][0]){
        return 0;
    }
    for(int i=0; i<FIVEBAND_NUMBANDS;i++){
        if((targetFreq > bandFreqRange[i][0])&&(targetFreq <= bandFreqRange[i][1])){
            band = i;
        }
    }
    return band;
}

//----------------------------------------------------------------------------
// EqualizerGetPreset(
//----------------------------------------------------------------------------
// Purpose:
//
// Gets the currently set preset ID.
// Will return PRESET_CUSTOM in case the EQ parameters have been modified
// manually since a preset was set.
//
// Inputs:
//  pContext:    effect engine context
//
//----------------------------------------------------------------------------
int32_t EqualizerGetPreset(EffectContext *pContext){
    return pContext->pBundledContext->CurPreset;
}

//----------------------------------------------------------------------------
// EqualizerSetPreset(
//----------------------------------------------------------------------------
// Purpose:
//
// Sets the current preset by ID.
// All the band parameters will be overridden.
//
// Inputs:
//  pContext:    effect engine context
//  preset       The preset ID.
//
//----------------------------------------------------------------------------
void EqualizerSetPreset(EffectContext *pContext, int preset){

    pContext->pBundledContext->CurPreset = preset;

    //ActiveParams.pEQNB_BandDefinition = &BandDefs[0];
    for (int i=0; i<FIVEBAND_NUMBANDS; i++)
    {
        pContext->pBundledContext->bandGaindB[i] =
                EQNB_5BandSoftPresets[i + preset * FIVEBAND_NUMBANDS];
    }

    EqualizerLimitBandLevels(pContext);

    return;
}

int32_t EqualizerGetNumPresets(){
    return sizeof(gEqualizerPresets) / sizeof(PresetConfig);
}

//----------------------------------------------------------------------------
// EqualizerGetPresetName(
//----------------------------------------------------------------------------
// Purpose:
// Gets a human-readable name for a preset ID. Will return "Custom" if
// PRESET_CUSTOM is passed.
//
// Inputs:
// preset       The preset ID. Must be less than number of presets.
//
//-------------------------------------------------------------------------
const char * EqualizerGetPresetName(int32_t preset){
    if (preset == PRESET_CUSTOM) {
        return "Custom";
    } else {
        return gEqualizerPresets[preset].name;
    }
    return 0;
}

//----------------------------------------------------------------------------
// VolumeSetVolumeLevel()
//----------------------------------------------------------------------------
// Purpose:
//
// Inputs:
//  pContext:   effect engine context
//  level       level to be applied
//
//----------------------------------------------------------------------------

int VolumeSetVolumeLevel(EffectContext *pContext, int16_t level){

    if (level > 0 || level < -9600) {
        return -EINVAL;
    }

    if (pContext->pBundledContext->bMuteEnabled == LVM_TRUE) {
        pContext->pBundledContext->levelSaved = level / 100;
    } else {
        pContext->pBundledContext->volume = level / 100;
    }

    EqualizerLimitBandLevels(pContext);

    return 0;
}    /* end VolumeSetVolumeLevel */

//----------------------------------------------------------------------------
// VolumeGetVolumeLevel()
//----------------------------------------------------------------------------
// Purpose:
//
// Inputs:
//  pContext:   effect engine context
//
//----------------------------------------------------------------------------

int VolumeGetVolumeLevel(EffectContext *pContext, int16_t *level){

    if (pContext->pBundledContext->bMuteEnabled == LVM_TRUE) {
        *level = pContext->pBundledContext->levelSaved * 100;
    } else {
        *level = pContext->pBundledContext->volume * 100;
    }
    return 0;
}    /* end VolumeGetVolumeLevel */

//----------------------------------------------------------------------------
// VolumeSetMute()
//----------------------------------------------------------------------------
// Purpose:
//
// Inputs:
//  pContext:   effect engine context
//  mute:       enable/disable flag
//
//----------------------------------------------------------------------------

int32_t VolumeSetMute(EffectContext *pContext, uint32_t mute){

    pContext->pBundledContext->bMuteEnabled = mute;

    /* Set appropriate volume level */
    if(pContext->pBundledContext->bMuteEnabled == LVM_TRUE){
        pContext->pBundledContext->levelSaved = pContext->pBundledContext->volume;
        pContext->pBundledContext->volume = -96;
    }else{
        pContext->pBundledContext->volume = pContext->pBundledContext->levelSaved;
    }

    EqualizerLimitBandLevels(pContext);

    return 0;
}    /* end setMute */

//----------------------------------------------------------------------------
// VolumeGetMute()
//----------------------------------------------------------------------------
// Purpose:
//
// Inputs:
//  pContext:   effect engine context
//
// Ourputs:
//  mute:       enable/disable flag
//----------------------------------------------------------------------------

int32_t VolumeGetMute(EffectContext *pContext, uint32_t *mute){
    if((pContext->pBundledContext->bMuteEnabled == LVM_FALSE)||
       (pContext->pBundledContext->bMuteEnabled == LVM_TRUE)){
        *mute = pContext->pBundledContext->bMuteEnabled;
        return 0;
    }else{
        return -EINVAL;
    }
}    /* end getMute */

int16_t VolumeConvertStereoPosition(int16_t position){
    int16_t convertedPosition = 0;

    convertedPosition = (int16_t)(((float)position/1000)*96);
    return convertedPosition;

}

//----------------------------------------------------------------------------
// VolumeSetStereoPosition()
//----------------------------------------------------------------------------
// Purpose:
//
// Inputs:
//  pContext:       effect engine context
//  position:       stereo position
//
// Outputs:
//----------------------------------------------------------------------------

int VolumeSetStereoPosition(EffectContext *pContext, int16_t position){

    LVM_ControlParams_t     ActiveParams;              /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;     /* Function call status */
    LVM_INT16               Balance = 0;



    pContext->pBundledContext->positionSaved = position;
    Balance = VolumeConvertStereoPosition(pContext->pBundledContext->positionSaved);

    if(pContext->pBundledContext->bStereoPositionEnabled == LVM_TRUE){

        pContext->pBundledContext->positionSaved = position;
        /* Get the current settings */
        LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
        if(LvmStatus != LVM_SUCCESS) return -EINVAL;

        /* Volume parameters */
        ActiveParams.VC_Balance  = Balance;

        /* Activate the initial settings */
        LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
        if(LvmStatus != LVM_SUCCESS) return -EINVAL;

        /* Get the current settings */
        LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
        if(LvmStatus != LVM_SUCCESS) return -EINVAL;
    }

    return 0;
}    /* end VolumeSetStereoPosition */


//----------------------------------------------------------------------------
// VolumeGetStereoPosition()
//----------------------------------------------------------------------------
// Purpose:
//
// Inputs:
//  pContext:       effect engine context
//
// Outputs:
//  position:       stereo position
//----------------------------------------------------------------------------

int32_t VolumeGetStereoPosition(EffectContext *pContext, int16_t *position){

    LVM_ControlParams_t     ActiveParams;                           /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */
    LVM_INT16               balance;

    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    balance = VolumeConvertStereoPosition(pContext->pBundledContext->positionSaved);

    if(pContext->pBundledContext->bStereoPositionEnabled == LVM_TRUE){
        if(balance != ActiveParams.VC_Balance){
            return -EINVAL;
        }
    }
    *position = (LVM_INT16)pContext->pBundledContext->positionSaved;     // Convert dB to millibels
    return 0;
}    /* end VolumeGetStereoPosition */

//----------------------------------------------------------------------------
// VolumeEnableStereoPosition()
//----------------------------------------------------------------------------
// Purpose:
//
// Inputs:
//  pContext:   effect engine context
//  mute:       enable/disable flag
//
//----------------------------------------------------------------------------

int32_t VolumeEnableStereoPosition(EffectContext *pContext, uint32_t enabled){

    pContext->pBundledContext->bStereoPositionEnabled = enabled;

    LVM_ControlParams_t     ActiveParams;              /* Current control Parameters */
    LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;     /* Function call status */

    /* Get the current settings */
    LvmStatus = LVM_GetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    /* Set appropriate stereo position */
    if(pContext->pBundledContext->bStereoPositionEnabled == LVM_FALSE){
        ActiveParams.VC_Balance = 0;
    }else{
        ActiveParams.VC_Balance  =
                            VolumeConvertStereoPosition(pContext->pBundledContext->positionSaved);
    }

    /* Activate the initial settings */
    LvmStatus = LVM_SetControlParameters(pContext->pBundledContext->hInstance, &ActiveParams);
    if(LvmStatus != LVM_SUCCESS) return -EINVAL;

    return 0;
}    /* end VolumeEnableStereoPosition */

//----------------------------------------------------------------------------
// BassBoost_getParameter()
//----------------------------------------------------------------------------
// Purpose:
// Get a BassBoost parameter
//
// Inputs:
//  pBassBoost       - handle to instance data
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

int BassBoost_getParameter(EffectContext     *pContext,
                           void              *pParam,
                           size_t            *pValueSize,
                           void              *pValue){
    int status = 0;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;
    int32_t param2;
    char *name;

    switch (param){
        case BASSBOOST_PARAM_STRENGTH_SUPPORTED:
            if (*pValueSize != sizeof(uint32_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case BASSBOOST_PARAM_STRENGTH:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;

        default:
            return -EINVAL;
    }

    switch (param){
        case BASSBOOST_PARAM_STRENGTH_SUPPORTED:
            *(uint32_t *)pValue = 1;

            break;

        case BASSBOOST_PARAM_STRENGTH:
            *(int16_t *)pValue = BassGetStrength(pContext);

            break;

        default:
            status = -EINVAL;
            break;
    }

    return status;
} /* end BassBoost_getParameter */

//----------------------------------------------------------------------------
// BassBoost_setParameter()
//----------------------------------------------------------------------------
// Purpose:
// Set a BassBoost parameter
//
// Inputs:
//  pBassBoost       - handle to instance data
//  pParam           - pointer to parameter
//  pValue           - pointer to value
//
// Outputs:
//
//----------------------------------------------------------------------------

int BassBoost_setParameter (EffectContext *pContext, void *pParam, void *pValue){
    int status = 0;
    int16_t strength;
    int32_t *pParamTemp = (int32_t *)pParam;

    switch (*pParamTemp){
        case BASSBOOST_PARAM_STRENGTH:
            strength = *(int16_t *)pValue;
            BassSetStrength(pContext, (int32_t)strength);
           break;
        default:
            break;
    }

    return status;
} /* end BassBoost_setParameter */

//----------------------------------------------------------------------------
// Virtualizer_getParameter()
//----------------------------------------------------------------------------
// Purpose:
// Get a Virtualizer parameter
//
// Inputs:
//  pVirtualizer     - handle to instance data
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

int Virtualizer_getParameter(EffectContext        *pContext,
                             void                 *pParam,
                             size_t               *pValueSize,
                             void                 *pValue){
    int status = 0;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;
    int32_t param2;
    char *name;

    switch (param){
        case VIRTUALIZER_PARAM_STRENGTH_SUPPORTED:
            if (*pValueSize != sizeof(uint32_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(uint32_t);
            break;
        case VIRTUALIZER_PARAM_STRENGTH:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;

        default:
            return -EINVAL;
    }

    switch (param){
        case VIRTUALIZER_PARAM_STRENGTH_SUPPORTED:
            *(uint32_t *)pValue = 1;

            break;

        case VIRTUALIZER_PARAM_STRENGTH:
            *(int16_t *)pValue = VirtualizerGetStrength(pContext);

            break;

        default:
            status = -EINVAL;
            break;
    }

    return status;
} /* end Virtualizer_getParameter */

//----------------------------------------------------------------------------
// Virtualizer_setParameter()
//----------------------------------------------------------------------------
// Purpose:
// Set a Virtualizer parameter
//
// Inputs:
//  pVirtualizer     - handle to instance data
//  pParam           - pointer to parameter
//  pValue           - pointer to value
//
// Outputs:
//
//----------------------------------------------------------------------------

int Virtualizer_setParameter (EffectContext *pContext, void *pParam, void *pValue){
    int status = 0;
    int16_t strength;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;

    switch (param){
        case VIRTUALIZER_PARAM_STRENGTH:
            strength = *(int16_t *)pValue;
            VirtualizerSetStrength(pContext, (int32_t)strength);
           break;
        default:
            break;
    }

    return status;
} /* end Virtualizer_setParameter */

//----------------------------------------------------------------------------
// Equalizer_getParameter()
//----------------------------------------------------------------------------
// Purpose:
// Get a Equalizer parameter
//
// Inputs:
//  pEqualizer       - handle to instance data
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
int Equalizer_getParameter(EffectContext     *pContext,
                           void              *pParam,
                           size_t            *pValueSize,
                           void              *pValue){
    int status = 0;
    int bMute = 0;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;
    int32_t param2;
    char *name;

    switch (param) {
    case EQ_PARAM_NUM_BANDS:
    case EQ_PARAM_CUR_PRESET:
    case EQ_PARAM_GET_NUM_OF_PRESETS:
    case EQ_PARAM_BAND_LEVEL:
    case EQ_PARAM_GET_BAND:
        if (*pValueSize < sizeof(int16_t)) {
            return -EINVAL;
        }
        *pValueSize = sizeof(int16_t);
        break;

    case EQ_PARAM_LEVEL_RANGE:
        if (*pValueSize < 2 * sizeof(int16_t)) {
            return -EINVAL;
        }
        *pValueSize = 2 * sizeof(int16_t);
        break;
    case EQ_PARAM_BAND_FREQ_RANGE:
        if (*pValueSize < 2 * sizeof(int32_t)) {
            return -EINVAL;
        }
        *pValueSize = 2 * sizeof(int32_t);
        break;

    case EQ_PARAM_CENTER_FREQ:
        if (*pValueSize < sizeof(int32_t)) {
            return -EINVAL;
        }
        *pValueSize = sizeof(int32_t);
        break;

    case EQ_PARAM_GET_PRESET_NAME:
        break;

    case EQ_PARAM_PROPERTIES:
        if (*pValueSize < (2 + FIVEBAND_NUMBANDS) * sizeof(uint16_t)) {
            return -EINVAL;
        }
        *pValueSize = (2 + FIVEBAND_NUMBANDS) * sizeof(uint16_t);
        break;

    default:
        return -EINVAL;
    }

    switch (param) {
    case EQ_PARAM_NUM_BANDS:
        *(uint16_t *)pValue = (uint16_t)FIVEBAND_NUMBANDS;
        break;

    case EQ_PARAM_LEVEL_RANGE:
        *(int16_t *)pValue = -1500;
        *((int16_t *)pValue + 1) = 1500;
        break;

    case EQ_PARAM_BAND_LEVEL:
        param2 = *pParamTemp;
        if (param2 >= FIVEBAND_NUMBANDS) {
            status = -EINVAL;
            break;
        }
        *(int16_t *)pValue = (int16_t)EqualizerGetBandLevel(pContext, param2);
        break;

    case EQ_PARAM_CENTER_FREQ:
        param2 = *pParamTemp;
        if (param2 >= FIVEBAND_NUMBANDS) {
            status = -EINVAL;
            break;
        }
        *(int32_t *)pValue = EqualizerGetCentreFrequency(pContext, param2);
        break;

    case EQ_PARAM_BAND_FREQ_RANGE:
        param2 = *pParamTemp;
        if (param2 >= FIVEBAND_NUMBANDS) {
            status = -EINVAL;
            break;
        }
        EqualizerGetBandFreqRange(pContext, param2, (uint32_t *)pValue, ((uint32_t *)pValue + 1));
        break;

    case EQ_PARAM_GET_BAND:
        param2 = *pParamTemp;
        *(uint16_t *)pValue = (uint16_t)EqualizerGetBand(pContext, param2);
        break;

    case EQ_PARAM_CUR_PRESET:
        *(uint16_t *)pValue = (uint16_t)EqualizerGetPreset(pContext);
        break;

    case EQ_PARAM_GET_NUM_OF_PRESETS:
        *(uint16_t *)pValue = (uint16_t)EqualizerGetNumPresets();
        break;

    case EQ_PARAM_GET_PRESET_NAME:
        param2 = *pParamTemp;
        if (param2 >= EqualizerGetNumPresets()) {
        //if (param2 >= 20) {     // AGO FIX
            status = -EINVAL;
            break;
        }
        name = (char *)pValue;
        strncpy(name, EqualizerGetPresetName(param2), *pValueSize - 1);
        name[*pValueSize - 1] = 0;
        *pValueSize = strlen(name) + 1;
        break;

    case EQ_PARAM_PROPERTIES: {
        int16_t *p = (int16_t *)pValue;
        p[0] = (int16_t)EqualizerGetPreset(pContext);
        p[1] = (int16_t)FIVEBAND_NUMBANDS;
        for (int i = 0; i < FIVEBAND_NUMBANDS; i++) {
            p[2 + i] = (int16_t)EqualizerGetBandLevel(pContext, i);
        }
    } break;

    default:
        status = -EINVAL;
        break;
    }

    //GV("\tEqualizer_getParameter end\n");
    return status;
} /* end Equalizer_getParameter */

//----------------------------------------------------------------------------
// Equalizer_setParameter()
//----------------------------------------------------------------------------
// Purpose:
// Set a Equalizer parameter
//
// Inputs:
//  pEqualizer    - handle to instance data
//  pParam        - pointer to parameter
//  pValue        - pointer to value
//
// Outputs:
//
//----------------------------------------------------------------------------
int Equalizer_setParameter (EffectContext *pContext, void *pParam, void *pValue){
    int status = 0;
    int32_t preset;
    int32_t band;
    int32_t level;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;

    switch (param) {
    case EQ_PARAM_CUR_PRESET:
        preset = (int32_t)(*(uint16_t *)pValue);

        if ((preset >= EqualizerGetNumPresets())||(preset < 0)) {
            status = -EINVAL;
            break;
        }
        EqualizerSetPreset(pContext, preset);
        break;
    case EQ_PARAM_BAND_LEVEL:
        band =  *pParamTemp;
        level = (int32_t)(*(int16_t *)pValue);
        if (band >= FIVEBAND_NUMBANDS) {
            status = -EINVAL;
            break;
        }
        EqualizerSetBandLevel(pContext, band, level);
        break;
    case EQ_PARAM_PROPERTIES: {
        int16_t *p = (int16_t *)pValue;
        if ((int)p[0] >= EqualizerGetNumPresets()) {
            status = -EINVAL;
            break;
        }
        if (p[0] >= 0) {
            EqualizerSetPreset(pContext, (int)p[0]);
        } else {
            if ((int)p[1] != FIVEBAND_NUMBANDS) {
                status = -EINVAL;
                break;
            }
            for (int i = 0; i < FIVEBAND_NUMBANDS; i++) {
                EqualizerSetBandLevel(pContext, i, (int)p[2 + i]);
            }
        }
    } break;
    default:
        status = -EINVAL;
        break;
    }

    return status;
} /* end Equalizer_setParameter */

//----------------------------------------------------------------------------
// Volume_getParameter()
//----------------------------------------------------------------------------
// Purpose:
// Get a Volume parameter
//
// Inputs:
//  pVolume          - handle to instance data
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

int Volume_getParameter(EffectContext     *pContext,
                        void              *pParam,
                        size_t            *pValueSize,
                        void              *pValue){
    int status = 0;
    int bMute = 0;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;;
    char *name;

    switch (param){
        case VOLUME_PARAM_LEVEL:
        case VOLUME_PARAM_MAXLEVEL:
        case VOLUME_PARAM_STEREOPOSITION:
            if (*pValueSize != sizeof(int16_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int16_t);
            break;

        case VOLUME_PARAM_MUTE:
        case VOLUME_PARAM_ENABLESTEREOPOSITION:
            if (*pValueSize < sizeof(int32_t)){
                return -EINVAL;
            }
            *pValueSize = sizeof(int32_t);
            break;

        default:
            return -EINVAL;
    }

    switch (param){
        case VOLUME_PARAM_LEVEL:
            status = VolumeGetVolumeLevel(pContext, (int16_t *)(pValue));
            break;

        case VOLUME_PARAM_MAXLEVEL:
            *(int16_t *)pValue = 0;
            break;

        case VOLUME_PARAM_STEREOPOSITION:
            VolumeGetStereoPosition(pContext, (int16_t *)pValue);
            break;

        case VOLUME_PARAM_MUTE:
            status = VolumeGetMute(pContext, (uint32_t *)pValue);
            break;

        case VOLUME_PARAM_ENABLESTEREOPOSITION:
            *(int32_t *)pValue = pContext->pBundledContext->bStereoPositionEnabled;
            break;

        default:
            status = -EINVAL;
            break;
    }

    return status;
} /* end Volume_getParameter */


//----------------------------------------------------------------------------
// Volume_setParameter()
//----------------------------------------------------------------------------
// Purpose:
// Set a Volume parameter
//
// Inputs:
//  pVolume       - handle to instance data
//  pParam        - pointer to parameter
//  pValue        - pointer to value
//
// Outputs:
//
//----------------------------------------------------------------------------

int Volume_setParameter (EffectContext *pContext, void *pParam, void *pValue){
    int      status = 0;
    int16_t  level;
    int16_t  position;
    uint32_t mute;
    uint32_t positionEnabled;
    int32_t *pParamTemp = (int32_t *)pParam;
    int32_t param = *pParamTemp++;

    switch (param){
        case VOLUME_PARAM_LEVEL:
            level = *(int16_t *)pValue;
            status = VolumeSetVolumeLevel(pContext, (int16_t)level);
            break;

        case VOLUME_PARAM_MUTE:
            mute = *(uint32_t *)pValue;
            status = VolumeSetMute(pContext, mute);
            break;

        case VOLUME_PARAM_ENABLESTEREOPOSITION:
            positionEnabled = *(uint32_t *)pValue;
            status = VolumeEnableStereoPosition(pContext, positionEnabled);
            status = VolumeSetStereoPosition(pContext, pContext->pBundledContext->positionSaved);
            break;

        case VOLUME_PARAM_STEREOPOSITION:
            position = *(int16_t *)pValue;
            status = VolumeSetStereoPosition(pContext, (int16_t)position);
            break;

        default:
            break;
    }

    return status;
} /* end Volume_setParameter */

/****************************************************************************************
 * Name : LVC_ToDB_s32Tos16()
 *  Input       : Signed 32-bit integer
 *  Output      : Signed 16-bit integer
 *                  MSB (16) = sign bit
 *                  (15->05) = integer part
 *                  (04->01) = decimal part
 *  Returns     : Db value with respect to full scale
 *  Description :
 *  Remarks     :
 ****************************************************************************************/

LVM_INT16 LVC_ToDB_s32Tos16(LVM_INT32 Lin_fix)
{
    LVM_INT16   db_fix;
    LVM_INT16   Shift;
    LVM_INT16   SmallRemainder;
    LVM_UINT32  Remainder = (LVM_UINT32)Lin_fix;

    /* Count leading bits, 1 cycle in assembly*/
    for (Shift = 0; Shift<32; Shift++)
    {
        if ((Remainder & 0x80000000U)!=0)
        {
            break;
        }
        Remainder = Remainder << 1;
    }

    /*
     * Based on the approximation equation (for Q11.4 format):
     *
     * dB = -96 * Shift + 16 * (8 * Remainder - 2 * Remainder^2)
     */
    db_fix    = (LVM_INT16)(-96 * Shift);               /* Six dB steps in Q11.4 format*/
    SmallRemainder = (LVM_INT16)((Remainder & 0x7fffffff) >> 24);
    db_fix = (LVM_INT16)(db_fix + SmallRemainder );
    SmallRemainder = (LVM_INT16)(SmallRemainder * SmallRemainder);
    db_fix = (LVM_INT16)(db_fix - (LVM_INT16)((LVM_UINT16)SmallRemainder >> 9));

    /* Correct for small offset */
    db_fix = (LVM_INT16)(db_fix - 5);

    return db_fix;
}

//----------------------------------------------------------------------------
// Effect_setEnabled()
//----------------------------------------------------------------------------
// Purpose:
// Enable or disable effect
//
// Inputs:
//  pContext      - pointer to effect context
//  enabled       - true if enabling the effect, false otherwise
//
// Outputs:
//
//----------------------------------------------------------------------------

int Effect_setEnabled(EffectContext *pContext, bool enabled)
{
    if (enabled) {
        // Bass boost or Virtualizer can be temporarily disabled if playing over device speaker due
        // to their nature.
        bool tempDisabled = false;
        switch (pContext->EffectType) {
            case LVM_BASS_BOOST:
                if (pContext->pBundledContext->bBassEnabled == LVM_TRUE) {
                     return -EINVAL;
                }
                if(pContext->pBundledContext->SamplesToExitCountBb <= 0){
                    pContext->pBundledContext->NumberEffectsEnabled++;
                }
                pContext->pBundledContext->SamplesToExitCountBb =
                     (LVM_INT32)(pContext->pBundledContext->SamplesPerSecond*0.1);
                pContext->pBundledContext->bBassEnabled = LVM_TRUE;
                tempDisabled = pContext->pBundledContext->bBassTempDisabled;
                break;
            case LVM_EQUALIZER:
                if (pContext->pBundledContext->bEqualizerEnabled == LVM_TRUE) {
                    return -EINVAL;
                }
                if(pContext->pBundledContext->SamplesToExitCountEq <= 0){
                    pContext->pBundledContext->NumberEffectsEnabled++;
                }
                pContext->pBundledContext->SamplesToExitCountEq =
                     (LVM_INT32)(pContext->pBundledContext->SamplesPerSecond*0.1);
                pContext->pBundledContext->bEqualizerEnabled = LVM_TRUE;
                break;
            case LVM_VIRTUALIZER:
                if (pContext->pBundledContext->bVirtualizerEnabled == LVM_TRUE) {
                    return -EINVAL;
                }
                if(pContext->pBundledContext->SamplesToExitCountVirt <= 0){
                    pContext->pBundledContext->NumberEffectsEnabled++;
                }
                pContext->pBundledContext->SamplesToExitCountVirt =
                     (LVM_INT32)(pContext->pBundledContext->SamplesPerSecond*0.1);
                pContext->pBundledContext->bVirtualizerEnabled = LVM_TRUE;
                tempDisabled = pContext->pBundledContext->bVirtualizerTempDisabled;
                break;
            case LVM_VOLUME:
                if (pContext->pBundledContext->bVolumeEnabled == LVM_TRUE) {
                    return -EINVAL;
                }
                pContext->pBundledContext->NumberEffectsEnabled++;
                pContext->pBundledContext->bVolumeEnabled = LVM_TRUE;
                break;
            default:
                return -EINVAL;
        }
        if (!tempDisabled) {
            LvmEffect_enable(pContext);
        }
    } else {
        switch (pContext->EffectType) {
            case LVM_BASS_BOOST:
                if (pContext->pBundledContext->bBassEnabled == LVM_FALSE) {
                    return -EINVAL;
                }
                pContext->pBundledContext->bBassEnabled = LVM_FALSE;
                break;
            case LVM_EQUALIZER:
                if (pContext->pBundledContext->bEqualizerEnabled == LVM_FALSE) {
                    return -EINVAL;
                }
                pContext->pBundledContext->bEqualizerEnabled = LVM_FALSE;
                break;
            case LVM_VIRTUALIZER:
                if (pContext->pBundledContext->bVirtualizerEnabled == LVM_FALSE) {
                    return -EINVAL;
                }
                pContext->pBundledContext->bVirtualizerEnabled = LVM_FALSE;
                break;
            case LVM_VOLUME:
                if (pContext->pBundledContext->bVolumeEnabled == LVM_FALSE) {
                    return -EINVAL;
                }
                pContext->pBundledContext->bVolumeEnabled = LVM_FALSE;
                break;
            default:
                return -EINVAL;
        }
        LvmEffect_disable(pContext);
    }

    return 0;
}

//----------------------------------------------------------------------------
// LVC_Convert_VolToDb()
//----------------------------------------------------------------------------
// Purpose:
// Convery volume in Q24 to dB
//
// Inputs:
//  vol:   Q.24 volume dB
//
//-----------------------------------------------------------------------

int16_t LVC_Convert_VolToDb(uint32_t vol){
    int16_t  dB;

    dB = LVC_ToDB_s32Tos16(vol <<7);
    dB = (dB +8)>>4;
    dB = (dB <-96) ? -96 : dB ;

    return dB;
}

} // namespace
} // namespace

extern "C" {
/* Effect Control Interface Implementation: Process */
int Effect_process(effect_handle_t     self,
                              audio_buffer_t         *inBuffer,
                              audio_buffer_t         *outBuffer){
    EffectContext * pContext = (EffectContext *) self;
    LVM_ReturnStatus_en     LvmStatus = LVM_SUCCESS;                /* Function call status */
    int    status = 0;
    int    lvmStatus = 0;
    LVM_INT16   *in  = (LVM_INT16 *)inBuffer->raw;
    LVM_INT16   *out = (LVM_INT16 *)outBuffer->raw;

    if (pContext == NULL){
        return -EINVAL;
    }

    if (inBuffer == NULL  || inBuffer->raw == NULL  ||
            outBuffer == NULL || outBuffer->raw == NULL ||
            inBuffer->frameCount != outBuffer->frameCount){
        return -EINVAL;
    }
    if ((pContext->pBundledContext->bBassEnabled == LVM_FALSE)&&
        (pContext->EffectType == LVM_BASS_BOOST)){
        if(pContext->pBundledContext->SamplesToExitCountBb > 0){
            pContext->pBundledContext->SamplesToExitCountBb -= outBuffer->frameCount * 2; // STEREO
        }
        if(pContext->pBundledContext->SamplesToExitCountBb <= 0) {
            status = -ENODATA;
            pContext->pBundledContext->NumberEffectsEnabled--;
        }
    }
    if ((pContext->pBundledContext->bVolumeEnabled == LVM_FALSE)&&
        (pContext->EffectType == LVM_VOLUME)){
        status = -ENODATA;
        pContext->pBundledContext->NumberEffectsEnabled--;
    }
    if ((pContext->pBundledContext->bEqualizerEnabled == LVM_FALSE)&&
        (pContext->EffectType == LVM_EQUALIZER)){
        if(pContext->pBundledContext->SamplesToExitCountEq > 0){
            pContext->pBundledContext->SamplesToExitCountEq -= outBuffer->frameCount * 2; // STEREO
        }
        if(pContext->pBundledContext->SamplesToExitCountEq <= 0) {
            status = -ENODATA;
            pContext->pBundledContext->NumberEffectsEnabled--;
        }
    }
    if ((pContext->pBundledContext->bVirtualizerEnabled == LVM_FALSE)&&
        (pContext->EffectType == LVM_VIRTUALIZER)){
        if(pContext->pBundledContext->SamplesToExitCountVirt > 0){
            pContext->pBundledContext->SamplesToExitCountVirt -= outBuffer->frameCount * 2;// STEREO
        }
        if(pContext->pBundledContext->SamplesToExitCountVirt <= 0) {
            status = -ENODATA;
            pContext->pBundledContext->NumberEffectsEnabled--;
        }
    }

    if(status != -ENODATA){
        pContext->pBundledContext->NumberEffectsCalled++;
    }

    if(pContext->pBundledContext->NumberEffectsCalled ==
       pContext->pBundledContext->NumberEffectsEnabled){

        pContext->pBundledContext->NumberEffectsCalled = 0;
        /* Process all the available frames, block processing is
           handled internalLY by the LVM bundle */
        lvmStatus = android::LvmBundle_process(    (LVM_INT16 *)inBuffer->raw,
                                                (LVM_INT16 *)outBuffer->raw,
                                                outBuffer->frameCount,
                                                pContext);
        if(lvmStatus != LVM_SUCCESS){
            return lvmStatus;
        }
    } else {
        if (pContext->config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE) {
            for (size_t i=0; i < outBuffer->frameCount*2; i++){
                outBuffer->s16[i] =
                        clamp16((LVM_INT32)outBuffer->s16[i] + (LVM_INT32)inBuffer->s16[i]);
            }
        } else if (outBuffer->raw != inBuffer->raw) {
            memcpy(outBuffer->raw, inBuffer->raw, outBuffer->frameCount*sizeof(LVM_INT16)*2);
        }
    }

    return status;
}   /* end Effect_process */

/* Effect Control Interface Implementation: Command */
int Effect_command(effect_handle_t  self,
                              uint32_t            cmdCode,
                              uint32_t            cmdSize,
                              void                *pCmdData,
                              uint32_t            *replySize,
                              void                *pReplyData){
    EffectContext * pContext = (EffectContext *) self;
    int retsize;

    if (pContext == NULL){
        return -EINVAL;
    }

    // Incase we disable an effect, next time process is
    // called the number of effect called could be greater

    switch (cmdCode){
        case EFFECT_CMD_INIT:
            if (pReplyData == NULL || *replySize != sizeof(int)){
                return -EINVAL;
            }
            *(int *) pReplyData = 0;
            if(pContext->EffectType == LVM_BASS_BOOST){
                android::BassSetStrength(pContext, 0);
            }
            if(pContext->EffectType == LVM_VIRTUALIZER){
                android::VirtualizerSetStrength(pContext, 0);
            }
            if(pContext->EffectType == LVM_EQUALIZER){
                android::EqualizerSetPreset(pContext, 0);
            }
            if(pContext->EffectType == LVM_VOLUME){
                *(int *) pReplyData = android::VolumeSetVolumeLevel(pContext, 0);
            }
            break;

        case EFFECT_CMD_SET_CONFIG:
            if (pCmdData    == NULL||
                cmdSize     != sizeof(effect_config_t)||
                pReplyData  == NULL||
                *replySize  != sizeof(int)){
                return -EINVAL;
            }
            *(int *) pReplyData = android::Effect_setConfig(pContext, (effect_config_t *) pCmdData);
            break;

        case EFFECT_CMD_GET_CONFIG:
            if (pReplyData == NULL ||
                *replySize != sizeof(effect_config_t)) {
                return -EINVAL;
            }

            android::Effect_getConfig(pContext, (effect_config_t *)pReplyData);
            break;

        case EFFECT_CMD_RESET:
            android::Effect_setConfig(pContext, &pContext->config);
            break;

        case EFFECT_CMD_GET_PARAM:{

            if(pContext->EffectType == LVM_BASS_BOOST){
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

                p->status = android::BassBoost_getParameter(pContext,
                                                            p->data,
                                                            (size_t  *)&p->vsize,
                                                            p->data + voffset);

                *replySize = sizeof(effect_param_t) + voffset + p->vsize;

            }

            if(pContext->EffectType == LVM_VIRTUALIZER){
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

                p->status = android::Virtualizer_getParameter(pContext,
                                                             (void *)p->data,
                                                             (size_t  *)&p->vsize,
                                                              p->data + voffset);

                *replySize = sizeof(effect_param_t) + voffset + p->vsize;

            }
            if(pContext->EffectType == LVM_EQUALIZER){
                if (pCmdData == NULL ||
                    cmdSize < (int)(sizeof(effect_param_t) + sizeof(int32_t)) ||
                    pReplyData == NULL ||
                    *replySize < (int) (sizeof(effect_param_t) + sizeof(int32_t))) {
                    return -EINVAL;
                }
                effect_param_t *p = (effect_param_t *)pCmdData;

                memcpy(pReplyData, pCmdData, sizeof(effect_param_t) + p->psize);

                p = (effect_param_t *)pReplyData;

                int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);

                p->status = android::Equalizer_getParameter(pContext,
                                                            p->data,
                                                            &p->vsize,
                                                            p->data + voffset);

                *replySize = sizeof(effect_param_t) + voffset + p->vsize;

            }
            if(pContext->EffectType == LVM_VOLUME){
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

                p->status = android::Volume_getParameter(pContext,
                                                         (void *)p->data,
                                                         (size_t  *)&p->vsize,
                                                         p->data + voffset);

                *replySize = sizeof(effect_param_t) + voffset + p->vsize;

            }
        } break;
        case EFFECT_CMD_SET_PARAM:{
            if(pContext->EffectType == LVM_BASS_BOOST){
                if (pCmdData   == NULL||
                    cmdSize    != (int)(sizeof(effect_param_t) + sizeof(int32_t) +sizeof(int16_t))||
                    pReplyData == NULL||
                    *replySize != sizeof(int32_t)){
                    return -EINVAL;
                }
                effect_param_t *p = (effect_param_t *) pCmdData;

                if (p->psize != sizeof(int32_t)){
                    return -EINVAL;
                }

                *(int *)pReplyData = android::BassBoost_setParameter(pContext,
                                                                    (void *)p->data,
                                                                    p->data + p->psize);
            }
            if(pContext->EffectType == LVM_VIRTUALIZER){
                if (pCmdData   == NULL||
                    cmdSize    != (int)(sizeof(effect_param_t) + sizeof(int32_t) +sizeof(int16_t))||
                    pReplyData == NULL||
                    *replySize != sizeof(int32_t)){
                    return -EINVAL;
                }
                effect_param_t *p = (effect_param_t *) pCmdData;

                if (p->psize != sizeof(int32_t)){
                    return -EINVAL;
                }

                *(int *)pReplyData = android::Virtualizer_setParameter(pContext,
                                                                      (void *)p->data,
                                                                       p->data + p->psize);
            }
            if(pContext->EffectType == LVM_EQUALIZER){
                if (pCmdData == NULL || cmdSize < (int)(sizeof(effect_param_t) + sizeof(int32_t)) ||
                    pReplyData == NULL || *replySize != sizeof(int32_t)) {
                    return -EINVAL;
                }
                effect_param_t *p = (effect_param_t *) pCmdData;

                *(int *)pReplyData = android::Equalizer_setParameter(pContext,
                                                                    (void *)p->data,
                                                                     p->data + p->psize);
            }
            if(pContext->EffectType == LVM_VOLUME){
                if (    pCmdData   == NULL||
                        cmdSize    < (int)(sizeof(effect_param_t) + sizeof(int32_t))||
                        pReplyData == NULL||
                        *replySize != sizeof(int32_t)){
                    return -EINVAL;
                }
                effect_param_t *p = (effect_param_t *) pCmdData;

                *(int *)pReplyData = android::Volume_setParameter(pContext,
                                                                 (void *)p->data,
                                                                 p->data + p->psize);
            }
        } break;

        case EFFECT_CMD_ENABLE:
            if (pReplyData == NULL || *replySize != sizeof(int)){
                return -EINVAL;
            }

            *(int *)pReplyData = android::Effect_setEnabled(pContext, LVM_TRUE);
            break;

        case EFFECT_CMD_DISABLE:
            if (pReplyData == NULL || *replySize != sizeof(int)){
                return -EINVAL;
            }
            *(int *)pReplyData = android::Effect_setEnabled(pContext, LVM_FALSE);
            break;

        case EFFECT_CMD_SET_DEVICE:
        {
            uint32_t device = *(uint32_t *)pCmdData;

            if (pContext->EffectType == LVM_BASS_BOOST) {
                if((device == AUDIO_DEVICE_OUT_SPEAKER) ||
                        (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) ||
                        (device == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER)){

                    // If a device doesnt support bassboost the effect must be temporarily disabled
                    // the effect must still report its original state as this can only be changed
                    // by the ENABLE/DISABLE command

                    if (pContext->pBundledContext->bBassEnabled == LVM_TRUE) {
                        android::LvmEffect_disable(pContext);
                    }
                    pContext->pBundledContext->bBassTempDisabled = LVM_TRUE;
                } else {
                    // If a device supports bassboost and the effect has been temporarily disabled
                    // previously then re-enable it

                    if (pContext->pBundledContext->bBassEnabled == LVM_TRUE) {
                        android::LvmEffect_enable(pContext);
                    }
                    pContext->pBundledContext->bBassTempDisabled = LVM_FALSE;
                }
            }
            if (pContext->EffectType == LVM_VIRTUALIZER) {
                if((device == AUDIO_DEVICE_OUT_SPEAKER)||
                        (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT)||
                        (device == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER)){
                    //If a device doesnt support virtualizer the effect must be temporarily disabled
                    // the effect must still report its original state as this can only be changed
                    // by the ENABLE/DISABLE command

                    if (pContext->pBundledContext->bVirtualizerEnabled == LVM_TRUE) {
                        android::LvmEffect_disable(pContext);
                    }
                    pContext->pBundledContext->bVirtualizerTempDisabled = LVM_TRUE;
                } else {
                    // If a device supports virtualizer and the effect has been temporarily disabled
                    // previously then re-enable it

                    if(pContext->pBundledContext->bVirtualizerEnabled == LVM_TRUE){
                        android::LvmEffect_enable(pContext);
                    }
                    pContext->pBundledContext->bVirtualizerTempDisabled = LVM_FALSE;
                }
            }
            break;
        }
        case EFFECT_CMD_SET_VOLUME:
        {
            uint32_t leftVolume, rightVolume;
            int16_t  leftdB, rightdB;
            int16_t  maxdB, pandB;
            int32_t  vol_ret[2] = {1<<24,1<<24}; // Apply no volume
            int      status = 0;
            LVM_ControlParams_t     ActiveParams;           /* Current control Parameters */
            LVM_ReturnStatus_en     LvmStatus=LVM_SUCCESS;  /* Function call status */

            // if pReplyData is NULL, VOL_CTRL is delegated to another effect
            if(pReplyData == LVM_NULL){
                break;
            }

            if (pCmdData == NULL ||
                cmdSize != 2 * sizeof(uint32_t)) {
                return -EINVAL;
            }

            leftVolume  = ((*(uint32_t *)pCmdData));
            rightVolume = ((*((uint32_t *)pCmdData + 1)));

            if(leftVolume == 0x1000000){
                leftVolume -= 1;
            }
            if(rightVolume == 0x1000000){
                rightVolume -= 1;
            }

            // Convert volume to dB
            leftdB  = android::LVC_Convert_VolToDb(leftVolume);
            rightdB = android::LVC_Convert_VolToDb(rightVolume);

            pandB = rightdB - leftdB;

            // Calculate max volume in dB
            maxdB = leftdB;
            if(rightdB > maxdB){
                maxdB = rightdB;
            }

            memcpy(pReplyData, vol_ret, sizeof(int32_t)*2);
            android::VolumeSetVolumeLevel(pContext, (int16_t)(maxdB*100));

            /* Get the current settings */
            LvmStatus =LVM_GetControlParameters(pContext->pBundledContext->hInstance,&ActiveParams);
            if(LvmStatus != LVM_SUCCESS) return -EINVAL;

            /* Volume parameters */
            ActiveParams.VC_Balance  = pandB;

            /* Activate the initial settings */
            LvmStatus =LVM_SetControlParameters(pContext->pBundledContext->hInstance,&ActiveParams);
            if(LvmStatus != LVM_SUCCESS) return -EINVAL;
            break;
         }
        case EFFECT_CMD_SET_AUDIO_MODE:
            break;
        default:
            return -EINVAL;
    }

    return 0;
}    /* end Effect_command */

/* Effect Control Interface Implementation: get_descriptor */
int Effect_getDescriptor(effect_handle_t   self,
                                    effect_descriptor_t *pDescriptor)
{
    EffectContext * pContext = (EffectContext *) self;
    const effect_descriptor_t *desc;

    if (pContext == NULL || pDescriptor == NULL) {
        return -EINVAL;
    }

    switch(pContext->EffectType) {
        case LVM_BASS_BOOST:
            desc = &android::gBassBoostDescriptor;
            break;
        case LVM_VIRTUALIZER:
            desc = &android::gVirtualizerDescriptor;
            break;
        case LVM_EQUALIZER:
            desc = &android::gEqualizerDescriptor;
            break;
        case LVM_VOLUME:
            desc = &android::gVolumeDescriptor;
            break;
        default:
            return -EINVAL;
    }

    *pDescriptor = *desc;

    return 0;
}   /* end Effect_getDescriptor */

// effect_handle_t interface implementation for effect
const struct effect_interface_s gLvmEffectInterface = {
    Effect_process,
    Effect_command,
    Effect_getDescriptor,
    NULL,
};    /* end gLvmEffectInterface */

// This is the only symbol that needs to be exported
__attribute__ ((visibility ("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    tag : AUDIO_EFFECT_LIBRARY_TAG,
    version : EFFECT_LIBRARY_API_VERSION,
    name : "Effect Bundle Library",
    implementor : "NXP Software Ltd.",
    create_effect : android::EffectCreate,
    release_effect : android::EffectRelease,
    get_descriptor : android::EffectGetDescriptor,
};

}

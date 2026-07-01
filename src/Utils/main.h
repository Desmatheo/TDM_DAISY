#pragma once
#include "Utils.h"

#include "daisy_tdm_slave.h"

#include "Effect.h"
#include "../EffetEarth/Earth.h"


#if MIDI_USB_DE_LA_MORT
extern MidiUsbHandler midi;
#endif

#if CPU_METER
CpuLoadMeter loadMeter;
#endif

extern DaisyTdmSlave hw;


extern EarthEffect* earth_effects[6];
// DelayEffect* delay_effects[6] = {nullptr};

enum class EffectType {
    Mute,
    Bypass,
    Earth,
    Delay,
    Drive
};

class StringUtil{
public : 
    EffectType type;
    int index;
    Effect* active_effect;

    StringUtil(EffectType type, int index){
        this->type = type;
        this->index = index;
        this->active_effect = nullptr;
    }

    EffectType GetType() {
        return type;
    }
};

extern StringUtil strings[];



#include "audio_processing.h"
#include "midi_processing.h"

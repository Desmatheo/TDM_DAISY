#pragma once
#include "Utils.h"

#include "daisy_tdm_slave.h"

#include "Effect.h"
#include "../EffetEarth/Earth.h"


#if MIDI_USB_DE_LA_MORT
MidiUsbHandler midi;
#endif

#if CPU_METER
CpuLoadMeter loadMeter;
#endif

DaisyTdmSlave hw;


EarthEffect* earth_effects[6] = {nullptr};
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

StringUtil strings[] = {
    StringUtil(EffectType::Earth, 0),
    StringUtil(EffectType::Earth, 1),
    StringUtil(EffectType::Bypass, 2),
    StringUtil(EffectType::Bypass, 3),
    StringUtil(EffectType::Bypass, 4),
    StringUtil(EffectType::Bypass, 5)
};



#include "audio_processing.h"



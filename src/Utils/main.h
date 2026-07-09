#pragma once
#include "Utils.h"

#include "daisy_tdm_slave.h"

#include "Effect.h"
#include "../EffetEarth/Earth.h"
#include "../EffetDelay/Delay.h"


#if MIDI_USB_DE_LA_MORT
extern MidiUsbHandler midi;
#endif

#if CPU_METER
extern CpuLoadMeter loadMeter;
#endif

extern DaisyTdmSlave hw;


extern EarthEffect* earth_effects[6];
extern DelayEffect* delay_effects[6];

enum class EffectType {
    Mute,
    Bypass,
    Earth,
    Delay,
    Drive,
    Testation
};

class StringUtil{
public : 
    EffectType type;
    int index;
    Effect* active_effect;
    Effect* active_effect_bonus;
    Effect* active_effect_bonus_bonus;

    StringUtil(EffectType type, int index){
        this->type = type;
        this->index = index;
        this->active_effect = nullptr;
        this->active_effect_bonus = nullptr;
        this->active_effect_bonus_bonus = nullptr;
    }

    EffectType GetType() {
        return type;
    }
};

extern StringUtil strings[];



#include "audio_processing.h"
#include "midi_processing.h"

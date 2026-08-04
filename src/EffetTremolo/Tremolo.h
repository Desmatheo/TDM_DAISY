#pragma once

#include "../Utils/Utils.h"

#if USE_DAISY
#include "daisy_seed.h"
#include "daisysp.h"
#endif

#include "../Utils/Effect.h"

#if USE_DAISY
class TremoloEffect : public Effect {
public:
    float samplerate;
    float dryMix;
    float wetMix;
    float volume;

    daisysp::Oscillator lfo;
    daisysp::OnePole lfoFilter;
    float depthVal = 0.5f;
    float anti_denormal = 1e-9f;

    TremoloEffect(float sampleRate); 

    void update(const float** in, float** out, int idx) override;
    
    void setMix(float mix);
    void setDepth(float depth);
    void setRate(float rate);
    void setWaveform(int waveform);
    void setVolume(float vol);

    void setParameter(int param_id, float value) override;

    void setEnabled(bool e) { active = e; }
    bool isEnabled() const  { return active; }

private:
    bool active = false; 
};
#endif

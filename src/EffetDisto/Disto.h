#pragma once

#include "../Utils/Utils.h"

#if USE_DAISY
#include "daisy_seed.h"
#include "daisysp.h"
#endif

#include <q/fx/biquad.hpp>
namespace q = cycfi::q;
using namespace daisy;
using namespace daisysp;

#include "../Utils/Effect.h"
#include "toneDaisySP/tone.h"

#if USE_DAISY
class DistoEffect : public Effect {

    public:

    float samplerate;

    float dryMix;
    float wetMix;
    float volume = 1;

    float gain;
    float min_gain = 1.0f;
    float max_gain = 20.0f;

    float toneFreq;
    bool oversamp;
    float intensity;


    int effect_mode = 0;

    Tone tone;

#if USE_DAISY
    DistoEffect(float sampleRate); 
#endif


#if USE_DAISY
    void update(const float** in, float** out, int idx) override;
#endif

    void setMix(float mix);                 // Ctrl 2 (0.0 -> 1.0)
    void setVolume(float vol);              // Ctrl 1 (0.0 -> 1.0)
    
    void setDistoMode(int mode);            // 3-Way Switch 2 (0, 1, 2)

    void setTone(float tone);               // Ctrl 3 (0.0 -> 1.0)
    void setIntensity(float intensity);     // Ctrl 4 (0.0 -> 1.0)
    void setGain(float gain);               // Ctrl 5 (0.0 -> 10.0)
    void setOversamp(bool oversamp);        // Switch 1 (0, 1)
    
    void InitializeFilters();

    float ProcessTiltToneControl(float input);

#if USE_DAISY
    void setParameter(int param_id, float value) override;
#endif

    // --- METHODES ET VARIABLES PARTAGEES ---
    void setEnabled(bool e) { active = e; }
    bool isEnabled() const  { return active; }

private:
    bool active = false; // effet actif ou non
    
};
#endif
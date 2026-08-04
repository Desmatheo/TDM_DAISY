#pragma once

#include "../Utils/Effect.h"
#include "daisy_seed.h"
#include "daisysp.h"
#include "arm_math.h"
#include "../Utils/Utils.h"

// 4s of delay at 48kHz
#define MAX_DELAY_SAMPLES (48000 * 4) 

class DelayEffect : public Effect {
public:

    struct DelayChannel {
        float* buffer = nullptr;
        uint32_t buf_len = 0;
        uint32_t write_idx = 0;
        
        float tone_z1 = 0.0f;
        float tone_a0 = 1.0f;
        float tone_b1 = 0.0f;

        float muteFade = 1.0f;
        uint32_t standbyTimer = 0;
        float lastTarget = 0.0f;
        float currentDelay = 0.0f;
        float delayTarget = 0.0f;
        float feedback = 0.0f;
        bool active = false;

        void Init(float* mem, float sampleRate, uint32_t max_delay_samples);
        float Process(float in);
    };

    DelayEffect(float sampleRate);

    void update(const float** in, float** out, int idx) override;

    void setMix(float mix);
    void setVolume(float vol);
    void setDelayTime(float time); // manual time mapping
    void setFeedback(float fdbk);
    
    // Nouveaux paramètres pour Tap Tempo
    void setType(float type); // 0 = manual, 1 = tempo
    void setDivision(float div); // 1 = ronde, 2 = blanche... 
    
    void setParameter(int param_id, float value) override;

private:
    DelayChannel delayL;

    float dryMix, wetMix, volume;
    float vdelayTime, vdelayFDBK;
    float vdelayDiv = 0.0f; // 0..1 pour division 1..8
    
    // Pour gérer le tempo dynamique
    bool isTempoMode = false;
    float sample_rate_;

    void updateTargetDelay();
};
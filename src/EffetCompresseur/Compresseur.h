#pragma once
#include "../Utils/Effect.h"
#include "daisy_seed.h"
#include "daisysp.h"
#include "arm_math.h"
#include "../Utils/Utils.h"
#ifndef PI_F
#define PI_F 3.14159265358979323846f
#endif

class CompresseurEffect : public Effect {
public:
    CompresseurEffect(float sampleRate);
    void update(const float** in, float** out, int idx) override;
    
    void setThreshold(float dB);
    void setRatio(float ratio);
    void setAttack(float ms);
    void setRelease(float ms);
    void setMakeupGain(float dB);
    virtual void setParameter(int param_id, float value) override;

private:
    void calculateCoefs();
    float dbToLinear(float db);
    float linearToDb(float linear);

    float sample_rate_;
    float thresholdLinear_;
    float thresholdDb_;
    float ratio_;
    float attackMs_;
    float releaseMs_;
    float makeupGainLinear_;
    float attackCoef_;
    float releaseCoef_;
    float envelope_;
};

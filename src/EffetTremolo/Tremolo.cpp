#include "Tremolo.h"
#include <span>

#if USE_DAISY
using namespace daisy;
using namespace daisysp;

TremoloEffect::TremoloEffect(float sampleRate){
    samplerate = sampleRate;

    lfo.Init(sampleRate);
    lfo.SetAmp(1.0f);
    
    lfoFilter.Init();
    lfoFilter.SetFrequency(50.0f / sampleRate); // 50Hz smoothing frequency
    
    setMix(1.0f);
    setDepth(0.5f);
    setRate(0.25f); 
    setWaveform(0);
    setVolume(1.0f);
}

void TremoloEffect::update(const float** in, float** out, int idx) {
    float inputL;
    float inputR;

    anti_denormal = -anti_denormal;
    inputL = inputR = in[0][idx] + anti_denormal; // Anti-denormal

    float rawLfo = lfo.Process();
    float smoothedLfo = lfoFilter.Process(rawLfo);
    
    // Calculate modulation matching daisysp::Tremolo:
    // mod = 1.0f - (depth * 0.5f) - (smoothedLfo * depth * 0.5f)
    float mod = 1.0f - (depthVal * 0.5f) - (smoothedLfo * depthVal * 0.5f);
    float processed = inputL * mod;

    out[0][idx] = (inputL * dryMix + processed * wetMix) * volume;
    out[1][idx] = out[0][idx];
}

void TremoloEffect::setMix(float mix) {
    wetMix = clampf(mix, 0.0f, 1.0f);
    dryMix = 1.0f - mix;
}

void TremoloEffect::setDepth(float val) {
    depthVal = clampf(val, 0.0f, 1.0f);
}

void TremoloEffect::setRate(float val) {
    lfo.SetFreq(clampf(val * 20.0f, 0.0f, 20.0f));
}

void TremoloEffect::setWaveform(int mode) {
    lfo.SetWaveform(mode); 
}

void TremoloEffect::setVolume(float vol){
    volume = clampf(vol, 0.0f, 1.0f);
}

void TremoloEffect::setParameter(int param_id, float value) {
    switch (param_id){
        case 0: setMix(value); break;
        case 1: setDepth(value); break;
        case 2: setRate(value); break;
        case 3: 
            if (value < 0.2f) setWaveform(0); 
            else if (value < 0.5f) setWaveform(1); 
            else if (value < 0.8f) setWaveform(2); 
            else setWaveform(3); 
            break; 
        case 5: setVolume(value); break;
        default: break;
    }
}
#endif

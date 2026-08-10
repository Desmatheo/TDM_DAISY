#include "Tremolo.h"
#include <span>

#if USE_DAISY
using namespace daisy;
using namespace daisysp;

TremoloEffect::TremoloEffect(float sampleRate){
    samplerate = sampleRate;

    phase = 0.0f;
    phaseOffset = 0.0f;
    phaseIncrement = 0.0f;
    waveform = 0;
    
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

    phase += phaseIncrement;
    if (phase >= 1.0f) phase -= 1.0f;

    float currentPhase = phase + phaseOffset;
    if (currentPhase >= 1.0f) currentPhase -= 1.0f;

    float rawLfo = 0.0f;
    switch (waveform) {
        case 0: // Sine
            rawLfo = sinf(currentPhase * 2.0f * (float)M_PI);
            break;
        case 1: // Tri
            if (currentPhase < 0.5f) rawLfo = currentPhase * 4.0f - 1.0f;
            else rawLfo = 3.0f - (currentPhase * 4.0f);
            break;
        case 2: // Square
            rawLfo = (currentPhase < 0.5f) ? 1.0f : -1.0f;
            break;
        case 3: // Saw
            rawLfo = currentPhase * 2.0f - 1.0f;
            break;
    }

    float smoothedLfo = lfoFilter.Process(rawLfo);
    
    // Calculate modulation matching daisysp::Tremolo:
    // mod = 1.0f - (depth * 0.5f) - (smoothedLfo * depth * 0.5f)
    float mod = 1.0f - (depthVal * 0.5f) - (smoothedLfo * depthVal * 0.5f);
    float processed = inputL * mod;

    float final_out = (inputL * dryMix + processed * wetMix) * volume;
    
    // Clipper (Sécurité saturation)
    if (final_out > 0.999f) final_out = 0.999f;
    if (final_out < -0.999f) final_out = -0.999f;

    out[0][idx] = final_out;
    out[1][idx] = final_out;
}

void TremoloEffect::setMix(float mix) {
    wetMix = clampf(mix, 0.0f, 1.0f);
    dryMix = 1.0f - mix;
}

void TremoloEffect::setDepth(float val) {
    depthVal = clampf(val, 0.0f, 1.0f);
}

void TremoloEffect::setRate(float val) {
    float freq = clampf(val * 20.0f, 0.0f, 20.0f);
    phaseIncrement = freq / samplerate;
}

void TremoloEffect::setWaveform(int mode) {
    waveform = mode;
}

void TremoloEffect::setPhaseOffset(float offset) {
    phaseOffset = offset;
    while (phaseOffset >= 1.0f) phaseOffset -= 1.0f;
    while (phaseOffset < 0.0f) phaseOffset += 1.0f;
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
        case 4: setPhaseOffset(value); break;
        case 5: setVolume(value); break;
        default: break;
    }
}
#endif

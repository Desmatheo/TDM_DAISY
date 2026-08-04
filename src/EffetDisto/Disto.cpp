// Disto Reverbscape

#include "Disto.h"

#include <span>

#if USE_DAISY
#include "daisy_seed.h"
#include "daisysp.h"
#include <q/support/literals.hpp>
namespace q = cycfi::q;
using namespace q::literals;

using namespace daisy;
using namespace daisysp;

#endif


constexpr float preFilterCutoffBase = 140.0f;
constexpr float preFilterCutoffMax = 300.0f;
constexpr float postFilterCutoff = 8000.0f;



DistoEffect::DistoEffect(float sampleRate){

    tone.Init(sampleRate);

    // Pivot between 500 Hz and 2 kHz as the tone amount changes
    tone.SetFreq(500.0f + 1500.0f * toneFreq);

    samplerate = sampleRate;

    setMix(1.0f);
    setDistoMode(1);
    setTone(0.5f);
    setVolume(1.0f);
    setGain(1.0f);
    setIntensity(0.5f);
    setOversamp(false);


    InitializeFilters();
}

void DistoEffect::InitializeFilters() {
    preFilter.config(preFilterCutoffBase, samplerate);
    postFilter.config(postFilterCutoff, samplerate);
}

float hardClipping(float input, float threshold) { return std::clamp(input, -threshold, threshold); }

float diodeClipping(float input, float threshold, float intensity) {
    // Boost signal beforehand to make it hit the threshold earlier
    float preGain = 1.0f + intensity * 4.0f; // up to 5x gain
    input *= preGain;
    float out;
    if (input > threshold)
        out = threshold - fastexp(-(input - threshold));
    else if (input < -threshold)
        out = -threshold + fastexp(input + threshold);
    else
        out = input;
    return out / preGain; // compensate volume
}

float testDistortion(float input, float gainVal){
    float g = input * gainVal;
    
#define Qlib 1
 
#if Qlib
#if 1
    float z = ((g < std::signbit(g))? (-1.0f) : 1.0f) * (1.0f - fastexp(-std::abs(g)));
#else
    float z = ((g < std::signbit(g))? (-1.0f) : 1.0f) * (1.0f - fasterexp(-std::abs(g)));
#endif
#else
    float z = ((g < std::signbit(g))? (-1.0f) : 1.0f) * (1.0f - std::exp(-std::abs(g)));
#endif
    return z;
}

float testOverDrive(float input, float intensity){
    float threshold = 1.0f - intensity;
    if (threshold < 0.0001f) threshold = 0.0001f;
    
    float abs_input = std::abs(input);
    float sign = (input > 0.0f) ? 1.0f : ((input < 0.0f) ? -1.0f : 0.0f);
 
    if(abs_input < threshold){
        return 2.0f * input;
    }
    else if (abs_input > 2.0f * threshold){
        return sign * 1.0f;
    }
    else {
        float tmp = 2.0f - abs_input * 3.0f;
        return sign * ((3.0f - tmp * tmp) / 3.0f);      
    }
}

inline float fast_tanh(float x) {
    if (x <= -3.0f) return -1.0f;
    if (x >= 3.0f) return 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float softClipping(float input, float gain) { return fast_tanh(input * gain); }


float fuzzEffect(float input, float intensity) {
    // Symmetrical clipping with extreme compression
    float fuzzed = softClipping(input, intensity);

    // Introduce a slight asymmetry for a classic fuzz character and adds harmonic content
    fuzzed += 0.05f * std::sin(input * 20.0f);

    // Dynamic response: Adjust the intensity based on the input signal's amplitude
    const float dynamicIntensity = intensity * (1.0f + 0.5f * std::abs(input));
    fuzzed = softClipping(fuzzed, dynamicIntensity);

    return fuzzed;
}

inline float fast_atan(float x) {
    // Fast overdrive approximation replacing atan
    return x / (1.0f + std::abs(x));
}

float tubeSaturation(float input, float gain) { return fast_atan(input * gain); }

float multiStage(float sample, float drive, float intensity) {
    // First stage
    const float stage1 = softClipping(sample, drive * intensity * 2.0f);

    // Second stage
    const float stage2 = softClipping(stage1, drive * intensity);

    // Power amp, mimic second tube clipping, possibly negative feedback
    const float result = tubeSaturation(stage2, drive * intensity);

    return result;
}

float dynamicPreFilterCutoff(float inputEnergy) {
    return preFilterCutoffBase + (preFilterCutoffMax - preFilterCutoffBase) * fast_tanh(inputEnergy);
}


void processDistortion(float &sample,           // Sample to process
                       const float &gain,       // Gain
                       const int &clippingType, // Clipping type
                       const float &intensity)  // Intensity
{
    sample *= gain;

    switch (clippingType) {
    case 0: // Hard Clipping
        sample = hardClipping(sample, 1.0f - intensity);
        break;
    case 1: // Soft Clipping
        sample = softClipping(sample, gain);
        break;
    case 2: // Fuzz
        sample = fuzzEffect(sample, intensity * 10.0f);
        break;
    case 3: // Tube Saturation
        sample = tubeSaturation(sample, intensity * 10.0f);
        break;
    case 4: // Multi-stage
        sample = multiStage(sample, gain, intensity);
        break;
    case 5: // Diode Clipping
        sample = diodeClipping(sample, 1.0f - (intensity * 0.5f), intensity);
        break;
    case 6: // Test Distortion
        sample = testDistortion(sample, gain);
        break;
    case 7: // Test Overdrive
        sample = testOverDrive(sample, intensity);
        break;
    }
}

void normalizeVolume(float &sample, int clippingType, float intensity, float gain) {
    switch (clippingType) {
    case 0: // Hard Clipping
        sample *= 1.8f;
        break;
    case 1: // Soft Clipping
        sample *= 0.8f;
        break;
    case 2: // Fuzz
        // Auto-gain depending on intensity to avoid huge volume jumps
        sample *= 1.0f / (1.0f + intensity * 2.0f);
        break;
    case 3: // Tube Saturation
        sample *= 0.9f / (1.0f + intensity);
        break;
    case 4: // Multi-stage
        sample *= 0.5f / (1.0f + intensity * gain * 0.1f);
        break;
    case 5: // Diode Clipping
        sample *= 1.8f;
        break;
    case 6: // Test Distortion
        sample *= 1.0f;
        break;
    case 7: // Test Overdrive
        sample *= 1.0f;
        break;
    }
}

float DistoEffect::ProcessTiltToneControl(float input) {

    // Process input with one-pole low-pass
    const float lp = tone.Process(input);

    // Compute the high-passed portion
    const float hp = input - lp;

    // Crossfade: toneAmount=0 => all LP (more bass), toneAmount=1 => all HP (more treble)
    return lp * (1.f - toneFreq) + hp * toneFreq;
}

void DistoEffect::update(const float** in, float** out, int idx) {
    float inputL;
    float inputR;

    // Bruit de Nyquist (alterné) : +1e-9f, -1e-9f, +1e-9f...
    // Contrairement au courant continu (DC), ce bruit traverse le filtre passe-haut (preFilter)
    // et empêche tous les filtres suivants (postFilter) de crasher sur des nombres sous-normaux.
    anti_denormal = -anti_denormal;

    inputL = inputR = in[0][idx] + anti_denormal;

    float distorted = inputL;

    // Apply high-pass filter to remove excessive low frequencies
    // (preFilter cutoff is now fixed in InitializeFilters to prevent audio-rate modulation crash)
    distorted = preFilter(distorted);


    const float computed_gain = min_gain + (this->gain * (max_gain - min_gain));

    // Reduce signal amplitude before clipping
    distorted = distorted * 0.5f;


    processDistortion(distorted, computed_gain, effect_mode, intensity);

    // Post-filter: Low-pass to smooth out harsh high frequencies
    distorted = postFilter(distorted);

    // Normalize the volume between the types of distortion
    normalizeVolume(distorted, effect_mode, intensity, gain);

    // Parameter smoothing for Tone to prevent zipper noise
    toneFreq = 0.999f * toneFreq + 0.001f * toneFreqTarget;
    tone.SetFreq(500.0f + toneFreq * 1500.0f);

    // Apply tilt-tone filter
    const float effect_output = ProcessTiltToneControl(distorted);

    // Mixage final dry/wet pour cet effet de corde
    out[0][idx] = (inputL * dryMix + effect_output * wetMix) * volume;
    out[1][idx] = out[0][idx];
}

// --- Implémentation des Setters Spécifiques ---

void DistoEffect::setMix(float mix) {
    wetMix = clampf(mix, 0.0f, 1.0f);
    dryMix = 1.0f - mix;
}

void DistoEffect::setGain(float val) {
    gain = clampf(val, 0.0f, 10.0f);
}

void DistoEffect::setTone(float freq) {
    toneFreqTarget = clampf(freq, 0.0f, 1.0f);
}

void DistoEffect::setVolume(float vol){
    volume = clampf(vol, 0.0f, 1.0f);
}

void DistoEffect::setOversamp(bool tmp){
    oversamp = tmp;
}

void DistoEffect::setDistoMode(int mode) {
    effect_mode = mode;
}

void DistoEffect::setIntensity(float val) {
    intensity = clampf(val, 0.0f, 1.0f);
}

void DistoEffect::setParameter(int param_id, float value) {
    switch (param_id){
        case 0 : 
            // Mix is always 1.0f (full disto), ignore MIDI value
            break;
        case 1 : 
            setGain(value);
            break;
        case 2 :  
            if (value < 0.125f) setDistoMode(0);
            else if (value < 0.25f) setDistoMode(1);
            else if (value < 0.375f) setDistoMode(2);
            else if (value < 0.5f) setDistoMode(3);
            else if (value < 0.625f) setDistoMode(4);
            else if (value < 0.75f) setDistoMode(5);
            else if (value < 0.875f) setDistoMode(6);
            else setDistoMode(7);
            break; 
        case 3 : 
            setTone(value);
            break;
        case 4 : 
            setIntensity(value);
            break;
        case 5 : 
            setOversamp(value);
            break;
        case 6 : 
            setVolume(value);
            break;
        default:
#if !USE_DAISY
            Serial.print("Parametre invalide: ");
            Serial.println(param_id);
#endif
            break;
    };
}
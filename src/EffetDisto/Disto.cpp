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
cycfi::q::highpass preFilter(preFilterCutoffBase, 48000); // Dummy values that get overwritten in Init
cycfi::q::lowpass postFilter(postFilterCutoff, 48000);    // Dummy values that get overwritten in Init
cycfi::q::lowpass upsamplingLowpassFilter(0.0f, 48000);   // Dummy values that get overwritten in Init

constexpr uint8_t overFactor = 16;


DistoEffect::DistoEffect(float sampleRate){

    tone.Init(sampleRate);

    // Pivot between 500 Hz and 2 kHz as the tone amount changes
    tone.SetFreq(toneFreq);

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

    if (oversamp) {
        postFilter.config(postFilterCutoff, samplerate * overFactor);
    } else {
        postFilter.config(postFilterCutoff, samplerate);
    }

    upsamplingLowpassFilter.config(samplerate / (2.0f * static_cast<float>(overFactor)), samplerate);
}

float hardClipping(float input, float threshold) { return std::clamp(input, -threshold, threshold); }

float diodeClipping(float input, float threshold) {
    if (input > threshold)
        return threshold - std::exp(-(input - threshold));
    else if (input < -threshold)
        return -threshold + std::exp(input + threshold);
    return input;
}

float softClipping(float input, float gain) { return std::tanh(input * gain); }


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

float tubeSaturation(float input, float gain) { return std::atan(input * gain); }

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
    return preFilterCutoffBase + (preFilterCutoffMax - preFilterCutoffBase) * std::tanh(inputEnergy);
}

// Helper functions for oversampling
std::vector<float> upsample(const std::vector<float> &input, int factor, float sample_rate) {
    std::vector<float> output(input.size() * factor, 0.0f);

    for (size_t i = 0; i < input.size(); ++i) {
        // Insert input samples, leaving zeros in between
        output[i * factor] = input[i];
    }

    // Apply the low-pass filter to smooth interpolated samples
    for (size_t i = 1; i < output.size(); ++i) {
        output[i] = upsamplingLowpassFilter(output[i]);
    }

    return output;
}

std::vector<float> downsample(const std::vector<float> &input, int factor) {
    std::vector<float> output(input.size() / factor);
    for (size_t i = 0; i < output.size(); ++i) {
        output[i] = input[i * factor]; // Take every nth sample
    }
    return output;
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
        sample = hardClipping(sample, 1.0f - intensity);
        break;
    }
}

void normalizeVolume(float &sample, int clippingType) {
    switch (clippingType) {
    case 0: // Hard Clipping
        sample *= 1.8f;
        break;
    case 1: // Soft Clipping
        sample *= 0.8f;
        break;
    case 2: // Fuzz
        sample *= 1.0f;
        break;
    case 3: // Tube Saturation
        sample *= 0.9f;
        break;
    case 4: // Multi-stage
        sample *= 0.5f;
        break;
    case 5: // Diode Clipping
        sample *= 1.8f;
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

    inputL = inputR = in[0][idx] + 1e-9f; // Anti-denormal

    float distorted = inputL;

    // Apply high-pass filter to remove excessive low frequencies
    const float energy = std::abs(distorted);
    preFilter.config(dynamicPreFilterCutoff(energy), samplerate);
    distorted = preFilter(distorted);


    const float computed_gain = min_gain + (this->gain * (max_gain - min_gain));

    // Reduce signal amplitude before clipping
    distorted = distorted * 0.5f;


    if (oversamp) {
        // Prepare signal for oversampling
        std::vector<float> monoInput = {distorted};
        std::vector<float> oversampledInput = upsample(monoInput, overFactor, samplerate);

        // Apply gain and distortion processing
        for (float &sample : oversampledInput) {
            processDistortion(sample, computed_gain, effect_mode, intensity);

            // Post-filter: Low-pass to smooth out harsh high frequencies
            sample = postFilter(sample);
        }

        // Downsample back to original sample rate
        const std::vector<float> downsampledOutput = downsample(oversampledInput, overFactor);
        distorted = downsampledOutput[0];

        // Apply gain compensation for oversampling
        distorted *= overFactor;
    } else {
        processDistortion(distorted, computed_gain, effect_mode, intensity);

        // Post-filter: Low-pass to smooth out harsh high frequencies
        distorted = postFilter(distorted);
    }

    // Normalize the volume between the types of distortion
    normalizeVolume(distorted, effect_mode);

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
    // freq = freq * 1500.0f;
    toneFreq = 500.0f + clampf(freq, 0.0f, 1.0f) * 1500.0f ;
    tone.SetFreq(toneFreq);
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
            setMix(value);
            break;
        case 1 : 
            setGain(value);
            break;
        case 2 :  
            if (value >= 0.82f) setDistoMode(0);
            else if ((0.82f > value) && (value > 0.66f)) setDistoMode(1);
            else if ((0.66f > value) && (value > 0.49f)) setDistoMode(2);
            else if ((0.49f > value) && (value > 0.33f)) setDistoMode(3);
            else if ((0.33f > value) && (value > 0.16f)) setDistoMode(4);
            else setDistoMode(5);
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
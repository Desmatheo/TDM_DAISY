// Disto Reverbscape

#include "Disto.h"

#include <span>
#include <cmath>

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

    // Initialisation du filtre de downsampling (tourne à 2x la fréquence d'origine)
    os_filter.Init(samplerate * 2.0f);
    os_filter.SetFreq(samplerate * 0.5f); // Coupe à la limite de Nyquist d'origine (ex: 24kHz)
    os_filter.SetRes(0.0f);               // Pas de résonance (Butterworth)
    os_filter.SetDrive(0.0f);
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


// --- ADAA INTEGRALS AND WRAPPERS ---

inline float integral_hardclip(float x, float threshold) {
    float abs_x = std::abs(x);
    if (abs_x > threshold) return threshold * abs_x - (threshold * threshold * 0.5f);
    else return x * x * 0.5f;
}

float hardClippingADAA(float input, float threshold, float &x_prev) {
    float diff = input - x_prev;
    float result;
    if (std::abs(diff) < 0.05f) {
        result = hardClipping((input + x_prev) * 0.5f, threshold);
    } else {
        result = (float)(( (double)integral_hardclip(input, threshold) - (double)integral_hardclip(x_prev, threshold) ) / (double)diff);
    }
    x_prev = input;
    return result;
}

inline float integral_fast_tanh(float x) {
    float abs_x = std::abs(x);
    if (abs_x >= 3.0f) return abs_x + 0.8132089f;
    float x2 = x * x;
    return (x2 / 18.0f) + (4.0f / 3.0f) * std::log(x2 + 3.0f);
}

float softClippingADAA(float input, float gain, float &x_prev) {
    float x = input * gain;
    float diff = x - x_prev;
    float result;
    if (std::abs(diff) < 0.05f) {
        result = fast_tanh((x + x_prev) * 0.5f);
    } else {
        result = (float)(( (double)integral_fast_tanh(x) - (double)integral_fast_tanh(x_prev) ) / (double)diff);
    }
    x_prev = x;
    return result;
}

inline float integral_fast_atan(float x) {
    float abs_x = std::abs(x);
    return abs_x - std::log(1.0f + abs_x);
}

float tubeSaturationADAA(float input, float gain, float &x_prev) {
    float x = input * gain;
    float diff = x - x_prev;
    float result;
    if (std::abs(diff) < 0.05f) {
        result = fast_atan((x + x_prev) * 0.5f);
    } else {
        result = (float)(( (double)integral_fast_atan(x) - (double)integral_fast_atan(x_prev) ) / (double)diff);
    }
    x_prev = x;
    return result;
}

inline float integral_diode(float x, float threshold) {
    float C = -(threshold * threshold * 0.5f) - 1.0f;
    if (x > threshold) return (threshold * x) + fastexp(-(x - threshold)) + C;
    else if (x < -threshold) return (-threshold * x) + fastexp(x + threshold) + C;
    else return (x * x) * 0.5f;
}

float diodeClippingADAA(float input, float threshold, float intensity, float &x_prev) {
    float preGain = 1.0f + intensity * 4.0f;
    float x = input * preGain;
    float diff = x - x_prev;
    float out;
    if (std::abs(diff) < 0.05f) {
        float mid = (x + x_prev) * 0.5f;
        if (mid > threshold) out = threshold - fastexp(-(mid - threshold));
        else if (mid < -threshold) out = -threshold + fastexp(mid + threshold);
        else out = mid;
    } else {
        out = (float)(( (double)integral_diode(x, threshold) - (double)integral_diode(x_prev, threshold) ) / (double)diff);
    }
    x_prev = x;
    return out / preGain;
}

inline float integral_testDistortion(float x) {
    float abs_x = std::abs(x);
    return abs_x + fastexp(-abs_x);
}

float testDistortionADAA(float input, float gainVal, float &x_prev) {
    float x = input * gainVal;
    float diff = x - x_prev;
    float result;
    if (std::abs(diff) < 0.05f) {
        float mid = (x + x_prev) * 0.5f;
        result = ((mid < std::signbit(mid))? (-1.0f) : 1.0f) * (1.0f - fastexp(-std::abs(mid)));
    } else {
        result = (float)(( (double)integral_testDistortion(x) - (double)integral_testDistortion(x_prev) ) / (double)diff);
    }
    x_prev = x;
    return result;
}

// --- OVERPROCESS WRAPPER ---
// Permet de traiter la distorsion en tant que fonction pour l'oversampling
float processDistortionCore(float sample, const float gain, const int clippingType, const float intensity) {
    sample *= gain;
    switch (clippingType) {
    case 2: return fuzzEffect(sample, intensity * 10.0f);
    case 4: return multiStage(sample, gain, intensity);
    case 7: return testOverDrive(sample, intensity);
    default: return sample; // Cas inatteignable si on filtre bien avant
    }
}

void processDistortion(float &sample,           // Sample to process
                       const float &gain,       // Gain
                       const int &clippingType, // Clipping type
                       const float &intensity,  // Intensity
                       float &x_prev_adaa,      // ADAA Memory
                       bool oversamp,           // Oversampling flag
                       float &os_prev_sample,   // Previous Sample for Linear Interp
                       Svf &os_filter)          // Oversampling Output Filter
{
    // Mode 2, 4, 7 -> OVERSAMPLING x2 (Interpolation Linéaire)
    if (clippingType == 2 || clippingType == 4 || clippingType == 7) {
        if (oversamp) {
            // Upsample V5 : Interpolation Linéaire x2
            // L'interpolation linéaire crée des encoches parfaites à 48kHz, empêchant l'intermodulation !
            float s1 = (os_prev_sample + sample) * 0.5f;
            float s2 = sample;
            os_prev_sample = sample;
            
            // Traitement de la distorsion sur le signal à 96kHz
            s1 = processDistortionCore(s1, gain, clippingType, intensity);
            s2 = processDistortionCore(s2, gain, clippingType, intensity);
            
            // Downsampling V5 : Filtrage Biquad Anti-Aliasing
            os_filter.Process(s1);
            os_filter.Process(s2);
            sample = os_filter.Low(); // On garde uniquement le 2ème échantillon (décimation)
        } else {
            // Pas d'oversampling
            sample = processDistortionCore(sample, gain, clippingType, intensity);
        }
        return;
    }

    // Modes ADAA -> Zéro Oversampling (Tourne à 48kHz natif)
    sample *= gain;
    switch (clippingType) {
    case 0: // Hard Clipping
        sample = hardClippingADAA(sample, 1.0f - intensity, x_prev_adaa);
        break;
    case 1: // Soft Clipping
        sample = softClippingADAA(sample, gain, x_prev_adaa);
        break;
    case 3: // Tube Saturation
        sample = tubeSaturationADAA(sample, intensity * 10.0f, x_prev_adaa);
        break;
    case 5: // Diode Clipping
        sample = diodeClippingADAA(sample, 1.0f - (intensity * 0.5f), intensity, x_prev_adaa);
        break;
    case 6: // Test Distortion
        sample = testDistortionADAA(sample, gain, x_prev_adaa);
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


    processDistortion(distorted, computed_gain, effect_mode, intensity, x_prev_adaa, oversamp, os_prev_sample, os_filter);

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
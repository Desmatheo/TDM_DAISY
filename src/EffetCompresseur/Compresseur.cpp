#include "Compresseur.h"
#include <math.h>

CompresseurEffect::CompresseurEffect(float sampleRate) {
    sample_rate_ = sampleRate;
    thresholdLinear_ = 0.1f;
    thresholdDb_ = -20.0f;
    ratio_ = 4.0f;
    attackMs_ = 10.0f;
    releaseMs_ = 100.0f;
    makeupGainLinear_ = 1.0f;
    envelope_ = 0.0f;
    calculateCoefs();
}

void CompresseurEffect::calculateCoefs() {
    // Calcul des coefficients IIR pour le suivi d'enveloppe
    attackCoef_ = expf(-1.0f / (attackMs_ * 0.001f * sample_rate_));
    releaseCoef_ = expf(-1.0f / (releaseMs_ * 0.001f * sample_rate_));
}

float CompresseurEffect::dbToLinear(float db) {
    // Conversion d'une valeur Décibel en amplitude linéaire (facteur multiplicatif)
    return powf(10.0f, db / 20.0f);
}

float CompresseurEffect::linearToDb(float linear) {
    // Conversion d'une amplitude linéaire en Décibels
    // Protection contre le log(0) qui renverrait -Inf
    if (linear < 1e-6f) return -120.0f; 
    return 20.0f * log10f(linear);
}

void CompresseurEffect::setThreshold(float dB) {
    thresholdDb_ = dB;
    thresholdLinear_ = dbToLinear(dB);
}

void CompresseurEffect::setRatio(float ratio) {
    ratio_ = ratio;
}

void CompresseurEffect::setAttack(float ms) {
    attackMs_ = ms;
    calculateCoefs();
}

void CompresseurEffect::setRelease(float ms) {
    releaseMs_ = ms;
    calculateCoefs();
}

void CompresseurEffect::setMakeupGain(float dB) {
    makeupGainLinear_ = dbToLinear(dB);
}

void CompresseurEffect::setParameter(int param_id, float value) {
    switch (param_id) {
        case 0:
            // Mappe [0.0, 1.0] vers un seuil de -60dB à 0dB
            setThreshold(-60.0f + value * 60.0f);
            break;
        case 1:
            // Ratio de 1:1 à 20:1
            setRatio(1.0f + value * 19.0f);
            break;
        case 2:
            // Attaque de 1ms à 100ms
            setAttack(1.0f + value * 99.0f);
            break;
        case 3:
            // Relâchement de 10ms à 1000ms
            setRelease(10.0f + value * 990.0f);
            break;
        case 4:
            // Gain de compensation (Makeup) de 0dB à +24dB
            setMakeupGain(value * 24.0f);
            break;
        default:
            break;
    }
}

void CompresseurEffect::update(const float** in, float** out, int idx) {
    float sample = in[0][idx];
    
    // 1. Détection de niveau (Redressement + Suivi d'enveloppe)
    float absSample = fabsf(sample);
    if (absSample > envelope_) {
        // Phase d'attaque : le signal monte
        envelope_ = attackCoef_ * envelope_ + (1.0f - attackCoef_) * absSample;
    } else {
        // Phase de relâchement : le signal redescend
        envelope_ = releaseCoef_ * envelope_ + (1.0f - releaseCoef_) * absSample;
    }
    
    // 2. Calcul du gain de réduction (en domaine Logarithmique / dB)
    float envDb = linearToDb(envelope_);
    float gainDb = 0.0f;
    
    if (envDb > thresholdDb_) {
        // Le signal dépasse le seuil : on calcule de combien de dB il le dépasse (overshoot)
        float overshootDb = envDb - thresholdDb_;
        // On applique le Ratio pour savoir combien on doit atténuer
        // Ex: Ratio de 4:1 -> Si overshoot de 12dB, l'atténuation sera de 12 * (1 - 1/4) = 12 * 0.75 = 9dB d'atténuation.
        float attenuationDb = overshootDb * (1.0f - (1.0f / ratio_));
        gainDb = -attenuationDb; // Le gain est négatif (on baisse le volume)
    }
    
    // 3. Application du compresseur au signal
    // Retour dans le domaine linéaire (mathématique standard)
    float gainLinear = dbToLinear(gainDb);
    // On applique la réduction de gain ET le Make-up Gain pour remonter le niveau global
    float output = sample * gainLinear * makeupGainLinear_;
    
    // 4. Sécurité anti-saturation
    output = clampf(output, -1.0f, 1.0f);
    
    out[0][idx] = output;
    out[1][idx] = output;
}

#include "NoiseGate.h"

NoiseGateEffect::NoiseGateEffect(float sampleRate) {
    sample_rate_ = sampleRate;
    threshold_ = 0.01f;
    attackMs_ = 1.0f;
    releaseMs_ = 50.0f;
    envelope_ = 0.0f;
    gain_ = 1.0f;
    calculateCoefs();
}

void NoiseGateEffect::calculateCoefs() {
    // Calcul des coefficients de lissage exponentiel (Filtre IIR passe-bas)
    // Plus le temps (en ms) est long, plus le coefficient se rapproche de 1.0.
    attackCoef_ = expf(-1.0f / (attackMs_ * 0.001f * sample_rate_));
    releaseCoef_ = expf(-1.0f / (releaseMs_ * 0.001f * sample_rate_));
}

void NoiseGateEffect::setThreshold(float value) {
    // Courbe de réponse non-linéaire (cubique) pour affiner le réglage du seuil dans les basses valeurs
    threshold_ = value * value * value * 0.1f;
}

void NoiseGateEffect::setAttack(float attackMs) {
    attackMs_ = attackMs;
    calculateCoefs();
}

void NoiseGateEffect::setRelease(float releaseMs) {
    releaseMs_ = releaseMs;
    calculateCoefs();
}

void NoiseGateEffect::setParameter(int param_id, float value) {
    switch (param_id) {
        case 0:
            setThreshold(value);
            break;
        case 1:
            // L'attaque varie de 1ms à 100ms
            setAttack(1.0f + value * 99.0f);
            break;
        case 2:
            // Le relâchement varie de 10ms à 1000ms
            setRelease(10.0f + value * 990.0f);
            break;
        default:
            break;
    }
}

void NoiseGateEffect::update(const float** in, float** out, int idx) {
    float sample = in[0][idx];
    
    // 1. Redressement du signal (valeur absolue)
    float absSample = fabsf(sample);
    
    // 2. Suivi d'enveloppe avec des temps de montée (attack) et descente (release) distincts
    if (absSample > envelope_) {
        // Le signal monte (coup de médiator) : on utilise le coefficient d'attaque
        envelope_ = attackCoef_ * envelope_ + (1.0f - attackCoef_) * absSample;
    } else {
        // Le signal descend (résonance) : on utilise le coefficient de relâchement
        envelope_ = releaseCoef_ * envelope_ + (1.0f - releaseCoef_) * absSample;
    }
    
    // 3. Détermination de la cible du gain : porte ouverte (1.0) ou fermée (0.0)
    float targetGain = (envelope_ > threshold_) ? 1.0f : 0.0f;
    
    // 4. Lissage (Slew limiter) appliqué au gain lui-même pour éviter les clics audio
    if (targetGain > gain_) {
        gain_ = attackCoef_ * gain_ + (1.0f - attackCoef_) * targetGain;
    } else {
        gain_ = releaseCoef_ * gain_ + (1.0f - releaseCoef_) * targetGain;
    }
    
    // 5. Application du gain lissé sur l'échantillon audio
    float output = sample * gain_;
    
    // Copie sur les canaux (ici stéréo ou dual mono par défaut dans le wrapper)
    out[0][idx] = output;
    out[1][idx] = output;
}

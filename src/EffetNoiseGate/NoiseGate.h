/**
 * @file NoiseGate.h
 * @brief Effet de porte de bruit (Noise Gate) sous forme de classe.
 *
 * Coupe le signal audio lorsqu'il descend en dessous d'un certain seuil
 * afin d'éliminer le souffle ou les bruits de fond indésirables.
 */

#pragma once
#include "../Utils/Effect.h"
#include "daisy_seed.h"
#include "daisysp.h"
#include "arm_math.h"
#include "../Utils/Utils.h"

#ifndef PI_F
#define PI_F 3.14159265358979323846f
#endif

/**
 * @class NoiseGateEffect
 * @brief Implémentation d'un Noise Gate avec temps d'attaque et de relâchement.
 */
class NoiseGateEffect : public Effect {
public:
    /**
     * @brief Constructeur. Initialise l'effet avec la fréquence d'échantillonnage.
     * @param sampleRate Fréquence d'échantillonnage (ex: 44100.0f).
     */
    NoiseGateEffect(float sampleRate);
    
    /**
     * @brief Traite un échantillon audio.
     */
    void update(const float** in, float** out, int idx) override;
    
    /**
     * @brief Règle le seuil de déclenchement du Noise Gate.
     * @param value Valeur normalisée (0.0 à 1.0) qui sera convertie en seuil interne.
     */
    void setThreshold(float value);
    
    /**
     * @brief Règle le temps d'attaque (vitesse d'ouverture).
     * @param attackMs Temps d'attaque en millisecondes.
     */
    void setAttack(float attackMs);
    
    /**
     * @brief Règle le temps de relâchement (vitesse de fermeture).
     * @param releaseMs Temps de relâchement en millisecondes.
     */
    void setRelease(float releaseMs);
    
    /**
     * @brief Routage MIDI/Paramètre générique.
     * Param 0 : Seuil, Param 1 : Attaque, Param 2 : Relâchement.
     */
    virtual void setParameter(int param_id, float value) override;

private:
    /**
     * @brief Recalcule les coefficients exponentiels à partir des temps en ms.
     */
    void calculateCoefs();
    
    float sample_rate_;     /**< Fréquence d'échantillonnage de la plateforme. */
    float threshold_;       /**< Seuil de volume (en dessous, la porte se ferme). */
    float attackMs_;        /**< Temps d'attaque en millisecondes. */
    float releaseMs_;       /**< Temps de relâchement en millisecondes. */
    float attackCoef_;      /**< Coefficient mathématique calculé pour l'attaque. */
    float releaseCoef_;     /**< Coefficient mathématique calculé pour le relâchement. */
    float envelope_;        /**< Valeur actuelle de l'enveloppe du signal (suivi du volume). */
    float gain_;            /**< Gain actuel appliqué au signal (entre 0.0 et 1.0). */
};

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
 * @class EqualizerEffect
 * @brief Implémentation d'un égaliseur graphique à 5 bandes.
 *
 * Utilise des filtres biquad pour ajuster le gain de 5 plages de fréquences 
 * distinctes, permettant un contrôle précis du spectre audio.
 */
class EqualizerEffect : public Effect {
public:
    /**
     * @brief Constructeur de l'effet Égaliseur.
     * @param sampleRate Fréquence d'échantillonnage système.
     */
    EqualizerEffect(float sampleRate);

    /**
     * @brief Boucle de traitement audio principale de l'effet.
     * @param in Tableau de pointeurs vers les buffers d'entrée.
     * @param out Tableau de pointeurs vers les buffers de sortie.
     * @param idx Index de l'échantillon à traiter (par canal).
     */
    void update(const float** in, float** out, int idx) override;
    
    /**
     * @brief Règle le gain d'une bande spécifique.
     * @param band_index Index de la bande (0 à 4).
     * @param value_norm Valeur normalisée du gain (généralement 0.0 à 1.0).
     */
    void setBand(int band_index, float value_norm);
    
    /** 
     * @brief Définit le volume global de l'effet en sortie. 
     * @param vol Volume (0.0 à 1.0).
     */
    void setVolume(float vol);
    
    /**
     * @brief Affecte une valeur brute à un paramètre spécifique (via ID).
     * @param param_id Identifiant unique du paramètre (ex: ID d'une bande).
     * @param value Valeur normalisée.
     */
    virtual void setParameter(int param_id, float value) override;

private:
    /**
     * @brief Calcule les coefficients des filtres biquad pour chaque bande.
     * Appelée en interne lors du changement de gain d'une bande.
     */
    void calculateCoeffs();

    float sample_rate_;           /**< Fréquence d'échantillonnage. */
    float volume;                 /**< Volume global appliqué post-EQ. */
    float gains_db[5];            /**< Gains actuels en décibels pour chaque bande. */

    float pCoeffs[5][5];          /**< Coefficients b0, b1, b2, a1, a2 pour chaque biquad. */
    float pState[5][4];           /**< États de mémoire x[n-1], x[n-2], y[n-1], y[n-2]. */
};

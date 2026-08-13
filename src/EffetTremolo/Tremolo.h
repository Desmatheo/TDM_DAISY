#pragma once

#include "../Utils/Utils.h"

#if USE_DAISY
#include "daisy_seed.h"
#include "daisysp.h"
#endif

#include "../Utils/Effect.h"

#if USE_DAISY
/**
 * @class TremoloEffect
 * @brief Implémentation d'un effet de trémolo basé sur la modulation d'amplitude.
 *
 * Utilise un oscillateur basse fréquence (LFO) généré en interne pour moduler 
 * l'amplitude du signal entrant. Le type de forme d'onde, la profondeur et la 
 * vitesse sont ajustables.
 */
class TremoloEffect : public Effect {
public:
    float samplerate;             /**< Fréquence d'échantillonnage. */
    float dryMix;                 /**< Proportion du signal d'origine. */
    float wetMix;                 /**< Proportion du signal traité. */
    float volume;                 /**< Volume global en sortie. */

    float phase = 0.0f;           /**< Phase courante de l'oscillateur. */
    float phaseOffset = 0.0f;     /**< Décalage de phase. */
    float phaseIncrement = 0.0f;  /**< Incrément de phase par échantillon (contrôle la fréquence). */
    int waveform = 0;             /**< Type de forme d'onde (0=Sinus, 1=Triangle, 2=Carré, etc.). */
    
    daisysp::OnePole lfoFilter;   /**< Filtre passe-bas pour adoucir les formes d'ondes abruptes. */
    float depthVal = 0.5f;        /**< Profondeur de la modulation (0.0 à 1.0). */
    float anti_denormal = 1e-9f;  /**< Prévention de la dénormalisation flottante. */

    /**
     * @brief Constructeur de l'effet Tremolo.
     * @param sampleRate Fréquence d'échantillonnage système.
     */
    TremoloEffect(float sampleRate); 

    /**
     * @brief Boucle de traitement audio principale de l'effet.
     * @param in Tableau de pointeurs vers les buffers d'entrée.
     * @param out Tableau de pointeurs vers les buffers de sortie.
     * @param idx Index de l'échantillon à traiter (par canal).
     */
    void update(const float** in, float** out, int idx) override;
    
    /** @brief Règle la proportion du signal traité (Dry/Wet). */
    void setMix(float mix);

    /** @brief Règle l'intensité de l'effet de trémolo. */
    void setDepth(float depth);

    /** @brief Règle la vitesse de l'oscillateur (LFO). */
    void setRate(float rate);

    /** @brief Sélectionne la forme d'onde de modulation. */
    void setWaveform(int waveform);

    /** @brief Règle le volume de sortie. */
    void setVolume(float vol);
    
    /** @brief Règle le décalage de phase de l'oscillateur. */
    void setPhaseOffset(float offset);

    /**
     * @brief Affecte une valeur brute à un paramètre spécifique (via ID).
     * @param param_id Identifiant unique du paramètre.
     * @param value Valeur normalisée (généralement 0.0 - 1.0).
     */
    void setParameter(int param_id, float value) override;

    /** @brief Active ou désactive l'effet. */
    void setEnabled(bool e) { active = e; }
    /** @brief Vérifie si l'effet est actuellement actif. */
    bool isEnabled() const  { return active; }

private:
    bool active = false;          /**< État d'activation de l'effet. */
};
#endif

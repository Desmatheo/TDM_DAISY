#pragma once

#include <vector>
#include "../Utils/Utils.h"

#if USE_DAISY
#include "daisy_seed.h"
#include "daisysp.h"
#endif

#include <q/fx/biquad.hpp>
namespace q = cycfi::q;
using namespace daisy;
using namespace daisysp;

#include "../Utils/Effect.h"
#include "../Utils/toneDaisySP/tone.h"

#if USE_DAISY
/**
 * @class DistoEffect
 * @brief Implémentation d'un effet de distorsion.
 *
 * Applique différentes méthodes de saturation/distorsion au signal entrant.
 * Inclut un filtre de tonalité et une option de sur-échantillonnage (oversampling)
 * pour réduire l'aliasing.
 */
class DistoEffect : public Effect {

    public:

    float samplerate;             /**< Fréquence d'échantillonnage. */

    float dryMix;                 /**< Proportion du signal d'origine. */
    float wetMix;                 /**< Proportion du signal traité. */
    float volume = 1;             /**< Volume global en sortie. */

    float gain;                   /**< Gain d'entrée avant saturation. */
    float min_gain = 1.0f;        /**< Gain minimum. */
    float max_gain = 20.0f;       /**< Gain maximum. */

    float toneFreq;               /**< Fréquence de coupure du filtre de tonalité. */
    float toneFreqTarget;         /**< Fréquence de coupure cible (pour interpolation). */
    bool oversamp;                /**< État de l'oversampling (activé/désactivé). */
    float intensity;              /**< Intensité de l'effet de distorsion. */
    float os_prev_sample = 0.0f;  /**< Échantillon précédent pour l'oversampling. */

    int effect_mode = 0;          /**< Mode de distorsion actif. */

    Tone2 tone;                   /**< Filtre de tonalité. */

    cycfi::q::highpass preFilter{140.0f, 48000};  /**< Filtre passe-haut en entrée. */
    cycfi::q::lowpass postFilter{8000.0f, 48000}; /**< Filtre passe-bas en sortie. */

#if USE_DAISY
    /**
     * @brief Constructeur de l'effet Distorsion.
     * @param sampleRate Fréquence d'échantillonnage système.
     */
    DistoEffect(float sampleRate); 
#endif


#if USE_DAISY
    /**
     * @brief Boucle de traitement audio principale de l'effet.
     * @param in Tableau de pointeurs vers les buffers d'entrée.
     * @param out Tableau de pointeurs vers les buffers de sortie.
     * @param idx Index de l'échantillon à traiter (par canal).
     */
    void update(const float** in, float** out, int idx) override;
#endif

    /** @brief Règle la proportion du signal traité (Dry/Wet). */
    void setMix(float mix);                 
    /** @brief Règle le volume de sortie global. */
    void setVolume(float vol);              
    
    /** @brief Règle le mode de distorsion (0, 1, 2). */
    void setDistoMode(int mode);            

    /** @brief Règle la fréquence de coupure du filtre de tonalité. */
    void setTone(float tone);               
    /** @brief Règle l'intensité ou la dureté de la saturation. */
    void setIntensity(float intensity);     
    /** @brief Règle le gain d'entrée appliqué avant la saturation. */
    void setGain(float gain);               
    /** @brief Active ou désactive l'oversampling pour réduire l'aliasing. */
    void setOversamp(bool oversamp);        
    
    /** @brief Initialise les filtres internes (pre/post/anti-aliasing). */
    void InitializeFilters();

    /**
     * @brief Traite un échantillon avec le contrôle de tonalité type Tilt.
     * @param input Échantillon d'entrée.
     * @return Échantillon filtré.
     */
    float ProcessTiltToneControl(float input);

#if USE_DAISY
    /**
     * @brief Affecte une valeur brute à un paramètre spécifique (via ID).
     * @param param_id Identifiant unique du paramètre.
     * @param value Valeur normalisée.
     */
    void setParameter(int param_id, float value) override;
#endif

    // --- METHODES ET VARIABLES PARTAGEES ---
    /** @brief Active ou désactive l'effet. */
    void setEnabled(bool e) { active = e; }
    /** @brief Vérifie si l'effet est actuellement actif. */
    bool isEnabled() const  { return active; }

private:
    bool active = false;          /**< État d'activation de l'effet. */
    float anti_denormal = 1e-9f;  /**< Prévention de la dénormalisation flottante. */
    float x_prev_adaa = 0.0f;     /**< Mémoire pour l'anti-aliasing ADAA (Antiderivative Anti-Aliasing). */
    daisysp::Svf os_filter;       /**< Filtre d'anti-aliasing (sortie) pour le downsampling. */
    
};
#endif

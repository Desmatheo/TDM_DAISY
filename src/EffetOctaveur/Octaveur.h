#pragma once

#define USE_DAISY 1
#include "../Utils/Utils.h"

#if USE_DAISY
#include "daisy_seed.h"
#include "daisysp.h"
#endif

#include <q/fx/biquad.hpp>
namespace q = cycfi::q;
using namespace daisy;
using namespace daisysp;

#define eq_ON 0
#define od_ON 0

#include "Util/Multirate.h"
#include "Util/OctaveGenerator.h"
#include "../Utils/Effect.h"

#if USE_DAISY
/**
 * @class OctaveurEffect
 * @brief Implémentation de l'effet "Octaveur" (Générateur d'octave).
 *
 * Utilise des techniques de décimation et d'interpolation pour générer
 * une ou plusieurs octaves inférieures/supérieures à partir du signal original.
 */
class OctaveurEffect : public Effect {

    public:

    float dryMix;                 /**< Proportion du signal d'origine. */
    float wetMix;                 /**< Proportion du signal traité (octave). */
    float volume = 1;             /**< Volume global en sortie. */

    Decimator2 decimate2;         /**< Filtre de décimation pour le sous-échantillonnage. */
    Interpolator interpolate;     /**< Filtre d'interpolation pour le sur-échantillonnage. */
    OctaveGenerator octave;       /**< Classe utilitaire de génération d'octave. */
    
#if USE_DAISY
#if eq_ON
    q::highshelf eq1;             /**< Filtre Highshelf (si activé). */
    q::lowshelf eq2;              /**< Filtre Lowshelf (si activé). */
#endif
#if od_ON
    Overdrive overdrive;          /**< Effet Overdrive optionnel. */
#endif
#endif
    float buff[6];                /**< Buffer circulaire interne. */
    float buff_out[6];            /**< Buffer de sortie interne. */
    int bin_counter = 0;          /**< Compteur pour la gestion du sous-échantillonnage. */
    float anti_denormal = 1e-9f;  /**< Prévention de la dénormalisation flottante. */

    float current_ODswell;        /**< Enveloppe/Intensité courante de l'Overdrive. */

    int effect_mode = 0;          /**< Mode de l'effet (ex: Sub-octave 1, Sub-octave 2, Octave Up). */

    bool odOn = false;            /**< État d'activation de l'overdrive intégré. */
    bool bypass = false;          /**< État de bypass matériel/logiciel. */

#if USE_DAISY
    /**
     * @brief Constructeur de l'effet Octaveur.
     * @param sampleRate Fréquence d'échantillonnage système.
     */
    OctaveurEffect(float sampleRate); 
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
    
    /** @brief Règle le mode d'octave (0, 1, 2). */
    void setOctaveMode(int mode);          

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
    
};
#endif

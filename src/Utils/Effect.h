#pragma once

#include "daisy_seed.h"

#define CPU_LoadEffect 1
#define CPU_LoadAll 1

/**
 * @class Effect
 * @brief Classe de base abstraite pour tous les effets DSP.
 *
 * Tous les algorithmes audio (Delay, Disto, Tremolo, etc.) héritent de cette
 * classe. Elle impose l'implémentation d'une fonction de mise à jour audio
 * (`update`) et d'une fonction de paramétrage générique (`setParameter`).
 */
class Effect {
public:

#if CPU_LoadEffect
    uint32_t profiled_ticks = 0;        /**< Compteur de cycles d'horloge pour le profilage CPU (accumulation). */
    uint32_t last_profiled_ticks = 0;   /**< Valeur du dernier relevé de profilage CPU pour cet effet. */
#endif


    virtual ~Effect() = default;
    
    /**
     * @brief Fonction de traitement audio principale.
     * @param in Tableau de pointeurs vers les buffers d'entrée.
     * @param out Tableau de pointeurs vers les buffers de sortie.
     * @param idx Index de l'échantillon courant (ou de la corde).
     */
    virtual void update(const float** in, float** out, int idx) = 0;
    
    // virtual float updateTest(const float in, float out, int idx) = 0; // Ancienne méthode de test, dépréciée
    
    /**
     * @brief Règle un paramètre générique de l'effet via un identifiant.
     * @param param_id Identifiant du paramètre (ex: 0 pour le Mix, 1 pour le Feedback...).
     * @param value Valeur normalisée du paramètre (généralement de 0.0 à 1.0).
     */
    virtual void setParameter(int param_id, float value) = 0;
};

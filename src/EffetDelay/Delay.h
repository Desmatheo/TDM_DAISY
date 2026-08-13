#pragma once

#include "../Utils/Effect.h"
#include "daisy_seed.h"
#include "daisysp.h"
#include "arm_math.h"
#include "../Utils/Utils.h"

/**
 * @def MAX_DELAY_SAMPLES
 * @brief 4 secondes de délai à une fréquence d'échantillonnage de 48 kHz.
 */
#define MAX_DELAY_SAMPLES (48000 * 4) 

/**
 * @class DelayEffect
 * @brief Implémentation d'un effet de délai avec gestion du feedback, du mixage et synchronisation au tempo.
 *
 * Hérite de la classe abstraite `Effect`. Cet effet supporte un mode manuel
 * (temps défini en millisecondes) ou un mode tempo (synchronisé sur le BPM).
 */
class DelayEffect : public Effect {
public:

    /**
     * @struct DelayChannel
     * @brief Structure interne représentant un canal de délai indépendant.
     *
     * Gère sa propre ligne de retard (buffer circulaire), son filtre de tonalité
     * (filtre passe-bas basique) et l'interpolation du temps de délai.
     */
    struct DelayChannel {
        float* buffer = nullptr;           /**< Pointeur vers le buffer de la ligne de retard. */
        uint32_t buf_len = 0;              /**< Longueur totale du buffer. */
        uint32_t write_idx = 0;            /**< Index d'écriture courant dans le buffer. */
        
        float tone_z1 = 0.0f;              /**< État précédent pour le filtre de tonalité. */
        float tone_a0 = 1.0f;              /**< Coefficient a0 du filtre. */
        float tone_b1 = 0.0f;              /**< Coefficient b1 du filtre. */

        float muteFade = 1.0f;             /**< Coefficient de fondu pour le mode standby. */
        uint32_t standbyTimer = 0;         /**< Compteur pour la désactivation de l'effet. */
        float lastTarget = 0.0f;           /**< Dernière cible de délai. */
        float currentDelay = 0.0f;         /**< Délai courant interpolé. */
        float delayTarget = 0.0f;          /**< Cible finale du délai. */
        float feedback = 0.0f;             /**< Taux de réinjection (feedback). */
        bool active = false;               /**< État d'activation du canal. */
        float anti_denormal = 1e-9f;       /**< Bruit infime pour éviter les dénormalisations du processeur. */

        /**
         * @brief Initialise le canal de délai.
         * @param mem Pointeur vers la mémoire allouée pour le buffer.
         * @param sampleRate Fréquence d'échantillonnage du système.
         * @param max_delay_samples Taille maximale allouée pour ce canal (en échantillons).
         */
        void Init(float* mem, float sampleRate, uint32_t max_delay_samples);
        
        /**
         * @brief Traite un échantillon audio entrant.
         * @param in Échantillon audio d'entrée.
         * @return Échantillon audio retardé.
         */
        float Process(float in);
    };

    /**
     * @brief Constructeur de l'effet Delay.
     * @param sampleRate Fréquence d'échantillonnage système.
     */
    DelayEffect(float sampleRate);

    /**
     * @brief Boucle de traitement audio principale de l'effet.
     * @param in Tableau de pointeurs vers les buffers d'entrée.
     * @param out Tableau de pointeurs vers les buffers de sortie.
     * @param idx Nombre d'échantillons à traiter par canal.
     */
    void update(const float** in, float** out, int idx) override;

    /** @brief Règle la proportion du signal traité (Dry/Wet). */
    void setMix(float mix);
    /** @brief Règle le volume de sortie global. */
    void setVolume(float vol);
    /** @brief Règle le mode de délai (0 = manuel, 1 = synchronisé au BPM). */
    void setDelayMode(float mode);
    /** @brief Règle le temps de délai manuel. */
    void setDelayTime(float time);
    /** @brief Règle le tempo de référence. */
    void setBpm(float bpm);
    /** @brief Règle la subdivision (croche, noire, etc.) pour le mode tempo. */
    void setSubdivision(float value); 
    /** @brief Règle le taux de répétition du délai. */
    void setFeedback(float fdbk);
    
    /**
     * @brief Affecte une valeur brute à un paramètre spécifique (via ID).
     * @param param_id Identifiant unique du paramètre.
     * @param value Valeur normalisée (généralement 0.0 - 1.0).
     */
    void setParameter(int param_id, float value) override;

private:
    DelayChannel delayL;

    float dryMix, wetMix, volume;
    float vdelayFDBK;
    
    int delayMode = 0;                      /**< 0 = Mode Manuel, 1 = Mode Tempo. */
    float manualTimeMs = 500.0f;            /**< Temps en millisecondes pour le mode manuel. */
    float currentBPM = 120.0f;              /**< BPM courant. */
    float currentSubdivisionMult = 1.0f;    /**< Multiplicateur de subdivision rythmique. */

    float sample_rate_;

    /**
     * @brief Recalcule le temps de délai cible en fonction du mode (Manuel ou Tempo).
     */
    void recalculateDelayTime();
};

/**
 * @file main.h
 * @brief En-tête principal regroupant les dépendances et déclarations globales.
 *
 * Déclare les tableaux globaux contenant les pointeurs vers les effets 
 * (pour chaque corde de la guitare hexaphonique) ainsi que les 
 * structures de routage (StringUtil).
 */

#pragma once
#include "Utils.h"

#include "daisy_tdm_slave.h"

#include "Effect.h"
#include "../EffetOctaveur/Octaveur.h"
#include "../EffetDelay/Delay.h"
#include "../EffetTremolo/Tremolo.h"
#include "../EffetDisto/Disto.h"
#include "../EffetEqualizer/Equalizer.h"
#include "../EffetNoiseGate/NoiseGate.h"
#include "../EffetCompresseur/Compresseur.h"


#if USE_MIDI_USB
extern MidiUsbHandler midi;
#endif

#if CPU_METER
extern CpuLoadMeter loadMeter;
#endif

extern DaisyTdmSlave hw;


extern OctaveurEffect* octaveur_effects[6];
extern DelayEffect* delay_effects[6];
extern TremoloEffect* tremolo_effects[6];
extern DistoEffect* disto_effects[6];
extern EqualizerEffect* eq_effects[6];
extern NoiseGateEffect* noisegate_effects[6];
extern CompresseurEffect* compresseur_effects[6];

/**
 * @enum EffectType
 * @brief Énumération des types d'effets disponibles dans le projet.
 */
enum class EffectType {
    Mute,
    Bypass,
    Octaveur,
    Delay,
    Disto,
    Tremolo,
    Equalizer,
    NoiseGate,
    Compressor
};

/**
 * @class StringUtil
 * @brief Représente l'état et le routage des effets pour une corde (String) donnée.
 * 
 * Permet de lier une corde (index) à un ou plusieurs effets actifs en série.
 */
class StringUtil{
public : 
    EffectType type;                    /**< Type d'effet principal actif sur cette corde. */
    int index;                          /**< Index de la corde (0 à 5). */
    Effect* active_effect;              /**< Pointeur vers l'effet principal. */
    Effect* active_effect_bonus;        /**< Pointeur vers un effet secondaire (en série). */
    Effect* active_effect_bonus_bonus;  /**< Pointeur vers un troisième effet. */

    StringUtil(EffectType type, int index){
        this->type = type;
        this->index = index;
        this->active_effect = nullptr;
        this->active_effect_bonus = nullptr;
        this->active_effect_bonus_bonus = nullptr;
    }

    EffectType GetType() {
        return type;
    }
};

extern StringUtil strings[];



#include "audio_processing.h"
#include "midi_processing.h"

/**
 * @file Utils.h
 * @brief Paramètres globaux de configuration (macros) et utilitaires génériques.
 *
 * Contient les drapeaux (flags) de compilation pour activer ou désactiver
 * le logging série, le MIDI par USB, ou le monitoring CPU, ainsi que des
 * fonctions mathématiques simples.
 */

#pragma once 

// /!\ Attention, si on utilise le MIDI, l'usb servira qu'a ça. Faudra un adaptateur si on veut faire du serial logging.
#define ENABLE_SERIAL_LOGGING 1
#define USE_DAISY 1
#define SerialMessagingGenial 1

#if ENABLE_SERIAL_LOGGING
#define SerialPeaking 0
#define CPU_METER 1
#define USE_MIDI_USB 1
#else
#endif

/**
 * @brief Fonction utilitaire pour borner une valeur (clamping).
 * @param value Valeur à contraindre.
 * @param min Limite inférieure.
 * @param max Limite supérieure.
 * @return La valeur contrainte entre min et max.
 */
static inline float clampf(float value, float min, float max){
    return (value < min) ? min : (value > max) ? max : value;
}

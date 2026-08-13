/**
 * @file midi_processing.h
 * @brief Gestion de la réception et du traitement des messages MIDI.
 *
 * ==============================================================================
 * ARCHITECTURE DU ROUTAGE MIDI 
 * ==============================================================================
 * La réception MIDI se divise en deux grandes familles :
 * 
 * 1️ LES CONTRÔLES PAR CORDE (Le "Channel" MIDI détermine la corde : 0 à 5)
 * ------------------------------------------------------------------------------
 * [A] LE PÉDALIER (ROUTAGE) : "Quelle pédale dans quelle prise ?"
 *     - CC 20 : Assigner le Slot 1 (active_effect)
 *     - CC 21 : Assigner le Slot 2 (active_effect_bonus)
 *     - CC 22 : Assigner le Slot 3 (active_effect_bonus_bonus)
 *     -> La Valeur du CC définit la pédale : 
 *        [0=Rien | 1=Delay | 2=Disto | 3=Octaveur | 4=Tremolo | 5=EQ]
 * 
 * [B] LES PARAMÈTRES : "Tourner les boutons de la pédale"
 *     - CC 10 à 15 : Boutons du Delay
 *     - CC 50 à 55 : Boutons de la Distorsion
 *     - CC 70 à 75 : Boutons de l'EQ
 *     - CC 90 à 95 : Boutons de l'Octaveur (Octaveur)
 *     - CC 110 à 115 : Boutons du Trémolo
 * 
 * [C] RESET DES SLOTS
 *     - CC 9  : Vide les 3 slots de la corde (True Bypass).
 * 
 * 2️ LES CONTRÔLES GLOBAUX (Le "Channel" MIDI est ignoré)
 * ------------------------------------------------------------------------------
 *     - CC 0 à 5 : Mute (Coupe-son) de la corde 0 à la corde 5.
 *     - CC 126 : BYPASS GLOBAL (Destruction totale du routage sur les 6 cordes).
 * ==============================================================================
 */

// On inclut main.h qui contient toutes les déclarations extern nécessaires
// (midi, strings, hw, octaveur_effects, etc.)
#include "main.h" 
#include "Utils.h"


// static void OnControlChange(byte channel, byte control, byte value) {

#if USE_MIDI_USB
/**
 * @brief Traite tous les messages MIDI entrants en attente.
 * 
 * Lit le buffer MIDI USB, décode les Control Changes (CC) et applique
 * les modifications (Dry/Wet, Volume, Temps, etc.) aux instances des
 * classes d'effets DSP selon la matrice de routage MIDI.
 */
static void HandleMidiMessages(){
    // Si des messages sont en attente, on allume la LED.
    if(midi.HasEvents()) {
        hw.seed.SetLed(true);
    }

    // On boucle pour traiter tous les messages MIDI en attente, pas juste un seul.
    while(midi.HasEvents()) {
        auto event = midi.PopEvent();

        // 1. On s'assure que le message est bien un Control Change (CC)
        if(event.type != ControlChange) {
            continue; // On ignore les autres types de messages
        }
        
        // 2. On extrait les données de manière sécurisée avec l'API libDaisy
        ControlChangeEvent cc = event.AsControlChange();
        const int channel      = event.channel;
        const int control      = cc.control_number;
        const float value_norm = cc.value / 127.0f;

        // --- 1. CONTRÔLES SPÉCIFIQUES À UNE CORDE (via le canal MIDI 0-5) ---
        if (channel >= 0 && channel < 6) {
            const int corde = channel;

            // Paramètres d'effets (envoyés par les sliders)
            
            // Delay (CC 10-15)
            // Potards : 0=Mode, 1=Time/Tempo, 2=Tap, 3=Subdivision, 4=Feedback, 5=Volume/Mix
            if (control >= 10 && control <= 15) {
                int potard = control - 10;
                delay_effects[corde]->setParameter(potard, value_norm);
            }
            // Distortion (CC 50-55)
            // Potards : 0=Mix (Ignoré), 1=Gain, 2=DistoMode (Type d'écrêtage)
            else if (control >= 50 && control <= 55) { 
                int potard = control - 50;
                disto_effects[corde]->setParameter(potard, value_norm);
            }
            // Octaveur / Octaveur (CC 90-95)
            // Potards : 0=Mix, 1=Octave, etc.
            else if (control >= 90 && control <= 95) {
                int potard = control - 90;
                octaveur_effects[corde]->setParameter(potard, value_norm);
            }
            // Tremolo (CC 110-115)
            // Potards : 0=Mix, 1=Rate (Vitesse du LFO), 2=Depth (Profondeur)
            else if (control >= 110 && control <= 115) {
                int potard = control - 110;
                tremolo_effects[corde]->setParameter(potard, value_norm);
            }
            // Equalizer (CC 70-75)
            // Potards : 0=Low, 1=Mid, 2=High
            else if (control >= 70 && control <= 75) {
                int potard = control - 70;
                eq_effects[corde]->setParameter(potard, value_norm);
            }
            // NoiseGate (CC 120-122)
            else if (control >= 120 && control <= 122) {
                int potard = control - 120;
                noisegate_effects[corde]->setParameter(potard, value_norm);
            }
            // Compresseur (CC 100-104)
            else if (control >= 100 && control <= 104) {
                int potard = control - 100;
                compresseur_effects[corde]->setParameter(potard, value_norm);
            }
            // --- A. RESET DES SLOTS DE LA CORDE ---
            // CC 9 : Bouton pour vider complètement les slots d'effets de cette corde
            else if (control == 9) {
                // Si la valeur est <= 63, on déclenche le True Bypass
                if (cc.value <= 63) {
                    // On débranche physiquement TOUTES les pédales des 3 slots pour retrouver le son clair
                    strings[corde].type = EffectType::Bypass;
                    strings[corde].active_effect = nullptr;
                    strings[corde].active_effect_bonus = nullptr;
                    strings[corde].active_effect_bonus_bonus = nullptr;
                }
            }
            // --- B. ROUTAGE DES EFFETS (Assignation aux Slots) ---
            // CC 20, 21, 22 : Permet d'assigner dynamiquement un effet à un slot spécifique (1, 2 ou 3)
            else if (control >= 20 && control <= 22) {
                Effect* selected_effect = nullptr;
                if (cc.value == 1) selected_effect = delay_effects[corde];
                else if (cc.value == 2) selected_effect = disto_effects[corde];
                else if (cc.value == 3) selected_effect = octaveur_effects[corde];
                else if (cc.value == 4) selected_effect = tremolo_effects[corde];
                else if (cc.value == 5) selected_effect = eq_effects[corde];
                else if (cc.value == 6) selected_effect = noisegate_effects[corde];
                else if (cc.value == 7) selected_effect = compresseur_effects[corde];

                if (control == 20) strings[corde].active_effect = selected_effect;
                else if (control == 21) strings[corde].active_effect_bonus = selected_effect;
                else if (control == 22) strings[corde].active_effect_bonus_bonus = selected_effect;
            }
        }
        
        // --- 2. CONTRÔLES GLOBAUX (le canal est ignoré ou vaut 0) ---

        // Mute par corde (CC 0-5)
        // Coupe totalement le son de la corde 
        if (control >= 0 && control <= 5) {
            int corde = control;
            bool isMuted = (cc.value > 63); 
            
            if (isMuted) {
                // On active le drapeau Mute. Dans AudioCallback, si type == Mute, il met out = 0.0f
                strings[corde].type = EffectType::Mute;
            } else {
                // UN-MUTE : On retire le drapeau Mute. 
                // Comme on n'a jamais mis les active_effect à nullptr, les pédales 
                // qui étaient branchées reprennent leur travail !
                strings[corde].type = EffectType::Bypass; 
            }
        }
        // L'ancien système de Bypass individuel (CC 48, 88...) a été supprimé car remplacé
        // par le système de Slots dynamiques (CC 20, 21, 22) utilisé par la nouvelle interface graphique.
        

        // Bypass Global pour toutes les cordes (CC 126)
        else if (control == 126) {
            bool isBypassed = (cc.value > 63);
            if(isBypassed) {
                // Destruction totale du Pedalboard sur toutes les cordes.
                for (int i = 0; i < 6; i++) {
                    strings[i].type = EffectType::Bypass;
                    strings[i].active_effect = nullptr;
                    strings[i].active_effect_bonus = nullptr;
                    strings[i].active_effect_bonus_bonus = nullptr;
                }
            }
            // NOTE : Il est physiquement impossible de faire un "Un-Bypass Global" ici.
            // Pourquoi ? Parce qu'en mettant tout à nullptr au-dessus, l'ordinateur a fait un trou de mémoire.
            // Il a complètement oublié quelles pédales étaient branchées avant le Bypass.
            // Pour sortir du Bypass Global, l'utilisateur DOIT renvoyer les Control Change d'assignation (CC 20, 21, 22) depuis son interface.
        }
    }
}
#endif

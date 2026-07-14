// On inclut main.h qui contient toutes les déclarations extern nécessaires
// (midi, strings, hw, earth_effects, etc.)
// Pas besoin de re-déclarer quoi que ce soit ici.
#include "main.h" 
#include "Utils.h"


// static void OnControlChange(byte channel, byte control, byte value) {

#if USE_MIDI_USB
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

        // --- A. CONTRÔLES SPÉCIFIQUES À UNE CORDE (via le canal MIDI 0-5) ---
        if (channel >= 0 && channel < 6) {
            const int corde = channel;

            // Paramètres d'effets (envoyés par les sliders)
            // Delay (CC 10-15)
            if (control >= 10 && control <= 15) {
                int potard = control - 10;
                strings[corde].type = EffectType::Delay;
                strings[corde].active_effect = delay_effects[corde];
                delay_effects[corde]->setParameter(potard, value_norm);
            }
            // Distortion (CC 50-55)
            else if (control >= 50 && control <= 55) { 
                int potard = control - 50;
                strings[corde].type = EffectType::Disto;
                strings[corde].active_effect = disto_effects[corde]; 
                disto_effects[corde]->setParameter(potard, value_norm);
            }
            // Earth (CC 90-95)
            else if (control >= 90 && control <= 95) {
                int potard = control - 90;
                strings[corde].type = EffectType::Earth;
                strings[corde].active_effect = earth_effects[corde];
                earth_effects[corde]->setParameter(potard, value_norm);
            }
        }
        
        // --- B. CONTRÔLES GLOBAUX (le canal est ignoré ou vaut 0) ---

        // Mute par corde (CC 0-5)
        if (control >= 0 && control <= 5) {
            int corde = control;
            bool isMuted = (cc.value > 63); 
            
            if (isMuted) {
                strings[corde].type = EffectType::Bypass;
                strings[corde].active_effect = nullptr;
            }
            // NOTE: La logique pour réactiver l'effet ("un-mute") n'est pas présente.
        }
        
        // Bypass par effet (CC 48, 88, 89)
        bool isBypassed = (cc.value > 63);
        if (control == 48 || control == 88 || control == 89) {
            EffectType typeToBypass;
            if (control == 48) typeToBypass = EffectType::Delay;
            // else if (control == 88) typeToBypass = EffectType::Drive;
            else typeToBypass = EffectType::Earth;

            if (isBypassed) {
                for (int i = 0; i < 6; i++) {
                    if (strings[i].type == typeToBypass) {
                        strings[i].type = EffectType::Bypass;
                        strings[i].active_effect = nullptr;
                    }
                }
            }
            // NOTE: La logique pour sortir du bypass de l'effet n'est pas présente.
        }
        
        // Bypass Global (CC 126)
        else if (control == 126) {
            if(isBypassed) {
                for (int i = 0; i < 6; i++) {
                    strings[i].type = EffectType::Bypass;
                    strings[i].active_effect = nullptr;
                }
            }
            // NOTE: La logique pour sortir du bypass global n'est pas présente.
        }
        
        // Changement d'effet (CC 9) - non utilisé
        else if (control == 9) {
            // Emplacement pour une future implémentation.
        }
    }
}
#endif
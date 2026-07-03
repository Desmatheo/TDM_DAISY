// On inclut main.h qui contient toutes les déclarations extern nécessaires
// (midi, strings, hw, earth_effects, etc.)
// Pas besoin de re-déclarer quoi que ce soit ici.
#include "main.h" 


// static void OnControlChange(byte channel, byte control, byte value) {

static void MidiHandlingDeLaMort(){
    auto event = midi.PopEvent();

    // 1. On s'assure que le message est bien un Control Change (CC)
    if(event.type == ControlChange) {
        
        // 2. On extrait les données de manière sécurisée avec l'API libDaisy
        ControlChangeEvent cc = event.AsControlChange();
        const int control   = cc.control_number;
        const float value   = cc.value / 127.0f;

        // --- EFFETS ---
        if (control >= 10 && control <= 45) {
            int ccRelatif = control - 10;
            int corde = ccRelatif / 6;
            int potard = ccRelatif % 6;
            if (corde >= 0 && corde < 6) {
                strings[corde].type = EffectType::Delay;
                strings[corde].active_effect = delay_effects[corde];
                delay_effects[corde]->setParameter(potard, value);
            }
        }
        else if (control >= 50 && control <= 85) { 
            int ccRelatif = control - 50;
            int corde = ccRelatif / 6;
            int potard = ccRelatif % 6;
            if (corde >= 0 && corde < 6) {
                strings[corde].type = EffectType::Drive;
                // strings[corde].active_effect = earth_effects[corde]; // Décommenter si branché
                // earth_effects[corde]->setParameter(potard, value);
            }
        }
        else if (control >= 90 && control <= 125) {
            int ccRelatif = control - 90;
            int corde = ccRelatif / 6;
            int potard = ccRelatif % 6;
            if (corde >= 0 && corde < 6) {
                strings[corde].type = EffectType::Earth;
                strings[corde].active_effect = earth_effects[corde];
                earth_effects[corde]->setParameter(potard, value);
            }
        }
        
        // --- MUTE PAR CORDE ---
        else if (control >= 0 && control <= 5) {
            // CORRECTION : control correspond directement à l'index de la corde (0 à 5)
            int corde = control;
            
            bool isBypassed = (cc.value > 63); 
            
            if (isBypassed) {
                strings[corde].type = EffectType::Bypass;
                strings[corde].active_effect = nullptr;
            }
        }
        
        // --- BYPASS GLOBAL ---
        else if (control == 126) {
            bool isBypassed = (cc.value > 63);
            if(isBypassed) {
                for (int i = 0; i < 6; i++) {
                    strings[i].type = EffectType::Bypass;
                    strings[i].active_effect = nullptr;
                }
            }
        }
        
        // --- CHANGEMENT D'EFFET (CC 9 depuis Python) ---
        else if (control == 9) {
            // Tu as codé l'envoi d'un CC 9 dans changer_effet() en Python. 
            // Tu peux récupérer l'index de l'effet ici si tu veux changer 
            // le routage global ou préparer des buffers.
        }
    }
}
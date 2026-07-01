// On inclut main.h qui contient toutes les déclarations extern nécessaires
// (midi, strings, hw, earth_effects, etc.)
// Pas besoin de re-déclarer quoi que ce soit ici.
#include "main.h" 


// static void OnControlChange(byte channel, byte control, byte value) {

static void MidiHandlingDeLaMort(){
    auto event = midi.PopEvent();

    const int control   = event.data[0];
    const float value   = event.data[1] / 127.0f;


    if (control >= 10 && control <= 45) {
        int ccRelatif = control - 10;
        int corde = ccRelatif / 6;
        int potard = ccRelatif % 6;
        if (corde >= 0 && corde < 6) {
            strings[corde].type = EffectType::Delay;
            strings[corde].active_effect = nullptr;
            // strings[corde].active_effect = mesDelays[corde].setEnabled(true);

            // mesDelays[corde].setParameter(potard, value);
        }
    }
    else if (control >= 50 && control <= 85) { 
        int ccRelatif = control - 50;
        int corde = ccRelatif / 6;
        int potard = ccRelatif % 6;
        if (corde >= 0 && corde < 6) {
            strings[corde].type = EffectType::Drive;
            strings[corde].active_effect = nullptr;
            // strings[corde].active_effect = earth_effects[corde];

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
    else if (control >= 0 && control <= 5) {
        int ccRelatif = control;
        int corde = ccRelatif / 6;
        int potard = ccRelatif % 6;

        // La valeur > 63 suppose que 127 = Bypass ON (son coupé) et 0 = Bypass OFF.
        // (Si ton bouton envoie l'inverse pour "Allumer l'effet", remplace par "value < 64")
        bool isBypassed = (value > 63); 
        
        strings[corde].type = EffectType::Bypass;
        strings[corde].active_effect = nullptr;

    }
    else if (control == 126) {
        bool isBypassed = (value > 63);
        
        for (int i = 0; i < 6; i++) {
            strings[i].type = EffectType::Bypass;
            strings[i].active_effect = nullptr;
        }
    }
    
}

#include <cmath>
#include "daisy_core.h"
#include "daisy_tdm_slave.h"
#include "../EffetEarth/Earth.h" // Inclusion de l'unique effet conservé
#include "Utils.h"


// static void OnControlChange(byte channel, byte control, byte value) {

static void OnControlChange(int channel, uint8_t control, uint8_t value){
    float valNorm = value / 127.0f;

    // --- TRANCHE 1 : DELAY (CC 10 à 45) ---
    if (control >= 10 && control <= 45) {
        int ccRelatif = control - 10;
        int corde = ccRelatif / 6;  // Division entière -> Donne la corde (0 à 5)
        int potard = ccRelatif % 6; // Reste -> Donne le bouton (0 à 5)

    }
    
    // --- TRANCHE 2 : DISTORTION (CC 50 à 85) ---
    else if (control >= 50 && control <= 85) { 
        int ccRelatif = control - 50;
        int corde = ccRelatif / 6;
        int potard = ccRelatif % 6;

    }

    // --- TRANCHE 3 : EARTH (CC 90 à 125 - Remplace la Reverb) ---
    else if (control >= 90 && control <= 125) {
        int ccRelatif = control - 90;
        int corde = ccRelatif / 6;
        int potard = ccRelatif % 6;

    }

    // --- TRANCHE 4 : BYPASS DES CORDES INDIVIDUELLES (CC 0 à 5) ---
    else if (control >= 0 && control <= 5) {
        int ccRelatif = control - 90;
        int corde = ccRelatif / 6;
        int potard = ccRelatif % 6;
        
    }
    
    // --- TRANCHE 5 : BYPASS GLOBAL (CC 126) ---
    else if (control == 126) {


    }
}
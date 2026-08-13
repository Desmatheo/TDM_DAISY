// Octaveur Reverbscape

#include "Octaveur.h"

#include <span>
#define USE_DAISY 1

#if USE_DAISY
#include "daisy_seed.h"
#include "daisysp.h"
#include <q/support/literals.hpp>
namespace q = cycfi::q;
using namespace q::literals;

using namespace daisy;
using namespace daisysp;

#endif

OctaveurEffect::OctaveurEffect(float sampleRate)
    : octave(sampleRate / resample_factor)
#if eq_ON
      , eq1(-11, 140_Hz, sampleRate),
      eq2(5, 160_Hz, sampleRate)
#endif
{
    for (int j = 0; j < 6; ++j) {
        buff[j] = 0.0f;
        buff_out[j] = 0.0f;
    }

    setMix(1.0f);
    setOctaveMode(1);

#if od_ON
    overdrive.Init();
    overdrive.SetDrive(0.4f);
    current_ODswell = 0.4f;
#endif
}

void OctaveurEffect::update(const float** in, float** out, int idx) {
    float inputL;
    float inputR;

    anti_denormal = -anti_denormal;
    inputL = inputR = in[0][idx] + anti_denormal; // Anti-denormal

    buff[bin_counter] = inputL;
    
    // On recupère 6 samples, on lance le traitement !
    if (bin_counter > 4) {
        if (wetMix > 0.01f) {
            // Création d'une fenêtre virtuelle (span) sur nos 6 samples collectés
            std::span<const float, resample_factor> in_chunk(&(buff[0]), resample_factor);
            
            // DÉCIMATION : On écrase les 6 samples en 1 seul (divise le CPU par 6)
            const auto sample = decimate2(in_chunk); 

            float octave_mix = 0.0;

            // TRAITEMENT LOURD : Le super-sample est envoyé au cerveau (OctaveGenerator)
            octave.update(sample, effect_mode);

            // RÉCUPÉRATION : On demande au cerveau le résultat selon le mode choisi
            // On multiplie par 6.0f pour compenser la perte de volume causée par la décimation
            if (effect_mode == 1) {
                float raw_up = octave.up1() * 6.0f;
                octave_mix += raw_up; 
            }
            if (effect_mode == 2) octave_mix += octave.down1() * 6.0f;
            if (effect_mode == 3) octave_mix += octave.down2() * 6.0f;

            // INTERPOLATION : On étire l'unique super-sample calculé pour recréer 6 samples lissés
            auto out_chunk = interpolate(octave_mix);
            
            // DISTRIBUTION : On range les 6 samples dans la boîte de sortie (buff_out)
            for (size_t j = 0; j < out_chunk.size(); ++j) {
#if eq_ON
                buff_out[j] = eq2(eq1(out_chunk[j]));
#else
                buff_out[j] = out_chunk[j];
#endif

            }
        }
    }

    // Avance le compteur de temps (0 à 5)
    bin_counter += 1;
    if (bin_counter > 5) bin_counter = 0;

    // Le signal d'effet est le son de l'octaver (ou 0 si désactivé)
    float octave_signal = buff_out[bin_counter];

    float effect_output = octave_signal;

    // Application de l'overdrive si activé
#if od_ON
    // Really cool sound when the low octave is overdriven, like epic sci fi blade runner
    effect_output = overdrive.Process(effect_output * 0.25f) * (1.0f - (current_ODswell * current_ODswell * 2.8f - 0.1296f));
#endif

    // Mixage final dry/wet pour cet effet de corde
    out[0][idx] = (inputL * dryMix + effect_output * wetMix) * volume;
    out[1][idx] = out[0][idx];
}

// --- Implémentation des Setters Spécifiques ---

void OctaveurEffect::setMix(float mix) {
    float clampedMix = clampf(mix, 0.0f, 1.0f);
    wetMix = sinf(clampedMix * (float)M_PI_2);
    dryMix = cosf(clampedMix * (float)M_PI_2);
}

void OctaveurEffect::setVolume(float vol)
{
    volume = clampf(vol, 0.0f, 1.0f);
}

void OctaveurEffect::setOctaveMode(int mode) {
    effect_mode = mode;
}


void OctaveurEffect::setParameter(int param_id, float value) {
    switch (param_id){
        case 0 : 
            setMix(value);
            break;
        case 1 :  
            if (value > 0.66f) setOctaveMode(1);
            else if ((0.66f > value) && (value > 0.33f)) setOctaveMode(2);
            else setOctaveMode(3);
            break; 
        case 5 : 
            setVolume(value);
            break;
        default:
#if !USE_DAISY
            Serial.print("Parametre invalide: ");
            Serial.println(param_id);
#endif
            break;
    };
}

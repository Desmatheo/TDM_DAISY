#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H

#include "main.h" 


extern DaisyTdmSlave hw;

extern CpuLoadMeter loadMeter;


// ================================================================
// Diagnostics shared between the audio callback (IRQ context) and the
// main loop. Written by the callback, read + reset by main().
// Never print from the audio callback: it runs in an interrupt and
// USB logging there can block the whole audio engine.
// ================================================================
struct AudioDiagnostics
{
    volatile uint32_t callback_count = 0;
    volatile float    in_peak[DaisyTdmSlave::kNumInputs] = {0};

    void ResetPeaks()
    {
        for(size_t ch = 0; ch < DaisyTdmSlave::kNumInputs; ch++)
            in_peak[ch] = 0.f;
    }
};

static AudioDiagnostics audio_diag;

// ================================================================
// Audio callback -- runs at 48 kHz / blocksize (1500 Hz for block 32),
// clocked by the Teensy TDM master.
//
//   in[0..5]  : the 6 hexaphonic channels sent by the Teensy (slots 0..5)
//   in[6..7]  : unused slots (silence as long as the Teensy sends nothing)
//   out[0..7] : the 8 channels sent back to the Teensy (slots 0..7)
//
// Routage actuel : Pass-through Hexaphonique (6 canaux) depuis l'USB (Teensy)
//   out[0..5] = in[0..5] (Canaux 1 à 6)
//   Les autres sorties sont mises au silence.
// ================================================================
// ================================================================
// Variables pour le Noise Gate (6 canaux)
// ================================================================
static float env[6] = {0.0f};
static float gate_gain[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

// Paramètres du Noise Gate (à ajuster selon la guitare et la diaphonie)
static const float NOISE_GATE_THRESHOLD = 0.010f; // Baissé (était 0.025) car trop fort pour le jeu en direct
static const float NOISE_GATE_ATTACK = 0.005f;    // TRÈS LENT (était 0.1) : l'enveloppe ne réagit pas aux impulsions ultra courtes (le coup de médiator)
static const float NOISE_GATE_RELEASE = 0.002f;   // Vitesse de chute de l'enveloppe
static const float NOISE_GATE_SMOOTH = 0.01f;     // Lissage de l'ouverture/fermeture pour éviter les clics

static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{

#if CPU_METER
    loadMeter.OnBlockStart();
#endif


    audio_diag.callback_count++;

    for(size_t i = 0; i < size; i++)
    {
        // Traitement DSP par corde
        for (size_t j = 0; j < 6; j++) 
        {
            if (strings[j].type == EffectType::Mute){
                out[j][i] = 0.0f;
            }
            else {
                float in_sample = in[j][i];
                float abs_in = fabsf(in_sample);
                
                // ==========================================
                // NOISE GATE 
                // ==========================================
                // 1. Suivi d'enveloppe (Envelope Follower)
                if (abs_in > env[j]) {
                    env[j] += NOISE_GATE_ATTACK * (abs_in - env[j]); // Attaque rapide
                } else {
                    env[j] += NOISE_GATE_RELEASE * (abs_in - env[j]); // Relâchement lent
                }

                // 2. Détermination de la cible du gain (Ouvert = 1.0, Fermé = 0.0)
                float target_gain = (env[j] > NOISE_GATE_THRESHOLD) ? 1.0f : 0.0f;
                
                // 3. Lissage du gain pour éviter le "zipper noise" (clics)
                gate_gain[j] += NOISE_GATE_SMOOTH * (target_gain - gate_gain[j]); 
                
                // 4. Application du gain au signal d'entrée
                in_sample *= gate_gain[j];
                // ==========================================

                float in_arr[2][1] = {{in_sample}, {in_sample}};
                const float* in_ptrs[2] = {in_arr[0], in_arr[1]};
                
                float out_arr[2][1] = {{0.0f}, {0.0f}};
                float* out_ptrs[2] = {out_arr[0], out_arr[1]};
                float out_sample = 0.0f;

                float current_sample = in_sample;

                // Stage 1
                if (strings[j].active_effect != nullptr) {
                    in_arr[0][0] = current_sample;
                    in_arr[1][0] = current_sample;
                    strings[j].active_effect->update(in_ptrs, out_ptrs, 0);
                    current_sample = out_arr[0][0];
                }

                // Stage 2 (seulement si l'effet est différent du Slot 1)
                if (strings[j].active_effect_bonus != nullptr && 
                    strings[j].active_effect_bonus != strings[j].active_effect) {
                    in_arr[0][0] = current_sample;
                    in_arr[1][0] = current_sample;
                    strings[j].active_effect_bonus->update(in_ptrs, out_ptrs, 0);
                    current_sample = out_arr[0][0];
                }

                // Stage 3 (seulement si l'effet est différent des Slots 1 et 2)
                if (strings[j].active_effect_bonus_bonus != nullptr && 
                    strings[j].active_effect_bonus_bonus != strings[j].active_effect &&
                    strings[j].active_effect_bonus_bonus != strings[j].active_effect_bonus) {
                    in_arr[0][0] = current_sample;
                    in_arr[1][0] = current_sample;
                    strings[j].active_effect_bonus_bonus->update(in_ptrs, out_ptrs, 0);
                    current_sample = out_arr[0][0];
                }

                out_sample = current_sample;

                out[j][i] = out_sample;
            }
        }

        out[6][i] = 0.0f;
        out[7][i] = 0.0f;
        
    }

#if CPU_METER
    loadMeter.OnBlockEnd();
#endif


}

#endif // AUDIO_PROCESSING_H

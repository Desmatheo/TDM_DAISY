/**
 * @file audio_processing.h
 * @brief Gestion du traitement audio principal (callback) pour la carte Daisy.
 */

#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H

#include "main.h" 

extern DaisyTdmSlave hw;
extern CpuLoadMeter loadMeter;

/**
 * @struct AudioDiagnostics
 * @brief Structure de diagnostic partagée entre la callback audio (contexte IRQ) et la boucle principale.
 * 
 * Modifiée par la callback, lue et réinitialisée par le main().
 * @warning Ne jamais utiliser de fonctions d'affichage (print) depuis la callback audio
 * car elle s'exécute dans une interruption et cela bloquerait le moteur audio.
 */
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

/**
 * @brief Fonction de rappel (Callback) audio principale.
 * 
 * Cette fonction s'exécute à la fréquence d'échantillonnage divisée par la taille du bloc
 * (par exemple 1378 Hz pour 44.1 kHz avec un bloc de 32), cadencée par le maître TDM Teensy.
 *
 * @param in[0..5] Les 6 canaux hexaphoniques envoyés par la Teensy (slots 0..5).
 * @param in[6..7] Slots inutilisés (silence si la Teensy n'envoie rien).
 * @param out[0..7] Les 8 canaux renvoyés vers la Teensy.
 *
 * @note Routage actuel : Pass-through Hexaphonique (6 canaux) avec traitement des effets
 * en série. Les sorties non utilisées sont mises au silence.
 */
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
                // NOISE GATE (Porte de bruit)
                // ==========================================
                // 1. Suivi d'enveloppe (Filtre Passe-Bas IIR)
                // On lisse l'onde brute pour obtenir une courbe de volume global (l'enveloppe).
                // Formule exponentielle : Nouvelle_Position = Ancienne + Vitesse * (Cible - Ancienne)
                if (abs_in > env[j]) {
                    env[j] += NOISE_GATE_ATTACK * (abs_in - env[j]); // L'enveloppe grimpe très vite (Coup de médiator)
                } else {
                    env[j] += NOISE_GATE_RELEASE * (abs_in - env[j]); // L'enveloppe redescend en douceur pour lier les notes
                }

                // 2. Cible du gain : Si l'enveloppe est sous le seuil, la porte doit se fermer (0.0).
                float target_gain = (env[j] > NOISE_GATE_THRESHOLD) ? 1.0f : 0.0f;
                
                // 3. Lissage du gain : On empêche la porte de se claquer d'un coup (ce qui ferait un 'clic').
                // On utilise la même formule exponentielle pour glisser doucement de 1.0 vers 0.0.
                gate_gain[j] += NOISE_GATE_SMOOTH * (target_gain - gate_gain[j]); 
                
                // 4. VCA : On coupe ou on laisse passer le son final.
                in_sample *= gate_gain[j];
                // ==========================================

                // ==========================================
                // LE WRAPPER / ADAPTATEUR STÉRÉO
                // ==========================================
                // Problème : Nos effets DSP (delay, tremolo...) attendent un énorme tableau Stéréo (const float**).
                // Mais ici, nous ne traitons qu'une seule corde et un seul sample (in_sample) !
                // Solution : On crée des "tableaux déguisements" temporaires sur la pile (Stack).
                
                // On crée un tableau 2D de taille [2][1] (Gauche et Droite, 1 sample chacun)
                float in_arr[2][1] = {{in_sample}, {in_sample}};
                // On crée un tableau de pointeurs qui pointe vers nos canaux pour correspondre à la signature de update()
                const float* in_ptrs[2] = {in_arr[0], in_arr[1]};
                
                // On prépare la boîte de réception (remplie de silence 0.0f) et ses pointeurs
                float out_arr[2][1] = {{0.0f}, {0.0f}};
                float* out_ptrs[2] = {out_arr[0], out_arr[1]};
                
                float out_sample = 0.0f;
                float current_sample = in_sample; // Le sample prêt à traverser le pedalboard
                // ==========================================

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

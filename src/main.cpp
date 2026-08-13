/**
 * @file main.cpp
 * @brief Point d'entrée principal pour le firmware Daisy Seed (Esclave TDM).
 *
 * Ce fichier initialise le matériel (TDM, MIDI sur USB), instancie les
 * blocs d'effets audio (Octaveur, Delay, Tremolo, Disto, Equalizer) pour chaque canal,
 * et contient la boucle principale (main loop) qui gère l'envoi de statistiques
 * CPU et MIDI vers l'hôte (Teensy ou PC).
 */

#include "Utils/main.h"

using namespace daisy; 

#if USE_MIDI_USB
MidiUsbHandler midi;
#endif

#if CPU_METER
CpuLoadMeter loadMeter;
#endif

DaisyTdmSlave hw;

OctaveurEffect* octaveur_effects[6];
DelayEffect* delay_effects[6];
TremoloEffect* tremolo_effects[6];
DistoEffect* disto_effects[6];
EqualizerEffect* eq_effects[6];
NoiseGateEffect* noisegate_effects[6];
CompresseurEffect* compresseur_effects[6];

StringUtil strings[] = {
    StringUtil(EffectType::Bypass, 0),
    StringUtil(EffectType::Bypass, 1),
    StringUtil(EffectType::Bypass, 2),
    StringUtil(EffectType::Bypass, 3),
    StringUtil(EffectType::Bypass, 4),
    StringUtil(EffectType::Bypass, 5)
};

// ================================================================
// Allocation statique de la mémoire pour les objets d'effets
// (Evite l'utilisation du tas (heap) dynamique pour des raisons de performance)
// ================================================================
alignas(OctaveurEffect) static uint8_t octaveur_mem[6 * sizeof(OctaveurEffect)];
alignas(DelayEffect) static uint8_t delay_mem[6 * sizeof(DelayEffect)];
alignas(TremoloEffect) static uint8_t tremolo_mem[6 * sizeof(TremoloEffect)];
alignas(DistoEffect) static uint8_t disto_mem[6 * sizeof(DistoEffect)];
alignas(EqualizerEffect) static uint8_t eq_mem[6 * sizeof(EqualizerEffect)];
alignas(NoiseGateEffect) static uint8_t noisegate_mem[6 * sizeof(NoiseGateEffect)];
alignas(CompresseurEffect) static uint8_t compresseur_mem[6 * sizeof(CompresseurEffect)];

#define STATUS_PERIOD_MS 1000

// ================================================================
/**
 * @brief Point d'entrée du programme.
 * 
 * Initialise les différents sous-systèmes de la carte Daisy Seed,
 * instancie les effets DSP et démarre la boucle audio et de contrôle.
 */
int main(void)
{
    hw.Init(true);

    // Initialisation série USB non bloquante (la carte tourne même
    // si aucun moniteur série n'est connecté).

#if ENABLE_SERIAL_LOGGING
    hw.seed.StartLog(false);
#if CPU_METER
    loadMeter.Init(DaisyTdmSlave::kSampleRate, DaisyTdmSlave::kBlockSize);
#endif 
#endif

#if USE_MIDI_USB
    MidiUsbHandler::Config midi_cfg;
    midi_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    midi.Init(midi_cfg);
#endif

    memset(octaveur_mem, 0, 6 * sizeof(OctaveurEffect));
    memset(delay_mem, 0, 6 * sizeof(DelayEffect));
    memset(tremolo_mem, 0, 6 * sizeof(TremoloEffect));
    memset(disto_mem, 0, 6 * sizeof(DistoEffect));
    memset(eq_mem, 0, 6 * sizeof(EqualizerEffect));
    memset(noisegate_mem, 0, 6 * sizeof(NoiseGateEffect));
    memset(compresseur_mem, 0, 6 * sizeof(CompresseurEffect));

    for (int j = 0; j < 6; j++){ 
        octaveur_effects[j] = new(&octaveur_mem[j * sizeof(OctaveurEffect)]) OctaveurEffect((float)DaisyTdmSlave::kSampleRate);
        delay_effects[j] = new(&delay_mem[j * sizeof(DelayEffect)]) DelayEffect((float)DaisyTdmSlave::kSampleRate);
        tremolo_effects[j] = new(&tremolo_mem[j * sizeof(TremoloEffect)]) TremoloEffect((float)DaisyTdmSlave::kSampleRate);
        disto_effects[j] = new(&disto_mem[j * sizeof(DistoEffect)]) DistoEffect((float)DaisyTdmSlave::kSampleRate);
        eq_effects[j] = new(&eq_mem[j * sizeof(EqualizerEffect)]) EqualizerEffect((float)DaisyTdmSlave::kSampleRate);
        noisegate_effects[j] = new(&noisegate_mem[j * sizeof(NoiseGateEffect)]) NoiseGateEffect((float)DaisyTdmSlave::kSampleRate);
        compresseur_effects[j] = new(&compresseur_mem[j * sizeof(CompresseurEffect)]) CompresseurEffect((float)DaisyTdmSlave::kSampleRate);
    }

    // Avec un block size de 32 à 44.1 kHz (fréquence par défaut de la Teensy), 
    // la callback s'exécute environ 1378 fois par seconde.
    // 0 appels/s signifie qu'il n'y a pas de BCLK/FS depuis le Teensy (vérifier le câblage).
    uint32_t last_status = System::GetNow();

    hw.StartAudio(AudioCallback);

    while(1)
    {

#if USE_MIDI_USB
        midi.Listen();
        // HandleMidiMessages s'occupe de vérifier s'il y a des événements
        // et d'allumer la LED. On l'appelle à chaque tour.
        HandleMidiMessages();
#endif

        if(System::GetNow() - last_status >= STATUS_PERIOD_MS)
        {
            last_status = System::GetNow();

            const uint32_t cb_per_s = audio_diag.callback_count;
            audio_diag.callback_count = 0;
            (void)cb_per_s;
            float peaks[DaisyTdmSlave::kNumInputs];
            for(size_t ch = 0; ch < DaisyTdmSlave::kNumInputs; ch++)
                peaks[ch] = audio_diag.in_peak[ch];
            audio_diag.ResetPeaks();

#if ENABLE_SERIAL_LOGGING
#if SerialPeaking
            hw.seed.PrintLine("callbacks/s: %lu (attendu ~%d)",
                              cb_per_s,
                              (int)(DaisyTdmSlave::kSampleRate
                                    / DaisyTdmSlave::kBlockSize));

            hw.seed.PrintLine(
                "Peaks IN - C1/2: " FLT_FMT3 " / " FLT_FMT3 " | C3/4: " FLT_FMT3 " / " FLT_FMT3 " | C5/6: " FLT_FMT3 " / " FLT_FMT3 " | C7/8: " FLT_FMT3 " / " FLT_FMT3 ,
                FLT_VAR3(peaks[0]), FLT_VAR3(peaks[1]),
                FLT_VAR3(peaks[2]), FLT_VAR3(peaks[3]),
                FLT_VAR3(peaks[4]), FLT_VAR3(peaks[5]), 
                FLT_VAR3(peaks[6]), FLT_VAR3(peaks[7])
            );
#endif
#if CPU_METER
            // Alerte Surcharge CPU : Transforme la LED de la carte en voyant d'alarme
            float currentMaxLoad = loadMeter.GetMaxCpuLoad();
            if(currentMaxLoad > 0.90f) {
                hw.seed.SetLed(true); // Alerte Rouge : Le processeur sature !
            } else {
                hw.seed.SetLed(false); // Tout va bien
            }

            float avgLoad = loadMeter.GetAvgCpuLoad();
            float maxLoad = loadMeter.GetMaxCpuLoad();
#if USE_MIDI_USB
 
            // On envoie la charge CPU moyenne via MIDI (Control Change n°80)
            // L'ordinateur la recevra sur le câble USB normal !
            uint8_t cc_msg[3];
            cc_msg[0] = 0xB0; // 0xB0 = Control Change sur le Canal 1
            cc_msg[1] = 80;   // Numéro du contrôleur (CC 80)
            cc_msg[2] = (uint8_t)(avgLoad * 100.0f); // Valeur de la charge (0 à 100%)
            midi.SendMessage(cc_msg, 3);
           
            // On peut aussi envoyer la charge MAX sur le CC n°81
            uint8_t cc_max[3];
            cc_max[0] = 0xB0;
            cc_max[1] = 81;
            cc_max[2] = (uint8_t)(maxLoad * 100.0f);
            midi.SendMessage(cc_max, 3);
#else 
            hw.seed.PrintLine("Charge CPU Moyenne : %d%% | Max : %d%%", 
                        (int)(avgLoad * 100.0f), 
                        (int)(maxLoad * 100.0f));
#endif
#endif
#endif
        }
        System::Delay(1);
    }
}

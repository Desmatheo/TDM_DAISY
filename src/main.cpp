#include "Utils/main.h"

using namespace daisy; 

#if MIDI_USB_DE_LA_MORT
MidiUsbHandler midi;
#endif

DaisyTdmSlave hw;

EarthEffect* earth_effects[6];
DelayEffect* delay_effects[6];

StringUtil strings[] = {
    StringUtil(EffectType::Earth, 0),
    StringUtil(EffectType::Bypass, 1),
    StringUtil(EffectType::Bypass, 2),
    StringUtil(EffectType::Bypass, 3),
    StringUtil(EffectType::Bypass, 4),
    StringUtil(EffectType::Bypass, 5)
};

// ================================================================
alignas(EarthEffect) static uint8_t earth_mem[6 * sizeof(EarthEffect)];
alignas(DelayEffect) static uint8_t delay_mem[6 * sizeof(DelayEffect)];

#define STATUS_PERIOD_MS 1000

// ================================================================
int main(void)
{
    hw.Init(true);

    // Non-blocking USB serial: the board keeps running even if no
    // serial monitor is connected.

#if SerialMessagingGenial
    hw.seed.StartLog(false);
#endif

    hw.StartAudio(AudioCallback);

    memset(earth_mem, 0, 6 * sizeof(EarthEffect));
    memset(delay_mem, 0, 6 * sizeof(DelayEffect));

    for (int j = 0; j < 6; j++){ 
        earth_effects[j] = new(&earth_mem[j * sizeof(EarthEffect)]) EarthEffect((float)DaisyTdmSlave::kSampleRate);
        delay_effects[j] = new(&delay_mem[j * sizeof(DelayEffect)]) DelayEffect((float)DaisyTdmSlave::kSampleRate);
    }


    strings[0].type = EffectType::Earth;
    strings[0].active_effect = earth_effects[0];
    earth_effects[0]->setParameter(1, 1.0f);



#if MIDI_USB_DE_LA_MORT
    MidiUsbHandler::Config midi_cfg;
    midi_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    midi.Init(midi_cfg);
#endif

    // With block 32 @ 48 kHz the callback should run 1500 times/s.
    // 0 calls/s means no BCLK/FS from the Teensy: check wiring and that
    // the Teensy sketch is running. ~1378/s means the Teensy was left at
    // its default 44.1 kHz (AUDIO_SAMPLE_RATE_EXACT not overridden).
    uint32_t last_status = System::GetNow();

    while(1)
    {

#if MIDI_USB_DE_LA_MORT
        midi.Listen();
        // La fonction MidiHandlingDeLaMort s'occupe déjà de vérifier s'il y a des
        // événements et d'allumer la LED. On l'appelle à chaque tour.
        MidiHandlingDeLaMort();
#endif
        // On éteint la LED à chaque tour de boucle.
        // Si un message MIDI a été reçu, elle aura été allumée juste avant,
        // créant un effet de clignotement pour le débogage.
        hw.seed.SetLed(false);

        if(System::GetNow() - last_status >= STATUS_PERIOD_MS)
        {
            last_status = System::GetNow();

            const uint32_t cb_per_s = audio_diag.callback_count;
            audio_diag.callback_count = 0;
            float peaks[DaisyTdmSlave::kNumInputs];
            for(size_t ch = 0; ch < DaisyTdmSlave::kNumInputs; ch++)
                peaks[ch] = audio_diag.in_peak[ch];
            audio_diag.ResetPeaks();

#if SerialMessagingGenial
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
            float avgLoad = loadMeter.GetAvgCpuLoad();
            float maxLoad = loadMeter.GetMaxCpuLoad();


            hw.seed.PrintLine("Charge CPU Moyenne : %d%% | Max : %d%%", 
                        (int)(avgLoad * 100.0f), 
                        (int)(maxLoad * 100.0f));
#endif
#endif

        }

        System::Delay(1);
    }
}
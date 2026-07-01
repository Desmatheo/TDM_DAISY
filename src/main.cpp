#include "Utils/main.h"

using namespace daisy; 

// ================================================================

alignas(EarthEffect) static uint8_t earth_mem[6 * sizeof(EarthEffect)];

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

    for (int j = 0; j < 6; j++){ 
        earth_effects[j] = new(&earth_mem[j * sizeof(EarthEffect)]) EarthEffect((float)DaisyTdmSlave::kSampleRate);
    }
    
    
    // earth_effects[0]->setOctaveMode(2);
    // earth_effects[1]->setOctaveMode(2);
    // strings[0].active_effect = earth_effects[0];
    // strings[1].active_effect = earth_effects[1];
    // earth_effects[2]->setMix(0.01f);
    // earth_effects[3]->setMix(0.01f);
    // earth_effects[4]->setMix(0.01f);
    // earth_effects[5]->setMix(0.01f);

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

        if (midi.HasEvents()){
            auto event = midi.PopEvent();
            if (event.type == MidiMessageType::ControlChange){
                auto cc = event.AsControlChange();
                const int channel = cc.channel;
                const int ctrl    = cc.control_number;
                const float value = cc.value / 127.0f;

                if (channel < 6 && strings[channel].active_effect != nullptr){
                    strings[channel].active_effect->setParameter(ctrl, value);
                }
            }
        }
#endif
        hw.seed.SetLed(false);

        if(System::GetNow() - last_status >= STATUS_PERIOD_MS)
        {
            last_status = System::GetNow();

            const uint32_t cb_per_s = audio_diag.callback_count;
            audio_diag.callback_count = 0;

            hw.seed.PrintLine("callbacks/s: %lu (attendu ~%d)",
                              cb_per_s,
                              (int)(DaisyTdmSlave::kSampleRate
                                    / DaisyTdmSlave::kBlockSize));
            float peaks[DaisyTdmSlave::kNumInputs];
            for(size_t ch = 0; ch < DaisyTdmSlave::kNumInputs; ch++)
                peaks[ch] = audio_diag.in_peak[ch];
            audio_diag.ResetPeaks();

#if SerialMessagingGenial
            hw.seed.PrintLine(
                "Peaks IN - C1/2: " FLT_FMT3 " / " FLT_FMT3 " | C3/4: " FLT_FMT3 " / " FLT_FMT3 " | C5/6: " FLT_FMT3 " / " FLT_FMT3 " | C7/8: " FLT_FMT3 " / " FLT_FMT3 ,
                FLT_VAR3(peaks[0]), FLT_VAR3(peaks[1]),
                FLT_VAR3(peaks[2]), FLT_VAR3(peaks[3]),
                FLT_VAR3(peaks[4]), FLT_VAR3(peaks[5]), 
                FLT_VAR3(peaks[6]), FLT_VAR3(peaks[7])
            );
#endif

        }

        System::Delay(1);
    }
}

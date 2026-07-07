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
static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{

#if CPU_METER
    loadMeter.OnBlockStart();
#endif


    audio_diag.callback_count++;
    bool signal_present = false;

    for(size_t i = 0; i < size; i++)
    {
        //ancienne partie du code de Yannick
        // for(size_t ch = 0; ch < DaisyTdmSlave::kNumInputs; ch++)
        // {
        //     const float mag = fabsf(in[ch][i]);
        //     if(mag > audio_diag.in_peak[ch])
        //         audio_diag.in_peak[ch] = mag;
        //     if(mag > 0.05f)
        //         signal_present = true;
        // }



        //nouvelle partie DSP etc...
        for (size_t j = 0; j < 6; j++) 
        {
            if (strings[j].type == EffectType::Mute){
                out[j][i] = 0.0f;
            }
            else {
                float in_sample = in[j][i];
                float in_arr[2][1] = {{in_sample}, {in_sample}};
                const float* in_ptrs[2] = {in_arr[0], in_arr[1]};
                
                float out_arr[2][1] = {{0.0f}, {0.0f}};
                float* out_ptrs[2] = {out_arr[0], out_arr[1]};
                float out_sample = 0.0f;


                if (strings[j].type == EffectType::Testation){
                    strings[j].active_effect->update(in_ptrs, out_ptrs, 0);

                    float in_sample2 = out_arr[0][0];
                    float in_arr2[2][1] = {{in_sample2}, {in_sample2}};
                    const float* in_ptrs2[2] = {in_arr2[0], in_arr2[1]};

                    float out_arr2[2][1] = {{0.0f}, {0.0f}};
                    float* out_ptrs2[2] = {out_arr2[0], out_arr2[1]};
                    strings[j].active_effect_bonus->update(in_ptrs2, out_ptrs2, 0);

                    out_sample = out_arr2[0][0];

                } else if (strings[j].active_effect != nullptr && strings[j].type != EffectType::Testation) {
                    strings[j].active_effect->update(in_ptrs, out_ptrs, 0);
                    
                    out_sample = out_arr[0][0];

                } else {
                    out_sample = in_sample;
                }

                out[j][i] = out_sample; // * 0.1f; 
            }
        }

        out[6][i] = 0.0f;
        out[7][i] = 0.0f;
        
    }

    hw.seed.SetLed(signal_present);

#if CPU_METER
    loadMeter.OnBlockEnd();
#endif


}

#endif // AUDIO_PROCESSING_H

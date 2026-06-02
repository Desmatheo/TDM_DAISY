#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H

#define USE_SDCARD 1

#include "daisy_core.h"
#include "heartware_pod_prototype.h"

// ================================================================
extern HeartwarePodPrototype heartware;

#if USE_SDCARD
#include "include/WavHexaPlayer.h"

extern SdmmcHandler   sdcard;
extern FatFSInterface fsi;
extern WavHexaPlayer  sampler;
#endif


// ================================================================
// ===== Audio "Thread"
// ================================================================
static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out,
                          size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
#if 1 // map all input to all output!
#if USE_SDCARD
        float mixed = 0;
        mixed += s162f(sampler.StreamHex(0)) + s162f(sampler.StreamHex(1)) + s162f(sampler.StreamHex(2))
              +  s162f(sampler.StreamHex(3)) + s162f(sampler.StreamHex(4)) + s162f(sampler.StreamHex(5));
#else
        float mixed = 0;
        mixed += in[0][i] + in[1][i];
        mixed += in[2][i] + in[3][i] + in[4][i] + in[5][i];
#endif
        for (int ch = 0; ch < 6; ch++)
        {
            out[ch][i] = mixed;
        }
#else   
    // out[2] = max output via audio.cpp
    // out[3] = in[2];
    // etc...
    for (int ch = 3; ch < 6; ch++)
        out[ch][i] = s162f(sampler.StreamHex(ch));
#endif
    }

    heartware.pmod.AudioVUMeter(in, out, size);
}

#endif // AUDIO_PROCESSING_H
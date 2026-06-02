
#include "daisy_core.h"
#include "audio_processing.h"
#include "heartware_pod_prototype.h"

// ================================================================
HeartwarePodPrototype heartware;

#if USE_SDCARD
#include "include/WavHexaPlayer.h"

SdmmcHandler   sdcard;
FatFSInterface fsi;
WavHexaPlayer  sampler;
#endif

#define MAIN_LOOP_DELAY 1   // milliseconds
#define AUDIO_BLOCK_SIZE 32 // keep this to 32


// ================================================================
int main(void)
{
    heartware.Init(1);
    heartware.SetAudioBlockSize(AUDIO_BLOCK_SIZE);

#if USE_SDCARD

    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sdcard.Init(sd_cfg);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    f_mount(&fsi.GetSDFileSystem(), "/", 1);

    sampler.Init(fsi.GetSDPath());
    
    // Ouvre les 6 fichiers
    for(int i = 0; i < 6; i++) {
        if (i < sampler.GetNumberFiles()) {
            sampler.OpenHex(i, i);
            sampler.SetLoopingHex(i, true);
        }
    }

#endif

    heartware.StartAdc();
    heartware.StartAudio(AudioCallback);

    while (1)
    {
#if USE_SDCARD
        sampler.update();
#endif

        heartware.loop();
        System::Delay(MAIN_LOOP_DELAY);
    }
}

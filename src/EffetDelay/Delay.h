#pragma once

#include "../Utils/Effect.h"
#include "daisy_seed.h"
#include "daisysp.h"
#include "delayline_oct.h"

#define MAX_DELAY 5000 // ~85 ms de délai maximum pour tenir dans la RAM de la Daisy

class DelayEffect : public Effect {
public:
    // Classe interne pour gérer un canal de delay
    class DelayChannel {
    public:
        void Init(daisysp::DelayLineOct<float, MAX_DELAY>* delayLine, float sampleRate);
        float Process(float in);

        daisysp::DelayLineOct<float, MAX_DELAY>* del;
        daisysp::Tone tone;

        float currentDelay;
        float delayTarget;
        float feedback;
        bool active;

        // Pour le mode standby anti-clics
        float muteFade;
        int standbyTimer;
        float lastTarget;
    };

    // Constructeur
    DelayEffect(float sampleRate);

    // La fonction de traitement audio principale
    void update(const float** in, float** out, int idx) override;

    // Fonctions pour régler les paramètres ("setters")
    void setMix(float mix);
    void setDelayTime(float time);
    void setFeedback(float fdbk);
    void setVolume(float vol);
    void setParameter(int param_id, float value) override;

private:
    DelayChannel delayL; // On en utilisera qu'un seul (L) pour l'effet mono
    daisysp::DelayLineOct<float, MAX_DELAY> delayLineOctLeft;

    float dryMix, wetMix, volume;
    float vdelayTime, vdelayFDBK; // Pour garder la valeur des potards
};
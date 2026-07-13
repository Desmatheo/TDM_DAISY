#include "Delay.h"

using namespace daisy;
using namespace daisysp;

// ------ Partie Daisy --------

void DelayEffect::DelayChannel::Init(DelayLineOct<float, MAX_DELAY>* delayLine, float sampleRate) {
    del = delayLine;
    del->Init();
    del->setOctave(false);

    tone.Init(sampleRate);
    tone.SetFreq(3000.0f);

    currentDelay = 2400.0f;
    delayTarget = 2400.0f;
    feedback = 0.5f;
    active = true;
    muteFade = 1.0f;
    standbyTimer = 0;
    lastTarget = 2400.0f;
    del->SetDelay(currentDelay);
}

float DelayEffect::DelayChannel::Process(float in) {
    const float delayed = del->Read();

    if (active) {
        const float feedback_sample = delayed * feedback;
        del->Write(in + feedback_sample);
        return delayed;
    }

    del->Write(in);
    return 0.0f;
}

DelayEffect::DelayEffect(float sampleRate) {
    delayL.Init(&delayLineOctLeft, sampleRate);

    setMix(0.75f);
    setDelayTime(0.25f);
    setFeedback(0.55f);
    setVolume(0.9f);
}

void DelayEffect::update(const float** in, float** out, int idx) {
    float inputL = in[0][idx];

    // Traitement du son par les lignes de delay
    // On utilise uniquement le canal gauche (delayL) pour un effet mono par corde.
    float delay_outL = delayL.Process(inputL);

    // Mixage simple et plus audible : le signal traité est ajouté au signal sec.
    const float wet = delay_outL * wetMix;
    out[0][idx] = (inputL + wet) * volume;
    out[1][idx] = out[0][idx];
}

void DelayEffect::setMix(float mix) {
    wetMix = clampf(mix, 0.0f, 1.0f);
    dryMix = 1.0f - wetMix;
}

void DelayEffect::setVolume(float vol)
{
    volume = clampf(vol, 0.0f, 1.0f);
}

void DelayEffect::setDelayTime(float time) {
    vdelayTime = clampf(time, 0.0f, 1.0f);

    const bool isActive = (vdelayTime > 0.01f);
    delayL.active = isActive;

    const float min_delay = 2400.0f;
    const float max_delay = static_cast<float>(MAX_DELAY);
    const float target = min_delay + (max_delay - min_delay) * vdelayTime;

    delayL.delayTarget = target;
    delayL.currentDelay = target;
    delayL.del->SetDelay(target);
}

void DelayEffect::setFeedback(float fdbk) {
    vdelayFDBK = clampf(fdbk, 0.0f, 0.99f);

    delayL.feedback = vdelayFDBK;
}


void DelayEffect::setParameter(int param_id, float value) {
    switch (param_id) {
        case 0:
            setMix(value);
            break;
        case 1:
            setDelayTime(value);
            break;
        case 2:
            setFeedback(value);
            break;
        case 5: 
            setVolume(value);
            break;
        default:
            break;
    }
}

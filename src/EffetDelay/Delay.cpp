#include "Delay.h"

using namespace daisy;
using namespace daisysp;

// Allocation en SDRAM des buffers pour 6 cordes, 4 secondes en mono (48kHz)
DSY_SDRAM_BSS float delay_buffers[6][MAX_DELAY_SAMPLES];
static int next_buffer_idx = 0;

void DelayEffect::DelayChannel::Init(float* mem, float sampleRate, uint32_t max_delay_samples) {
    buffer = mem;
    buf_len = max_delay_samples;
    write_idx = 0;
    
    // Initialisation du filtre Tone (Low-pass 1-pole) à 3000 Hz
    float tone_hz = 3000.0f;
    float alpha = expf(-2.0f * (float)PI * tone_hz / sampleRate);
    tone_a0 = 1.0f - alpha;
    tone_b1 = alpha;
    tone_z1 = 0.0f;

    // A 48kHz, 2400 samples = 50ms (valeur par défaut)
    currentDelay = 2400.0f;
    delayTarget = 2400.0f;
    feedback = 0.5f;
    active = true;
    muteFade = 1.0f;
    standbyTimer = 0;
    lastTarget = 2400.0f;
}

float DelayEffect::DelayChannel::Process(float in) {
    if (!buffer) return in;

    // Si le potard est en train d'être tourné (la cible change)
    if (fabsf(delayTarget - lastTarget) > 0.1f) {
        lastTarget = delayTarget;
        standbyTimer = 10000; // Maintient le standby pendant ~208ms après le dernier mouvement
    }

    if (standbyTimer > 0) {
        standbyTimer--;
        
        // Fade-out ultra-rapide
        muteFade -= 0.01f;
        if (muteFade <= 0.0f) {
            muteFade = 0.0f;
            // Dès qu'on est sous silence, le pointeur saute instantanément (aucun pitch-shift)
            currentDelay = delayTarget;
        }
    } else {
        // Le potard ne bouge plus, on sort du standby (Fade-in)
        muteFade += 0.01f;
        if (muteFade > 1.0f) {
            muteFade = 1.0f;
        }
    }

    // Lecture du son retardé avec interpolation linéaire
    float read_idx_f = (float)write_idx - currentDelay;
    while (read_idx_f < 0.0f) read_idx_f += (float)buf_len;
    while (read_idx_f >= (float)buf_len) read_idx_f -= (float)buf_len;

    uint32_t r0 = (uint32_t)read_idx_f;
    uint32_t r1 = r0 + 1; 
    if (r1 >= buf_len) r1 = 0;
    float frac = read_idx_f - (float)r0;

    float del_read = buffer[r0] + (buffer[r1] - buffer[r0]) * frac;

    // Application du fade pour le mode standby
    del_read *= muteFade;

    // Application du filtre passe-bas
    float read = tone_a0 * del_read + tone_b1 * tone_z1;
    tone_z1 = read;

    // Écriture dans la ligne de delay (avec feedback)
    float write_val = feedback * read;
    if (active) {
        write_val += in;
    }
    
    // Limiteur de saturation interne
    if (write_val > 1.0f) write_val = 1.0f;
    if (write_val < -1.0f) write_val = -1.0f;
    
    buffer[write_idx] = write_val;

    write_idx++;
    if (write_idx >= buf_len) write_idx = 0;

    return read;
}

DelayEffect::DelayEffect(float sampleRate) {
    sample_rate_ = sampleRate;
    
    float* mem = nullptr;
    if (next_buffer_idx < 6) {
        mem = delay_buffers[next_buffer_idx];
        next_buffer_idx++;
    }
    delayL.Init(mem, sampleRate, MAX_DELAY_SAMPLES);

    setMix(0.75f);
    setDelayTime(0.25f);
    setFeedback(0.55f);
    setVolume(0.9f);
}

void DelayEffect::update(const float** in, float** out, int idx) {
    float inputL = in[0][idx];

    // Traitement du son par les lignes de delay
    float delay_outL = delayL.Process(inputL);

    // Mixage : dry/wet
    const float wet = delay_outL * wetMix;
    out[0][idx] = (inputL * dryMix + wet) * volume;
    out[1][idx] = out[0][idx];
}

void DelayEffect::setMix(float mix) {
    wetMix = clampf(mix, 0.0f, 1.0f);
    dryMix = 1.0f - wetMix;
}

void DelayEffect::setVolume(float vol) {
    volume = clampf(vol, 0.0f, 1.0f);
}

void DelayEffect::setFeedback(float fdbk) {
    vdelayFDBK = clampf(fdbk, 0.0f, 0.99f);
    delayL.feedback = vdelayFDBK;
}

void DelayEffect::updateTargetDelay() {
    float target_ms = 50.0f;
    if (isTempoMode) {
        float bpm = 40.0f + (vdelayTime * 200.0f);
        float div = roundf(1.0f + (vdelayDiv * 7.0f));
        float beat_duration_ms = 60000.0f / bpm;
        target_ms = beat_duration_ms / div;
    } else {
        target_ms = 50.0f + (vdelayTime * 3950.0f);
    }

    target_ms = clampf(target_ms, 1.0f, 4000.0f);
    float target_samples = (target_ms / 1000.0f) * sample_rate_;
    
    delayL.delayTarget = clampf(target_samples, 1.0f, (float)MAX_DELAY_SAMPLES - 1.0f);
}

void DelayEffect::setType(float type) {
    isTempoMode = (type >= 0.5f);
    updateTargetDelay();
}

void DelayEffect::setDivision(float div) {
    vdelayDiv = clampf(div, 0.0f, 1.0f);
    updateTargetDelay();
}

void DelayEffect::setDelayTime(float time) {
    vdelayTime = clampf(time, 0.0f, 1.0f);
    bool isActive = (vdelayTime > 0.01f);
    delayL.active = isActive;
    updateTargetDelay();
}

void DelayEffect::setParameter(int param_id, float value) {
    switch (param_id) {
        case 0: // Type
            setType(value);
            break;
        case 1: // Time (Delay ms ou Tempo bpm)
            setDelayTime(value);
            break;
        case 2: // Tap (bouton ignoré en MIDI logiquement, géré par le GUI)
            break;
        case 3: // Temps (division)
            setDivision(value);
            break;
        case 4: // FeedBack
            setFeedback(value);
            break;
        case 5: // Volume
            setVolume(value);
            break;
        case 6: // Mix
            setMix(value);
            break;
        default:
            break;
    }
}

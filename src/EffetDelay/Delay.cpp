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

    // Écriture dans la ligne de delay (avec feedback et anti-denormal)
    anti_denormal = -anti_denormal;
    float write_val = feedback * read + anti_denormal;
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
    float clampedMix = clampf(mix, 0.0f, 1.0f);
    // Constant power panning law
    wetMix = sinf(clampedMix * (float)M_PI_2);
    dryMix = cosf(clampedMix * (float)M_PI_2);
}

void DelayEffect::setVolume(float vol) {
    volume = clampf(vol, 0.0f, 1.0f);
}

void DelayEffect::setFeedback(float fdbk) {
    vdelayFDBK = clampf(fdbk, 0.0f, 0.99f);
    delayL.feedback = vdelayFDBK;
}

void DelayEffect::setDelayMode(float mode) {
    delayMode = (mode < 0.5f) ? 0 : 1;
    recalculateDelayTime();
}

void DelayEffect::setDelayTime(float time) {
    // Mapping linéaire de 50ms à 4000ms
    manualTimeMs = 50.0f + time * (4000.0f - 50.0f);
    recalculateDelayTime();
}

void DelayEffect::setBpm(float value) {
    currentBPM = 40.0f + value * (250.0f - 40.0f);
    recalculateDelayTime();
}

void DelayEffect::setSubdivision(float value) {
    int idx = roundf(value * 7.0f); 
    if (idx < 0) idx = 0;
    if (idx > 7) idx = 7;
    
    const float multipliers[8] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    currentSubdivisionMult = multipliers[idx];
    recalculateDelayTime();
}

void DelayEffect::recalculateDelayTime() {
    float target_ms = 0.0f;
    
    if (delayMode == 0) { // Manual Mode
        target_ms = manualTimeMs;
    } else { // Tempo Mode
        float ms_per_quarter = 60000.0f / (currentBPM > 0.01f ? currentBPM : 120.0f);
        target_ms = ms_per_quarter * currentSubdivisionMult;
    }

    // Convert ms to samples
    float target_samples = target_ms * (sample_rate_ / 1000.0f);

    // Limit to max and min
    if (target_samples > static_cast<float>(MAX_DELAY_SAMPLES - 1.0f)) {
        target_samples = static_cast<float>(MAX_DELAY_SAMPLES - 1.0f);
    }
    if (target_samples < 2400.0f) { // Min internal limit
        target_samples = 2400.0f;
    }

    delayL.delayTarget = target_samples;
    delayL.active = true;
}

void DelayEffect::setParameter(int param_id, float value) {
    switch (param_id) {
        case 0: // Type (Mode)
            setDelayMode(value);
            break;
        case 1: // Time / Tempo
            if (delayMode == 0) {
                setDelayTime(value);
            } else {
                setBpm(value);
            }
            break;
        case 2: // Tap
            break;
        case 3: // Temps (Subdivision)
            setSubdivision(value);
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

#include "Equalizer.h"

EqualizerEffect::EqualizerEffect(float sampleRate) {
    sample_rate_ = sampleRate;
    volume = 1.0f;
    for(int i = 0; i < 5; i++) {
        gains_db[i] = 0.0f;
        for (int j = 0; j < 4; j++) pState[i][j] = 0.0f;
    }
    
    calculateCoeffs();
}

void EqualizerEffect::calculateCoeffs() {
    // Fréquences standards pour un EQ 5 bandes
    float frequencies[5] = {80.0f, 250.0f, 750.0f, 2200.0f, 6600.0f};
    float Q = 1.414f; 
    float Fs = sample_rate_;
    
    for (int i = 0; i < 5; i++) {
        float f0 = frequencies[i];
        float gainDB = gains_db[i];
        
        float A = powf(10.0f, gainDB / 40.0f); // sqrt(10^(gainDB/20))
        float w0 = 2.0f * PI_F * f0 / Fs;
        float alpha = sinf(w0) / (2.0f * Q);
        
        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cosf(w0);
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cosf(w0);
        float a2 = 1.0f - alpha / A;
        
        // Normalisation par a0
        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
        // Custom storage
        pCoeffs[i][0] = b0;
        pCoeffs[i][1] = b1;
        pCoeffs[i][2] = b2;
        pCoeffs[i][3] = a1;
        pCoeffs[i][4] = a2;
    }
}

void EqualizerEffect::setBand(int band_index, float value_norm) {
    if (band_index >= 0 && band_index < 5) {
        // Mappe [0.0, 1.0] vers [-12dB, +12dB]
        gains_db[band_index] = (value_norm - 0.5f) * 24.0f;
        calculateCoeffs();
    }
}

void EqualizerEffect::setVolume(float vol) {
    volume = vol;
}

void EqualizerEffect::setParameter(int param_id, float value) {
    if (param_id >= 0 && param_id <= 4) {
        setBand(param_id, value);
    } else if (param_id == 5) {
        setVolume(value);
    }
}

void EqualizerEffect::update(const float** in, float** out, int idx) {
    float sample = in[0][idx];
    
    // Process 5 cascaded biquads
    for (int i = 0; i < 5; i++) {
        float xn = sample;
        float yn = pCoeffs[i][0] * xn 
                 + pCoeffs[i][1] * pState[i][0] 
                 + pCoeffs[i][2] * pState[i][1] 
                 - pCoeffs[i][3] * pState[i][2] 
                 - pCoeffs[i][4] * pState[i][3];
                 
        // Update states
        pState[i][1] = pState[i][0]; // x[n-2] = x[n-1]
        pState[i][0] = xn;           // x[n-1] = x[n]
        pState[i][3] = pState[i][2]; // y[n-2] = y[n-1]
        pState[i][2] = yn;           // y[n-1] = y[n]
        
        sample = yn;
    }
    
    sample = sample * volume;
    sample = clampf(sample, -1.0f, 1.0f);
    
    out[0][idx] = sample;
    out[1][idx] = sample;
}

#pragma once

#include "BandShifter.h"

#include <cmath>

//=============================================================================
class OctaveGenerator
{
public:
    OctaveGenerator(float sample_rate)
    {
        for (int i = 0; i < 24; ++i)
        {
            const auto center = centerFreq(i);
            const auto bw = bandwidth(i);
            _shifters.emplace_back(center, sample_rate, bw);
        }
    }

   void update(float sample, int type)
    {
        int numBand;
        
        // Purge des variables ou on accumule les résultats de chaque tranche de fréquence
        _up1 = 0;
        _down1 = 0;
        _down2 = 0;

        // Optimisation CPU : Si on veut du grave, on coupe complètement les 12 tranches aiguës
        if (type == 1) numBand = 24;
        if (type == 2) numBand = 12;
        if (type == 3) numBand = 12;

        // Boucle sur les sous-unités (BandShifters)
        for (int i = 0; i < numBand; i++) {
            // Le sample traverse la tranche de fréquence N° i
            _shifters[i].update(sample, type);
            
            // RECONSTRUCTION : On additionne (+=) le résultat de cette tranche au mix global
            if (type == 1) _up1 += _shifters[i].up1();
            if (type == 2 || type == 3) _down1 += _shifters[i].down1();
            if (type == 3) _down2 += _shifters[i].down2();
        }
    }

    float up1() const
    {
        return _up1;
    }

    float down1() const
    {
        return _down1;
    }

    float down2() const
    {
        return _down2;
    }

private:
    static inline float centerFreq(const int n)
    {
        return 480.0f * std::pow(2.0f, (0.09f * n)) - 420.0f;
    }

    static inline float bandwidth(const int n)
    {
        const float f0 = centerFreq(n-1);
        const float f1 = centerFreq(n);
        const float f2 = centerFreq(n+1);
        const float a = (f2 - f1);
        const float b = (f1 - f0);
        return 2.0f * (a*b) / (a+b);
    }

    std::vector<BandShifter> _shifters;

    float _up1 = 0;
    float _down1 = 0;
    float _down2 = 0;
};

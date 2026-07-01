#pragma once
#ifndef DELAYLINE_OCT_H
#define DELAYLINE_OCT_H
#include <stdlib.h>
#include <stdint.h>
#include <cmath>
#include <algorithm>

namespace daisysp
{

class Tone
{
  public:
    Tone() : sample_rate_(48000.0f), a0_(1.0f), b1_(0.0f), z1_(0.0f) {}

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        SetFreq(3000.0f);
    }

    void SetFreq(float freq)
    {
        if(sample_rate_ <= 0.0f)
        {
            a0_ = 1.0f;
            b1_ = 0.0f;
            z1_ = 0.0f;
            return;
        }

        float cutoff = freq;
        if(cutoff <= 0.0f) cutoff = 20.0f;
        if(cutoff >= sample_rate_ * 0.5f) cutoff = sample_rate_ * 0.5f - 1.0f;

        const float alpha = std::exp(-2.0f * 3.14159265358979323846f * cutoff / sample_rate_);
        a0_               = 1.0f - alpha;
        b1_               = alpha;
        z1_               = 0.0f;
    }

    float Process(float sample)
    {
        const float out = a0_ * sample + b1_ * z1_;
        z1_             = out;
        return out;
    }

  private:
    float sample_rate_;
    float a0_;
    float b1_;
    float z1_;
};
/** Simple Delay line.
November 2019

Converted to Template December 2019

declaration example: (1 second of floats)

DelayLine<float, SAMPLE_RATE> del;

By: shensley
*/
template <typename T, size_t max_size>
class DelayLineOct
{
  public:
    DelayLineOct() {}
    ~DelayLineOct() {}

    void Init() { Reset(); }

    void Reset()
    {
        std::fill(line_, line_ + max_size, T(0));
        write_index_ = 0;
        delay_samples_ = 1;
        frac_ = 0.0f;
        speed_ = 1;
    }

    inline void setOctave(bool isOctave)
    {
        speed_ = isOctave ? 2 : 1;
    }

    inline void SetDelay(size_t delay)
    {
        frac_ = 0.0f;
        delay_samples_ = delay > 0 ? (delay < max_size ? delay : max_size - 1) : 1;
    }

    inline void SetDelay(float delay)
    {
        int32_t int_delay = static_cast<int32_t>(delay);
        frac_ = delay - static_cast<float>(int_delay);
        delay_samples_ = int_delay > 0 ? (static_cast<size_t>(int_delay) < max_size ? static_cast<size_t>(int_delay) : max_size - 1) : 1;
    }

    inline void Write(const T sample)
    {
        line_[write_index_] = sample;
        write_index_ = (write_index_ + 1) % max_size;
    }

    inline const T Read() const
    {
        const size_t read_distance = delay_samples_ * static_cast<size_t>(speed_);
        const size_t read_index = (write_index_ + max_size - read_distance) % max_size;
        const size_t read_index_next = (read_index + 1) % max_size;
        const T a = line_[read_index];
        const T b = line_[read_index_next];
        return a + (b - a) * frac_;
    }

    inline const T Read(float delay) const
    {
        const int32_t delay_integral = static_cast<int32_t>(delay);
        const float delay_fractional = delay - static_cast<float>(delay_integral);
        const size_t read_index = (write_index_ + max_size - static_cast<size_t>(delay_integral > 0 ? delay_integral : 1)) % max_size;
        const size_t read_index_next = (read_index + 1) % max_size;
        const T a = line_[read_index];
        const T b = line_[read_index_next];
        return a + (b - a) * delay_fractional;
    }

    inline const T ReadSecondTap() const
    {
        return Read();
    }

    inline const T ReadHermite(float delay) const
    {
        return Read(delay);
    }

    inline const T Allpass(const T sample, size_t delay, const T coefficient)
    {
        const T read = Read(static_cast<float>(delay));
        const T write = sample + coefficient * read;
        Write(write);
        return -write * coefficient + read;
    }

  private:
    float frac_ = 0.0f;
    size_t write_index_ = 0;
    size_t delay_samples_ = 1;
    T line_[max_size] = {};
    int speed_ = 1;
};
} // namespace daisysp
#endif

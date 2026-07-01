#include "hid/audio.h"

namespace daisy
{
// ================================================================
// Static Globals
// ================================================================

// Ordinarily the user would be required to provide their own buffer, but
// in the interest in encourage newcomers, and this also being an audio-centric platform
// these buffers will always be present, and usable.

/** DMA buffer size per SAI peripheral, in samples (i.e. in 32-bit words).
 ** Each SAI stream must satisfy: blocksize * 2 * channels <= kAudioMaxBufferSize
 ** where channels = tdm_slots for a TDM block, 2 for a stereo I2S block.
 ** e.g. 8 TDM slots allow a blocksize of up to 64. */
static const size_t kAudioMaxBufferSize = 1024;

/** Maximum channels per SAI peripheral (stereo I2S = 2, TDM up to 8 slots) */
static const size_t kMaxSaiChannels = 8;

/** Maximum total channels across both SAI peripherals */
static const size_t kMaxTotalChannels = 2 * kMaxSaiChannels;

// Static Global Buffers
// One rx and one tx stream per SAI peripheral, in SRAM1 non-cached memory.
// Channels are interleaved on the hardware within each stream.
static int32_t DMA_BUFFER_MEM_SECTION
    dsy_audio_rx_buffer[2][kAudioMaxBufferSize];
static int32_t DMA_BUFFER_MEM_SECTION
    dsy_audio_tx_buffer[2][kAudioMaxBufferSize];

// Scratch buffers for the float (de)interleaved audio handed to the user
// callback. Their bound holds by construction: per SAI, half a DMA buffer is
// blocksize * channels <= kAudioMaxBufferSize / 2 samples, so the two SAIs
// together never exceed kAudioMaxBufferSize samples per direction.
// Not DMA memory; plain bss, only touched from the audio callback context.
static float audio_scratch_in[kAudioMaxBufferSize];
static float audio_scratch_out[kAudioMaxBufferSize];

// ================================================================
// Conversion helpers
// ================================================================

/** Returns the number of audio channels carried by one SAI peripheral:
 ** its TDM slot count if TDM is enabled, otherwise 2 (stereo I2S),
 ** and 0 if the handle is not initialized. */
static size_t ChannelsOnSai(const SaiHandle& sai)
{
    if(!sai.IsInitialized())
        return 0;
    size_t tdm_slots = sai.GetConfig().tdm_slots;
    return tdm_slots > 0 ? tdm_slots : 2;
}

/** On-wire sample format for one SAI peripheral. TDM streams always ship
 ** 32-bit slots (see SaiHandle::Impl::InitProtocolTDM), regardless of the
 ** configured bit_depth; stereo I2S streams use the configured depth. */
static SaiHandle::Config::BitDepth WireDepth(const SaiHandle& sai)
{
    if(sai.GetConfig().tdm_slots > 0)
        return SaiHandle::Config::BitDepth::SAI_32BIT;
    return sai.GetConfig().bit_depth;
}

/** Converts one interleaved int32 stream into `nch` deinterleaved float
 ** channel buffers, starting at channel index `ch_offset` of `dst`. */
static void DeinterleaveToFloat(const int32_t*              src,
                                float* const*               dst,
                                size_t                      ch_offset,
                                size_t                      nch,
                                size_t                      block,
                                SaiHandle::Config::BitDepth bd,
                                float                       gain)
{
    switch(bd)
    {
        case SaiHandle::Config::BitDepth::SAI_16BIT:
            for(size_t i = 0; i < block; i++)
                for(size_t c = 0; c < nch; c++)
                    dst[ch_offset + c][i] = s162f(src[i * nch + c]) * gain;
            break;
        case SaiHandle::Config::BitDepth::SAI_24BIT:
            for(size_t i = 0; i < block; i++)
                for(size_t c = 0; c < nch; c++)
                    dst[ch_offset + c][i] = s242f(src[i * nch + c]) * gain;
            break;
        case SaiHandle::Config::BitDepth::SAI_32BIT:
            for(size_t i = 0; i < block; i++)
                for(size_t c = 0; c < nch; c++)
                    dst[ch_offset + c][i] = s322f(src[i * nch + c]) * gain;
            break;
        default: break;
    }
}

/** Converts `nch` deinterleaved float channel buffers (starting at channel
 ** index `ch_offset` of `src`) back into one interleaved int32 stream. */
static void InterleaveFromFloat(int32_t*                    dst,
                                float* const*               src,
                                size_t                      ch_offset,
                                size_t                      nch,
                                size_t                      block,
                                SaiHandle::Config::BitDepth bd,
                                float                       adjust)
{
    switch(bd)
    {
        case SaiHandle::Config::BitDepth::SAI_16BIT:
            for(size_t i = 0; i < block; i++)
                for(size_t c = 0; c < nch; c++)
                    dst[i * nch + c] = f2s16(src[ch_offset + c][i] * adjust);
            break;
        case SaiHandle::Config::BitDepth::SAI_24BIT:
            for(size_t i = 0; i < block; i++)
                for(size_t c = 0; c < nch; c++)
                    dst[i * nch + c] = f2s24(src[ch_offset + c][i] * adjust);
            break;
        case SaiHandle::Config::BitDepth::SAI_32BIT:
            for(size_t i = 0; i < block; i++)
                for(size_t c = 0; c < nch; c++)
                    dst[i * nch + c] = f2s32(src[ch_offset + c][i] * adjust);
            break;
        default: break;
    }
}

// ================================================================
// Private Implementation Definition
// ================================================================
class AudioHandle::Impl
{
  public:
    // Interface
    AudioHandle::Result Init(const AudioHandle::Config config, SaiHandle sai);
    AudioHandle::Result
                        Init(const AudioHandle::Config config, SaiHandle sai1, SaiHandle sai2);
    AudioHandle::Result DeInit();
    AudioHandle::Result Start(AudioHandle::AudioCallback callback);
    AudioHandle::Result Start(AudioHandle::InterleavingAudioCallback callback);
    AudioHandle::Result Stop();
    AudioHandle::Result ChangeCallback(AudioHandle::AudioCallback callback);
    AudioHandle::Result
    ChangeCallback(AudioHandle::InterleavingAudioCallback callback);

    inline size_t GetChannels() const
    {
        return ChannelsOnSai(sai1_) + ChannelsOnSai(sai2_);
    }

    AudioHandle::Result SetBlockSize(size_t size)
    {
        // The most channel-hungry SAI bounds the blocksize.
        size_t max_ch = ChannelsOnSai(sai1_);
        if(ChannelsOnSai(sai2_) > max_ch)
            max_ch = ChannelsOnSai(sai2_);
        if(max_ch < 2)
            max_ch = 2;
        size_t maxSize    = kAudioMaxBufferSize / (2 * max_ch);
        config_.blocksize = size <= maxSize ? size : maxSize;
        return size <= maxSize ? AudioHandle::Result::OK
                               : AudioHandle::Result::ERR;
    }

    float GetSampleRate() { return sai1_.GetSampleRate(); }

    AudioHandle::Result SetPostGain(float val)
    {
        if(val <= 0.f)
            return AudioHandle::Result::ERR;
        config_.postgain = val;
        // Precompute input adjust
        postgain_recip_ = 1.f / config_.postgain;
        // Precompute output adjust
        output_adjust_ = config_.postgain * config_.output_compensation;
        return AudioHandle::Result::OK;
    }

    AudioHandle::Result SetOutputCompensation(float val)
    {
        config_.output_compensation = val;
        // recompute output adjustment (no need to recompute input adjust here)
        output_adjust_ = config_.output_compensation * config_.postgain;
        return AudioHandle::Result::OK;
    }

    AudioHandle::Result SetSampleRate(SaiHandle::Config::SampleRate sampelrate);

    // Internal Callback
    static void InternalCallback(int32_t* in, int32_t* out, size_t size);

    void *callback_, *interleaved_callback_;

    // Data
    AudioHandle::Config config_;
    SaiHandle           sai1_, sai2_;
    int32_t*            buff_rx_[2];
    int32_t*            buff_tx_[2];
    float               postgain_recip_;
    float               output_adjust_;
};

// ================================================================
// Static Reference for Object
// ================================================================

static AudioHandle::Impl audio_handle;

// ================================================================
// Private Implementation
// ================================================================

AudioHandle::Result AudioHandle::Impl::Init(const AudioHandle::Config config,
                                            SaiHandle                 sai)
{
    config_ = config;

    /** Precompute input level adjustment */
    if(config_.postgain > 0.f)
        postgain_recip_ = 1.f / config_.postgain;
    else
        return Result::ERR;

    /** Precompute output level adjustment */
    output_adjust_ = config_.postgain * config_.output_compensation;

    if(sai.IsInitialized())
    {
        sai1_              = sai;
        config_.samplerate = sai1_.GetConfig().sr;
    }
    else
    {
        return Result::ERR;
    }
    if(ChannelsOnSai(sai1_) > kMaxSaiChannels)
        return Result::ERR;
    // Re-initializing with a single SAI must fully replace a previous
    // two-SAI configuration (e.g. DaisySeed::Init sets up the internal
    // codec before the application re-inits audio for its own SAI).
    sai2_       = SaiHandle();
    buff_rx_[0] = dsy_audio_rx_buffer[0];
    buff_tx_[0] = dsy_audio_tx_buffer[0];
    // Clamp the configured blocksize to what the DMA buffers can hold for
    // this channel count (e.g. max 64 for 8 TDM slots) -- an oversized
    // value would otherwise program a circular DMA past the buffer end.
    SetBlockSize(config_.blocksize);
    return Result::OK;
}

AudioHandle::Result AudioHandle::Impl::Init(const AudioHandle::Config config,
                                            SaiHandle                 sai1,
                                            SaiHandle                 sai2)
{
    if(this->Init(config, sai1) != Result::OK)
        return Result::ERR;
    if(ChannelsOnSai(sai2) > kMaxSaiChannels)
        return Result::ERR;
    sai2_       = sai2;
    buff_rx_[1] = dsy_audio_rx_buffer[1];
    buff_tx_[1] = dsy_audio_tx_buffer[1];
    // Re-clamp: the second SAI may be more channel-hungry than the first.
    SetBlockSize(config_.blocksize);
    return Result::OK;
}

AudioHandle::Result AudioHandle::Impl::DeInit()
{
    Stop();
    if(sai1_.IsInitialized())
    {
        if(sai1_.DeInit() != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    if(sai2_.IsInitialized())
    {
        if(sai2_.DeInit() != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    return Result::OK;
}

AudioHandle::Result
AudioHandle::Impl::Start(AudioHandle::AudioCallback callback)
{
    if(!sai1_.IsInitialized())
        return Result::ERR;
    // Set the user callback before arming the DMA: with an external clock
    // master already running, the first IRQ can fire immediately.
    callback_             = (void*)callback;
    interleaved_callback_ = nullptr;
    if(sai2_.IsInitialized())
    {
        // Start stream with no callback. Data will be read/written from
        // InternalCallback, which is driven by sai1_.
        sai2_.StartDma(buff_rx_[1], buff_tx_[1], config_.blocksize, nullptr);
    }
    sai1_.StartDma(buff_rx_[0],
                   buff_tx_[0],
                   config_.blocksize,
                   audio_handle.InternalCallback);
    return Result::OK;
}

AudioHandle::Result
AudioHandle::Impl::Start(AudioHandle::InterleavingAudioCallback callback)
{
    if(!sai1_.IsInitialized())
        return Result::ERR;
    interleaved_callback_ = (void*)callback;
    callback_             = nullptr;
    sai1_.StartDma(buff_rx_[0],
                   buff_tx_[0],
                   config_.blocksize,
                   audio_handle.InternalCallback);
    return Result::OK;
}

AudioHandle::Result AudioHandle::Impl::Stop()
{
    if(sai1_.IsInitialized())
        sai1_.StopDma();
    if(sai2_.IsInitialized())
        sai2_.StopDma();
    return Result::OK;
}

AudioHandle::Result
AudioHandle::Impl::ChangeCallback(AudioHandle::AudioCallback callback)
{
    if(callback != nullptr)
    {
        callback_             = (void*)callback;
        interleaved_callback_ = nullptr;
        return Result::OK;
    }
    else
    {
        return Result::ERR;
    }
}

AudioHandle::Result AudioHandle::Impl::ChangeCallback(
    AudioHandle::InterleavingAudioCallback callback)
{
    if(callback != nullptr)
    {
        interleaved_callback_ = (void*)callback;
        callback_             = nullptr;
        return Result::OK;
    }
    else
    {
        return Result::ERR;
    }
}

AudioHandle::Result
AudioHandle::Impl::SetSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    config_.samplerate = samplerate;
    if(sai1_.IsInitialized())
    {
        // Set, and reinit
        SaiHandle::Config cfg;
        cfg    = sai1_.GetConfig();
        cfg.sr = config_.samplerate;
        if(sai1_.Init(cfg) != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    if(sai2_.IsInitialized())
    {
        // Set, and reinit
        SaiHandle::Config cfg;
        cfg    = sai2_.GetConfig();
        cfg.sr = config_.samplerate;
        if(sai2_.Init(cfg) != SaiHandle::Result::OK)
        {
            return Result::ERR;
        }
    }
    return Result::OK;
}

// The callback is driven by sai1_'s DMA: `in`/`out` point at the half of
// sai1_'s rx/tx buffers that is ready, and `size` is that half-buffer's
// length in samples (= blocksize * channels-on-sai1).
//
// If a second SAI is running, its DMA free-runs on the same clock tree and
// its current half-buffer is read/written here through GetOffset(). Channels
// are exposed to the user callback as:
//   ch 0 .. (n1-1)        -> sai1_ (slot order on the wire)
//   ch n1 .. (n1+n2-1)    -> sai2_ (slot order on the wire)
void AudioHandle::Impl::InternalCallback(int32_t* in, int32_t* out, size_t size)
{
    const size_t ch1  = ChannelsOnSai(audio_handle.sai1_);
    const size_t ch2  = ChannelsOnSai(audio_handle.sai2_);
    const size_t chns = ch1 + ch2;
    if(chns == 0 || chns > kMaxTotalChannels)
        return;
    const size_t block = size / ch1;

    // Handle Interleaved / Non Interleaved separate
    if(audio_handle.interleaved_callback_)
    {
        // The interleaved callback format is fixed to 2 channels (L/R).
        if(chns != 2)
            return;

        float* fin  = audio_scratch_in;
        float* fout = audio_scratch_out;
        float  gain = audio_handle.postgain_recip_;
        const SaiHandle::Config::BitDepth bd = WireDepth(audio_handle.sai1_);

        switch(bd)
        {
            case SaiHandle::Config::BitDepth::SAI_16BIT:
                for(size_t i = 0; i < size; i++)
                    fin[i] = s162f(in[i]) * gain;
                break;
            case SaiHandle::Config::BitDepth::SAI_24BIT:
                for(size_t i = 0; i < size; i++)
                    fin[i] = s242f(in[i]) * gain;
                break;
            case SaiHandle::Config::BitDepth::SAI_32BIT:
                for(size_t i = 0; i < size; i++)
                    fin[i] = s322f(in[i]) * gain;
                break;
            default: break;
        }

        InterleavingAudioCallback cb
            = (InterleavingAudioCallback)audio_handle.interleaved_callback_;
        cb(fin, fout, size);

        float adjust = audio_handle.output_adjust_;
        switch(bd)
        {
            case SaiHandle::Config::BitDepth::SAI_16BIT:
                for(size_t i = 0; i < size; i++)
                    out[i] = f2s16(fout[i] * adjust);
                break;
            case SaiHandle::Config::BitDepth::SAI_24BIT:
                for(size_t i = 0; i < size; i++)
                    out[i] = f2s24(fout[i] * adjust);
                break;
            case SaiHandle::Config::BitDepth::SAI_32BIT:
                for(size_t i = 0; i < size; i++)
                    out[i] = f2s32(fout[i] * adjust);
                break;
            default: break;
        }
    }
    else if(audio_handle.callback_)
    {
        float* fin[kMaxTotalChannels];
        float* fout[kMaxTotalChannels];
        fin[0]  = audio_scratch_in;
        fout[0] = audio_scratch_out;
        for(size_t ch = 1; ch < chns; ch++)
        {
            fin[ch]  = fin[ch - 1] + block;
            fout[ch] = fout[ch - 1] + block;
        }

        const float gain   = audio_handle.postgain_recip_;
        const float adjust = audio_handle.output_adjust_;
        // Snapshot the second SAI's buffer position once: reading it again
        // after the user callback could yield the other half if its DMA IRQ
        // slipped in between, splitting one block across two buffer halves.
        const size_t offset2
            = ch2 > 0 ? audio_handle.sai2_.GetOffset() : 0;

        DeinterleaveToFloat(
            in, fin, 0, ch1, block, WireDepth(audio_handle.sai1_), gain);
        if(ch2 > 0)
        {
            const int32_t* in2 = audio_handle.buff_rx_[1] + offset2;
            DeinterleaveToFloat(in2,
                                fin,
                                ch1,
                                ch2,
                                block,
                                WireDepth(audio_handle.sai2_),
                                gain);
        }

        AudioCallback cb = (AudioCallback)audio_handle.callback_;
        cb(fin, fout, block);

        InterleaveFromFloat(
            out, fout, 0, ch1, block, WireDepth(audio_handle.sai1_), adjust);
        if(ch2 > 0)
        {
            int32_t* out2 = audio_handle.buff_tx_[1] + offset2;
            InterleaveFromFloat(out2,
                                fout,
                                ch1,
                                ch2,
                                block,
                                WireDepth(audio_handle.sai2_),
                                adjust);
        }
    }
}

// ================================================================
// SaiHandle -> SaiHandle::Pimpl
// ================================================================

AudioHandle::Result AudioHandle::Init(const Config& config, SaiHandle sai)
{
    // Figure out proper pattern for singleton behavior here.
    pimpl_ = &audio_handle;
    return pimpl_->Init(config, sai);
}

AudioHandle::Result
AudioHandle::Init(const Config& config, SaiHandle sai1, SaiHandle sai2)
{
    // Figure out proper pattern for singleton behavior here.
    pimpl_ = &audio_handle;
    return pimpl_->Init(config, sai1, sai2);
}

AudioHandle::Result AudioHandle::DeInit()
{
    return pimpl_->DeInit();
}

const AudioHandle::Config& AudioHandle::GetConfig() const
{
    return pimpl_->config_;
}

size_t AudioHandle::GetChannels() const
{
    return pimpl_->GetChannels();
}

AudioHandle::Result AudioHandle::SetBlockSize(size_t size)
{
    return pimpl_->SetBlockSize(size);
}

float AudioHandle::GetSampleRate()
{
    return pimpl_->GetSampleRate();
}

AudioHandle::Result
AudioHandle::SetSampleRate(SaiHandle::Config::SampleRate samplerate)
{
    return pimpl_->SetSampleRate(samplerate);
}

AudioHandle::Result AudioHandle::Start(AudioCallback callback)
{
    return pimpl_->Start(callback);
}

AudioHandle::Result AudioHandle::Start(InterleavingAudioCallback callback)
{
    return pimpl_->Start(callback);
}

AudioHandle::Result AudioHandle::Stop()
{
    return pimpl_->Stop();
}

AudioHandle::Result AudioHandle::ChangeCallback(AudioCallback callback)
{
    return pimpl_->ChangeCallback(callback);
}

AudioHandle::Result
AudioHandle::ChangeCallback(InterleavingAudioCallback callback)
{
    return pimpl_->ChangeCallback(callback);
}

AudioHandle::Result AudioHandle::SetPostGain(float val)
{
    return pimpl_->SetPostGain(val);
}

AudioHandle::Result AudioHandle::SetOutputCompensation(float val)
{
    return pimpl_->SetOutputCompensation(val);
}

} // namespace daisy

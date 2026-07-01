#include "per/sai.h"
#include "daisy_core.h"
extern "C"
{
#include "util/hal_map.h"
}

namespace daisy
{
class SaiHandle::Impl
{
  public:
    enum class PeripheralBlock
    {
        BLOCK_A,
        BLOCK_B,
    };

    SaiHandle::Result        Init(const SaiHandle::Config& config);
    SaiHandle::Result        DeInit();
    const SaiHandle::Config& GetConfig() const { return config_; }

    HAL_StatusTypeDef InitProtocolTDM(SAI_HandleTypeDef* hsai, uint32_t nbslot);

    SaiHandle::Result StartDmaTransfer(int32_t*                       buffer_rx,
                                       int32_t*                       buffer_tx,
                                       size_t                         block_size,
                                       SaiHandle::CallbackFunctionPtr callback);
    SaiHandle::Result StopDmaTransfer();

    // Utility functions
    float  GetSampleRate();
    size_t GetBlockSize();
    float  GetBlockRate();

    SaiHandle::Config config_;
    SAI_HandleTypeDef sai_a_handle_, sai_b_handle_;
    DMA_HandleTypeDef sai_a_dma_handle_, sai_b_dma_handle_;

    // Data kept for Callback usage
    int32_t *                      buff_rx_, *buff_tx_;
    size_t                         buff_size_;
    SaiHandle::CallbackFunctionPtr callback_;

    /** Offset stored for accessing the correct part of the buffer from InternalCallback */
    size_t dma_offset;

    /** Callback that dispatches user callback from Cplt and HalfCplt DMA Callbacks */
    void InternalCallback(size_t offset);

    /** Pin Initiazlization */
    void InitPins();
    void DeInitPins();

    /** DMA Initialization */
    void InitDma(PeripheralBlock block);
    void DeInitDma(PeripheralBlock block);
};

// ================================================================
// Generic Error Handler
// ================================================================

static void Error_Handler()
{
    asm("bkpt 255");
    while(1) {}
}

// ================================================================
// Static References for available SaiHandle::Impls
// ================================================================

static SaiHandle::Impl sai_handles[2];

// ================================================================
// SAI Functions
// ================================================================

SaiHandle::Result SaiHandle::Impl::Init(const SaiHandle::Config& config)
{
    // do some init stuff
    const int sai_idx = int(config.periph);
    if(sai_idx >= 2)
        return Result::ERR;

    // Default Buffer states
    buff_rx_   = nullptr;
    buff_tx_   = nullptr;
    buff_size_ = 0;
    config_    = config;

    constexpr SAI_Block_TypeDef* a_instances[2] = {SAI1_Block_A, SAI2_Block_A};
    constexpr SAI_Block_TypeDef* b_instances[2] = {SAI1_Block_B, SAI2_Block_B};

    sai_a_handle_.Instance = a_instances[sai_idx];
    sai_b_handle_.Instance = b_instances[sai_idx];

    // Samplerate
    switch(config.sr)
    {
        case Config::SampleRate::SAI_8KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_8K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_8K;
            break;
        case Config::SampleRate::SAI_16KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_16K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_16K;
            break;
        case Config::SampleRate::SAI_32KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_32K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_32K;
            break;
        case Config::SampleRate::SAI_44_1KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_44K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_44K;
            break;
        case Config::SampleRate::SAI_48KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
            break;
        case Config::SampleRate::SAI_96KHZ:
            sai_a_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_96K;
            sai_b_handle_.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_96K;
            break;
        default: break;
    }
    // When both blocks are SLAVE (external master drives FS/SCK), exactly one
    // block must run SAI_ASYNCHRONOUS so that it samples FS/SCK from its own
    // external pins; the other block is internally synchronized to it
    // (SAI_SYNCHRONOUS). With both blocks SAI_SYNCHRONOUS, each waits for
    // clocks from the other over the intra-SAI sync bus and neither ever
    // looks at the pins: no clock is captured, no DMA completes, and the
    // audio callback never fires. config.ext_clock_block selects the block
    // wired to the external FS/SCK pins (block B on Daisy Seed SAI2).
    const bool both_slave = config.a_sync == Config::Sync::SLAVE
                            && config.b_sync == Config::Sync::SLAVE;

    // Audio Mode A
    if(config.a_sync == Config::Sync::MASTER)
    {
        sai_a_handle_.Init.AudioMode
            = config.a_dir == Config::Direction::TRANSMIT ? SAI_MODEMASTER_TX
                                                          : SAI_MODEMASTER_RX;
        sai_a_handle_.Init.Synchro = SAI_ASYNCHRONOUS;
    }
    else
    {
        sai_a_handle_.Init.AudioMode
            = config.a_dir == Config::Direction::TRANSMIT ? SAI_MODESLAVE_TX
                                                          : SAI_MODESLAVE_RX;
        sai_a_handle_.Init.Synchro
            = (both_slave
               && config.ext_clock_block == Config::ExtClockBlock::BLOCK_A)
                  ? SAI_ASYNCHRONOUS
                  : SAI_SYNCHRONOUS;
    }
    // Audio Mode B
    if(config.b_sync == Config::Sync::MASTER)
    {
        sai_b_handle_.Init.AudioMode
            = config.b_dir == Config::Direction::TRANSMIT ? SAI_MODEMASTER_TX
                                                          : SAI_MODEMASTER_RX;
        sai_b_handle_.Init.Synchro = SAI_ASYNCHRONOUS;
    }
    else
    {
        sai_b_handle_.Init.AudioMode
            = config.b_dir == Config::Direction::TRANSMIT ? SAI_MODESLAVE_TX
                                                          : SAI_MODESLAVE_RX;
        sai_b_handle_.Init.Synchro
            = (both_slave
               && config.ext_clock_block == Config::ExtClockBlock::BLOCK_B)
                  ? SAI_ASYNCHRONOUS
                  : SAI_SYNCHRONOUS;
    }
    // Bitdepth / protocol (currently based on .bit_depth)
    // TODO probably split these up for better flexibility..
    // These are also currently fixed to be the same per block.
    uint8_t  bd;
    uint32_t protocol;
    switch(config.bit_depth)
    {
        case Config::BitDepth::SAI_16BIT:
            bd       = SAI_PROTOCOL_DATASIZE_16BIT;
            protocol = SAI_I2S_STANDARD;
            break;
        case Config::BitDepth::SAI_24BIT:
            bd       = SAI_PROTOCOL_DATASIZE_24BIT;
            protocol = SAI_I2S_MSBJUSTIFIED;
            break;
        case Config::BitDepth::SAI_32BIT:
            // Untested Configuration
            bd       = SAI_PROTOCOL_DATASIZE_32BIT;
            protocol = SAI_I2S_STANDARD;
            break;
        default: break;
    }

    // Generic Inits that we don't have API control over.
    // A
    sai_a_handle_.Init.OutputDrive    = SAI_OUTPUTDRIVE_DISABLE;
    sai_a_handle_.Init.NoDivider      = SAI_MASTERDIVIDER_ENABLE;
    sai_a_handle_.Init.FIFOThreshold  = SAI_FIFOTHRESHOLD_EMPTY;
    sai_a_handle_.Init.SynchroExt     = SAI_SYNCEXT_DISABLE;
    sai_a_handle_.Init.MonoStereoMode = SAI_STEREOMODE;
    sai_a_handle_.Init.CompandingMode = SAI_NOCOMPANDING;
    sai_a_handle_.Init.TriState       = SAI_OUTPUT_NOTRELEASED;
    // B
    sai_b_handle_.Init.OutputDrive    = SAI_OUTPUTDRIVE_DISABLE;
    sai_b_handle_.Init.NoDivider      = SAI_MASTERDIVIDER_ENABLE;
    sai_b_handle_.Init.FIFOThreshold  = SAI_FIFOTHRESHOLD_EMPTY;
    sai_b_handle_.Init.SynchroExt     = SAI_SYNCEXT_DISABLE;
    sai_b_handle_.Init.MonoStereoMode = SAI_STEREOMODE;
    sai_b_handle_.Init.CompandingMode = SAI_NOCOMPANDING;
    sai_b_handle_.Init.TriState       = SAI_OUTPUT_NOTRELEASED;

#if 0 // use standard HAL_SAI_InitProtocol method for TDM and I2S
    uint32_t nbslot;
    if (config.tdm_slots > 0)
    {
        bd = SAI_PROTOCOL_DATASIZE_32BIT;
        protocol = SAI_PCM_SHORT;

        nbslot = config.tdm_slots;
    }
    else
    {
        nbslot = 2;
    }

    if(HAL_SAI_InitProtocol(&sai_a_handle_, protocol, bd, nbslot) != HAL_OK)
    {
        Error_Handler();
        return Result::ERR;
    }

    if(HAL_SAI_InitProtocol(&sai_b_handle_, protocol, bd, nbslot) != HAL_OK)
    {
        Error_Handler();
        return Result::ERR;
    }

#else // use custom InitProtocolTDM method for TDM and standard HAL_SAI_InitProtocol method for I2S

    if(config.tdm_slots > 0)
    {
        if (InitProtocolTDM(&sai_a_handle_, config.tdm_slots)  != HAL_OK)
        {
            Error_Handler();
            return Result::ERR;
         }
        if (InitProtocolTDM(&sai_b_handle_, config.tdm_slots)  != HAL_OK)
        {
            Error_Handler();
            return Result::ERR;
         }
    }
    else
    {
        if(HAL_SAI_InitProtocol(&sai_a_handle_, protocol, bd, 2) != HAL_OK)
        {
            Error_Handler();
            return Result::ERR;
        }

        if(HAL_SAI_InitProtocol(&sai_b_handle_, protocol, bd, 2) != HAL_OK)
        {
            Error_Handler();
            return Result::ERR;
        }
    }
#endif


    return Result::OK;
}

// similar to stm32h7xx_hal_sai.c/SAI_InitPCM(....)
HAL_StatusTypeDef SaiHandle::Impl::InitProtocolTDM(SAI_HandleTypeDef* hsai,
                                                   uint32_t           nbslot)
{
    // see: https://www.st.com/content/ccc/resource/training/technical/product_training/group0/d3/c0/b0/0e/fe/eb/40/a9/STM32H7-Peripheral-Serial-Audio-Interface_SAI/files/STM32H7-Peripheral-Serial-Audio-Interface_SAI.pdf/_jcr_content/translations/en.STM32H7-Peripheral-Serial-Audio-Interface_SAI.pdf
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t datasize = SAI_PROTOCOL_DATASIZE_32BIT; // fix to 32 bit for transfer/reality of protcol (works with ak4619). data is converted in audio.cpp/InternalCallback corresponding to bit_depth
    uint32_t protocol = SAI_PCM_SHORT;

    /* Check the parameters */
    assert_param(IS_SAI_SUPPORTED_PROTOCOL(protocol));
    assert_param(IS_SAI_PROTOCOL_DATASIZE(datasize));

    hsai->Init.Protocol = SAI_FREE_PROTOCOL;
    hsai->Init.FirstBit = SAI_FIRSTBIT_MSB;
    /* Standard TDM/I2S-style clocking: data lines change on the SCK falling
     * edge and are sampled on the rising edge (CKSTR=1 per RM0433), matching
     * the i.MX SAI of the Teensy 4.x (BCP=1) as well as the AK4619.
     * Beware: HAL_SAI_Init maps ClockStrobing to the CKSTR bit DIRECTION-
     * DEPENDENTLY (stm32h7xx_hal_sai.c): TX blocks get CKSTR=1 from
     * FALLINGEDGE, RX blocks get CKSTR=1 from RISINGEDGE. So to land on
     * CKSTR=1 for every block, TX and RX must use opposite HAL constants. */
    if((hsai->Init.AudioMode == SAI_MODEMASTER_TX)
       || (hsai->Init.AudioMode == SAI_MODESLAVE_TX))
    {
        hsai->Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
    }
    else
    {
        hsai->Init.ClockStrobing = SAI_CLOCKSTROBING_RISINGEDGE;
    }

    hsai->FrameInit.FSDefinition = SAI_FS_STARTFRAME;
    hsai->FrameInit.FSPolarity
        = config_.tdm_fs_polarity == Config::TdmFsPolarity::ACTIVE_HIGH
              ? SAI_FS_ACTIVE_HIGH
              : SAI_FS_ACTIVE_LOW;
    hsai->FrameInit.FSOffset      = SAI_FS_BEFOREFIRSTBIT; // FS asserted one bit before the first data bit (matches i.MX "FSE" early frame sync)
    hsai->SlotInit.FirstBitOffset = 0;
    hsai->SlotInit.SlotNumber     = nbslot;
    hsai->SlotInit.SlotActive     = SAI_SLOTACTIVE_ALL;

    switch(protocol)
    {
        case SAI_PCM_SHORT: hsai->FrameInit.ActiveFrameLength = 1; break;
        case SAI_PCM_LONG: hsai->FrameInit.ActiveFrameLength = 13; break;
        default: status = HAL_ERROR; break;
    }

    switch(datasize)
    {
        case SAI_PROTOCOL_DATASIZE_16BIT:
            hsai->Init.DataSize         = SAI_DATASIZE_16;
            hsai->FrameInit.FrameLength = 16U * nbslot;
            hsai->SlotInit.SlotSize     = SAI_SLOTSIZE_16B;
            break;
        case SAI_PROTOCOL_DATASIZE_16BITEXTENDED:
            hsai->Init.DataSize         = SAI_DATASIZE_16;
            hsai->FrameInit.FrameLength = 32U * nbslot;
            hsai->SlotInit.SlotSize     = SAI_SLOTSIZE_32B;
            break;
        case SAI_PROTOCOL_DATASIZE_24BIT:
            hsai->Init.DataSize         = SAI_DATASIZE_24;
            hsai->FrameInit.FrameLength = 32U * nbslot;
            hsai->SlotInit.SlotSize     = SAI_SLOTSIZE_32B;
            break;
        case SAI_PROTOCOL_DATASIZE_32BIT:
            hsai->Init.DataSize         = SAI_DATASIZE_32;
            hsai->FrameInit.FrameLength = 32U * nbslot;
            hsai->SlotInit.SlotSize     = SAI_SLOTSIZE_32B;
            break;
        default: status = HAL_ERROR; break;
    }
    if(status == HAL_OK)
    {
        status = HAL_SAI_Init(hsai);
    }

    return status;
}


SaiHandle::Result SaiHandle::Impl::DeInit()
{
    // Must have been initialized before deinitialization
    if(&config_ == nullptr)
        return Result::ERR;

    DeInitDma(PeripheralBlock::BLOCK_A);
    DeInitDma(PeripheralBlock::BLOCK_B);

    if(HAL_SAI_DeInit(&sai_a_handle_) != HAL_OK)
        return Result::ERR;
    if(HAL_SAI_DeInit(&sai_b_handle_) != HAL_OK)
        return Result::ERR;

    return Result::OK;
}

void SaiHandle::Impl::InitDma(PeripheralBlock block)
{
    SAI_HandleTypeDef* hsai;
    DMA_HandleTypeDef* hdma;
    uint32_t           req, dir;

    const int sai_idx = int(config_.periph);

    if(block == PeripheralBlock::BLOCK_A)
    {
        hsai = &sai_a_handle_;
        hdma = &sai_a_dma_handle_;
        dir  = config_.a_dir == Config::Direction::RECEIVE
                  ? DMA_PERIPH_TO_MEMORY
                  : DMA_MEMORY_TO_PERIPH;
        req = sai_idx == int(Config::Peripheral::SAI_1) ? DMA_REQUEST_SAI1_A
                                                        : DMA_REQUEST_SAI2_A;

        if(sai_idx == int(Config::Peripheral::SAI_1))
            hdma->Instance = DMA1_Stream0;
        else
            hdma->Instance = DMA1_Stream3;
    }
    else
    {
        hsai = &sai_b_handle_;
        hdma = &sai_b_dma_handle_;
        dir  = config_.b_dir == Config::Direction::RECEIVE
                  ? DMA_PERIPH_TO_MEMORY
                  : DMA_MEMORY_TO_PERIPH;
        req = sai_idx == int(Config::Peripheral::SAI_1) ? DMA_REQUEST_SAI1_B
                                                        : DMA_REQUEST_SAI2_B;

        if(sai_idx == int(Config::Peripheral::SAI_1))
            hdma->Instance = DMA1_Stream1;
        else
            hdma->Instance = DMA1_Stream4;
    }

    // Generic
    hdma->Init.Request             = req;
    hdma->Init.Direction           = dir;
    hdma->Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma->Init.MemInc              = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma->Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    hdma->Init.Mode                = DMA_CIRCULAR;
    hdma->Init.Priority            = DMA_PRIORITY_HIGH;
    hdma->Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(hdma) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_LINKDMA(hsai, hdmarx, *hdma);
    __HAL_LINKDMA(hsai, hdmatx, *hdma);
}

void SaiHandle::Impl::DeInitDma(PeripheralBlock block)
{
    if(block == PeripheralBlock::BLOCK_A)
    {
        HAL_DMA_DeInit(sai_a_handle_.hdmarx);
        HAL_DMA_DeInit(sai_a_handle_.hdmatx);
    }
    else if(block == PeripheralBlock::BLOCK_B)
    {
        HAL_DMA_DeInit(sai_b_handle_.hdmarx);
        HAL_DMA_DeInit(sai_b_handle_.hdmatx);
    }
}

void SaiHandle::Impl::InternalCallback(size_t offset)
{
    int32_t *in, *out;
    in  = buff_rx_ + offset;
    out = buff_tx_ + offset;
    if(callback_)
        callback_(in, out, buff_size_ / 2);
}

SaiHandle::Result
SaiHandle::Impl::StartDmaTransfer(int32_t*                       buffer_rx,
                                  int32_t*                       buffer_tx,
                                  size_t                         block_size,
                                  SaiHandle::CallbackFunctionPtr callback)
{
    buff_rx_   = buffer_rx;
    buff_tx_   = buffer_tx;
    dma_offset = 0; // fresh run: GetOffset() must not report a stale half

    size_t tdm_slots = GetConfig().tdm_slots;
    size_t ch = tdm_slots > 0 ? tdm_slots : 2; // backwards compatible: if tdm_slots is 0 the act like sai2_ is 2 channels

    buff_size_ = block_size * 2 * ch;
    callback_  = callback;

    // The block configured SAI_SYNCHRONOUS must be enabled before the block
    // it gets its clocks from (SAI_ASYNCHRONOUS), per RM0433. The async block
    // is: the MASTER one if any, else (both slave) the one selected by
    // ext_clock_block.
    const bool both_slave = config_.a_sync == Config::Sync::SLAVE
                            && config_.b_sync == Config::Sync::SLAVE;
    const bool a_is_async
        = config_.a_sync == Config::Sync::MASTER
          || (both_slave
              && config_.ext_clock_block == Config::ExtClockBlock::BLOCK_A);

    if(!a_is_async)
    {
        config_.a_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_a_handle_, (uint8_t*)buffer_rx, buff_size_)
            : HAL_SAI_Transmit_DMA(&sai_a_handle_, (uint8_t*)buffer_tx, buff_size_);
        config_.b_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_b_handle_, (uint8_t*)buffer_rx, buff_size_)
            : HAL_SAI_Transmit_DMA(&sai_b_handle_, (uint8_t*)buffer_tx, buff_size_);
    }
    else
    {
        config_.b_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_b_handle_, (uint8_t*)buffer_rx, buff_size_)
            : HAL_SAI_Transmit_DMA(&sai_b_handle_, (uint8_t*)buffer_tx, buff_size_);
        config_.a_dir == Config::Direction::RECEIVE
            ? HAL_SAI_Receive_DMA(&sai_a_handle_, (uint8_t*)buffer_rx, buff_size_)
            : HAL_SAI_Transmit_DMA(&sai_a_handle_, (uint8_t*)buffer_tx, buff_size_);
    }

    return Result::OK;
}
SaiHandle::Result SaiHandle::Impl::StopDmaTransfer()
{
    HAL_SAI_DMAStop(&sai_a_handle_);
    HAL_SAI_DMAStop(&sai_b_handle_);
    return Result::OK;
}

float SaiHandle::Impl::GetSampleRate()
{
    switch(config_.sr)
    {
        case Config::SampleRate::SAI_8KHZ: return 8000.f;
        case Config::SampleRate::SAI_16KHZ: return 16000.f;
        case Config::SampleRate::SAI_32KHZ: return 32000.f;
        case Config::SampleRate::SAI_44_1KHZ: return 44100.f;
        case Config::SampleRate::SAI_48KHZ: return 48000.f;
        case Config::SampleRate::SAI_96KHZ: return 96000.f;
        default: return 48000.f;
    }
}
size_t SaiHandle::Impl::GetBlockSize()
{
    // Buffer handled in halves, 1 sample per channel per frame
    size_t ch = config_.tdm_slots > 0 ? config_.tdm_slots : 2;
    return buff_size_ / 2 / ch;
}
float SaiHandle::Impl::GetBlockRate()
{
    return GetSampleRate() / GetBlockSize();
}

void SaiHandle::Impl::InitPins()
{
    bool             is_master;
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_TypeDef*    port;
    dsy_gpio_pin*    cfg[] = {&config_.pin_config.fs,
                           &config_.pin_config.mclk,
                           &config_.pin_config.sck,
                           &config_.pin_config.sa,
                           &config_.pin_config.sb};
    // Special Case checks
    dsy_gpio_pin sck_af_pin = {DSY_GPIOA, 2};
    is_master               = (config_.a_sync == Config::Sync::MASTER
                 || config_.b_sync == Config::Sync::MASTER);
    // Generics
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    for(size_t i = 0; i < 5; i++)
    {
        // Skip MCLK if not master.
        if(dsy_pin_cmp(&config_.pin_config.mclk, cfg[i]) && !is_master)
            continue;

        // Special case for SAI2-sck (should add a map for Alternate Functions at some point..)
        switch(config_.periph)
        {
            case Config::Peripheral::SAI_1:
                GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
                break;
            case Config::Peripheral::SAI_2:
                GPIO_InitStruct.Alternate = dsy_pin_cmp(cfg[i], &sck_af_pin)
                                                ? GPIO_AF8_SAI2
                                                : GPIO_AF10_SAI2;
                break;
            default: break;
        }

        GPIO_InitStruct.Pin = dsy_hal_map_get_pin(cfg[i]);
        port                = dsy_hal_map_get_port(cfg[i]);
        HAL_GPIO_Init(port, &GPIO_InitStruct);
    }
}

void SaiHandle::Impl::DeInitPins()
{
    GPIO_TypeDef* port;
    uint16_t      pin;
    bool          is_master;
    is_master = (config_.a_sync == Config::Sync::MASTER
                 || config_.b_sync == Config::Sync::MASTER);
    if(is_master)
    {
        port = dsy_hal_map_get_port(&config_.pin_config.mclk);
        pin  = dsy_hal_map_get_pin(&config_.pin_config.mclk);
        HAL_GPIO_DeInit(port, pin);
    }
    port = dsy_hal_map_get_port(&config_.pin_config.fs);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.fs);
    HAL_GPIO_DeInit(port, pin);
    port = dsy_hal_map_get_port(&config_.pin_config.sck);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.sck);
    HAL_GPIO_DeInit(port, pin);
    port = dsy_hal_map_get_port(&config_.pin_config.sa);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.sa);
    HAL_GPIO_DeInit(port, pin);
    port = dsy_hal_map_get_port(&config_.pin_config.sb);
    pin  = dsy_hal_map_get_pin(&config_.pin_config.sb);
    HAL_GPIO_DeInit(port, pin);
}

// ================================================================
// HAL Service Functions
// ================================================================

extern "C" void HAL_SAI_MspInit(SAI_HandleTypeDef* hsai)
{
    // Due to the BlockA/BlockB stuff right now
    // it is required that they both be used.
    // We could add a layer of separate config for them.
    if(hsai->Instance == SAI1_Block_A || hsai->Instance == SAI1_Block_B)
    {
        // Since this isn't using the daisy gpio yet, we need to manually start the necessary pclks
        // these aren't all used on all hardware, but they cover existing hardware so far.
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOE_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_SAI1_CLK_ENABLE();
        sai_handles[0].InitPins();
        __HAL_RCC_DMA1_CLK_ENABLE();
        sai_handles[0].InitDma(hsai->Instance == SAI1_Block_A
                                   ? SaiHandle::Impl::PeripheralBlock::BLOCK_A
                                   : SaiHandle::Impl::PeripheralBlock::BLOCK_B);
    }
    else if(hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_SAI2_CLK_ENABLE();
        sai_handles[1].InitPins();
        __HAL_RCC_DMA1_CLK_ENABLE();
        sai_handles[1].InitDma(hsai->Instance == SAI2_Block_A
                                   ? SaiHandle::Impl::PeripheralBlock::BLOCK_A
                                   : SaiHandle::Impl::PeripheralBlock::BLOCK_B);
    }
}
extern "C" void HAL_SAI_MspDeInit(SAI_HandleTypeDef* hsai)
{
    if(hsai->Instance == SAI1_Block_A)
    {
        __HAL_RCC_SAI1_CLK_DISABLE();
        sai_handles[0].DeInitPins();
    }
    else if(hsai->Instance == SAI2_Block_A)
    {
        __HAL_RCC_SAI2_CLK_DISABLE();
        sai_handles[1].DeInitPins();
    }
}

// ================================================================
// ISRs and event handlers
// ================================================================

extern "C" void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[0].sai_a_dma_handle_);
}

extern "C" void DMA1_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[0].sai_b_dma_handle_);
}

extern "C" void DMA1_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[1].sai_a_dma_handle_);
}

extern "C" void DMA1_Stream4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&sai_handles[1].sai_b_dma_handle_);
}

extern "C" void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef* hsai)
{
    if(hsai->Instance == SAI1_Block_A || hsai->Instance == SAI1_Block_B)
    {
        sai_handles[0].dma_offset = 0;
        sai_handles[0].InternalCallback(0);
    }
    else if(hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B)
    {
        sai_handles[1].dma_offset = 0;
        sai_handles[1].InternalCallback(0);
    }
}

extern "C" void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef* hsai)
{
    if(hsai->Instance == SAI1_Block_A || hsai->Instance == SAI1_Block_B)
    {
        sai_handles[0].dma_offset = sai_handles[0].buff_size_ / 2;
        sai_handles[0].InternalCallback(sai_handles[0].dma_offset);
    }
    else if(hsai->Instance == SAI2_Block_A || hsai->Instance == SAI2_Block_B)
    {
        sai_handles[1].dma_offset = sai_handles[1].buff_size_ / 2;
        sai_handles[1].InternalCallback(sai_handles[1].dma_offset);
    }
}

// ================================================================
// SaiHandle -> SaiHandle::Pimpl
// ================================================================

SaiHandle::Result SaiHandle::Init(const Config& config)
{
    pimpl_ = &sai_handles[int(config.periph)];
    return pimpl_->Init(config);
}
SaiHandle::Result SaiHandle::DeInit()
{
    return pimpl_->DeInit();
}
const SaiHandle::Config& SaiHandle::GetConfig() const
{
    return pimpl_->GetConfig();
}

SaiHandle::Result SaiHandle::StartDma(int32_t*            buffer_rx,
                                      int32_t*            buffer_tx,
                                      size_t              block_size,
                                      CallbackFunctionPtr callback)
{
    return pimpl_->StartDmaTransfer(buffer_rx, buffer_tx, block_size, callback);
}

SaiHandle::Result SaiHandle::StopDma()
{
    return pimpl_->StopDmaTransfer();
}

float SaiHandle::GetSampleRate()
{
    return pimpl_->GetSampleRate();
}

size_t SaiHandle::GetBlockSize()
{
    return pimpl_->GetBlockSize();
}

float SaiHandle::GetBlockRate()
{
    return pimpl_->GetBlockRate();
}

size_t SaiHandle::GetOffset() const
{
    return pimpl_->dma_offset;
}


} // namespace daisy

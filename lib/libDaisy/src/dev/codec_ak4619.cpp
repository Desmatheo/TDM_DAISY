#include "dev/codec_ak4619.h"
#include "sys/system.h"

#define I2C_ADDR 0x20 // I2C slave address (0x10<<1) and RW bit (W=0)

#define DISABLE_DC 1
#define DISABLE_ADC_MIC_GAIN 0

// Datasheet: 9.14. Register Map
#define NUM_REG 21
const uint8_t config[NUM_REG] = {
    0b00110111, // 0x00 Power Management (all Normal Operation)
#if 1 
    0b10101111, // 0x01 Audio I/F Format (TDM=1, DCF=010=I2S compatible, DSL=11=32bit, BCKP=1=Rising  with LRCK, SDOPH=1=Fast) >>> Figure 17
#else
    0b11111110, // 0x01 Audio I/F Format (TDM=1, DCF=111=MSB justified,  DSL=11=32bit, BCKP=1=Rising  with LRCK, SDOPH=0=Slow) >>> Figure 18
#endif
    0b00011100, // 0x02 Reset Control =~ Audio I/F Format 2 (SLOT=1, DIDL=11=32-Bit, DODL=00=24-Bit)
    0b00000000, // 0x03 System Clock Setting (FS=000 = MCLK=256*fs, BICK=128*fs, fs=48khz )
#if DISABLE_ADC_MIC_GAIN
    0b00100010, // 0x04 MIC AMP Gain (0010 = 0dB, 0010 = 0dB)
    0b00100010, // 0x05 MIC AMP Gain (0010 = 0dB, 0010 = 0dB)
#else
#if 0 // 3db
    0b00110011, // 0x04 MIC AMP Gain (0011 = 3dB.....)
    0b00110011, // 0x05 MIC AMP Gain
#else // 6db
    0b01000100, // 0x04 MIC AMP Gain (0100 = 6dB.....)
    0b01000100, // 0x05 MIC AMP Gain
#endif
#endif
    0b00110000, // 0x06 ADC1 Lch Digital Volume (0011 = 0dB)
    0b00110000, // 0x07 ADC1 Rch Digital Volume (0011 = 0dB)
    0b00110000, // 0x08 ADC2 Lch Digital Volume (0011 = 0dB)
    0b00110000, // 0x09 ADC2 Rch Digital Volume (0011 = 0dB)
    0b00100010, // 0x0A ADC Digital Filter Setting (both Short Delay Sharp Roll-Off Filter)
    0b01010101, // 0x0B ADC Analog Input Setting (all Single-Ended1 = AIN1L, AIN1R, AIN4L, AIN4R)
    0b00000000, // 0x0C Reserved
#if DISABLE_DC
    0b00000110, // 0x0D ADC Mute & HPF Control (ADC1/2 Soft Mute Disable, ADC1/2 HPF = 1 = Disable) --> AD1HPFN: ADC1 DC Offset Cancel HPF
#else
    0b00000000, // 0x0D ADC Mute & HPF Control (ADC1/2 Soft Mute Disable, ADC1/2 HPF = 0 =  enable) --> AD1HPFN: ADC1 DC Offset Cancel HPF
#endif
    0b00011000, // 0x0E DAC1 Lch Digital Volume (0x18 = 0dB)
    0b00011000, // 0x0F DAC1 Rch Digital Volume (0x18 = 0dB)
    0b00011000, // 0x10 DAC2 Lch Digital Volume (0x18 = 0dB)
    0b00011000, // 0x11 DAC2 Rch Digital Volume (0x18 = 0dB)
    0b00000100, // 0x12 DAC Input Select Setting (DAC1 = SDIN1, DAC2=SDIN2) (can be used to route audio from ADC directly to DAC)
    0b00000101, // 0x13 DAC De-Emphasis Setting (off)
    0b00001010  // 0x14 DAC Mute & Filter Setting
};


namespace daisy
{
AK4619::Result AK4619::Init(I2CHandle i2c)
{
    i2c_ = i2c;

    // TODO: Reset the codec (though by default we may not need to do this)
    for(int i = 0; i < NUM_REG; i++)
    {
        if(WriteRegister(i, config[i]) != Result::OK)
            return Result::ERR;

        System::Delay(4);
    }

    // Success
    return Result::OK;
}


AK4619::Result AK4619::ReadRegister(uint8_t addr, uint8_t* data)
{
    if(i2c_.ReadDataAtAddress(I2C_ADDR, addr, 1, data, 1, 250)
       != I2CHandle::Result::OK)
    {
        return Result::ERR;
    }
    return Result::OK;
}

AK4619::Result AK4619::WriteRegister(uint8_t addr, uint8_t val)
{
    if(i2c_.WriteDataAtAddress(I2C_ADDR, addr, 1, &val, 1, 250)
       != I2CHandle::Result::OK)
    {
        return Result::ERR;
    }
    return Result::OK;
}

} // namespace daisy

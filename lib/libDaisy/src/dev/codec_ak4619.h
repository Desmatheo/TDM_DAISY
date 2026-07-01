#pragma once
#ifndef DSY_CODEC_AK4619_H
#define DSY_CODEC_AK4619_H
#include "per/i2c.h"
namespace daisy
{
/**
 * @brief Driver for the AK4619 Audio Codec.
 * @addtogroup codec
 * 
 * For now this uses only I2C to communicate with the AK4619
 * Developed and tested with github.com/apfelaudio/eurorack-pmod
 * 
 */
class AK4619
{
  public:
    enum class Result
    {
        OK,
        ERR,
    };

    AK4619() {}
    ~AK4619() {}

    /** Initializes the AK4619
     * \param i2c Initialized I2CHandle configured at 400kHz or less
     */
    Result Init(I2CHandle i2c);
  private:
    /** Reads the data byte corresponding to the register address */
    Result ReadRegister(uint8_t addr, uint8_t *data);

    /** Writes the specified byte to the register at the specified address.*/
    Result WriteRegister(uint8_t addr, uint8_t val);

    I2CHandle i2c_;
    uint8_t   dev_addr_;
};

} // namespace daisy
#endif
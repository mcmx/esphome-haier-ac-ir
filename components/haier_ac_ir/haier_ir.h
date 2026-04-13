#pragma once

#include "esphome.h"
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/climate/climate_mode.h"

namespace esphome {
namespace haier_ac_ir {

static const uint32_t MARK = 530;
static const uint32_t SPACE_ZERO = 575;
static const uint32_t SPACE_ONE = 1650;

static const uint32_t PREAMBULE[] = {
    3100,
    3100,
    3100,
    4500,
};
static const uint8_t PREFIX = 0b10100110;
static const uint8_t PACKET_SIZE = 13;
static const uint8_t BURST_SIZE = 230;

static const char* const TAG = "haier_ac_ir.climate";
static const int MIN_TEMP = 16;
static const int MAX_TEMP = 30;
static const float STEP_TEMP = 1.0f;

enum swing_t : uint8_t {
    SWING_OFF = 0b0000,
    SWING_UP = 0b0010,
    SWING_UP_WIDE = 0b0001,
    SWING_DOWN_WIDE = 0b0011,
    SWING_OSCILATE = 0b1100,
};

enum speed_t : uint8_t {
    SPEED_LOW = 3,
    SPEED_MEDIUM = 2,
    SPEED_HIGH = 1,
    SPEED_AUTO = 5,
};

enum ac_mode_t : uint8_t {
    MODE_AUTO = 0b000,
    MODE_COOLING = 0b001,
    MODE_HEATING = 0b100,
    MODE_FAN = 0b110,
    MODE_DEHUMIDIFICATION = 0b010,
};


class HaierIRClimate : public climate_ir::ClimateIR {
public:
    HaierIRClimate()
        : climate_ir::ClimateIR(
            MIN_TEMP,
            MAX_TEMP,
            STEP_TEMP,
            true, // supports_dry
            true, // supports_fan_only
            { 
                climate::CLIMATE_FAN_AUTO,
                climate::CLIMATE_FAN_LOW,
                climate::CLIMATE_FAN_MEDIUM,
                climate::CLIMATE_FAN_HIGH
            },
            { 
                climate::CLIMATE_SWING_OFF,
                // climate::CLIMATE_SWING_BOTH,
                // climate::CLIMATE_SWING_VERTICAL,
                climate::CLIMATE_SWING_HORIZONTAL
            },
            {
                climate::CLIMATE_PRESET_NONE,
                climate::CLIMATE_PRESET_BOOST,
                climate::CLIMATE_PRESET_SLEEP
            }
        )
    {
    }

protected:

    climate::ClimateTraits traits() override;

    void printBin(uint8_t bin);

    void printBinR(uint8_t bin);

    // говнокод
    uint8_t readUnallinedByte(uint8_t array[], uint8_t offset, uint8_t length);

    void setBits(uint8_t array[], uint8_t value, uint8_t bit_offset, uint8_t num_bits);

    uint8_t calc_checksum(uint8_t array[]);
    uint8_t calc_checksum_r(uint8_t array[]);

    void transmit_state() override;

    bool on_receive(remote_base::RemoteReceiveData data) override;
};

} // namespace haier_ac_ir
} // namespace esphome
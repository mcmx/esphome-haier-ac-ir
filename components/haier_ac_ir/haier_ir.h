#pragma once

#include "esphome.h"
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/climate/climate_mode.h"

namespace esphome {
namespace haier_ac_ir {

static const uint8_t PACKET_SIZE = 13;
static const uint8_t PREFIX = 0xA6;

static const char* const TAG = "haier_ac_ir.climate";
static const int MIN_TEMP = 16;
static const int MAX_TEMP = 30;
static const float STEP_TEMP = 1.0f;

enum swing_t : uint8_t {
    SWING_1 = 0x02,
    SWING_2 = 0x04,
    SWING_3 = 0x06,
    SWING_4 = 0x08,
    SWING_5 = 0x0A, // not used
    SWING_AUTO = 0x0C,
};

enum speed_t : uint8_t {
    SPEED_LOW = 0x60,
    SPEED_MEDIUM = 0x40,
    SPEED_HIGH = 0x20,
    SPEED_AUTO = 0xA0,
};

enum ac_mode_t : uint8_t {
    MODE_RECIRCULATION = 0x00,
    MODE_COOLING = 0x20,
    MODE_DEHUMIDIFICATION = 0x40,
    MODE_FAN = 0xC0,
    MODE_HEATING = 0x80,
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
                climate::CLIMATE_SWING_BOTH,
                climate::CLIMATE_SWING_VERTICAL,
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

    // Trackers to detect what specifically changed
    float last_temp_ = 25.0f;
    uint8_t last_mode_ = climate::CLIMATE_MODE_OFF;
    uint8_t state_ = 0;
    climate::ClimateFanMode last_fan_ = climate::CLIMATE_FAN_AUTO;
    climate::ClimateSwingMode last_swing_ = climate::CLIMATE_SWING_OFF;

    climate::ClimateTraits traits() override;

    // говнокод
    uint8_t readUnallinedByte(uint8_t array[], uint8_t offset, uint8_t length);

    void transmit_state() override;

    bool on_receive(remote_base::RemoteReceiveData data) override;
};

} // namespace haier_ac_ir
} // namespace esphome
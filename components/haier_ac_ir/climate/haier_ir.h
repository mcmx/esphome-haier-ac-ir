#pragma once

#include "esphome.h"
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/climate/climate_mode.h"

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
static const uint8_t PACKET_SIZE = 14;
static const uint8_t BURST_SIZE = 230;

static const char* const TAG = "haier.climate";
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

class HaierClimate : public climate_ir::ClimateIR {
public:
    HaierClimate()
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
    void printBin(uint8_t bin) {
        ESP_LOGD("custom", "%c%c%c%c%c%c%c%c",
            ((bin) & 0x80 ? '1' : '0'),
            ((bin) & 0x40 ? '1' : '0'),
            ((bin) & 0x20 ? '1' : '0'),
            ((bin) & 0x10 ? '1' : '0'),
            ((bin) & 0x08 ? '1' : '0'),
            ((bin) & 0x04 ? '1' : '0'),
            ((bin) & 0x02 ? '1' : '0'),
            ((bin) & 0x01 ? '1' : '0')
        );
    }

    void printBinR(uint8_t bin) {
        ESP_LOGD("custom", "%c%c%c%c%c%c%c%c",
            ((bin) & 0x01 ? '1' : '0'),
            ((bin) & 0x02 ? '1' : '0'),
            ((bin) & 0x04 ? '1' : '0'),
            ((bin) & 0x08 ? '1' : '0'),
            ((bin) & 0x10 ? '1' : '0'),
            ((bin) & 0x20 ? '1' : '0'),
            ((bin) & 0x40 ? '1' : '0'),
            ((bin) & 0x80 ? '1' : '0')
        );
    }

    // говнокод
    uint8_t readUnallinedByte(uint8_t array[], uint8_t offset, uint8_t length) {
        uint8_t result = 0;

        uint8_t mask = 1 << (length - 1);
        for (uint8_t i = offset; i < offset + length; i++) {
            bool b = (array[i / 8] >> (i % 8)) & 0b1;

            if (b) {
                result |= mask;
            }

            mask >>= 1;
        }

        return result;
    }

    void setByte(uint8_t array[], uint8_t value, uint8_t offset, uint8_t length) {
        array[offset / 8] |= value << 8 - (offset % 8) - length;
    }

    uint8_t calc_checksum(uint8_t array[]) {
        uint8_t checksum = 0;
        
        for (uint8_t i = 0; i < PACKET_SIZE - 1; i++) {
            checksum += array[i];
        }

        return checksum;
    }

    uint8_t calc_checksum_r(uint8_t array[]) {
        uint8_t checksum = 0;
        
        for (uint8_t i = 0; i < PACKET_SIZE - 1; i++) {
            checksum += this->readUnallinedByte(array, i * 8, 8);
        }

        return checksum;
    }

    void transmit_state() override
    {
        uint8_t raw[PACKET_SIZE];

        for (uint8_t i = 0; i < sizeof(raw); i++) {
            raw[i] = 0;
        }

        this->setByte(raw, PREFIX, 0, 8);

        uint8_t temp = this->target_temperature - 16;

        uint8_t swing;
        switch (this->swing_mode) {
            case CLIMATE_SWING_OFF:
                swing = SWING_OFF;
                break;
            case CLIMATE_SWING_VERTICAL:
                swing = SWING_UP_WIDE;
                break;
            case CLIMATE_SWING_HORIZONTAL:
                swing = SWING_DOWN_WIDE;
                break;
            case CLIMATE_SWING_BOTH:
                swing = SWING_OSCILATE;
                break;
            default:
                swing = SWING_OFF;
        }
        
        bool state;
        uint8_t mode;
        switch (this->mode)
        {
            case CLIMATE_MODE_OFF:
                state = false;
                mode = MODE_AUTO;
                break;
            case CLIMATE_MODE_AUTO:
            case CLIMATE_MODE_HEAT_COOL:
                state = true;
                mode = MODE_AUTO;
                break;
            case CLIMATE_MODE_COOL:
                state = true;
                mode = MODE_COOLING;
                break;
            case CLIMATE_MODE_HEAT:
                state = true;
                mode = MODE_HEATING;
                break;
            case CLIMATE_MODE_FAN_ONLY:
                state = true;
                mode = MODE_FAN;
                break;
            case CLIMATE_MODE_DRY:
                state = true;
                mode = MODE_DEHUMIDIFICATION;
                break;
            default:
                state = false;
                mode = MODE_AUTO;
                break;
        }

        uint8_t speed;
        switch (this->fan_mode.value_or(255))
        {
            case CLIMATE_FAN_AUTO:
                speed = SPEED_AUTO;
                break;

            case CLIMATE_FAN_LOW:
                speed = SPEED_LOW;
                break;
            
            case CLIMATE_FAN_MEDIUM:
                speed = SPEED_MEDIUM;
                break;

            case CLIMATE_FAN_HIGH:
                speed = SPEED_HIGH;
                break;

            default:
                speed = SPEED_AUTO;
                break;
        }

        bool silent, turbo;
            
        switch (this->preset.value_or(255)) {
            case CLIMATE_PRESET_NONE:
                silent = false;
                turbo = false;
                break;
            case CLIMATE_PRESET_SLEEP:
                silent = true;
                turbo = false;
                break;
            case CLIMATE_PRESET_BOOST:
                silent = false;
                turbo = true;
                break;
        }


        this->setByte(raw, temp, 8, 4);
        this->setByte(raw, swing, 12, 4);
        this->setByte(raw, state, 33, 1);
        this->setByte(raw, mode, 56, 3);
        this->setByte(raw, speed, 40, 3);
        this->setByte(raw, silent, 48, 1);
        this->setByte(raw, turbo, 49, 1);
        
        this->setByte(raw, this->calc_checksum(raw), 13 * 8, 8);

        for (uint8_t i = 0; i < sizeof(raw); i++) {
            this->printBin(raw[i]); 
        }
        
        auto transmit = this->transmitter_->transmit();
        auto dst = transmit.get_data();
        
        dst->set_carrier_frequency(38000);
        dst->reserve(BURST_SIZE);

        int cnt = 0;
        for (int i = 0; i < 2; i++) {
            dst->item(PREAMBULE[i * 2], PREAMBULE[i * 2 + 1]);
            cnt++;
        }

        for (uint8_t i = 0; i < sizeof(raw); i++) {
            for (uint8_t mask = 1 << 7; mask != 0; mask >>= 1) {
                dst->item(MARK, raw[i] & mask ? SPACE_ONE : SPACE_ZERO);
                cnt++;
            }
        }

        dst->item(MARK, -1000);

        transmit.perform();
    }

    bool on_receive(remote_base::RemoteReceiveData data) override
    {
        if (data.size() != BURST_SIZE) {
            ESP_LOGD("custom", "wrong data size %d", data.size());
            return false;
        }

        // data.set_tolerance(200);

        for(int i = 0; i < 2; i++) {
            // TODO: check preamble
            ESP_LOGV("custom", "p %d", data.peek());
            ESP_LOGV("custom", "p %d", data.peek(1));

            data.advance(2);
        }

        // ESP_LOGD("custom", "p %d", data.peek());
        // ESP_LOGD("custom", "p %d", data.peek(1));
        // data.advance(2);
        
        uint8_t raw[PACKET_SIZE];

        uint16_t size = 0;

        for (uint8_t i = 0; i < sizeof(raw); i++) {
            raw[i] = 0;

            for (uint8_t mask = 1; mask != 0; mask <<= 1) {
                // ESP_LOGD("custom", "%d %d", data.peek());
                // data.advance();

                // ESP_LOGD("custom", "index %d", data.get_index());
                // ESP_LOGD("custom", "%d mark %d", data.get_index(), data.peek());

                if (!data.expect_mark(MARK)) {
                    ESP_LOGV("custom", "wrong mark %d", data.peek());
                    // data.advance();
                    return false;
                }

                bool b = false;
                bool wrong = false;

                if (data.expect_space(SPACE_ZERO)) {
                    b = false; 
                    wrong = false;
                } else if (data.expect_space(SPACE_ONE)) {
                    b = true; 
                    wrong = false;
                } else {
                    wrong = true;
                    ESP_LOGV("custom", "wrong bit %d", data.peek());

                    data.advance();
                }
                    
                if (b && !wrong) {
                    raw[i] |= mask;
                }
                
                size++;

                if (data.size() <= data.get_index()) {
                    break;
                }
            }

            if (data.size() <= data.get_index()) {
                break;
            }
        }


        if (size != 112) {
            ESP_LOGV("custom", "wrong size %d", size);

            return false;
        }

        for (uint8_t i = 0; i < sizeof(raw); i++) {
            this->printBinR(raw[i]); 
        }
        
        auto prefix = this->readUnallinedByte(raw, 0, 8);
        if (prefix != PREFIX) {
            ESP_LOGV("custom", "wrong prefix %d", prefix);
            return false;
        } 

        uint8_t checksum_calc = this->calc_checksum_r(raw);

        auto checksum = this->readUnallinedByte(raw, 104, 8);
        
        if (checksum != checksum_calc) {
            ESP_LOGD("custom", "wrong checksum %d. calc: %d", checksum, checksum_calc);

            return false;
        }

        auto temp = this->readUnallinedByte(raw, 8, 4);
        uint8_t swing = this->readUnallinedByte(raw, 12, 4);
        bool state = this->readUnallinedByte(raw, 33, 1);
        uint8_t mode = this->readUnallinedByte(raw, 56, 3);
        uint8_t speed = this->readUnallinedByte(raw, 40, 3);
        bool silent = this->readUnallinedByte(raw, 48, 1);
        bool turbo = this->readUnallinedByte(raw, 49, 1);
        this->target_temperature = temp + 16;

        // Костыль
        switch (swing) {
            case SWING_OFF:
                this->swing_mode = CLIMATE_SWING_OFF;
                break;
            case SWING_UP:
            case SWING_UP_WIDE:
                this->swing_mode = CLIMATE_SWING_VERTICAL;
                break;
            case SWING_DOWN_WIDE:
                this->swing_mode = CLIMATE_SWING_HORIZONTAL;
                break;
            case SWING_OSCILATE:
                this->swing_mode = CLIMATE_SWING_BOTH;
                break;
            default:
                this->swing_mode = CLIMATE_SWING_OFF;
        }

        if (!state) {
            this->mode = CLIMATE_MODE_OFF;
        } else {
            switch (mode)
            {
                case MODE_AUTO:
                    this->mode = CLIMATE_MODE_HEAT_COOL;
                    break;
                case MODE_COOLING:
                    this->mode = CLIMATE_MODE_COOL;
                    break;
                case MODE_HEATING:
                    this->mode = CLIMATE_MODE_HEAT;
                    break;
                case MODE_FAN:
                    this->mode = CLIMATE_MODE_FAN_ONLY;
                    break;
                case MODE_DEHUMIDIFICATION:
                    this->mode = CLIMATE_MODE_DRY;
                    break;
                default:
                    this->mode = CLIMATE_MODE_AUTO;
                    break;
            }
        }
        
        switch (speed)
        {
            case SPEED_AUTO:
                this->fan_mode = CLIMATE_FAN_AUTO;
                break;

            case SPEED_LOW:
                this->fan_mode = CLIMATE_FAN_LOW;
                break;
            
            case SPEED_MEDIUM:
                this->fan_mode = CLIMATE_FAN_MEDIUM;
                break;

            case SPEED_HIGH:
                this->fan_mode = CLIMATE_FAN_HIGH;
                break;

            default:
                this->fan_mode = CLIMATE_FAN_AUTO;
                break;
        }

        if (silent) {
            this->preset = CLIMATE_PRESET_SLEEP;
        } else if (turbo) {
            this->preset = CLIMATE_PRESET_BOOST;
        } else {
            this->preset = CLIMATE_PRESET_NONE;
        }

        this->publish_state();

        return true;
    }
};

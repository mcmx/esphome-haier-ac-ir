#include "haier_ir.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/remote_base/haier_protocol.h"

namespace esphome {
namespace haier_ac_ir {


climate::ClimateTraits HaierIRClimate::traits() {
  auto traits = climate::ClimateTraits();
  if (this->sensor_ != nullptr) {
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  }
  traits.set_visual_min_temperature(MIN_TEMP);
  traits.set_visual_max_temperature(MAX_TEMP);
  traits.set_visual_temperature_step(1.0f);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF});

  if (this->supports_cool_)
    traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
  if (this->supports_heat_)
    traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);

  if (this->supports_cool_ && this->supports_heat_)
    traits.add_supported_mode(climate::CLIMATE_MODE_HEAT_COOL);

  if (this->supports_dry_)
    traits.add_supported_mode(climate::CLIMATE_MODE_DRY);
  if (this->supports_fan_only_)
    traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);

  // Default to only 3 levels in ESPHome even if most unit supports 4. The 3rd level is not used.
  traits.set_supported_fan_modes(
      {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH});

  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL, 
                                    climate::CLIMATE_SWING_HORIZONTAL, climate::CLIMATE_SWING_BOTH}
);

  traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
                                climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP});

  return traits;
}

uint8_t HaierIRClimate::readUnallinedByte(uint8_t array[], uint8_t offset, uint8_t length) {
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



void HaierIRClimate::transmit_state() {
    std::vector<uint8_t> packet(PACKET_SIZE, 0x00);
    packet[0] = PREFIX; // Header

    // --- Byte 1: Temperature ---
    packet[1] = (uint8_t)(this->target_temperature - 16) << 4; // Map 16-30°C to 0x00-0xE0 in steps of 0x10

    // --- Byte 8: Mode ---

    if (this->mode == climate::CLIMATE_MODE_OFF) {
        // packet[7] = this->last_mode_;
        // bit 3 of upper byte
        // todo set packet[4] = haier_ac_ir::MODE_OFF;
        // packet[12] = 0x05; // ON/OFF button
    } else {
        switch (this->mode) {
            case climate::CLIMATE_MODE_COOL:      packet[7] = haier_ac_ir::MODE_COOLING; break;
            case climate::CLIMATE_MODE_DRY:       packet[7] = haier_ac_ir::MODE_DEHUMIDIFICATION; break;
            case climate::CLIMATE_MODE_FAN_ONLY:  packet[7] = haier_ac_ir::MODE_FAN; break;
            case climate::CLIMATE_MODE_HEAT:      packet[7] = haier_ac_ir::MODE_HEATING; break;
            default:                              packet[7] = haier_ac_ir::MODE_RECIRCULATION; break;
        }
        this->last_mode_ = packet[7];
    }

    // --- Byte 3: Fan Speed ---
    switch (this->fan_mode.value()) {
        case climate::CLIMATE_FAN_LOW:    packet[5] = haier_ac_ir::SPEED_LOW; break;
        case climate::CLIMATE_FAN_MEDIUM: packet[5] = haier_ac_ir::SPEED_MEDIUM; break;
        case climate::CLIMATE_FAN_HIGH:   packet[5] = haier_ac_ir::SPEED_HIGH; break;
        default:                          packet[5] = haier_ac_ir::SPEED_AUTO; break;
    }

    // --- Byte 1: Swing ---

    uint8_t swing_val = haier_ac_ir::SWING_AUTO; // default to auto if not set
    switch (this->swing_mode){
        case climate::CLIMATE_SWING_OFF:
            swing_val = haier_ac_ir::SWING_1;
            break;
        case climate::CLIMATE_SWING_VERTICAL:
            swing_val = haier_ac_ir::SWING_2;
            break;
        case climate::CLIMATE_SWING_HORIZONTAL:
            swing_val = haier_ac_ir::SWING_3;
            break;
        case climate::CLIMATE_SWING_BOTH:
            swing_val = haier_ac_ir::SWING_AUTO;
            break;
        default: break;
    }
    packet[1] = packet[1] | swing_val; // Set lower 4 bits for swing

    // --- Byte 5: POWER & AUX (Bitmask Logic) ---
    uint8_t byte5 = this->state_; // Start with previous state to preserve unchanged bits
    if (this->mode != climate::CLIMATE_MODE_OFF) {
        // power on bits: 0100 or 0111
        byte5 |= 0x40; // Set Bit 6 to 1 (Power On)
    } else {
        // power off bits: 0000 or 0011
        byte5 &= ~0x60; // Set Bit 6 to 0 (Power Off)
    }
    this->state_ = byte5; // Store state for future comparisons
       
    packet[4] = byte5;

    // --- Byte 12: Command Trigger ---
    uint8_t trigger = 0x00;
    if (this->mode != last_mode_) {
        if (this->last_mode_ == climate::CLIMATE_MODE_OFF) {
            trigger = 0x05; // ON/OFF button
        } else if (this->mode == climate::CLIMATE_MODE_OFF) {
            trigger = 0x05; // ON/OFF button
        } else {
            trigger = 0x06; // Mode change
        }
    } else if (this->target_temperature != this->last_temp_) {
        trigger = (this->target_temperature > this->last_temp_) ? 0x00 : 0x01;
    } else if (this->fan_mode != this->last_fan_) {
        trigger = 0x04;
    } else if (this->swing_mode != this->last_swing_) {
        trigger = 0x02;
    }
    packet[12] = trigger;
    
    // --- Send via haier_protocol.cpp ---
    auto transmit = this->transmitter_->transmit();
    remote_base::HaierProtocol protocol;
    remote_base::HaierData data;
    data.data = packet;
    ESP_LOGD(TAG, "Transmitting Haier");
    protocol.dump(data);

    protocol.encode(transmit.get_data(), data);
    transmit.perform();

    // Sync states
    this->last_temp_ = this->target_temperature;
    this->last_mode_ = this->mode;
    this->last_fan_ = this->fan_mode.value();
    this->last_swing_ = this->swing_mode;

    return;


//     // Current time (Hour in byte 2, Minutes in byte 4)
//     //   ESPTime now = ESPTime::null();
//     //   auto *time_comp = App.get_time_component();
//     //   if (time_comp != nullptr) {
//     //     now = time_comp->now();
//     //   }

//     //   if (now.is_valid()) {
//     //     this->setBits(raw, now.hour,   16, 8);   // Byte 2: Hour
//     //     this->setBits(raw, now.minute, 32, 8);   // Byte 4: Minutes
//     //   } else {
//     //     ESP_LOGW(TAG, "Time not available, sending without time");
//     //   }

}


bool HaierIRClimate::on_receive(remote_base::RemoteReceiveData src)
{
    remote_base::HaierProtocol protocol;
    auto maybe = protocol.decode(src);
    if (!maybe.has_value()) {
        ESP_LOGV(TAG, "Haier protocol decode failed");
        return false;
    }

    remote_base::HaierData data = *maybe;
    auto temp = ((data.data[1] >> 4) & 0x0F) + 16;
    this->target_temperature = temp;
    auto swing = data.data[1] & 0x0F;

    ESP_LOGD(TAG, "Received Haier packet (Swing: %02X):", swing);

    uint8_t state = data.data[4];
    uint8_t fan = data.data[5];
    uint8_t mode = data.data[7];
    uint8_t button = data.data[12];

    ESP_LOGD(TAG, "Received Haier packet (State: %02X):", state);
    ESP_LOGD(TAG, "Received Haier packet (Mode: %02X):", mode);
    ESP_LOGD(TAG, "Received Haier packet (Button: %02X):", button);

    // uint8_t swing = this->readUnallinedByte(raw, 12, 4);
    // bool state = this->readUnallinedByte(raw, 33, 1);
    // uint8_t mode = this->readUnallinedByte(raw, 56, 3);
    // bool silent = this->readUnallinedByte(raw, 48, 1);
    // bool turbo = this->readUnallinedByte(raw, 49, 1);

    // switch (swing) {
    //     case haier_ac_ir::SWING_OFF:
    //         this->swing_mode = climate::CLIMATE_SWING_OFF;
    //         break;
    //     case haier_ac_ir::SWING_UP:
    //     case haier_ac_ir::SWING_UP_WIDE:
    //         this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
    //         break;
    //     case haier_ac_ir::SWING_DOWN_WIDE:
    //         this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
    //         break;
    //     case haier_ac_ir::SWING_OSCILATE:
    //         this->swing_mode = climate::CLIMATE_SWING_BOTH;
    //         break;
    //     default:
    //         this->swing_mode = climate::CLIMATE_SWING_OFF;
    // }

    // if (!state) {
    //     this->mode = climate::CLIMATE_MODE_OFF;
    // } else {
    //     switch (mode)
    //     {
    //         case haier_ac_ir::MODE_AUTO:
    //             this->mode = climate::CLIMATE_MODE_HEAT_COOL;
    //             break;
    //         case haier_ac_ir::MODE_COOLING:
    //             this->mode = climate::CLIMATE_MODE_COOL;
    //             break;
    //         case haier_ac_ir::MODE_HEATING:
    //             this->mode = climate::CLIMATE_MODE_HEAT;
    //             break;
    //         case haier_ac_ir::MODE_FAN:
    //             this->mode = climate::CLIMATE_MODE_FAN_ONLY;
    //             break;
    //         case haier_ac_ir::MODE_DEHUMIDIFICATION:
    //             this->mode = climate::CLIMATE_MODE_DRY;
    //             break;
    //         default:
    //             this->mode = climate::CLIMATE_MODE_AUTO;
    //             break;
    //     }
    // }
    
    switch (fan)
    {
        case haier_ac_ir::SPEED_AUTO:
            this->fan_mode = climate::CLIMATE_FAN_AUTO;
            break;

        case haier_ac_ir::SPEED_LOW:
            this->fan_mode = climate::CLIMATE_FAN_LOW;
            break;
        
        case haier_ac_ir::SPEED_MEDIUM:
            this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
            break;

        case haier_ac_ir::SPEED_HIGH:
            this->fan_mode = climate::CLIMATE_FAN_HIGH;
            break;

        default:
            this->fan_mode = climate::CLIMATE_FAN_AUTO;
            break;
    }

    // if (silent) {
    //     this->preset = climate::CLIMATE_PRESET_SLEEP;
    // } else if (turbo) {
    //     this->preset = climate::CLIMATE_PRESET_BOOST;
    // } else {
    //     this->preset = climate::CLIMATE_PRESET_NONE;
    // }

    this->publish_state();

    return true;
}

} // namespace haier_ac_ir
} // namespace esphome
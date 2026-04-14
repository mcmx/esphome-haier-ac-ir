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

  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL});

  traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
                                climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP});

  return traits;
}

void HaierIRClimate::printBin(uint8_t bin) {
    ESP_LOGD(TAG, "%c%c%c%c%c%c%c%c 0x%02X",
        ((bin) & 0x80 ? '1' : '0'),
        ((bin) & 0x40 ? '1' : '0'),
        ((bin) & 0x20 ? '1' : '0'),
        ((bin) & 0x10 ? '1' : '0'),
        ((bin) & 0x08 ? '1' : '0'),
        ((bin) & 0x04 ? '1' : '0'),
        ((bin) & 0x02 ? '1' : '0'),
        ((bin) & 0x01 ? '1' : '0'),
        static_cast<unsigned>(bin)
    );
}

void HaierIRClimate::printBinR(uint8_t bin) {
    ESP_LOGD(TAG, "%c%c%c%c%c%c%c%c",
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

void HaierIRClimate::setBits(uint8_t array[], uint8_t value, uint8_t bit_offset, uint8_t num_bits) {
  uint8_t byte_idx = bit_offset / 8;
  uint8_t bit_idx = bit_offset % 8;

  uint16_t mask = ((1u << num_bits) - 1) << bit_idx;

  array[byte_idx] &= ~mask;
  array[byte_idx] |= (static_cast<uint16_t>(value) << bit_idx) & mask;

  if (bit_idx + num_bits > 8) {
    uint8_t overflow = bit_idx + num_bits - 8;
    array[byte_idx + 1] &= ~((1u << overflow) - 1);
    array[byte_idx + 1] |= value >> (num_bits - overflow);
  }
}

uint8_t HaierIRClimate::calc_checksum(uint8_t array[]) {
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < PACKET_SIZE - 1; i++) {
    checksum += array[i];
  }
  return checksum;
}
uint8_t HaierIRClimate::calc_checksum_r(uint8_t array[]) {
    uint8_t checksum = 0;
    
    for (uint8_t i = 0; i < PACKET_SIZE - 1; i++) {
        checksum += this->readUnallinedByte(array, i * 8, 8);
    }

    return checksum;
}

void HaierIRClimate::transmit_state() {
    std::vector<uint8_t> packet(PACKET_SIZE, 0x00);
    packet[0] = 0xA6; // Header

    // --- Byte 1: Temperature ---
    packet[1] = (uint8_t)(this->target_temperature - 16);

    // --- Byte 2: Mode ---
    if (this->mode == climate::CLIMATE_MODE_OFF) {
        packet[2] = last_mode_val_; 
    } else {
        switch (this->mode) {
            case climate::CLIMATE_MODE_COOL:      packet[2] = 0x01; break;
            case climate::CLIMATE_MODE_DRY:       packet[2] = 0x02; break;
            case climate::CLIMATE_MODE_FAN_ONLY:  packet[2] = 0x03; break;
            case climate::CLIMATE_MODE_HEAT:      packet[2] = 0x04; break;
            default:                              packet[2] = 0x00; break;
        }
        last_mode_val_ = packet[2];
    }

    // --- Byte 3: Fan Speed ---
    switch (this->fan_mode.value()) {
        case climate::CLIMATE_FAN_LOW:    packet[3] = 0x01; break;
        case climate::CLIMATE_FAN_MEDIUM: packet[3] = 0x02; break;
        case climate::CLIMATE_FAN_HIGH:   packet[3] = 0x03; break;
        default:                          packet[3] = 0x00; break;
    }

    // --- Byte 4: Swing ---
    packet[4] = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL) ? 0x01 : 0x00;

    // --- Byte 5: POWER & AUX (Bitmask Logic) ---
    uint8_t byte5 = 0x00;
    if (this->mode != climate::CLIMATE_MODE_OFF) {
        byte5 |= 0x40; // Set Bit 6 to 1 (Power On)
    } else {
        byte5 &= ~0x40; // Set Bit 6 to 0 (Power Off)
    }
    
    // Note: If you find that 'Turbo' is Bit 0, you would add:
    // if (this->preset == CLIMATE_PRESET_BOOST) byte5 |= 0x01;
    
    packet[5] = byte5;

    // --- Byte 12: Command Trigger ---
    uint8_t trigger = 0x00;
    if (this->mode != last_mode_) {
        trigger = (this->mode == climate::CLIMATE_MODE_OFF) ? 0x05 : 0x04;
    } else if (this->target_temperature != last_temp_) {
        trigger = (this->target_temperature > last_temp_) ? 0x00 : 0x01;
    } else if (this->fan_mode != last_fan_) {
        trigger = 0x02;
    } else if (this->swing_mode != last_swing_) {
        trigger = 0x03;
    }
    packet[12] = trigger;
    
    // --- Send via haier_protocol.cpp ---
    auto transmit = this->transmitter_->transmit();
    remote_base::HaierProtocol protocol;
    remote_base::HaierData data;
    data.data = packet; 

    protocol.encode(transmit.get_data(), data);
    transmit.perform();

    // Sync states
    last_temp_ = this->target_temperature;
    last_mode_ = this->mode;
    last_fan_ = this->fan_mode.value();
    last_swing_ = this->swing_mode;

    return;

    uint8_t raw[PACKET_SIZE] = {0};

    // Byte 0: Fixed prefix
    this->setBits(raw, PREFIX, 0, 8); // A6

    // Byte 1: Temperature (main fix)
    // 27°C → 0xBC
    // 28°C → 0xCC
    // This mapping matches your captured packets exactly
    uint8_t temp_code = 0xBC + (this->target_temperature - 27) * 0x10;
    this->setBits(raw, temp_code, 8, 8);

    // Current time (Hour in byte 2, Minutes in byte 4)
    //   ESPTime now = ESPTime::null();
    //   auto *time_comp = App.get_time_component();
    //   if (time_comp != nullptr) {
    //     now = time_comp->now();
    //   }

    //   if (now.is_valid()) {
    //     this->setBits(raw, now.hour,   16, 8);   // Byte 2: Hour
    //     this->setBits(raw, now.minute, 32, 8);   // Byte 4: Minutes
    //   } else {
    //     ESP_LOGW(TAG, "Time not available, sending without time");
    //   }

    // Swing (byte ~1.5, bits 12-15)
    uint8_t swing_val = haier_ac_ir::SWING_OFF;
    switch (this->swing_mode)
    {
    case climate::CLIMATE_SWING_OFF:        swing_val = haier_ac_ir::SWING_OFF; break;
    case climate::CLIMATE_SWING_VERTICAL:   swing_val = haier_ac_ir::SWING_UP_WIDE; break;
    case climate::CLIMATE_SWING_HORIZONTAL: swing_val = haier_ac_ir::SWING_DOWN_WIDE; break;
    case climate::CLIMATE_SWING_BOTH:       swing_val = haier_ac_ir::SWING_OSCILATE; break;
    default: break;
  }
  this->setBits(raw, swing_val, 12, 4);

  // Power / Mode (bit 33 + bits 56-58)
  bool power_on = (this->mode != climate::CLIMATE_MODE_OFF);
  uint8_t mode_val = haier_ac_ir::MODE_AUTO;

  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:     mode_val = haier_ac_ir::MODE_COOLING; break;
    case climate::CLIMATE_MODE_HEAT:     mode_val = haier_ac_ir::MODE_HEATING; break;
    case climate::CLIMATE_MODE_DRY:      mode_val = haier_ac_ir::MODE_DEHUMIDIFICATION; break;
    case climate::CLIMATE_MODE_FAN_ONLY: mode_val = haier_ac_ir::MODE_FAN; break;
    default:                             mode_val = haier_ac_ir::MODE_AUTO; break;
  }
  this->setBits(raw, power_on ? 1 : 0, 33, 1);
  this->setBits(raw, mode_val, 56, 3);

  // Fan speed (bits 40-42)
  uint8_t fan_val = haier_ac_ir::SPEED_AUTO;
  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:    fan_val = haier_ac_ir::SPEED_LOW; break;
    case climate::CLIMATE_FAN_MEDIUM: fan_val = haier_ac_ir::SPEED_MEDIUM; break;
    case climate::CLIMATE_FAN_HIGH:   fan_val = haier_ac_ir::SPEED_HIGH; break;
    default: break;
  }
  this->setBits(raw, fan_val, 40, 3);

  // Presets: Silent & Turbo (bits 48 and 49)
  bool silent = false;
  bool turbo = false;
  auto preset = this->preset.value_or(climate::CLIMATE_PRESET_NONE);
  if (preset == climate::CLIMATE_PRESET_SLEEP) silent = true;
  else if (preset == climate::CLIMATE_PRESET_BOOST) turbo = true;

  this->setBits(raw, silent ? 1 : 0, 48, 1);
  this->setBits(raw, turbo ? 1 : 0, 49, 1);

  // Checksum (last byte - byte 12, bits 104-111)
  uint8_t checksum = this->calc_checksum(raw);
  this->setBits(raw, checksum, 104, 8);

  // Debug: Print the full packet
  ESP_LOGD(TAG, "Sending Haier packet (Temp: %d°C):", (int)this->target_temperature);
  for (uint8_t i = 0; i < PACKET_SIZE; i++) {
    this->printBin(raw[i]);
  }

  // ====================== TRANSMIT ======================
  auto transmit = this->transmitter_->transmit();
  auto dst = transmit.get_data();
  dst->set_carrier_frequency(38000);
  dst->reserve(BURST_SIZE);

  // Preamble
  for (int i = 0; i < 2; i++) {
    dst->item(PREAMBULE[i * 2], PREAMBULE[i * 2 + 1]);
  }

  // Data bits (MSB first)
  for (uint8_t i = 0; i < PACKET_SIZE; i++) {
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
      dst->item(MARK, (raw[i] & mask) ? SPACE_ONE : SPACE_ZERO);
    }
  }

  dst->item(MARK, -1000);   // ending gap
  transmit.perform();
}


bool HaierIRClimate::on_receive(remote_base::RemoteReceiveData src)
{
    HaierData data = HaierProtocol::decode(src);
    HaierProtocol::dump(data);

    return false;

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
        case haier_ac_ir::SWING_OFF:
            this->swing_mode = climate::CLIMATE_SWING_OFF;
            break;
        case haier_ac_ir::SWING_UP:
        case haier_ac_ir::SWING_UP_WIDE:
            this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
            break;
        case haier_ac_ir::SWING_DOWN_WIDE:
            this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
            break;
        case haier_ac_ir::SWING_OSCILATE:
            this->swing_mode = climate::CLIMATE_SWING_BOTH;
            break;
        default:
            this->swing_mode = climate::CLIMATE_SWING_OFF;
    }

    if (!state) {
        this->mode = climate::CLIMATE_MODE_OFF;
    } else {
        switch (mode)
        {
            case haier_ac_ir::MODE_AUTO:
                this->mode = climate::CLIMATE_MODE_HEAT_COOL;
                break;
            case haier_ac_ir::MODE_COOLING:
                this->mode = climate::CLIMATE_MODE_COOL;
                break;
            case haier_ac_ir::MODE_HEATING:
                this->mode = climate::CLIMATE_MODE_HEAT;
                break;
            case haier_ac_ir::MODE_FAN:
                this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                break;
            case haier_ac_ir::MODE_DEHUMIDIFICATION:
                this->mode = climate::CLIMATE_MODE_DRY;
                break;
            default:
                this->mode = climate::CLIMATE_MODE_AUTO;
                break;
        }
    }
    
    switch (speed)
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

    if (silent) {
        this->preset = climate::CLIMATE_PRESET_SLEEP;
    } else if (turbo) {
        this->preset = climate::CLIMATE_PRESET_BOOST;
    } else {
        this->preset = climate::CLIMATE_PRESET_NONE;
    }

    this->publish_state();

    return true;
}

} // namespace haier_ac_ir
} // namespace esphome
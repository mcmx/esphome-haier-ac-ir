#include "haier_ir.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

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
    ESP_LOGD(TAG, "%c%c%c%c%c%c%c%c",
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
  uint8_t raw[PACKET_SIZE] = {0};

  // Header
  this->setBits(raw, PREFIX, 0, 8);

  // === Temperature (byte 1) ===
  // Based on your captures: 27°C → 0xBC, 28°C → 0xCC
  // This simple linear mapping works for the two values you provided.
  // Test more temperatures and adjust if needed.
  uint8_t temp_code = 0xBC + (this->target_temperature - 27) * 0x10;
  this->setBits(raw, temp_code, 8, 8);   // full byte 1

  // === Current Time (very important for this protocol) ===
  ESPTime now = ESPTime::null();
  auto *time_comp = App.get_time_component();  // This is the safe way in custom components
  if (time_comp != nullptr) {
    now = time_comp->now();
  }

  if (now.is_valid()) {
    this->setBits(raw, now.hour, 16, 8);     // byte 2: hour (matches your logs: 0F=15, 10=16)
    this->setBits(raw, now.minute, 32, 8);   // byte 4: minutes (this is approximate - may need fine-tuning)
  } else {
    ESP_LOGW(TAG, "No valid time available - sending packet without time");
  }

  // === Swing ===
  uint8_t swing = haier_ac_ir::SWING_OFF;
  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:        swing = haier_ac_ir::SWING_OFF; break;
    case climate::CLIMATE_SWING_VERTICAL:   swing = haier_ac_ir::SWING_UP_WIDE; break;
    case climate::CLIMATE_SWING_HORIZONTAL: swing = haier_ac_ir::SWING_DOWN_WIDE; break;
    case climate::CLIMATE_SWING_BOTH:       swing = haier_ac_ir::SWING_OSCILATE; break;
    default: break;
  }
  this->setBits(raw, swing, 12, 4);

  // === Mode / Power ===
  bool power_on = (this->mode != climate::CLIMATE_MODE_OFF);
  uint8_t mode_val = haier_ac_ir::MODE_AUTO;

  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL: mode_val = haier_ac_ir::MODE_COOLING; break;
    case climate::CLIMATE_MODE_HEAT: mode_val = haier_ac_ir::MODE_HEATING; break;
    case climate::CLIMATE_MODE_DRY:  mode_val = haier_ac_ir::MODE_DEHUMIDIFICATION; break;
    case climate::CLIMATE_MODE_FAN_ONLY: mode_val = haier_ac_ir::MODE_FAN; break;
    default: mode_val = haier_ac_ir::MODE_AUTO; break;
  }
  this->setBits(raw, power_on ? 1 : 0, 33, 1);
  this->setBits(raw, mode_val, 56, 3);

  // === Fan speed ===
  uint8_t speed = haier_ac_ir::SPEED_AUTO;
  auto fan_mode = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  switch (fan_mode) {
    case climate::CLIMATE_FAN_LOW:    speed = haier_ac_ir::SPEED_LOW; break;
    case climate::CLIMATE_FAN_MEDIUM: speed = haier_ac_ir::SPEED_MEDIUM; break;
    case climate::CLIMATE_FAN_HIGH:   speed = haier_ac_ir::SPEED_HIGH; break;
    default: break;
  }
  this->setBits(raw, speed, 40, 3);

  // === Presets (silent / turbo) ===
  bool silent = false;
  bool turbo = false;
  auto preset = this->preset.value_or(climate::CLIMATE_PRESET_NONE);
  if (preset == climate::CLIMATE_PRESET_SLEEP) silent = true;
  else if (preset == climate::CLIMATE_PRESET_BOOST) turbo = true;

  this->setBits(raw, silent ? 1 : 0, 48, 1);
  this->setBits(raw, turbo ? 1 : 0, 49, 1);

  // === Checksum (last byte) ===
  this->setBits(raw, this->calc_checksum(raw), 104, 8);

  // Debug output
  ESP_LOGD(TAG, "Sending Haier IR packet:");
  for (uint8_t i = 0; i < PACKET_SIZE; i++) {
    this->printBin(raw[i]);
  }

  // === Transmit the packet ===
  auto transmit = this->transmitter_->transmit();
  auto dst = transmit.get_data();
  dst->set_carrier_frequency(38000);
  dst->reserve(BURST_SIZE);

  // Preamble
  for (int i = 0; i < 2; i++) {
    dst->item(PREAMBULE[i * 2], PREAMBULE[i * 2 + 1]);
  }

  // Data (MSB first)
  for (uint8_t i = 0; i < PACKET_SIZE; i++) {
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
      dst->item(MARK, (raw[i] & mask) ? SPACE_ONE : SPACE_ZERO);
    }
  }

  dst->item(MARK, -1000);   // ending gap
  transmit.perform();
}
{
    uint8_t raw[PACKET_SIZE];

    for (uint8_t i = 0; i < sizeof(raw); i++) {
        raw[i] = 0;
    }

    this->setByte(raw, PREFIX, 0, 8);

    uint8_t temp = this->target_temperature - 16;

    uint8_t swing;
    switch (this->swing_mode) {
        case climate::CLIMATE_SWING_OFF:
            swing = haier_ac_ir::SWING_OFF;
            break;
        case climate::CLIMATE_SWING_VERTICAL:
            swing = haier_ac_ir::SWING_UP_WIDE;
            break;
        case climate::CLIMATE_SWING_HORIZONTAL:
            swing = haier_ac_ir::SWING_DOWN_WIDE;
            break;
        case climate::CLIMATE_SWING_BOTH:
            swing = haier_ac_ir::SWING_OSCILATE;
            break;
        default:
            swing = haier_ac_ir::SWING_OFF;
    }
    
    bool state;
    uint8_t mode;
    switch (this->mode)
    {
        case climate::CLIMATE_MODE_OFF:
            state = false;
            mode = haier_ac_ir::MODE_AUTO;
            break;
        case climate::CLIMATE_MODE_AUTO:
        case climate::CLIMATE_MODE_HEAT_COOL:
            state = true;
            mode = haier_ac_ir::MODE_AUTO;
            break;
        case climate::CLIMATE_MODE_COOL:
            state = true;
            mode = haier_ac_ir::MODE_COOLING;
            break;
        case climate::CLIMATE_MODE_HEAT:
            state = true;
            mode = haier_ac_ir::MODE_HEATING;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            state = true;
            mode = haier_ac_ir::MODE_FAN;
            break;
        case climate::CLIMATE_MODE_DRY:
            state = true;
            mode = haier_ac_ir::MODE_DEHUMIDIFICATION;
            break;
        default:
            state = false;
            mode = haier_ac_ir::MODE_AUTO;
            break;
    }

    uint8_t speed;
    switch (this->fan_mode.value_or(climate::CLIMATE_FAN_ON))
    {
        case climate::CLIMATE_FAN_AUTO:
            speed = haier_ac_ir::SPEED_AUTO;
            break;

        case climate::CLIMATE_FAN_LOW:
            speed = haier_ac_ir::SPEED_LOW;
            break;
        
        case climate::CLIMATE_FAN_MEDIUM:
            speed = haier_ac_ir::SPEED_MEDIUM;
            break;

        case climate::CLIMATE_FAN_HIGH:
            speed = haier_ac_ir::SPEED_HIGH;
            break;

        default:
            speed = haier_ac_ir::SPEED_AUTO;
            break;
    }

    bool silent, turbo = false;
        
    switch (this->preset.value_or(climate::CLIMATE_PRESET_NONE)) {
        case climate::CLIMATE_PRESET_NONE:
            silent = false;
            turbo = false;
            break;
        case climate::CLIMATE_PRESET_SLEEP:
            silent = true;
            turbo = false;
            break;
        case climate::CLIMATE_PRESET_BOOST:
            silent = false;
            turbo = true;
            break;
        default:
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

bool HaierIRClimate::on_receive(remote_base::RemoteReceiveData data)
{
    if (data.size() != BURST_SIZE) {
        ESP_LOGD(TAG, "wrong data size %d", data.size());
        return false;
    }

    // data.set_tolerance(200);

    for(int i = 0; i < 2; i++) {
        // TODO: check preamble
        ESP_LOGV(TAG, "p %d", data.peek());
        ESP_LOGV(TAG, "p %d", data.peek(1));

        data.advance(2);
    }

    // ESP_LOGD(TAG, "p %d", data.peek());
    // ESP_LOGD(TAG, "p %d", data.peek(1));
    // data.advance(2);
    
    uint8_t raw[PACKET_SIZE];

    uint16_t size = 0;

    for (uint8_t i = 0; i < sizeof(raw); i++) {
        raw[i] = 0;

        for (uint8_t mask = 1; mask != 0; mask <<= 1) {
            // ESP_LOGD(TAG, "%d %d", data.peek());
            // data.advance();

            // ESP_LOGD(TAG, "index %d", data.get_index());
            // ESP_LOGD(TAG, "%d mark %d", data.get_index(), data.peek());

            if (!data.expect_mark(MARK)) {
                ESP_LOGV(TAG, "wrong mark %d", data.peek());
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
                ESP_LOGV(TAG, "wrong bit %d", data.peek());

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
        ESP_LOGV(TAG, "wrong size %d", size);

        return false;
    }

    for (uint8_t i = 0; i < sizeof(raw); i++) {
        this->printBinR(raw[i]);
    }
    
    auto prefix = this->readUnallinedByte(raw, 0, 8);
    if (prefix != PREFIX) {
        ESP_LOGV(TAG, "wrong prefix %d", prefix);
        return false;
    }

    uint8_t checksum_calc = this->calc_checksum_r(raw);

    auto checksum = this->readUnallinedByte(raw, 104, 8);
    
    if (checksum != checksum_calc) {
        ESP_LOGD(TAG, "wrong checksum %d. calc: %d", checksum, checksum_calc);
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
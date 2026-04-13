#include "haier_ir.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace haier_ac_ir {

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

void HaierIRClimate::transmit_state() {
  uint8_t raw[PACKET_SIZE] = {0};

  // Header
  this->setBits(raw, PREFIX, 0, 8);

  // Temperature - main field in byte 1 (from your captures)
  // 27°C → 0xBC, 28°C → 0xCC
  uint8_t temp_code = 0xBC + (this->target_temperature - 27) * 0x10;
  this->setBits(raw, temp_code, 8, 8);   // byte 1

  // Current time (required by the remote)
  ESPTime now = esphome::time::global_time_component->now();  // safer way
  if (now.is_valid()) {
    this->setBits(raw, now.hour, 16, 8);     // byte 2: hour
    this->setBits(raw, now.minute, 32, 8);   // byte 4: minutes (may need fine-tuning later)
  } else {
    ESP_LOGW(TAG, "Time not available, sending packet without time");
  }

  // Swing
  uint8_t swing = haier_ac_ir::SWING_OFF;
  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:        swing = haier_ac_ir::SWING_OFF; break;
    case climate::CLIMATE_SWING_VERTICAL:   swing = haier_ac_ir::SWING_UP_WIDE; break;
    case climate::CLIMATE_SWING_HORIZONTAL: swing = haier_ac_ir::SWING_DOWN_WIDE; break;
    case climate::CLIMATE_SWING_BOTH:       swing = haier_ac_ir::SWING_OSCILATE; break;
    default: break;
  }
  this->setBits(raw, swing, 12, 4);

  // Mode / Power
  bool power_on = (this->mode != climate::CLIMATE_MODE_OFF);
  uint8_t mode_val = haier_ac_ir::MODE_AUTO;
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:      mode_val = haier_ac_ir::MODE_COOLING; break;
    case climate::CLIMATE_MODE_HEAT:      mode_val = haier_ac_ir::MODE_HEATING; break;
    case climate::CLIMATE_MODE_DRY:       mode_val = haier_ac_ir::MODE_DEHUMIDIFICATION; break;
    case climate::CLIMATE_MODE_FAN_ONLY:  mode_val = haier_ac_ir::MODE_FAN; break;
    default: break;
  }
  this->setBits(raw, power_on ? 1 : 0, 33, 1);
  this->setBits(raw, mode_val, 56, 3);

  // Fan speed
  uint8_t speed = haier_ac_ir::SPEED_AUTO;
  auto fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  if (fan == climate::CLIMATE_FAN_LOW)    speed = haier_ac_ir::SPEED_LOW;
  else if (fan == climate::CLIMATE_FAN_MEDIUM) speed = haier_ac_ir::SPEED_MEDIUM;
  else if (fan == climate::CLIMATE_FAN_HIGH)   speed = haier_ac_ir::SPEED_HIGH;
  this->setBits(raw, speed, 40, 3);

  // Presets (silent / turbo)
  bool silent = false;
  bool turbo = false;
  auto preset = this->preset.value_or(climate::CLIMATE_PRESET_NONE);
  if (preset == climate::CLIMATE_PRESET_SLEEP) silent = true;
  else if (preset == climate::CLIMATE_PRESET_BOOST) turbo = true;
  this->setBits(raw, silent ? 1 : 0, 48, 1);
  this->setBits(raw, turbo ? 1 : 0, 49, 1);

  // Checksum (last byte)
  this->setBits(raw, this->calc_checksum(raw), 104, 8);

  // Debug output
  ESP_LOGD(TAG, "Sending Haier IR packet:");
  for (uint8_t i = 0; i < PACKET_SIZE; i++) {
    this->printBin(raw[i]);
  }

  // Transmit
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

  dst->item(MARK, -1000);
  transmit.perform();
}

// Keep your on_receive function exactly as it was in the original code
// (only paste it back if you changed it)

}  // namespace haier_ac_ir
}  // namespace esphome
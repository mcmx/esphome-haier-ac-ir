#include "haier_ir.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace haier_ac_ir {

static const char *const TAG = "haier_ac_ir";

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

  // === Header ===
  this->setBits(raw, PREFIX, 0, 8);

  // === Temperature (main field in byte 1) ===
  // From captures: 28°C → 0xCC, 27°C → 0xBC  (difference of 0x10)
  // We use a simple mapping. Adjust the base if more temps show different pattern.
  uint8_t temp_code = 0xBC + (this->target_temperature - 27) * 0x10;  // 27→BC, 28→CC, etc.
  this->setBits(raw, temp_code, 8, 8);   // full byte 1

  // === Current Time (required by the protocol) ===
  auto now = id(time_id).now();  // <-- you must have a time: component with id: time_id
  if (now.is_valid()) {
    uint8_t hour = now.hour;      // 0-23
    uint8_t minute = now.minute;  // 0-59

    this->setBits(raw, hour, 16, 8);   // byte 2: hour (matches 0F=15, 10=16 in your logs)

    // Minutes are mostly in byte 4, often combined with other bits.
    // For now we put minute directly (you may need to tweak this after more testing).
    this->setBits(raw, minute, 32, 8); // byte 4
  } else {
    ESP_LOGW(TAG, "Time not available - sending without time");
  }

  // === Swing ===
  uint8_t swing = haier_ac_ir::SWING_OFF;
  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:       swing = haier_ac_ir::SWING_OFF; break;
    case climate::CLIMATE_SWING_VERTICAL:  swing = haier_ac_ir::SWING_UP_WIDE; break;
    case climate::CLIMATE_SWING_HORIZONTAL:swing = haier_ac_ir::SWING_DOWN_WIDE; break;
    case climate::CLIMATE_SWING_BOTH:      swing = haier_ac_ir::SWING_OSCILATE; break;
    default: break;
  }
  this->setBits(raw, swing, 12, 4);   // keep your original position for swing (may need adjustment)

  // === Mode / Power ===
  bool power = (this->mode != climate::CLIMATE_MODE_OFF);
  uint8_t mode = haier_ac_ir::MODE_AUTO;

  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL: mode = haier_ac_ir::MODE_COOLING; break;
    case climate::CLIMATE_MODE_HEAT: mode = haier_ac_ir::MODE_HEATING; break;
    case climate::CLIMATE_MODE_DRY:  mode = haier_ac_ir::MODE_DEHUMIDIFICATION; break;
    case climate::CLIMATE_MODE_FAN_ONLY: mode = haier_ac_ir::MODE_FAN; break;
    default: mode = haier_ac_ir::MODE_AUTO; break;
  }
  this->setBits(raw, power ? 1 : 0, 33, 1);
  this->setBits(raw, mode, 56, 3);

  // === Fan speed ===
  uint8_t speed = haier_ac_ir::SPEED_AUTO;
  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:    speed = haier_ac_ir::SPEED_LOW; break;
    case climate::CLIMATE_FAN_MEDIUM: speed = haier_ac_ir::SPEED_MEDIUM; break;
    case climate::CLIMATE_FAN_HIGH:   speed = haier_ac_ir::SPEED_HIGH; break;
    default: break;
  }
  this->setBits(raw, speed, 40, 3);

  // === Presets (silent / turbo) ===
  bool silent = false;
  bool turbo = false;
  switch (this->preset.value_or(climate::CLIMATE_PRESET_NONE)) {
    case climate::CLIMATE_PRESET_SLEEP: silent = true; break;
    case climate::CLIMATE_PRESET_BOOST: turbo = true; break;
    default: break;
  }
  this->setBits(raw, silent ? 1 : 0, 48, 1);
  this->setBits(raw, turbo ? 1 : 0, 49, 1);

  // === Checksum (last byte) ===
  this->setBits(raw, this->calc_checksum(raw), 104, 8);

  // Debug print the packet
  ESP_LOGD(TAG, "Sending Haier packet:");
  for (uint8_t i = 0; i < PACKET_SIZE; i++) {
    this->printBin(raw[i]);
  }

  // === Transmit ===
  auto transmit = this->transmitter_->transmit();
  auto dst = transmit.get_data();
  dst->set_carrier_frequency(38000);
  dst->reserve(BURST_SIZE);

  // Preamble
  for (int i = 0; i < 2; i++) {
    dst->item(PREAMBULE[i * 2], PREAMBULE[i * 2 + 1]);
  }

  // Data bits (MSB first)
  for (uint8_t i = 0; i < sizeof(raw); i++) {
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
      dst->item(MARK, (raw[i] & mask) ? SPACE_ONE : SPACE_ZERO);
    }
  }

  dst->item(MARK, -1000);  // final mark + gap
  transmit.perform();
}

// on_receive remains mostly unchanged (it was working better than transmit)
bool HaierIRClimate::on_receive(remote_base::RemoteReceiveData data) {
  // ... your existing receive code ...
  // (I recommend keeping it as-is for now, only fix obvious bugs if any appear)
  // At the end it calls publish_state()
  return true;  // simplified placeholder - keep your full implementation
}

}  // namespace haier_ac_ir
}  // namespace esphome
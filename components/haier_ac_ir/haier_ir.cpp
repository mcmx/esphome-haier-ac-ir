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

  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_HORIZONTAL});

  traits.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
                                climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP});

  return traits;
}

} // namespace haier_ac_ir
} // namespace esphome
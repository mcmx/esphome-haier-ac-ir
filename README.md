# Haier AC over IR for ESPHome

Crappy implementation of the Haier IR protocol for air conditioner for the ESPHome climate component.

Based on [this](https://github.com/crankyoldgit/IRremoteESP8266/issues/404)

Due to the limited number of swing modes that ESPHome supports swing_mode used in wrong way.

- CLIMATE_SWING_BOTH - swing enabled
- CLIMATE_SWING_VERTICAL - blinds top  
- CLIMATE_SWING_VERTICAL - blinds bottom

If you want add this code to main ESPHome repo you can do it.
You can use this code however you want.

Another [reference](https://github.com/TRuHa83/ESPHome_IR_Haier_Climate)

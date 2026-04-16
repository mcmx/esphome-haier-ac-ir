# Haier AC over IR for ESPHome

Crappy implementation of the Haier IR protocol for air conditioner for the ESPHome climate component.

Based on [this](https://github.com/crankyoldgit/IRremoteESP8266/issues/404)

Due to the limited number of swing modes that ESPHome supports swing_mode used in wrong way.

- CLIMATE_SWING_OFF        - top
- CLIMATE_SWING_VERTICAL   - second from top to bottom
- CLIMATE_SWING_HORIZONTAL - third from top to bottom  
- CLIMATE_SWING_BOTH       - full swing

If you want add this code to main ESPHome repo you can do it.
You can use this code however you want.

Another [reference](https://github.com/TRuHa83/ESPHome_IR_Haier_Climate)

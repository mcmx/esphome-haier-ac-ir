import esphome.codegen as cg
from esphome.components import climate_ir

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@mcmx"]

haier_ac_ir_ns = cg.esphome_ns.namespace('haier_ac_ir')
HaierACIRClimate = haier_ac_ir_ns.class_('HaierACIRClimate', climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(HaierACIRClimate)


async def to_code(config):
    await climate_ir.new_climate_ir(config)

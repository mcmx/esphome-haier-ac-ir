import esphome.codegen as cg
from esphome.components import climate_ir, remote_base

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@mcmx"]

haier_ac_ir_ns = cg.esphome_ns.namespace('haier_ac_ir')
HaierIRClimate = haier_ac_ir_ns.class_('HaierIRClimate', climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(HaierIRClimate)


async def to_code(config):
    # HaierProtocol is used directly from C++, so keep its source in the build.
    remote_base.request_protocol("haier")
    await climate_ir.new_climate_ir(config)

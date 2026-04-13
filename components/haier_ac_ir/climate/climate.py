import esphome.codegen as cg
from esphome.components import climate_ir


haier_ac_ir_ns = cg.esphome_ns.namespace('haier_ac_ir')
HaierACIRClimate = haier_ac_ir_ns.class_('HaierACIRClimate', climate_ir.ClimateIR)

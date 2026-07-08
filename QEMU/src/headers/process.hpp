#pragma once
#include "defs.hpp"
#include "helpers.hpp"

namespace process {

      namespace inst {

            /* Process first instruction */
            __forceinline void first(const std::uint32_t vcpu_index, lurapro::inst *inst) {

#ifdef QEMU_PLUGIN_DEBUG
                  std::printf("VCPU[%d][%llu] %s \n", vcpu_index, inst->inst.pc, inst->DEBUG_DISM);
#endif
                  /* Once it excutes mark the inst as valid */
                  auto &prev = (*lurapro::curr_pc)[vcpu_index];
                  if (!inst->fvalid) [[unlikely]] {
                        inst->fvalid = true;
                        inst->inst.vcpu = vcpu_index;
                        inst->inst.real_pc = helpers::get_real_address(vcpu_index);
                  } else if (!prev || (inst->inst.fvalid_prev_real_pc && *prev != inst->inst.prev_real_pc)) [[unlikely]] {
                        /* Previous instruction came from somewhere else */
                        (*lurapro::prevd_jumps)[vcpu_index].emplace_back(lurapro::edge(!prev ? cpu_tracer::blocks::edges::kind::next_punk : cpu_tracer::blocks::edges::kind::next, inst->inst.real_pc, prev.value_or(0u)));
                  }
                  if (!inst->inst.fvalid_prev_real_pc && prev) [[unlikely]] {
                        inst->inst.prev_real_pc = *prev;
                        inst->inst.fvalid_prev_real_pc = true;
                  }

                  /* Set any pending real PC */
                  if (lurapro::interrupts::fadd_real_pc) [[unlikely]] {
                        if (auto &intr = (*lurapro::interrupts::ints)[vcpu_index].back(); intr.dst == inst->inst.pc) {
                              intr.dst_real = inst->inst.real_pc;
                              lurapro::interrupts::fadd_real_pc = false;
                        }
                  }

                  prev = inst->inst.real_pc;
                  return;
            }

            /* Process an instruction */
            __forceinline void inst(const std::uint32_t vcpu_index, lurapro::inst *inst) {

#ifdef QEMU_PLUGIN_DEBUG
                  std::printf("VCPU[%d][%llu] %s \n", vcpu_index, inst->inst.pc, inst->DEBUG_DISM);
#endif
                  /* Once it excutes mark the inst as valid */
                  auto &prev = (*lurapro::curr_pc)[vcpu_index];
                  if (!inst->fvalid) [[unlikely]] {
                        inst->fvalid = true;
                        inst->inst.vcpu = vcpu_index;
                        inst->inst.real_pc = helpers::get_real_address(vcpu_index);
                  }
                  inst->inst.prev_real_pc = *prev;
                  prev = inst->inst.real_pc;
                  return;
            }
      } // namespace inst

} // namespace process
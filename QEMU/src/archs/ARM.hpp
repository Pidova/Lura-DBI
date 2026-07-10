#pragma once
#include "../../UPDATEME.hpp"
#include "../headers/defs.hpp"
#include "../headers/helpers.hpp"
#include "../headers/process.hpp"

namespace arm {

      /* Event registers, Lets QEMU handle mailbox */
      struct event_reg {
            bool event_pending = false; /* Signaled an event */
            bool waiting = false;       /* Waiting for an event */
      };
      std::array<event_reg, config::MAX_CORES> event_registers = {event_reg()}; /* Private fast event register, default is false/undefined */

      namespace arm_helpers {

            static constexpr auto SMC_PSCI_CPU_ON = 0xC4000003;
            static constexpr auto MPIDR_AFF0_MASK = 0x00000000FFULL;
            static constexpr auto MPIDR_AFF1_MASK = 0x000000FF00ULL;
            static constexpr auto MPIDR_AFF2_MASK = 0x0000FF0000ULL;
            static constexpr auto MPIDR_AFF3_MASK = 0xFF00000000ULL;

            /* MPIDR to vcpu_index */
            inline std::optional<std::uint32_t> mpidr_to_vcpu_index(const std::uint64_t target_mpidr) {

                  const auto affinity = target_mpidr & (MPIDR_AFF3_MASK | MPIDR_AFF2_MASK | MPIDR_AFF1_MASK | MPIDR_AFF0_MASK);
                  return (affinity < config::MAX_CORES) ? std::optional(static_cast<std::uint32_t>(affinity)) : std::nullopt;
            }
      } // namespace arm_helpers

      namespace handlers {

            /* All VCPUs waiting for an event will be signaled with an edge */
            inline void signal_edges(std::uint32_t vcpu_index, lurapro::inst *ins) {

                  auto &inst = ins->inst;
                  for (auto vcpu = 0u; vcpu < event_registers.size(); ++vcpu) {

                        auto &reg = event_registers[vcpu];
                        reg.event_pending = true;
                        if (!reg.waiting) [[likely]] {
                              continue;
                        }
                        reg.waiting = false;
                        (*lurapro::signaled_edges)[vcpu] = lurapro::signaled_edge(inst.real_pc, std::nullopt);
                  }
                  return;
            }

            /* Wait for VCPU event */
            inline void wait_for_event(std::uint32_t vcpu_index) {

                  auto &reg = event_registers[vcpu_index];
                  if (reg.event_pending) {
                        reg.event_pending = false;
                        reg.waiting = false;
                  } else {
                        reg.waiting = true;
                  }
                  return;
            }

            /* SMC */
            inline void secure_monitor_call(std::uint32_t vcpu_index, lurapro::inst *inst) {

                  std::uint64_t X0 = 0u, X1 = 0u, X2 = 0u;
                  if (const auto arr = lurapro::qemu_w<qemu_plugin_get_registers>(); arr && arr->data) [[likely]] {

                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::aarch64::x0), arr)) [[likely]] {
                              std::memcpy(&X0, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(X0)));
                        }
                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::aarch64::x1), arr)) [[likely]] {
                              std::memcpy(&X1, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(X1)));
                        }
                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::aarch64::x2), arr)) [[likely]] {
                              std::memcpy(&X2, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(X2)));
                        }
                  }

                  if (X0 == arm_helpers::SMC_PSCI_CPU_ON) [[unlikely]] {

                        if (const auto target_idx = arm_helpers::mpidr_to_vcpu_index(X1); target_idx.has_value()) [[likely]] {

                              /* Kick it awake */
                              auto &target_reg = event_registers[*target_idx];
                              target_reg.event_pending = true;
                              (*lurapro::signaled_edges)[*target_idx] = lurapro::signaled_edge(inst->inst.real_pc, X2);
                              if (target_reg.waiting) {
                                    target_reg.waiting = false;
                              }
                        }
                  }
                  return;
            }
      } // namespace handlers

      namespace cbs::insts {

            /* SEV: ARM instruction */
            static void __cdecl first_sevl_exec(std::uint32_t vcpu_index, void *userdata) {

                  event_registers[vcpu_index].event_pending = true;
                  process::inst::first(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* SEVL ARM instruction */
            static void __cdecl sevl_exec(std::uint32_t vcpu_index, void *userdata) {

                  event_registers[vcpu_index].event_pending = true;
                  process::inst::inst(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* SEV ARM instruction */
            static void __cdecl first_sev_exec(std::uint32_t vcpu_index, void *userdata) {

                  handlers::signal_edges(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  process::inst::first(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* SEV ARM instruction */
            static void __cdecl sev_exec(std::uint32_t vcpu_index, void *userdata) {

                  handlers::signal_edges(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  process::inst::inst(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* WFE ARM instruction */
            static void __cdecl first_wfe_exec(std::uint32_t vcpu_index, void *userdata) {

                  handlers::wait_for_event(vcpu_index);
                  process::inst::first(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* WFE ARM instruction */
            static void __cdecl wfe_exec(std::uint32_t vcpu_index, void *userdata) {

                  handlers::wait_for_event(vcpu_index);
                  process::inst::inst(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* SMC ARM instruction */
            static void __cdecl first_smc_exec(std::uint32_t vcpu_index, void *userdata) {

                  handlers::secure_monitor_call(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  process::inst::first(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* SMC ARM instruction */
            static void __cdecl smc_exec(std::uint32_t vcpu_index, void *userdata) {

                  handlers::secure_monitor_call(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  process::inst::inst(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }
      } // namespace cbs::insts
} // namespace arm
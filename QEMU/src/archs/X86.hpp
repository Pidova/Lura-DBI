#pragma once
#include "../../UPDATEME.hpp"
#include "../headers/defs.hpp"
#include "../headers/helpers.hpp"
#include "../headers/process.hpp"

namespace x86 {

      namespace mem {

            static constexpr auto APIC_ID_REG_WRITE = 0x20; /* APIC Register write */
            static constexpr auto ICR_HIGH = 0x310;         /* Interrupt Command Register High */
            static constexpr auto ICR_LOW = 0x300;          /* Interrupt Command Register Low */

            inline std::uint8_t last_target_apic_id[256u] = {0u}; /* Last target APIC for mapping to APIC index */

            /*  Ctor to {0, 1, 2, ... to MAX CORES} */
            template <std::size_t... Is>
            constexpr boost::fixed_vector<std::uint32_t, sizeof...(Is)> make_default_core_map(std::index_sequence<Is...>) {
                  return boost::fixed_vector<std::uint32_t, sizeof...(Is)>({static_cast<std::uint32_t>(Is)...});
            }
            boost::fixed_vector<std::uint32_t, config::MAX_CORES> map_id = make_default_core_map(std::make_index_sequence<config::MAX_CORES>{}); /* Map VCPUs IDs */

            /* Adds edges to pending edges */
            inline void add_edge(const std::uint32_t vcpu_index, const std::uint64_t from_realpc, const std::uint64_t target_physical_address) {
                  (*lurapro::signaled_edges)[vcpu_index] = lurapro::signaled_edge(from_realpc, target_physical_address);
                  return;
            }

            /* CB for memstorage for detecting MMIO */
            inline void access(const std::uint32_t vcpu_index, const std::uint64_t vaddr, const std::uint64_t realpc) {

                  if (vaddr >= lurapro::mmio::base && vaddr < (lurapro::mmio::base + lurapro::mmio::size)) [[unlikely]] {

                        switch (vaddr - lurapro::mmio::base) {
                              case APIC_ID_REG_WRITE: {

                                    std::uint32_t incoming_reg_value = 0u;
                                    auto buf = g_byte_array_sized_new(sizeof(incoming_reg_value));
                                    if (lurapro::qemu_w<qemu_plugin_read_memory_vaddr>(vaddr, buf, sizeof(incoming_reg_value))) {

                                          map_id[std::uint8_t((incoming_reg_value >> 24) & 0xFF)] = vcpu_index;
                                    }
                                    g_byte_array_free(buf, TRUE);
                                    break;
                              }
                              case ICR_HIGH: {

                                    std::uint32_t icr_high = 0u;
                                    auto buf = g_byte_array_sized_new(sizeof(icr_high));
                                    if (lurapro::qemu_w<qemu_plugin_read_memory_vaddr>(vaddr, buf, sizeof(icr_high))) {

                                          if (std::memcpy(&icr_high, buf->data, sizeof(icr_high)); vcpu_index < 256) {
                                                last_target_apic_id[vcpu_index] = (icr_high >> 24) & 0xFF;
                                          }
                                    }
                                    g_byte_array_free(buf, TRUE);
                                    break;
                              }
                              case ICR_LOW: {

                                    std::uint32_t icr_low = 0u;
                                    auto mem_read_buf = g_byte_array_sized_new(sizeof(icr_low));
                                    if (lurapro::qemu_w<qemu_plugin_read_memory_vaddr>(vaddr, mem_read_buf, sizeof(icr_low))) {

                                          if (std::memcpy(&icr_low, mem_read_buf->data, sizeof(icr_low)); ((icr_low >> 8) & 0x7) == 0x6) { /* Delivery mode */

                                                const cpu_tracer::address target_address = (std::uint8_t(icr_low & 0xFF)) << 12;

                                                std::optional<std::uint32_t> target_vcpu = std::nullopt;
                                                switch ((icr_low >> 18) & 0x3) {
                                                      case 0x3: { /* All excluding self */

                                                            for (auto idx = 0u; idx < lurapro::signaled_edges->size(); ++idx) {
                                                                  if (idx == vcpu_index) {
                                                                        continue;
                                                                  }
                                                                  add_edge(idx, realpc, target_address);
                                                            }
                                                            break;
                                                      }
                                                      case 0x2: { /* All including self */

                                                            for (auto idx = 0u; idx < lurapro::signaled_edges->size(); ++idx) {
                                                                  add_edge(idx, realpc, target_address);
                                                            }
                                                            break;
                                                      }
                                                      case 0x1: { /* Self */
                                                            target_vcpu = vcpu_index;
                                                            break;
                                                      }
                                                      case 0x0: { /* Specific VCPU */

                                                            auto raw = last_target_apic_id[vcpu_index];
                                                            for (auto idx = 0u; idx < map_id.size(); ++idx) {
                                                                  if (map_id[idx] != raw) {
                                                                        continue;
                                                                  }
                                                                  target_vcpu = idx;
                                                                  break;
                                                            }
                                                            break;
                                                      }
                                                      default: {
                                                            break;
                                                      }
                                                }
                                                if (target_vcpu) {
                                                      add_edge(*target_vcpu, realpc, target_address);
                                                }
                                          }
                                    }
                                    g_byte_array_free(mem_read_buf, TRUE);
                                    break;
                              }
                              default: {
                                    break;
                              }
                        }
                  }
                  return;
            }
      } // namespace mem

      namespace cbs::insts {

            /* WRMSR X86 instruction */
            static void __cdecl first_wrmsr_exec(std::uint32_t vcpu_index, void *userdata) {

                  std::uint64_t rax = 0u, rcx = 0u, rdx = 0u;
                  if (const auto arr = lurapro::qemu_w<qemu_plugin_get_registers>(); arr && arr->data) [[likely]] {

                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::x86::rax), arr)) [[likely]] {
                              std::memcpy(&rax, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(rax)));
                        }
                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::x86::rcx), arr)) [[likely]] {
                              std::memcpy(&rcx, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(rcx)));
                        }
                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::x86::rdx), arr)) [[likely]] {
                              std::memcpy(&rdx, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(rdx)));
                        }
                  }
                  if (rcx == 0x1B) [[unlikely]] {
                        lurapro::mmio::base = ((rdx << 32) | (rax & 0xFFFFFFFF)) & 0xFFFFFFFFFFFFF000;
                        lurapro::mmio::size = 0x1000;
                  }
                  process::inst::first(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }

            /* WRMSR X86 instruction */
            static void __cdecl wrmsr_exec(std::uint32_t vcpu_index, void *userdata) {

                  std::uint64_t rax = 0u, rcx = 0u, rdx = 0u;
                  if (const auto arr = lurapro::qemu_w<qemu_plugin_get_registers>(); arr && arr->data) [[likely]] {

                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::x86::rax), arr)) [[likely]] {
                              std::memcpy(&rax, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(rax)));
                        }
                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::x86::rcx), arr)) [[likely]] {
                              std::memcpy(&rcx, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(rcx)));
                        }
                        if (helpers::read_register(lurapro::garr_buf, static_cast<std::uint8_t>(QEMU::regs::x86::rdx), arr)) [[likely]] {
                              std::memcpy(&rdx, lurapro::garr_buf->data, std::min<std::size_t>(lurapro::garr_buf->len, sizeof(rdx)));
                        }
                  }
                  if (rcx == 0x1B) [[unlikely]] {
                        lurapro::mmio::base = ((rdx << 32) | (rax & 0xFFFFFFFF)) & 0xFFFFFFFFFFFFF000;
                        lurapro::mmio::size = 0x1000;
                  }
                  process::inst::inst(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }
      } // namespace cbs::insts
} // namespace x86
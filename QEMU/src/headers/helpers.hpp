#pragma once
#include "defs.hpp"

namespace helpers {

      /* Compare buffer string with capstones disassembly string output. */
      inline std::size_t disassemble(const lurapro::capstone_handles::handle &handle, const lurapro::inst &inst, char *buffer, const std::size_t buffer_size) {

            if (inst.inst.bytes.empty()) {
                  return 0u;
            }
            const auto *tmp_ptr = inst.inst.bytes.data();
            std::size_t ts = inst.inst.bytes.size();
            std::uint64_t ta = inst.inst.pc;
            auto insn = handle.insn;
            return cs_disasm_iter(handle.ch, &tmp_ptr, &ts, &ta, insn) ? std::snprintf(buffer, buffer_size, "%s %s", insn->mnemonic, insn->op_str) : 0u;
      }

      /* Gets real address, logical PC tracked relative to incoming VCPU */
      inline cpu_tracer::address get_real_address(const std::uint32_t vcpu_index) {

            auto &v = *lurapro::real_pc;
            auto &cnt = v[vcpu_index];
            const auto result = (cnt * v.size()) + vcpu_index;
            ++cnt;
            return result;
      }

      /* Gets interpration mode through QEMUs disassembly output (Internal mode is not exposed without modifying QEMU) */
      inline std::pair<cpu_tracer::archs::interpretation_mode, lurapro::capstone_handles::handle> get_mode(const char *const dism, const lurapro::inst &inst) {

            char buffer[512u];
            const auto dism_len = std::strlen(dism);
            for (const auto &h : lurapro::capstone_handles::handles) {

                  if (const auto &n = disassemble(h, inst, buffer, sizeof(buffer)); n && n == dism_len && !std::memcmp(buffer, dism, dism_len)) {

                        return std::make_pair(h.imode, h);
                  }
            }
            return std::make_pair(config::DEFAULT_MODE, lurapro::capstone_handles::handles.front());
      }

      /* Gets capstone OP ID */
      inline std::optional<std::uint32_t> get_op_id(const lurapro::capstone_handles::handle &handle, const lurapro::inst &inst) {

            const auto *tmp_ptr = inst.inst.bytes.data();
            std::size_t ts = inst.inst.bytes.size();
            std::uint64_t ta = inst.inst.pc;
            auto insn = handle.insn;
            return cs_disasm_iter(handle.ch, &tmp_ptr, &ts, &ta, insn) ? std::optional<std::uint32_t>(insn->id) : std::nullopt;
      }

      /* Reads register returns true if successful */
      inline bool read_register(GByteArray *buffer, const std::size_t idx, const GArray *arr) {

            const auto &desc = reinterpret_cast<qemu_plugin_reg_descriptor *>(arr->data)[idx];
            g_byte_array_set_size(buffer, 0u);
            return lurapro::qemu_w<qemu_plugin_read_register>(desc.handle, buffer) != -1 && buffer->len;
      }
} // namespace helpers
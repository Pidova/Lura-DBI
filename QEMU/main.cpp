
/*


MSDOS:
    qemu-img create -f raw msdos.img 20M
    qemu-system-x86_64.exe  -plugin ":\projects\LuraDBI\x64\Release\QEMU.dll" -drive file="C:\qemudumps\MS-DOS.iso",format=raw,media=cdrom -drive file=msdos.img,format=raw,media=disk
    
VISTA:

qemu-system-x86_64.exe ^
-cpu qemu64 ^
-smp 16 ^
-m 16G ^
-accel tcg,thread=multi,tb-size=4096 ^
-boot d ^
-cdrom "C:\vm\windows\vista_x64.iso" ^
-drive file=winvista.qcow2,if=ide,id=hd0,format=qcow2,cache=unsafe,aio=threads ^
-vga std ^
-plugin "C:\projects\LuraDBI\x64\Release\QEMU.dll"

Windows 10:
  cd /d C:\qemu_imgs
    qemu-img create -f qcow2 win10.qcow2 80G
    qemu-system-x86_64.exe ^
 -cpu qemu64,+x2apic ^
-smp 16 ^
-m 16G ^
-accel tcg,thread=multi,tb-size=4096 ^
-boot d ^
-cdrom "C:\vm\windows\windows.iso" ^
-drive file="C:\vm\windows\virtio-win.iso",media=cdrom,id=virtio_drivers ^
-drive file=win10.qcow2,if=none,id=hd0,format=qcow2,cache=unsafe,aio=threads ^
-device virtio-blk-pci,drive=hd0 ^
-vga none ^
-device virtio-vga ^
-plugin "C:\projects\LuraDBI\x64\Release\QEMU.dll"
To invoke quit:
    CTRL + ALT + 2
    stop
    quit

*/

//#define QEMU_PLUGIN_DELAY_CBS 1
#define QEMU_DUMP_REG_LIST 1
//#define QEMU_PLUGIN_DEBUG
//#define QEMU_LOG_MMIO

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#endif
#include "UPDATEME.hpp"
#include "src/common.hpp"

namespace cbs {

      namespace inst {

            namespace debug {

                  /* Print regs */
                  [[maybe_unused]] inline void __cdecl print_regs() {

                        const auto arr = lurapro::qemu_w<qemu_plugin_get_registers>();
                        if (!arr || !arr->data) {
                              return;
                        }
                        thread_local GByteArray *buf = g_byte_array_new();
                        for (auto i = 0u; i < arr->len; ++i) {

                              if (!helpers::read_register(buf, i, arr)) {
                                    continue;
                              }
#ifdef QEMU_DUMP_REG_LIST
                              std::printf("%s = %d,", reinterpret_cast<qemu_plugin_reg_descriptor *>(arr->data)[i].name, i);
#else
                              std::printf("[%d]%-8s | Size: %d bytes | Value: 0x", i, reinterpret_cast<qemu_plugin_reg_descriptor *>(arr->data)[i].name, buf->len);
                              for (auto j = 0u; j < buf->len; ++j) {
                                    std::printf("%02X", buf->data[j]);
                              }
#endif
                              std::printf("\n");
                        }
                        return;
                  }
            } // namespace debug

            /* CB for memstorage for detecting MMIO */
            static void __cdecl mem_write(std::uint32_t vcpu_index, qemu_plugin_meminfo_t info, uint64_t vaddr, void *userdata) {
#ifdef QEMU_LOG_MMIO
                  const auto is_io = lurapro::qemu_w<qemu_plugin_hwaddr_is_io>(lurapro::qemu_w<qemu_plugin_get_hwaddr>(info, vaddr));
#ifdef QEMU_PLUGIN_DEBUG
                  std::printf("WRITE[%d](%s) %llu , %llu\n", vcpu_index, is_io ? "t" : "f", vaddr, lurapro::mmio::base);
#endif
                  if (is_io) [[unlikely]] {

                        (*lurapro::mmio::data)[vcpu_index].d.insert(cpu_tracer::blocks::mmio::data(vaddr, reinterpret_cast<lurapro::inst *>(userdata)->inst.real_pc));
                  }
#endif
                  switch (config::ARCH) {
                        case cpu_tracer::archs::arch::x86: {
                              x86::mem::access(vcpu_index, vaddr, reinterpret_cast<lurapro::inst *>(userdata)->inst.real_pc);
                              break;
                        }
                        default: {
                              throw std::runtime_error("Unsupported architecture on memwrite");
                              break;
                        }
                  }
                  return;
            }

            /* On Inst execute validate it or no */
            static void __cdecl first_exec(std::uint32_t vcpu_index, void *userdata) {

                  process::inst::first(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }
            static void __cdecl exec(std::uint32_t vcpu_index, void *userdata) {

                  process::inst::inst(vcpu_index, reinterpret_cast<lurapro::inst *>(userdata));
                  return;
            }
      } // namespace inst

      namespace vcpus {

            /* Init globals values on VCPU inits */
            static void __cdecl init(qemu_plugin_id_t id, std::uint32_t vcpu_index) {

                  std::unique_lock<std::shared_mutex> lock(*lurapro::tbmutex);
                  lurapro::garr_buf = g_byte_array_new();
                  if (lurapro::real_pc->size() <= vcpu_index) {
                        lurapro::real_pc->resize(vcpu_index + 1u);
                  }
                  if (lurapro::curr_pc->size() <= vcpu_index) {
                        lurapro::curr_pc->resize(vcpu_index + 1u);
                  }
                  if (lurapro::prevd_jumps->size() <= vcpu_index) {
                        lurapro::prevd_jumps->resize(vcpu_index + 1u);
                  }
                  if (lurapro::vcpu::vcpu_states->size() <= vcpu_index) {
                        lurapro::vcpu::vcpu_states->resize(vcpu_index + 1u);
                  }
                  if (lurapro::interrupts::ints->size() <= vcpu_index) {
                        lurapro::interrupts::ints->resize(vcpu_index + 1u);
                  }
                  if (lurapro::signaled_edges->size() <= vcpu_index) {
                        lurapro::signaled_edges->resize(vcpu_index + 1u);
                        (*lurapro::signaled_edges)[vcpu_index] = std::nullopt;
                  }
                  if (lurapro::prevd_jumps_set->size() <= vcpu_index) {
                        lurapro::prevd_jumps_set->resize(vcpu_index + 1u);
                        (*lurapro::prevd_jumps_set)[vcpu_index].reserve(config::MAX_BUFFER_N_SET);
                  }
#ifdef QEMU_LOG_MMIO
                  if (lurapro::mmio::data->size() <= vcpu_index) {
                        lurapro::mmio::data->resize(vcpu_index + 1u);
                        auto &ptr = (*lurapro::mmio::data)[vcpu_index];
                        ptr.d.reserve(config::MAX_BUFFER_N_SET);
                        ptr.file = std::ofstream(std::string(config::SAVE_DIRECTORY) + config::SAVEMMIO_NAME + std::to_string(vcpu_index) + config::SAVE_EXTENSION, std::ios::binary);
                  }
#endif
                  if (lurapro::save::save_jmp_locs->size() <= vcpu_index) {
                        lurapro::save::save_jmp_locs->resize(vcpu_index + 1u);
                        (*lurapro::save::save_jmp_locs)[vcpu_index] = std::ofstream(std::string(config::SAVE_DIRECTORY) + config::SAVELOCS_NAME + std::to_string(vcpu_index) + config::SAVE_EXTENSION, std::ios::binary);
                  }
                  switch (config::ARCH) {
                        case cpu_tracer::archs::arch::x86: {
                              if (csh h; cs_open(CS_ARCH_X86, CS_MODE_16, &h) == CS_ERR_OK) {
                                    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
                                    cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
                                    lurapro::capstone_handles::handles.emplace_back(lurapro::capstone_handles::handle(h, cs_malloc(h), cpu_tracer::archs::interpretation_mode::x16));
                              }
                              if (csh h; cs_open(CS_ARCH_X86, CS_MODE_32, &h) == CS_ERR_OK) {
                                    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
                                    cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
                                    lurapro::capstone_handles::handles.emplace_back(lurapro::capstone_handles::handle(h, cs_malloc(h), cpu_tracer::archs::interpretation_mode::x32));
                              }
                              if (csh h; cs_open(CS_ARCH_X86, CS_MODE_64, &h) == CS_ERR_OK) {
                                    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
                                    cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
                                    lurapro::capstone_handles::handles.emplace_back(lurapro::capstone_handles::handle(h, cs_malloc(h), cpu_tracer::archs::interpretation_mode::x64));
                              }
                              break;
                        }
                        case cpu_tracer::archs::arch::ARM: {
                              if (csh h; cs_open(CS_ARCH_ARM, CS_MODE_ARM, &h) == CS_ERR_OK) {
                                    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
                                    lurapro::capstone_handles::handles.emplace_back(lurapro::capstone_handles::handle(h, cs_malloc(h), cpu_tracer::archs::interpretation_mode::arm32));
                              }
                              if (csh h; cs_open(CS_ARCH_ARM, CS_MODE_THUMB, &h) == CS_ERR_OK) {
                                    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
                                    lurapro::capstone_handles::handles.emplace_back(lurapro::capstone_handles::handle(h, cs_malloc(h), cpu_tracer::archs::interpretation_mode::arm_thumb));
                              }
                              if (csh h; cs_open(CS_ARCH_ARM, CS_MODE_MCLASS, &h) == CS_ERR_OK) {
                                    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
                                    lurapro::capstone_handles::handles.emplace_back(lurapro::capstone_handles::handle(h, cs_malloc(h), cpu_tracer::archs::interpretation_mode::arm_mclass));
                              }
                              if (csh h; cs_open(CS_ARCH_ARM, CS_MODE_V8, &h) == CS_ERR_OK) {
                                    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);
                                    lurapro::capstone_handles::handles.emplace_back(lurapro::capstone_handles::handle(h, cs_malloc(h), cpu_tracer::archs::interpretation_mode::arm_v8));
                              }
                              break;
                        }
                        default: {
                              throw std::runtime_error("Unsupported architecture on init");
                              break;
                        }
                  }
                  return;
            }

            /* Discontinuity callback */
            static void __cdecl discon(qemu_plugin_id_t id, std::uint32_t vcpu_index, enum qemu_plugin_discon_type type, uint64_t from_pc, uint64_t to_pc) {

#ifdef QEMU_PLUGIN_DELAY_CBS
                  if (!lurapro::start_cbs.load(std::memory_order_relaxed)) {
                        return;
                  }
#endif
                  auto t = cpu_tracer::blocks::interrupts::type::EXCEPTION;
                  switch (type) {
                        case qemu_plugin_discon_type::QEMU_PLUGIN_DISCON_HOSTCALL: {
                              t = cpu_tracer::blocks::interrupts::type::ETC;
                              break;
                        }
                        case qemu_plugin_discon_type::QEMU_PLUGIN_DISCON_INTERRUPT: {
                              t = cpu_tracer::blocks::interrupts::type::INTERUPT;
                              break;
                        }
                        case qemu_plugin_discon_type::QEMU_PLUGIN_DISCON_EXCEPTION: {
                              t = cpu_tracer::blocks::interrupts::type::EXCEPTION;
                              break;
                        }
                        default: {
                              break;
                        }
                  }

#ifdef QEMU_PLUGIN_DEBUG
                  std::printf("INT [%d](%d) %llu -> %llu\n", static_cast<const std::uint8_t>(t), vcpu_index, from_pc, to_pc);
#endif
                  lurapro::interrupts::fadd_real_pc = true;
                  auto &vec = (*lurapro::interrupts::ints)[vcpu_index];
                  if (vec.size() >= vec.capacity()) {
                        vec.reserve(!vec.capacity() ? 1024u : vec.capacity() * 2u); /* Grow *2 */
                  }
                  vec.emplace_back(cpu_tracer::blocks::interrupts::interrupt(t, to_pc, from_pc, 0u, lurapro::translation::global_block_id, vcpu_index));
                  return;
            }

            /* Flushes graveyard */
            static void __cdecl tb_flush(qemu_plugin_id_t id) {

#ifdef QEMU_PLUGIN_DELAY_CBS
                  if (!lurapro::start_cbs.load(std::memory_order_relaxed)) {
                        return;
                  }
#endif
                  std::unique_lock<std::shared_mutex> lock(*lurapro::tbmutex);
                  if (lurapro::translation::block_graveyard) {
                        for (const auto &i : *lurapro::translation::block_graveyard) {
                              i->write(*lurapro::save::save);
                        }
                        lurapro::translation::block_graveyard->clear();
                  }
                  if (lurapro::interrupts::ints) {
                        for (auto &i : *lurapro::interrupts::ints) {
                              cpu_tracer::blocks::interrupts::write(*lurapro::save::save, i);
                              i.clear();
                        }
                  }
                  if (lurapro::vcpu::vcpu_states) {
                        for (auto &i : *lurapro::vcpu::vcpu_states) {
                              cpu_tracer::blocks::vcpu::write(*lurapro::save::save, i);
                              i.clear();
                        }
                  }
                  return;
            }

            /* Called when VCPU idles (used for determining cpu kick edges) */
            static void __cdecl idle(qemu_plugin_id_t id, std::uint32_t vcpu_index) {

#ifdef QEMU_PLUGIN_DELAY_CBS
                  if (!lurapro::start_cbs.load(std::memory_order_relaxed)) {
                        return;
                  }
#endif
#ifdef QEMU_PLUGIN_DEBUG
                  std::printf("VCPU[%d] Idled\n", vcpu_index);
#endif

                  auto &vec = (*lurapro::vcpu::vcpu_states)[vcpu_index];
                  if (vec.size() >= vec.capacity()) {
                        vec.reserve(!vec.capacity() ? 1024u : vec.capacity() * 2u); /* Grow *2 */
                  }
                  auto &curr_pc = (*lurapro::curr_pc)[vcpu_index];
                  vec.emplace_back(cpu_tracer::blocks::vcpu::captured_block_state(curr_pc.has_value(), cpu_tracer::blocks::vcpu::state::PAUSED, vcpu_index, curr_pc.value_or(0u), lurapro::translation::global_block_id));
                  curr_pc = std::nullopt;
                  return;
            }

            /* Called when VCPU resumes (used for determining cpu kick edges) */
            static void __cdecl resume(qemu_plugin_id_t id, std::uint32_t vcpu_index) {

#ifdef QEMU_PLUGIN_DELAY_CBS
                  if (!lurapro::start_cbs.load(std::memory_order_relaxed)) {
                        return;
                  }
#endif
#ifdef QEMU_PLUGIN_DEBUG
                  std::printf("VCPU[%d] Resumed\n", vcpu_index);
#endif

                  auto &vec = (*lurapro::vcpu::vcpu_states)[vcpu_index];
                  if (vec.size() >= vec.capacity()) {
                        vec.reserve(!vec.capacity() ? 1024u : vec.capacity() * 2u); /* Grow *2 */
                  }
                  auto &curr_pc = (*lurapro::curr_pc)[vcpu_index];
                  vec.emplace_back(cpu_tracer::blocks::vcpu::captured_block_state(curr_pc.has_value(), cpu_tracer::blocks::vcpu::state::RESUME, vcpu_index, curr_pc.value_or(0u), lurapro::translation::global_block_id));
                  curr_pc = std::nullopt;
                  return;
            }

      } // namespace vcpus

      namespace tb {

            /* Translation block execution CB */
            static void __cdecl exec(std::uint32_t vcpu_index, void *userdata) {

                  auto &djmps = (*lurapro::prevd_jumps)[vcpu_index];
                  const auto b = reinterpret_cast<lurapro::block *>(userdata);

                  /* Signaled edges */
                  if (auto &pe = (*lurapro::signaled_edges)[vcpu_index]; pe) [[unlikely]] {

                        if (std::uint64_t addr_buf = 0u; lurapro::qemu_w<qemu_plugin_translate_vaddr>(b->loc, &addr_buf) && pe->target_pc == addr_buf) {

                              djmps.emplace_back(lurapro::edge(cpu_tracer::blocks::edges::kind::signaled, b->loc, pe->from));
                        }
                        pe = std::nullopt;
                  }

                  /* Edges set */
                  if (djmps.empty()) [[likely]] {
                        return;
                  }
                  auto &jset = (*lurapro::prevd_jumps_set)[vcpu_index];
                  jset.insert(std::make_move_iterator(djmps.begin()), std::make_move_iterator(djmps.end()));
                  djmps.clear();
                  if (jset.size() > config::MAX_BUFFER_N_SET) [[unlikely]] {

                        /* Save edges */
                        auto nodes_to_save = std::move(jset);
                        jset.clear();
                        boost::asio::post(*lurapro::save::pool, [to_save = std::move(nodes_to_save), vcpu_index]() mutable {
                              cpu_tracer::blocks::edges::write(to_save, (*lurapro::save::save_jmp_locs)[vcpu_index]);
                        });
                  }
#ifdef QEMU_LOG_MMIO
                  if ((*lurapro::mmio::data)[vcpu_index].d.size() > config::MAX_BUFFER_N_SET) [[unlikely]] {

                        /* Save mmio data */
                        boost::asio::post(*lurapro::save::pool, [vcpu_index]() mutable {
                              auto &data = (*lurapro::mmio::data)[vcpu_index];
                              cpu_tracer::blocks::mmio::write(data.file, boost::container::vector<cpu_tracer::blocks::mmio::data>(data.d.begin(), data.d.end()));
                              data.d.clear();
                        });
                  }
#endif
                  return;
            }

            /* Translate per block CB */
            static void __cdecl trans(qemu_plugin_id_t id, qemu_plugin_tb *tb) {

#ifdef QEMU_PLUGIN_DELAY_CBS
                  if (!lurapro::start_cbs.load(std::memory_order_relaxed)) {
                        return;
                  }
#endif
                  const auto instc = lurapro::qemu_w<qemu_plugin_tb_n_insns>(tb);
                  if (!instc) {
                        return;
                  }
                  auto b = boost::make_shared<lurapro::block>();
                  b->time = std::time(nullptr);
                  b->inst_count = instc;
                  b->vcpu_n = (*lurapro::real_pc).size();

                  /* Translate current block */
                  {
                        bool fhas_pc = false;
                        b->insts.reserve(instc);
                        lurapro::capstone_handles::handle h;
                        for (auto i = 0u; i < instc; ++i) {

                              const auto insn = lurapro::qemu_w<qemu_plugin_tb_get_insn>(tb, i);
                              const auto len = lurapro::qemu_w<qemu_plugin_insn_size>(insn);
                              if (!len) [[unlikely]] {
                                    continue;
                              }

                              /* Set PC */
                              const auto pc = static_cast<cpu_tracer::address>(lurapro::qemu_w<qemu_plugin_insn_vaddr>(insn));
                              if (!fhas_pc) {
                                    fhas_pc = true;
                                    b->loc = pc;
                              }
                              b->insts.emplace_back();
                              auto &inst = b->insts.back();

                              /* Write bytes to TB */
                              inst.inst.bytes.resize(len);
                              lurapro::qemu_w<qemu_plugin_insn_data>(insn, inst.inst.bytes.data(), len);
                              inst.inst.pc = pc;

                              /* Get mode */
                              if (!i) {
                                    const auto &[mode, handle] = helpers::get_mode(lurapro::qemu_w<qemu_plugin_insn_disas>(insn), inst);
                                    h = handle;
                                    b->interpretation_id = mode;
                              }

                              /* Special instructions */
                              cpu_tracer::flag fset_exec = false;
                              switch (config::ARCH) {
                                    case cpu_tracer::archs::arch::x86: {

                                          /* WRMSR */
                                          if (inst.inst.contains({0x0F, 0x30})) [[unlikely]] {
                                                fset_exec = true;
                                                lurapro::qemu_w<qemu_plugin_register_vcpu_insn_exec_cb>(insn, !i ? x86::cbs::insts::first_wrmsr_exec : x86::cbs::insts::wrmsr_exec, qemu_plugin_cb_flags::QEMU_PLUGIN_CB_R_REGS, reinterpret_cast<void *>(&b->insts[i]));
                                          }
                                          break;
                                    }
                                    default: {
                                          break;
                                    }
                              }
                              if (!fset_exec) [[likely]] {
                                    lurapro::qemu_w<qemu_plugin_register_vcpu_insn_exec_cb>(insn, !i ? inst::first_exec : inst::exec, qemu_plugin_cb_flags::QEMU_PLUGIN_CB_NO_REGS, reinterpret_cast<void *>(&b->insts[i]));
                              }

                              /* MMIO */
                              lurapro::qemu_w<qemu_plugin_register_vcpu_mem_cb>(insn, inst::mem_write, qemu_plugin_cb_flags::QEMU_PLUGIN_CB_NO_REGS, qemu_plugin_mem_rw::QEMU_PLUGIN_MEM_RW, reinterpret_cast<void *>(&b->insts[i]));

#ifdef QEMU_PLUGIN_DEBUG
                              helpers::disassemble(h, inst, inst.DEBUG_DISM, sizeof(inst.DEBUG_DISM));
#endif
                        }
                        if (!fhas_pc) [[unlikely]] {
                              return;
                        }
                  }
                  if (b->insts.empty()) [[unlikely]] {
                        return;
                  }
                  const auto range = lurapro::range(b->loc, (b->insts.back().inst.pc + b->insts.back().inst.bytes.size() - 1u));

                  /* See if current block is getting translated again */
                  {
                        lurapro::qemu_w<qemu_plugin_register_vcpu_tb_exec_cb>(tb, tb::exec, qemu_plugin_cb_flags::QEMU_PLUGIN_CB_NO_REGS, b.get());

                        std::unique_lock<std::shared_mutex> lock(*lurapro::tbmutex);
                        b->id = lurapro::translation::global_block_id++;
                        if (lurapro::translation::block_graveyard->size() >= lurapro::translation::block_graveyard->capacity()) {
                              lurapro::translation::block_graveyard->reserve(!lurapro::translation::block_graveyard->capacity() ? 1024u : lurapro::translation::block_graveyard->capacity() * 2u);
                        }
                        auto bit = lurapro::translation::blocks->find(range.first);
                        if (bit != lurapro::translation::blocks->end()) {

                              bit->second->fretranslated = true;
                              auto it = lurapro::translation::logged_ranges->find(boost::icl::discrete_interval<cpu_tracer::address>::closed(range.first, range.first));
                              if (it != lurapro::translation::logged_ranges->end()) {
                                    lurapro::translation::logged_ranges->erase(it);
                              }

                              lurapro::translation::block_graveyard->emplace_back(bit->second);
                              lurapro::translation::blocks->erase(bit);
                        }

                        bit = lurapro::translation::blocks->find(range.second);
                        if (bit != lurapro::translation::blocks->end()) {

                              bit->second->fretranslated = true;
                              auto it = lurapro::translation::logged_ranges->find(boost::icl::discrete_interval<cpu_tracer::address>::closed(range.second, range.second));
                              if (it != lurapro::translation::logged_ranges->end()) {
                                    lurapro::translation::logged_ranges->erase(it);
                              }

                              lurapro::translation::block_graveyard->emplace_back(bit->second);
                              lurapro::translation::blocks->erase(bit);
                        }
                        /* Log block */
                        lurapro::translation::logged_ranges->insert(boost::icl::discrete_interval<cpu_tracer::address>::closed(range.first, range.second));
                        lurapro::translation::blocks->insert_or_assign(range.first, std::move(b));
                  }
                  return;
            }
      } // namespace tb

      /* On vcpu exit CB save instruction data */
      static void __cdecl at_exit(qemu_plugin_id_t id, void *userdata) {

            if (lurapro::save::pool) {
                  lurapro::save::pool->join();
            }
            for (const auto &[_, b] : *lurapro::translation::blocks) {
                  b->write(*lurapro::save::save);
            }
            for (const auto &i : *lurapro::translation::block_graveyard) {
                  i->write(*lurapro::save::save);
            }
            for (const auto &i : *lurapro::interrupts::ints) {
                  cpu_tracer::blocks::interrupts::write(*lurapro::save::save, i);
            }
            for (const auto &i : *lurapro::vcpu::vcpu_states) {
                  cpu_tracer::blocks::vcpu::write(*lurapro::save::save, i);
            }
#ifdef QEMU_LOG_MMIO
            for (const auto &i : *lurapro::mmio::data) {
                  if (i.d.empty()) {
                        continue;
                  }
                  cpu_tracer::blocks::mmio::write(*lurapro::save::save, boost::container::vector<cpu_tracer::blocks::mmio::data>(i.d.begin(), i.d.end()));
            }
#endif
            for (auto idx = 0u; idx < lurapro::prevd_jumps_set->size(); ++idx) {
                  cpu_tracer::blocks::edges::write((*lurapro::prevd_jumps_set)[idx], (*lurapro::save::save_jmp_locs)[idx]);
            }
            lurapro::destroy();
            return;
      }
} // namespace cbs

#ifdef QEMU_PLUGIN_DELAY_CBS
/* Listen for command to enable expensive Cbs */
void cmd_listener() {

      std::string input("");
      std::printf("Enter 'x' to enable expensive Cbs\n");
      do {
            std::cin >> input;
            if (input == "x") {
                  std::printf("Enabled Cbs\n");
                  lurapro::start_cbs.store(true);
                  break;
            }
      } while (true);
      return;
}
#endif

/* Main plugin install */
extern "C" QEMU_PLUGIN_EXPORT std::int32_t __cdecl qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv) {

      lurapro::init();
      switch (config::ARCH) {
            case cpu_tracer::archs::arch::x86: {
                  lurapro::mmio::base = 0xFEE00000;
                  lurapro::mmio::size = 0x1000;
                  break;
            }
            default: {
                  throw std::runtime_error("Unsupported architecture on install");
                  break;
            }
      }
#ifdef _WIN32
      lurapro::mod = LoadLibraryA("qemu-system-x86_64.exe");
#endif
      lurapro::qemu_w<qemu_plugin_register_vcpu_init_cb>(id, cbs::vcpus::init);
      lurapro::qemu_w<qemu_plugin_register_atexit_cb>(id, cbs::at_exit, nullptr);

      /* Can optionaly start being executed later */
      {
#ifdef QEMU_PLUGIN_DELAY_CBS
            std::thread(cmd_listener).detach();
#endif
            lurapro::qemu_w<qemu_plugin_register_vcpu_tb_trans_cb>(id, cbs::tb::trans);
            lurapro::qemu_w<qemu_plugin_register_flush_cb>(id, cbs::vcpus::tb_flush);
            lurapro::qemu_w<qemu_plugin_register_vcpu_idle_cb>(id, cbs::vcpus::idle);
            lurapro::qemu_w<qemu_plugin_register_vcpu_resume_cb>(id, cbs::vcpus::resume);
            lurapro::qemu_w<qemu_plugin_register_vcpu_discon_cb>(id, qemu_plugin_discon_type::QEMU_PLUGIN_DISCON_ALL, cbs::vcpus::discon);
      }
      return 0;
}
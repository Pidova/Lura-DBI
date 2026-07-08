#include "../config.hpp"
#include "../shared/cfg_tools/common.hpp"
#include "../shared/cpu_tracer/common.hpp"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/sort/pdqsort/pdqsort.hpp>
#include <capstone/capstone.h>
#include <capstone/x86.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>

using edge_data = cpu_tracer::blocks::loader::analyze::edge_data;
using save_data = cpu_tracer::blocks::loader::save_data<config::MAX_LEN>;
using block_ptr = cpu_tracer::blocks::loader::block_ptr<config::MAX_LEN>;
using analyzed_data = cpu_tracer::blocks::loader::analyze::analyzed_data<config::MAX_LEN>;
using cap_map = boost::unordered_flat_map<cpu_tracer::archs::interpretation_mode, std::pair<csh, cs_insn *>>; /* Capstone map Interp mode -> { CS DATA } */

namespace analysis {

      /* Disassemble data */
      inline bool disassemble(const block_ptr &b, const cpu_tracer::blocks::inst_data<config::MAX_LEN> *inst, cap_map &cmap) {
            const auto *tmp_ptr = inst->inst.bytes.data();
            std::size_t ts = inst->inst.bytes.size();
            std::uint64_t ta = inst->inst.pc;
            auto &[capstone_handle, insn] = cmap[b->interpretation_id];
            return cs_disasm_iter(capstone_handle, &tmp_ptr, &ts, &ta, insn);
      }
} // namespace analysis

std::int32_t main() {

      cap_map capmap;      /* Capstone map */
      save_data sdata;     /* Save data  */
      analyzed_data adata; /* Analyzed data */
      edge_data edata;     /* Raw edge data */

      /* X86 capstone handles */
      {
            if (csh h; cs_open(CS_ARCH_X86, CS_MODE_16, &h) == CS_ERR_OK) {
                  cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
                  cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
                  capmap[cpu_tracer::archs::interpretation_mode::x16] = std::pair(h, cs_malloc(h));
            }
            if (csh h; cs_open(CS_ARCH_X86, CS_MODE_32, &h) == CS_ERR_OK) {
                  cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
                  cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
                  capmap[cpu_tracer::archs::interpretation_mode::x32] = std::pair(h, cs_malloc(h));
            }
            if (csh h; cs_open(CS_ARCH_X86, CS_MODE_64, &h) == CS_ERR_OK) {
                  cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
                  cs_option(h, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
                  capmap[cpu_tracer::archs::interpretation_mode::x64] = std::pair(h, cs_malloc(h));
            }
      }

      /* Load data */
      for (const auto &entry : std::filesystem::directory_iterator(config::SAVE_DIRECTORY)) {

            if (!entry.is_regular_file()) {
                  continue;
            }
            std::ifstream file(entry.path(), std::ios::binary);
            if (!file) {
                  std::cerr << "Failed: " << entry.path() << "\n";
                  continue;
            }
            std::printf("On file %s\n", entry.path().string().c_str());
            cpu_tracer::blocks::loader::load(sdata, file);
      }

      /* Analyze data */
      {
            cpu_tracer::blocks::loader::analyze::analyze(adata, sdata);         /* Compiled data */
            cpu_tracer::blocks::loader::analyze::analyze(edata, sdata, adata);  /* Edges */
            cpu_tracer::blocks::loader::tools::fix_blocks(edata, adata, sdata); /* Fix blocks */
      }

      std::size_t total_insts = 0u; /* Total valid instruction count */

      /* Count instructions */
      for (auto &[addr, b] : sdata.block_map) {

            /* Total instructions */
            for (const auto &i : b->insts) {
                  total_insts += i.fvalid;
            }
      }

      /* Print data */
      std::printf("Total valid instruction count: %zu\n", total_insts);
      std::printf("Total blocks: %zu\n", sdata.block_map.size());
      std::printf("Total edges: %zu\n", edata.successors.size());

      std::printf("[PAUSED] Press enter to see linearized graph\n");
      std::cin.get();

      boost::unordered_flat_set<cpu_tracer::address> references_labels;  /* Labels referenced by instructions */
      boost::unordered_flat_map<cpu_tracer::address, std::string> nodes; /* Address to ndoe string */

      /* Compile block map string */
      for (const auto &[addr, b] : sdata.block_map) {

            for (auto idx = 0u; idx < b->insts.size(); ++idx) {

                  const auto &i = b->insts[idx];

                  cpu_tracer::blocks::flags::finsts finsts;
                  finsts.unpack(i.flags);

                  /* Dissassemble */
                  const auto *tmp_ptr = i.inst.bytes.data();
                  std::size_t ts = i.inst.bytes.size();
                  std::uint64_t ta = i.inst.pc;
                  auto &[capstone_handle, insn] = capmap[b->interpretation_id];
                  if (cs_disasm_iter(capstone_handle, &tmp_ptr, &ts, &ta, insn)) {

                        /* Flag self modified */
                        auto &str = nodes[addr];
                        if (finsts.fself_modified) {
                              str += "[V]";
                        }

                        /* Compile */
                        str += "  " + std::string(insn->mnemonic) + " ";

                        /* Add reference stack edges */
                        if (idx + 1u >= b->insts.size()) {

                              std::string refs("; { ");           /* Compiled references */
                              cpu_tracer::flag fhas_refs = false; /* Have references to multiple unique edges? */
                              cpu_tracer::flag fchanged = false;  /* Label appended to the start of the string? */
                              if (const auto &it = edata.successors.find(i.inst.real_pc); it != edata.successors.end() && it->second.size()) {

                                    for (const auto &dest : it->second) {

                                          const auto bit = sdata.block_map.find(dest.dst_realpc);
                                          if (bit == sdata.block_map.end() || bit->second->insts.empty()) {
                                                continue;
                                          }
                                          const auto label_addr = bit->second->insts.front().inst.real_pc;
                                          if (i.inst.bytes.size() + i.inst.pc != bit->second->loc) {
                                                if (!fchanged && finsts.fref) {
                                                      str += edata.gen_label(label_addr) + " ";
                                                      fchanged = true;
                                                      references_labels.insert(label_addr);
                                                } else {
                                                      fhas_refs = true;
                                                      refs += edata.gen_label(label_addr) + " ";
                                                      references_labels.insert(label_addr);
                                                }
                                          } else if (!finsts.fref) {
                                                fhas_refs = true;
                                                refs += edata.gen_label(label_addr) + " ";
                                                references_labels.insert(label_addr);
                                          }
                                    }
                                    refs += "}";
                              }
                              if (!fchanged) {
                                    str += std::string(insn->op_str);
                              }
                              if (fhas_refs) {
                                    str += refs;
                              }
                        } else {
                              str += std::string(insn->op_str);
                        }
                        str += "\n";
                  }
            }
      }

      /* Add Labels */
      for (const auto &[addr, b] : sdata.block_map) {

            const auto label_addr = b->insts.front().inst.real_pc;
            if (!references_labels.contains(label_addr)) {
                  continue;
            }
            auto &str = nodes[addr];
            str = edata.gen_label(label_addr) + ":\n" + str;
      }

      /* Boost graph */
      const auto g = cpu_tracer::blocks::graph::generate_graph(adata, edata);

      /* Node String CB */
      using graph_t = std::remove_cvref_t<decltype(g)>;
      cfg_tools::cb_node_string<graph_t> func = [&](const graph_t &graph, const cfg_tools::basic_block<graph_t> &bb) {
            const auto addr = g[bb]->insts.front().inst.real_pc;
            const auto it = nodes.find(addr);
            return it != nodes.end() ? it->second : std::string("");
      };

      /* Output linearization */
      cfg_tools::linearize(g, func, std::cout);

      std::cin.get();
      return 0;
}
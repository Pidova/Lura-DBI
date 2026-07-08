#pragma once
#include <cstdint>

namespace cpu_tracer {

      /* Type of data saved */
      enum class save_type : std::uint8_t {
            none,        /* Nothing*/
            block,       /* Block */
            edge_map,    /* Edges */
            vcpu_states, /* VCPU states */
            interrupts,  /* Interrupt data */
            MMIO,        /* MMIO data */
            reg_data     /* Register data */
      };

      /* List of supported architectures, mainly used for groupings */
      namespace archs {

            /* Supported architectures */
            enum class arch : std::uint8_t {
                  none,   /* No arch */
                  x86,    /* X86 */
                  ARM,    /* ARM */
                  mips,   /* MIPS */
                  ppc,    /* PowerPC */
                  sparc,  /* SPARC */
                  riscv,  /* RISC-V */
                  m68k,   /* Motorola 68K */
                  systemz /* IBM SystemZ */
            };

            /* Supported interpretation mode for architecture (grouped) */
            enum class interpretation_mode : std::uint8_t {

                  none, /* No mode */

                  /* x86 */
                  x16, /* (x86) X16 */
                  x32, /* (x86) X32 */
                  x64, /* (x86) X64 */

                  /* ARM */
                  arm_thumb,  /* (ARM) Thumb */
                  arm32,      /* (ARM) 32 bit */
                  arm_mclass, /* (ARM) Cortex-M */
                  arm_v8,     /* (ARM) V8 / AArch64 */

                  /* MIPS */
                  mips32,     /* (MIPS) 32 bit */
                  mips64,     /* (MIPS) 64 bit */
                  mips_micro, /* (MIPS) microMIPS */
                  mips32r6,   /* (MIPS) 32 bit Release 6 */

                  /* PowerPC */
                  ppc32, /* (PPC) 32 bit */
                  ppc64, /* (PPC) 64 bit */

                  /* SPARC */
                  sparc32, /* (SPARC) 32 bit */
                  sparc64, /* (SPARC) 64 bit / V9 */

                  /* RISC-V */
                  rv32,  /* (RISC-V) 32 bit */
                  rv64,  /* (RISC-V) 64 bit */
                  rv32c, /* (RISC-V) 32 bit Compressed */
                  rv64c, /* (RISC-V) 64 bit Compressed */

                  /* Motorola 68K */
                  m68000, /* (M68K) 68000 */
                  m680x0, /* (M68K) 68010 - 68060 */

                  /* SystemZ */
                  s390x /* (SystemZ) 64 bit */
            };
      } // namespace archs
} // namespace cpu_tracer
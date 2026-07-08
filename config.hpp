#include "shared/cpu_tracer/common.hpp"

namespace config {
	 
	  /* Max data in buffers before emitting to a set */
      static constexpr auto MAX_BUFFER_N = 128u;
	  
	  /* Minimum data in set before saving */
      static constexpr auto MAX_BUFFER_N_SET = MAX_BUFFER_N * 32u;
	  
	  /* Maximum length per instruction */
      static constexpr auto MAX_LEN = 15u;
	  
	  /* Maximum register data */
	  static constexpr auto MAX_REG_DATA = sizeof(std::uint64_t) * 4u;

	  /* Directory to save edge and block data */
      static constexpr auto SAVE_DIRECTORY = "C:\\qemudumps\\blocks\\";
	  
	  /* Save name of misc data */
      static constexpr auto SAVEMAIN_NAME = "save";
	  
	  /* Save name of edge data */
      static constexpr auto SAVELOCS_NAME = "edges_";

	  /* Save name of MMIO data */
	  static constexpr auto SAVEMMIO_NAME = "mmio_";
	  
	  /* Save name of reg data */
	  static constexpr auto SAVEREG_NAME = "reg_";

	  /* Extension */
      static constexpr auto SAVE_EXTENSION = ".lurablks";
	  
	  /* Default interpretation mode */
      static constexpr auto DEFAULT_MODE = cpu_tracer::archs::interpretation_mode::x64;
	  
	   /* Target architecture */
      static constexpr auto ARCH = cpu_tracer::archs::arch::x86;

	  /* Maximum Possible emulating cores */
	  static constexpr auto MAX_CORES = 128u;
} // namespace config
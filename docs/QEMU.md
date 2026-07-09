# QEMU Plugin

The Lura-DBI [QEMU TCG plugin](https://www.qemu.org/docs/master/devel/tcg-plugins.html) - 
captures per-VCPU execution traces directly from a running guest and serializes them with CPU-Tracer's block format.

## What is the QEMU Plugin?

QEMU's plugin API lets an external shared library register callbacks on translation, execution, memory access, and lifecycle events without modifying QEMU itself. 
Lura-DBI's plugin registers on every one of these to capture a full-system trace: translated blocks and their instructions, control-flow edges, interrupts/exceptions, VCPU pause/resume transitions, 
and MMIO accesses, buffering each per-VCPU and flushing to disk in the background.

The plugin is laid out as:
* **QEMU/main.cpp** - entry point (**qemu_plugin_install**) and every callback (**cbs** namespace)
* **QEMU/src/headers/defs.hpp** - global plugin state (**lurapro** namespace): per-VCPU buffers, save streams, thread pool, Capstone handles, init/destroy
* **QEMU/src/headers/helpers.hpp** - small utilities: real-PC allocation, interpretation-mode detection, register reads
* **QEMU/src/headers/process.hpp** - shared per-instruction processing logic, used by both the plain execution callbacks and the Architecture Specific Cbs
* **config.hpp** - Configurable (**config** namespace)
* **QEMU/UPDATEME.hpp** - See [Register Indices](#register-indices-updatemehpp) for more details
For architecture Specific files see [Architecture Helpers](#architecture-helpers).


## Idea
```
qemu_plugin_install
  -> register_vcpu_init_cb      (cbs::vcpus::init)      per-VCPU buffer allocation, Capstone handle setup
  -> register_vcpu_tb_trans_cb  (cbs::tb::trans)        translate a TB into a block, register per-instruction callbacks
  -> register_flush_cb          (cbs::vcpus::tb_flush)  drain the graveyard/interrupts/vcpu states on TB cache flush
  -> register_vcpu_idle_cb      (cbs::vcpus::idle)
  -> register_vcpu_resume_cb    (cbs::vcpus::resume)
  -> register_vcpu_discon_cb    (cbs::vcpus::discon)    interrupts, exceptions, hostcalls
  -> register_atexit_cb         (cbs::at_exit)          final flush + lurapro::destroy()
```


## Algorithm

### Translation (**cbs::tb::trans**)

For every instruction in a newly translated TB, the plugin reads its virtual PC and raw bytes into a **cpu_tracer::blocks::block<MAX_LEN>**. 
The block's interpretation mode is resolved once, by disassembling the first instruction with every configured Capstone handle (**helpers::get_mode**) 
and matching the result against QEMU's own disassembly string. 
A per-instruction execution callback (**inst::first_exec** / **inst::exec**) and memory callback (**inst::mem_write**, for MMIO) are then registered 
for each instruction except for architecture specific memwrites see [MemWrites](#memwrites) for more details.

Once translation finishes, the block's address range is recorded in 
**logged_ranges** (an interval set used to detect retranslation from self-modifying code); any older block(s) already covering that range are moved into 
**block_graveyard** rather than freed immediately, since a TB already scheduled to execute may still hold a raw pointer into them.

* Time - O(n) per TB, n = instruction count
* Memory - O(n)


### Execution (**cbs::tb::exec**, **process::inst::first** / **inst**)

On each TB execution, **cbs::tb::exec** resolves any pending **signaled_edge** for the current VCPU into a exact edge, and flushes the VCPU's buffered edge set to disk via a background 
**boost::asio::thread_pool** once it crosses **config::MAX_BUFFER_N_SET**, so tracing doesn't stall on I/O. Per instruction, **process::inst::first** / **process::inst::inst** mark it valid, 
assign it a real PC (**helpers::get_real_address**), and detect discontinuities by comparing the previous real PC against the instruction's **prev_real_pc** chain - a mismatch is buffered as an 
unresolved **next** / **next_punk** edge, later reconciled by CPU-Tracer's edge-reconstruction pass (see CPU-Tracer's [Loading & Analysis](https://github.com/Pidova/CPU-Tracer/blob/main/docs/loading_and_analysis.md) docs).

* Time - O(1) amortized per instruction
* Memory - O(1) amortized per instruction, batched buffers flushed at **config::MAX_BUFFER_N_SET**

### Flushing (**cbs::vcpus::tb_flush**, **cbs::at_exit**)

**tb_flush** runs whenever QEMU invalidates its TB cache: every graveyarded block, buffered interrupt, and VCPU state is written out and cleared, so nothing captured before a retranslation is lost. 
**cbs::at_exit** performs a final full flush of every remaining block, interrupt, VCPU state, and per-VCPU edge set, joins the background save thread pool, then releases all global state through **lurapro::destroy()**.

## Configuration (**config.hpp**)

* **MAX_BUFFER_N** - Max entries buffered before moving into a set 
* **MAX_BUFFER_N_SET** - Min entries in a set before it's flushed to disk (**MAX_BUFFER_N x 32**) 
* **MAX_LEN** - Max bytes per instruction 
* **MAX_REG_DATA** - Max register data size 
* **SAVE_DIRECTORY** - Output directory for all trace files 
* **SAVEMAIN_NAME** / **SAVELOCS_NAME** / **SAVEMMIO_NAME** / **SAVEREG_NAME** - Filename prefixes for block, edge, MMIO, and register save files 
* **SAVE_EXTENSION** - Output file extension (**.lurablks**) 
* **DEFAULT_MODE** - Interpretation mode used when **helpers::get_mode** can't match a Capstone handle 
* **ARCH** - Target architecture (currently **X86**) 
* **MAX_CORES** - Max emulated VCPUs 


## Debug macros

* **QEMU_DUMP_REG_LIST** - print **name = index** for every QEMU register on demand (see above), instead of the normal per-instruction value dump
* **QEMU_PLUGIN_DEBUG** - enables verbose per-instruction/interrupt/VCPU-state logging and live disassembly via **helpers::disassemble**
* **QEMU_LOG_MMIO** - enables MMIO capture and the associated per-VCPU save files
* **QEMU_PLUGIN_DELAY_CBS** - gates the expensive callbacks (translation, flush, idle/resume, discontinuity) behind a console prompt (**cmd_listener**, type **x** to start), so tracing only begins once the guest has reached a desired state




## Architecture Specific Support

Currently: **X86** Guest Architecture is supported. 

#### Adding Support

To add support for a new guest architecture, you must implement its specific hardware quirks, interrupt handling, and register layouts by following these steps:

1. **Generate Register Indices (UPDATEME.hpp):**
* Create a new namespace/enum inside **UPDATEME.hpp** (e.g., QEMU::regs::arm).
* Compile the plugin with **QEMU_DUMP_REG_LIST** enabled and run it against the target QEMU emulator (e.g., qemu-system-aarch64).
* Copy the printed name = index pairs into your new enum.


2. **Identify Special Instructions (Hidden Side-Effects):**
* Determine if the architecture uses specific instructions to modify critical system state or relocate memory-mapped hardware (analogous to WRMSR on X86).
* Implement dedicated instruction callbacks to intercept and track these state changes ahead of standard memory tracking.


3. **Implement Architecture Helpers (QEMU/src/archs/):**
* Create a new header file in the architecture directory (e.g., **QEMU/src/archs/ARM.hpp**).
* Implement the necessary decoding logic for the architecture's specific hardware and interrupt controller (e.g., the GIC for ARM).


4. **Handle Signaled Edges (Inter-Processor Interrupts):**
* Map how the new architecture handles cross-CPU communication and IPIs.
* Handle writes to the specific interrupt controller's MMIO synthesize a **signaled_edge** targeting the resolved destination VCPU. This guarantees that hardware-level wakeups and interrupts are properly realized as graph edges during offline CFG reconstruction.


#### register indices (**UPDATEME.hpp**)

Each architecture has a manually maintained copy of the register index ordering that QEMU's plugin API exposes through **qemu_plugin_get_registers** / **qemu_plugin_read_register**.
 
**Note on Stability:** This ordering is internal to a given QEMU build and is not guaranteed to be stable across versions. 
The enum needs to be regenerated whenever the QEMU tree the plugin links against changes.

**How to Generate:**

1. Build with **QEMU_DUMP_REG_LIST** defined.
2. Run the plugin once with target architecture defined.
3. **cbs::inst::debug::print_regs** will print each register's **name = index** pair.
4. Copy that output into **QEMU::regs::[[ARCHITECTURE]]**.

#### Memwrites

Given a specific instruction in a given architecture it can have hidden side effects that signal non explicit edges.
This is designed to hook that specific instruction and analyze it.

* X86: **WRMSR** instructions, which get dedicated callbacks (**x86::cbs::insts::first_wrmsr_exec** / **wrmsr_exec**). Required because a write to **IA32_APIC_BASE** (MSR **0x1B**) relocates the local APIC's MMIO window and must be intercepted ahead of normal memory tracking.

#### Architecture Helpers

Architecture-specific data is organized as follows:

* **QEMU/src/archs/X86.hpp** — Handles x86-specific operations: **WRMSR**-based APIC relocation. ICR/APIC-ID MMIO decoding for cross-VCPU edges.

##### X86 (Signaled Edges)

Writes into the local APIC's MMIO window are decoded directly, without waiting for trace analysis:
* **VCPU Mapping:** A write to the APIC ID register updates a **VCPU-index <-> APIC-ID** map.
* **Edge Synthesis:** A write to the Interrupt Command Register (ICR) with a fixed/startup delivery mode synthesizes a **signaled_edge** targeting the resolved destination VCPU(s) (*self*, *all*, *all-but-self*, or one specific VCPU looked up through the ID map).
* **Graph Realization:** That edge is realized as a graph edge the next time the destination VCPU executes a TB (**cbs::tb::exec**).

**Key Takeaway:** Because of this design, cross-CPU interrupts (e.g., an AP wakeup via **INIT**/**SIPI**) successfully show up in the reconstructed CFG even though they never appear as a normal instruction-to-instruction transition.




## Example Usage
ISO images used directory: **qemu_imgs/**, Plugin in **plugin/**.

### MSDOS
Create image:
```
qemu-img create -f raw msdos.img 500M
```
Execute:
```
    qemu-system-x86_64.exe -plugin "plugin/QEMU.dll" -drive file="qemu_imgs/MS-DOS.iso",format=raw,media=cdrom -drive file=msdos.img,format=raw,media=disk
```

### Windows-Vista
Create image:
```
qemu-img create -f qcow2 winvista.qcow2 40G
```
Execute:
```
    qemu-system-x86_64.exe ^
    -cpu qemu64 ^
    -smp 16 ^
    -m 16G ^
    -accel tcg,thread=multi,tb-size=4096 ^
    -boot d ^
    -cdrom "qemu_imgs/vista_x64.iso" ^
    -drive file=winvista.qcow2,if=ide,id=hd0,format=qcow2,cache=unsafe,aio=threads ^
    -vga std ^
    -plugin "QEMU.dll"
```

### Windows-10
Create image:
```
qemu-img create -f qcow2 win10.qcow2 80G
```
Execute:
```
    qemu-system-x86_64.exe ^
    -cpu qemu64,+x2apic ^
    -smp 16 ^
    -m 16G ^
    -accel tcg,thread=multi,tb-size=4096 ^
    -boot d ^
    -cdrom "qemu_imgs\windows.iso" ^
    -drive file="qemu_imgs\virtio-win.iso",media=cdrom,id=virtio_drivers ^
    -drive file=win10.qcow2,if=none,id=hd0,format=qcow2,cache=unsafe,aio=threads ^
    -device virtio-blk-pci,drive=hd0 ^
    -vga none ^
    -device virtio-vga ^
    -plugin "plugin\QEMU.dll"
```

To end tracing and flush everything cleanly, quit QEMU from its monitor/console rather than killing the process:
```
CTRL + ALT + 2
stop
quit
```
This routes through **cbs::at_exit**, which is what performs the final flush and releases global state and killing the process instead risks losing whatever is still buffered.

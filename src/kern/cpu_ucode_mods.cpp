INTERFACE[ia32,amd64]:

#include "types.h"

class Cpu_ucode_mods
{};

//----------------------------------------------------------------------------
IMPLEMENTATION[ia32,amd64]:

#include "cpu.h"
#include "kip.h"
#include "kmem_mmio.h"
#include "warn.h"

PUBLIC static
void
Cpu_ucode_mods::init()
{
  for (auto &md: Kip::k()->mem_descs_a())
    if (md.valid() && !md.is_virtual()
        && md.type() == Mem_desc::Arch
        && md.ext_type() == Mem_desc::Arch_cpu_fw)
      {
        Address start = md.start();
        Address size = md.size();
        void *mod = Kmem_mmio::map(start, size);
        if (!mod)
          {
            WARN("Cannot map microcode module @ %lx-%lx\n", start, start + size);
            continue;
          }

        if (!Cpu::add_ucode_mod(reinterpret_cast<Address>(mod), size))
          {
            WARN("Cannot load microcode module @ %lx (increase Cpu::Max_mods?)\n",
                 start);
            continue;
          }
      }
}

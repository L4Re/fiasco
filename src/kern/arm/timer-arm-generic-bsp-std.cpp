IMPLEMENTATION [arm_generic_timer && (dt || arm_acpi)]:

#include "dt.h"

EXTENSION class Timer
{
  static unsigned _irq_phys, _irq_virt, _irq_hyp, _irq_secure_hyp;
};

unsigned Timer::_irq_phys, Timer::_irq_virt, Timer::_irq_hyp, Timer::_irq_secure_hyp;

PUBLIC static inline
unsigned Timer::irq()
{
  switch (Gtimer::Type)
  {
    case Generic_timer::Physical:   return _irq_phys;
    case Generic_timer::Virtual:    return _irq_virt;
    case Generic_timer::Hyp:        return _irq_hyp;
    case Generic_timer::Secure_hyp: return _irq_secure_hyp;
  };
}

IMPLEMENT
void Timer::bsp_init(Cpu_number cpu)
{
  if (cpu != Cpu_number::boot_cpu())
    return;

  if (Dt::have_fdt())
    init_dt();
  else
    init_acpi();
}

// ------------------------------------------------------------------
IMPLEMENTATION [arm_generic_timer && !dt && !arm_acpi]:

PUBLIC static inline
unsigned Timer::irq()
{
  using T = Generic_timer::Timer_type;
  static_assert(   static_cast<T>(Gtimer::Type) == Generic_timer::Physical
                || static_cast<T>(Gtimer::Type) == Generic_timer::Virtual
                || static_cast<T>(Gtimer::Type) == Generic_timer::Hyp);

  switch (Gtimer::Type)
    {
    case Generic_timer::Physical:   return 29;
    case Generic_timer::Virtual:    return 27;
    case Generic_timer::Hyp:        return 26;
    case Generic_timer::Secure_hyp: return 20;
    };
}

IMPLEMENT
void Timer::bsp_init(Cpu_number)
{}

// ------------------------------------------------------------------
IMPLEMENTATION [arm_generic_timer && dt]:

#include "dt.h"
#include "panic.h"

PRIVATE static
void Timer::init_dt()
{
  const char *c[] = { "arm,armv7-timer", "arm,armv8-timer",
                      "arm,cortex-a15-timer" };
  Dt::Node timer_node = Dt::node_by_compatible_list(c);
  if (!timer_node.is_valid())
    panic("No timer node found in DT");

  bool have_interrupt_names = false;
  timer_node.stringlist_for_each("interrupt-names",
                                 [&](unsigned idx, const char *n)
    {
      have_interrupt_names = true;
      if (!strcmp(n, "phys"))
        _irq_phys = Dt::get_arm_gic_irq(timer_node, idx);
      else if (!strcmp(n, "virt"))
        _irq_virt = Dt::get_arm_gic_irq(timer_node, idx);
      else if (!strcmp(n, "hyp-phys"))
        _irq_hyp = Dt::get_arm_gic_irq(timer_node, idx);
      // other names: sec-phys, hyp-virt
    });

  if (!have_interrupt_names)
    {
      _irq_phys = Dt::get_arm_gic_irq(timer_node, static_cast<int>(0)); // Secure EL1
      // 1: Non-Secure EL1
      _irq_virt = Dt::get_arm_gic_irq(timer_node, 2); // Virt
      _irq_hyp = Dt::get_arm_gic_irq(timer_node, 3); // Non-Secure EL2
      if (_irq_hyp == ~0u)
        _irq_hyp = 0;
    }

  if constexpr (TAG_ENABLED(cpu_virt))
    {
      if (_irq_hyp == 0)
        panic("No hypervisor interrupt given in timer DT");
    }
}

// ------------------------------------------------------------------
IMPLEMENTATION [arm_generic_timer && !dt]:

PRIVATE static
void Timer::init_dt()
{}

// ------------------------------------------------------------------
IMPLEMENTATION [arm_generic_timer && arm_acpi]:

#include "acpi.h"

PRIVATE static
void Timer::init_acpi()
{
  // ARM Base System Architecture 3.6 - PPI assignments, used as defaults
  // in case ACPI does not provide a GTDT
  _irq_phys       = 30;
  _irq_virt       = 27;
  _irq_hyp        = 26;
  _irq_secure_hyp = 20;

  auto const *gtdt = Acpi::find<Acpi_gtdt const *>("GTDT");
  if (!gtdt)
    return;

  if (gtdt->non_secure_el1_gsiv)
    _irq_phys = gtdt->non_secure_el1_gsiv;
  if (gtdt->virtual_el1_gsiv)
    _irq_virt = gtdt->virtual_el1_gsiv;
  if (gtdt->non_secure_el2_gsiv)
    _irq_hyp = gtdt->non_secure_el2_gsiv;
}

// ------------------------------------------------------------------
IMPLEMENTATION [arm_generic_timer && !arm_acpi]:

PRIVATE static
void Timer::init_acpi()
{}

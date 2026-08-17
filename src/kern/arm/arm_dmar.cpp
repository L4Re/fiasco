IMPLEMENTATION [iommu]:

#include "kobject_helper.h"
#include "dmar_space.h"
#include "iommu.h"
#include "mem_unit.h"
#include "warn.h"

JDB_DEFINE_TYPENAME(Dmar, "SMMU");

struct Dmar : Kobject_h<Dmar, Kobject>
{
  L4_RPC(0, bind,   (Unsigned64 src_id, Ko::Cap<Dmar_space> dma_space,
                     Unsigned64 *min_addr, Unsigned64 *max_addr));
  L4_RPC(1, unbind, (Unsigned64 src_id, Ko::Cap<Dmar_space> dma_space));

  L4_msg_tag kinvoke(L4_obj_ref, L4_fpage::Rights rights,
                     Syscall_frame *f, Utcb const *in, Utcb *out)
  {
    L4_msg_tag tag = f->tag();

    if (!Ko::check_basics(&tag, rights, L4_msg_tag::Label_iommu,
                          L4_fpage::Rights::CS()))
      return tag;

    switch (access_once(&in->values[0]))
      {
      case Msg_bind::Opcode:
        return Msg_bind::call(this, tag, in, out, rights);

      case Msg_unbind::Opcode:
        return Msg_unbind::call(this, tag, in , out, rights);

      default:
        return commit_result(-L4_err::ENosys);
      }
  }

private:
  struct Src_id
  {
    Unsigned64 raw;
    explicit Src_id(Unsigned64 raw) : raw(raw) {};
    /**
     * Stream ID
     *   - SMMUv1 allows a stream ID of up-to 15 bits.
     *   - SMMUv2 allows a stream ID of up-to 16 bits.
     *   - SMMUv3 allows a stream ID of up-to 32 bits.
     */
    CXX_BITFIELD_MEMBER( 0, 31, stream_id, raw);
    /**
     * Index of the SMMU that is responsible for the stream (device) to which
     * the DMA space shall be (un)bound. Kernel and user space must agree on the
     * numbering of the SMMUs in the system. How this agreement is reached is
     * platform specific. For example, it may correspond to the order of the
     * SMMUs in the ACPI IO Remapping Table (IORT).
     */
    CXX_BITFIELD_MEMBER(32, 47, smmu_idx, raw);
  };
};

/*
 * L4-IFACE: kernel-iommu.iommu-bind
 * PROTOCOL: L4_PROTO_IOMMU
 */
PRIVATE
L4_msg_tag
Dmar::op_bind(Ko::Rights, Unsigned64 src_id, Ko::Cap<Dmar_space> space_cap,
              Unsigned64 *min_addr, Unsigned64 *max_addr)
{
  Src_id s(src_id);
  Iommu *iommu = Iommu::iommu(s.smmu_idx());
  if (!iommu)
    return Kobject_iface::commit_result(-L4_err::EInval);

  // Prevent the Dmar_space from being deleted while using it without holding
  // the CPU lock.
  Ref_ptr<Dmar_space> space(space_cap.obj);

  // Release CPU lock because when binding a Dmar_space, it may be necessary
  // to execute one or more SMMU commands. While waiting for these to execute we
  // must be interruptible.
  auto guard_cpu = lock_guard<Lock_guard_inverse_policy>(cpu_lock);

  *min_addr = 0;
  return Kobject_iface::commit_result(space->bind_mmu(iommu, s.stream_id(),
                                                      max_addr));
}

/*
 * L4-IFACE: kernel-iommu.iommu-unbind
 * PROTOCOL: L4_PROTO_IOMMU
 */
PRIVATE
L4_msg_tag
Dmar::op_unbind(Ko::Rights, Unsigned64 src_id, Ko::Cap<Dmar_space> space_cap)
{
  Src_id s(src_id);
  Iommu *iommu = Iommu::iommu(s.smmu_idx());
  if (!iommu)
    return Kobject_iface::commit_result(-L4_err::EInval);

  // Prevent the Dmar_space from being deleted while using it without holding
  // the CPU lock.
  Ref_ptr<Dmar_space> space(space_cap.obj);

  // Release CPU lock because when unbinding a Dmar_space, it may be necessary
  // to execute one or more SMMU commands. While waiting for these to execute we
  // must be interruptible.
  auto guard_cpu = lock_guard<Lock_guard_inverse_policy>(cpu_lock);

  return Kobject_iface::commit_result(space->unbind_mmu(iommu, s.stream_id()));
}

static Static_object<Dmar> _glbl_iommu;

PUBLIC static
void
Dmar::init()
{
  if (!Iommu::iommus().size())
    return;

  Iommu::Iommu_type type = Iommu::iommus()[0]->type();
  for (auto &iommu : Iommu::iommus())
    if (iommu->type() != type)
      panic("IOMMU: Platform provided IOMMUs of different types.");

  if (type == Iommu::Iommu_type::Smmu_v2)
    register_dmar_space_smmuv2();
  else if(type == Iommu::Iommu_type::Smmu_v3)
    register_dmar_space_smmuv3();

  _glbl_iommu.construct();
  initial_kobjects->register_obj(_glbl_iommu, Initial_kobjects::Iommu);
}

STATIC_INITIALIZE_P(Dmar, DMAR_INIT_PRIO);

//----------------------------------------------------------------------------
IMPLEMENTATION [iommu && iommu_arm_smmu_v2]:

PRIVATE static
void Dmar::register_dmar_space_smmuv2()
{
  Dmar_space_t<Dmar_space_smmu_v2>::init();
  Kobject_iface::set_factory(
    L4_msg_tag::Label_dma_space,
    &Task::generic_factory<Dmar_space_t<Dmar_space_smmu_v2>>);
}

//----------------------------------------------------------------------------
IMPLEMENTATION [iommu && !iommu_arm_smmu_v2]:

PRIVATE static
void Dmar::register_dmar_space_smmuv2()
{}

//----------------------------------------------------------------------------
IMPLEMENTATION [iommu && iommu_arm_smmu_v3]:

PRIVATE static
void Dmar::register_dmar_space_smmuv3()
{
  if (Iommu_smmu_v3::stage2())
    {
      Dmar_space_t<Dmar_space_smmu_v3_stage2>::init();
      Kobject_iface::set_factory(
          L4_msg_tag::Label_dma_space,
          &Task::generic_factory<Dmar_space_t<Dmar_space_smmu_v3_stage2>>);
    }
  else
    {
      Dmar_space_t<Dmar_space_smmu_v3_stage1>::init();
      Kobject_iface::set_factory(
        L4_msg_tag::Label_dma_space,
        &Task::generic_factory<Dmar_space_t<Dmar_space_smmu_v3_stage1>>);
    }
}

//----------------------------------------------------------------------------
IMPLEMENTATION [iommu && !iommu_arm_smmu_v3]:

PRIVATE static
void Dmar::register_dmar_space_smmuv3()
{}

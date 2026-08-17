INTERFACE [iommu_arm_smmu_v2]:

class Dmar_space_smmu_v2
{
public:
  // Va64_support==false: LPAE page table format
  // Va64_support==true:  AArch64 page table format, with a concatenated root
  //                      page table (10-bits), which means we skip level zero.
  typedef Ptab::Tupel< Ptab::Traits< Unsigned64, 30,
                                     Iommu_smmu_v2::Va64_support ? 10 : 2, true>,
                       Ptab::Traits< Unsigned64, 21, 9, true>,
                       Ptab::Traits< Unsigned64, 12, 9, true> >::List Ptab_traits;
  using Dmar_ptab_traits_vpn = Ptab::Shift<Ptab_traits, Virt_addr::Shift>::List;

  class Dmar_pte_ptr :
    public Pte_long_desc<Dmar_pte_ptr>,
    public Dmar_pte_iommu<Dmar_pte_ptr>,
    public Pte_stage2_attribs<Dmar_pte_ptr, Page>,
    public Pte_generic<Dmar_pte_ptr, Unsigned64>
  {
  public:
    static constexpr unsigned super_level() { return 1; }
    static constexpr unsigned max_level()   { return 2; }
    Dmar_pte_ptr() = default;
    Dmar_pte_ptr(void *p, unsigned char level) : Pte_long_desc<Dmar_pte_ptr>(p, level) {}

    unsigned char page_order() const
    {
      return Ptab::Level<Dmar_ptab_traits_vpn>::shift(level)
        + Dmar_ptab_traits_vpn::Head::Base_shift;
    }
  };
  using Dmar_pdir = Pdir_t<Dmar_pte_ptr, Dmar_ptab_traits_vpn, Ptab_va_vpn>;

  Dmar_pdir *pt() { return _dmarpt; }

private:
  Dmar_pdir *_dmarpt;
  Iommu_smmu_v2::Space_id _space_id;
};

// -----------------------------------------------------------
IMPLEMENTATION [iommu_arm_smmu_v2]:

#include "kmem.h"

static Kmem_slab_t<Dmar_space_smmu_v2::Dmar_pdir,
                   sizeof(Dmar_space_smmu_v2::Dmar_pdir)> _dmarpt_alloc;

PUBLIC
bool
Dmar_space_smmu_v2::alloc_pt(Ram_quota *ram_quota)
{
  _dmarpt = _dmarpt_alloc.q_new(ram_quota);
  if (!_dmarpt)
    return false;

  _dmarpt->clear(Dmar_pte_ptr::need_cache_write_back());
  return true;
}

PUBLIC
void
Dmar_space_smmu_v2::free_pt(Ram_quota *ram_quota)
{
  if (!_dmarpt)
    return;

  _dmarpt->destroy(Virt_addr(0UL), Virt_addr(~0UL), 0, Dmar_pdir::Depth,
                   Kmem_alloc::q_allocator(ram_quota));
  _dmarpt_alloc.q_free(ram_quota, _dmarpt);
  _dmarpt = nullptr;
}

PUBLIC static inline
void
Dmar_space_smmu_v2::init_page_sizes(auto add_page_size)
{
  add_page_size(Mem_space::Page_order(12));
  add_page_size(Mem_space::Page_order(21));
  add_page_size(Mem_space::Page_order(30));
}

PRIVATE inline
Address
Dmar_space_smmu_v2::pt_phys_addr() const
{
  return Mem_layout::pmem_to_phys(_dmarpt);
}

PUBLIC
void
Dmar_space_smmu_v2::tlb_flush_current_cpu()
{
  Iommu_smmu_v2::tlb_invalidate_space(_space_id);
}

PUBLIC
int
Dmar_space_smmu_v2::bind_mmu(Iommu *mmu, Unsigned32 stream_id, Unsigned64 *max_addr)
{
  // All registered IOMMUs are SMMUv2 in this configuration.
  auto *smmu = static_cast<Iommu_smmu_v2 *>(mmu);
  *max_addr = (Unsigned64{1} << smmu->ipa_size()) - 1;
  return smmu->bind(stream_id, pt_phys_addr(), &_space_id);
}

PUBLIC
int
Dmar_space_smmu_v2::unbind_mmu(Iommu *mmu, Unsigned32 stream_id)
{
  // All registered IOMMUs are SMMUv2 in this configuration.
  return static_cast<Iommu_smmu_v2 *>(mmu)->unbind(stream_id, pt_phys_addr());
}

PUBLIC
void
Dmar_space_smmu_v2::remove_from_all_iommus()
{
  for (Iommu *iommu : Iommu::iommus())
    // All registered IOMMUs are SMMUv2 in this configuration.
    static_cast<Iommu_smmu_v2 *>(iommu)->remove(pt_phys_addr());
}

static Kmem_slab_t<Dmar_space_t<Dmar_space_smmu_v2>>
  dmar_space_alloc("Dmar_space_smmu_v2");

PUBLIC static
Dmar_space_t<Dmar_space_smmu_v2> *
Dmar_space_smmu_v2::alloc_space(Ram_quota *quota)
{
  return dmar_space_alloc.q_new(quota, quota);
}

PUBLIC static
void
Dmar_space_smmu_v2::free_space(Ram_quota *quota,
                               Dmar_space_t<Dmar_space_smmu_v2> *space)
{
  dmar_space_alloc.q_del(quota, space);
}

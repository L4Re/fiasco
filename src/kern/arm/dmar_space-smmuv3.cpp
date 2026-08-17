// -----------------------------------------------------------
INTERFACE [iommu_arm_smmu_v3 && 64bit]:

template<typename IMPL>
class Dmar_space_smmu_v3_mixin
{
public:
  struct Ptab_cfg
  {
    Address pt_phys_addr;
    unsigned char virt_addr_size;
    unsigned char start_level;
  };

  typedef Ptab::Tupel<Ptab::Traits<Unsigned64, 39, 9, false>,
                      Ptab::Traits<Unsigned64, 30, 9, true>,
                      Ptab::Traits<Unsigned64, 21, 9, true>,
                      Ptab::Traits<Unsigned64, 12, 9, true> >::List Dmar_traits;
  typedef Ptab::Shift<Dmar_traits, 12>::List Dmar_traits_vpn;
  typedef Ptab::Page_addr_wrap<Page_number, 12> Dmar_va_vpn;

protected:
  IMPL *impl() { return nonull_static_cast<IMPL *>(this); }

private:
  Iommu_domain _domain;
};

class Dmar_space_smmu_v3_stage1
: public Dmar_space_smmu_v3_mixin<Dmar_space_smmu_v3_stage1>
{
public:
  struct Stage1_page_attr
  {
    // See Iommu_smmu_v3::Mair0_bits for the definition.
    enum Attribs_enum
    {
      Cache_mask    = 0x01c, ///< MAIR index 0..7
      NONCACHEABLE  = 0x000, ///< MAIR Attr0: Caching is off
      CACHEABLE     = 0x008, ///< MAIR Attr2: Cache is enabled
      BUFFERED      = 0x004, ///< MAIR Attr1: Write buffer enabled -- Normal, non-cached
    };

    enum
    {
      /// The NS-EL1&0 translation regime supports two privilege levels.
      Priv_levels = 2,
      PXN = 1ULL << 53, ///< Privileged Execute Never
      UXN = 1ULL << 54, ///< Unprivileged Execute Never
      XN = 0,           ///< Execute Never feature not available
    };
  };

  class Dmar_pte_ptr :
    public Pte_long_desc<Dmar_pte_ptr>,
    public Dmar_pte_iommu<Dmar_pte_ptr>,
    public Pte_long_attribs<Dmar_pte_ptr, Stage1_page_attr>,
    public Pte_generic<Dmar_pte_ptr, Unsigned64>
  {
  public:
    static constexpr unsigned super_level() { return 2; }
    static constexpr unsigned max_level()   { return 3; }
    using Pte_long_desc<Dmar_pte_ptr>::Pte_long_desc;

    unsigned char page_order() const
    {
      return Ptab::Level<Dmar_traits_vpn>::shift(level)
        + Dmar_traits_vpn::Head::Base_shift;
    }
  };

  using Dmar_pdir = Pdir_t<Dmar_pte_ptr, Dmar_traits_vpn, Dmar_va_vpn>;

  Dmar_pdir *pt() { return _dmarpt; }

private:
  Dmar_pdir *_dmarpt;
};

class Dmar_space_smmu_v3_stage2
: public Dmar_space_smmu_v3_mixin<Dmar_space_smmu_v3_stage2>
{
public:
  struct Stage2_page_attr
    {
      enum Attribs_enum : Mword
      {
        Cache_mask    = 0x03c,
        NONCACHEABLE  = 0x000, ///< Caching is off
        CACHEABLE     = 0x03c, ///< Cache is enabled
        BUFFERED      = 0x014, ///< Write buffer enabled -- Normal, non-cached
      };
    };

  class Dmar_pte_ptr :
    public Pte_long_desc<Dmar_pte_ptr>,
    public Dmar_pte_iommu<Dmar_pte_ptr>,
    public Pte_stage2_attribs<Dmar_pte_ptr, Stage2_page_attr>,
    public Pte_generic<Dmar_pte_ptr, Unsigned64>
  {
  public:
    static constexpr unsigned super_level() { return 2; }
    static constexpr unsigned max_level()   { return 3; }
    using Pte_long_desc<Dmar_pte_ptr>::Pte_long_desc;

    unsigned char page_order() const
    {
      return Ptab::Level<Dmar_traits_vpn>::shift(level)
        + Dmar_traits_vpn::Head::Base_shift;
    }
  };

  using Dmar_pdir = Pdir_t<Dmar_pte_ptr, Dmar_traits_vpn, Dmar_va_vpn>;

  Dmar_pdir *pt() { return _dmarpt; }

private:
  Dmar_pdir *_dmarpt;
};

// -----------------------------------------------------------
IMPLEMENTATION [iommu_arm_smmu_v3]:

#include "kmem.h"

PUBLIC static template<typename IMPL>
void
Dmar_space_smmu_v3_mixin<IMPL>::init_page_sizes(auto add_page_size)
{
  add_page_size(Mem_space::Page_order(12));
  add_page_size(Mem_space::Page_order(21)); // 2 MiB
  add_page_size(Mem_space::Page_order(30)); // 1 GiB
}

PUBLIC template<typename IMPL>
void
Dmar_space_smmu_v3_mixin<IMPL>::tlb_flush_current_cpu()
{
  Iommu_smmu_v3::tlb_invalidate_domain(_domain);
}

PUBLIC template<typename IMPL>
int
Dmar_space_smmu_v3_mixin<IMPL>::bind_mmu(Iommu *mmu, Unsigned32 stream_id,
                                         Unsigned64 *max_addr)
{
  // All registered IOMMUs are SMMUv3 in this configuration.
  auto *smmu = static_cast<Iommu_smmu_v3 *>(mmu);
  auto [pt_phys_addr, virt_addr_size, start_level] = impl()->get_ptab_cfg(smmu);
  *max_addr = (Unsigned64{1} << virt_addr_size) - 1;
  return smmu->bind(stream_id, _domain, pt_phys_addr, virt_addr_size,
                    start_level);
}

PUBLIC template<typename IMPL>
int
Dmar_space_smmu_v3_mixin<IMPL>::unbind_mmu(Iommu *mmu,
                                           Unsigned32 stream_id)
{
  // All registered IOMMUs are SMMUv3 in this configuration.
  auto *smmu = static_cast<Iommu_smmu_v3 *>(mmu);
  auto pt_phys_addr = impl()->get_ptab_cfg(smmu).pt_phys_addr;
  return smmu->unbind(stream_id, _domain, pt_phys_addr);
}

PUBLIC template<typename IMPL>
void
Dmar_space_smmu_v3_mixin<IMPL>::remove_from_all_iommus()
{
  for (Iommu *iommu : Iommu::iommus())
    {
      // All registered IOMMUs are SMMUv3 in this configuration.
      auto *smmu = static_cast<Iommu_smmu_v3 *>(iommu);
      auto pt_phys_addr = impl()->get_ptab_cfg(smmu).pt_phys_addr;
      smmu->remove(_domain, pt_phys_addr);
    }
}

// Stage 1
static Kmem_slab_t<Dmar_space_smmu_v3_stage1::Dmar_pdir,
                   sizeof(Dmar_space_smmu_v3_stage1::Dmar_pdir)> _dmarpt_alloc_stage1;

PUBLIC
bool
Dmar_space_smmu_v3_stage1::alloc_pt(Ram_quota *ram_quota)
{
  _dmarpt = _dmarpt_alloc_stage1.q_new(ram_quota);
  if (!_dmarpt)
    return false;

  _dmarpt->clear(Dmar_pte_ptr::need_cache_write_back());
  return true;
}

PUBLIC
void
Dmar_space_smmu_v3_stage1::free_pt(Ram_quota *ram_quota)
{
  if (!_dmarpt)
    return;

  _dmarpt->destroy(Virt_addr(0UL), Virt_addr(~0UL), 0, Dmar_pdir::Depth,
                   Kmem_alloc::q_allocator(ram_quota));
  _dmarpt_alloc_stage1.q_free(ram_quota, _dmarpt);
  _dmarpt = nullptr;
}

PUBLIC
Dmar_space_smmu_v3_stage1::Ptab_cfg
Dmar_space_smmu_v3_stage1::get_ptab_cfg(Iommu_smmu_v3 *) const
{
  unsigned char virt_addr_size = Dmar_pdir::page_order_for_level(0)
                                 + Dmar_pdir::Levels::size(0);
  // The start level is irrelevant for stage 1 PTs...
  return {Mem_layout::pmem_to_phys(_dmarpt), virt_addr_size, 0};
}

static Kmem_slab_t<Dmar_space_t<Dmar_space_smmu_v3_stage1>>
  dmar_space_alloc_stage1("Dmar_space_smmu_v3_stage1");

PUBLIC static
Dmar_space_t<Dmar_space_smmu_v3_stage1> *
Dmar_space_smmu_v3_stage1::alloc_space(Ram_quota *quota)
{
  return dmar_space_alloc_stage1.q_new(quota, quota);
}

PUBLIC static
void
Dmar_space_smmu_v3_stage1::free_space(Ram_quota *quota,
                                      Dmar_space_t<Dmar_space_smmu_v3_stage1> *space)
{
  dmar_space_alloc_stage1.q_del(quota, space);
}

// Stage2
static Kmem_slab_t<Dmar_space_smmu_v3_stage2::Dmar_pdir,
                   sizeof(Dmar_space_smmu_v3_stage2::Dmar_pdir)> _dmarpt_alloc_stage2;

PUBLIC
bool
Dmar_space_smmu_v3_stage2::alloc_pt(Ram_quota *ram_quota)
{
  _dmarpt = _dmarpt_alloc_stage2.q_new(ram_quota);
  if (!_dmarpt)
    return false;

  _dmarpt->clear(Dmar_pte_ptr::need_cache_write_back());

  // Force allocation of the first level 1 page table entry. Required to
  // support SMMUs that require to start at level 1 instead of level 0. See
  // get_ptab_cfg() below.
  auto i = _dmarpt->walk(Virt_addr(0), 1, Dmar_pdir::Pte_ptr::need_cache_write_back(),
                         Kmem_alloc::q_allocator(ram_quota));
  if (i.level == 1) [[likely]]
    return true;

  // Allocation failed.
  free_pt(ram_quota);
  return false;
}

PUBLIC
void
Dmar_space_smmu_v3_stage2::free_pt(Ram_quota *ram_quota)
{
  if (!_dmarpt)
    return;

  _dmarpt->destroy(Virt_addr(0UL), Virt_addr(~0UL), 0, Dmar_pdir::Depth,
                   Kmem_alloc::q_allocator(ram_quota));
  _dmarpt_alloc_stage2.q_free(ram_quota, _dmarpt);
  _dmarpt = nullptr;
}

PUBLIC
Dmar_space_smmu_v3_stage2::Ptab_cfg
Dmar_space_smmu_v3_stage2::get_ptab_cfg(Iommu_smmu_v3 *smmu) const
{
  constexpr unsigned max_ipa_size = Dmar_pdir::page_order_for_level(0)
                                    + Dmar_pdir::Levels::size(0);
  unsigned char ias = min(smmu->ipa_size(), max_ipa_size);
  if (ias >= 44)
    // From 44 bits and above, use 4 levels and start at level 0
    return {Mem_layout::pmem_to_phys(_dmarpt), ias, 2};

  // There is an unusable range between 40 and 43. Such hardware requires to
  // start at level 1 (see ARM IHI 0070 STE.S2T0SZ description in conjunction
  // with ARM DDI 0487 section D5.2.3, "Controlling Address translation
  // stages"). Because we have no concatenated first level table, constrain the
  // input size. :-(
  if (ias >= 40)
    {
      static bool warned = false;
      if (!warned)
        {
          warned = true;
          WARN("IOMMU: hardware supports more bits (%d) than the kernel can use!"
               " DMA addresses constrained to 39 bits.\n", ias);
        }
      ias = 39;
    }

  // Skip the first level...
  auto pte = _dmarpt->walk(Virt_addr(0), 0);
  assert(pte.is_valid());
  return {pte.next_level(), ias, 1};
}

static Kmem_slab_t<Dmar_space_t<Dmar_space_smmu_v3_stage2>>
  dmar_space_alloc_stage2("Dmar_space_smmu_v3_stage2");

PUBLIC static
Dmar_space_t<Dmar_space_smmu_v3_stage2> *
Dmar_space_smmu_v3_stage2::alloc_space(Ram_quota *quota)
{
  return dmar_space_alloc_stage2.q_new(quota, quota);
}

PUBLIC static
void
Dmar_space_smmu_v3_stage2::free_space(Ram_quota *quota,
                                      Dmar_space_t<Dmar_space_smmu_v3_stage2> *space)
{
  dmar_space_alloc_stage2.q_del(quota, space);
}

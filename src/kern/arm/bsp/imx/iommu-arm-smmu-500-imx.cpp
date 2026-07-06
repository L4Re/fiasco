IMPLEMENTATION [arm && iommu && pf_imx_8xq]:

#include "boot_alloc.h"
#include "kmem_mmio.h"

IMPLEMENT
void
Iommu_smmu_v2::init_platform()
{
  static_assert(Max_iommus >= 1, "Unexpected number of IOMMUs.");
  unsigned const nonsec_irqs[] =
  {
    // Global/Context Irq
    64,
  };

  auto *smmu = new Boot_object<Iommu_smmu_v2>();
  void *v = Kmem_mmio::map(0x51400000, 0x40000);
  smmu->setup(Iommu_smmu_v2::Version::Smmu_v2, v, 0x7f80);
  smmu->setup_irqs(nonsec_irqs, 1, 1);
}

IMPLEMENTATION [arm && iommu && pf_imx_95]:

#include "boot_alloc.h"
#include "kmem_mmio.h"

IMPLEMENT
void
Iommu_smmu_v3::init_platform()
{
  static_assert(Max_iommus >= 1, "Unexpected number of IOMMUs.");

  auto *smmu = new Boot_object<Iommu_smmu_v3>();
  void *v = Kmem_mmio::map(0x490d0000, 0x100000);
  smmu->setup(v, 0x165, 0x168);
}

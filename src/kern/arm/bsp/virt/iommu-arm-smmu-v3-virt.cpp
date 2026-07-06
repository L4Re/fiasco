IMPLEMENTATION [arm && iommu && pf_arm_virt]:

#include "boot_alloc.h"
#include "kmem_mmio.h"

IMPLEMENT
void
Iommu_smmu_v3::init_platform()
{
  static_assert(Max_iommus >= 1, "Unexpected number of IOMMUs.");
  Address base_addr = 0x09050000;

  auto *smmu = new Boot_object<Iommu_smmu_v3>();
  void *v = Kmem_mmio::map(base_addr, 0x20000);
  smmu->setup(v, 106, 109);
}

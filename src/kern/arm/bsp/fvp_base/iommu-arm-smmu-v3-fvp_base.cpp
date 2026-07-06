IMPLEMENTATION [arm && iommu && pf_fvp_base]:

#include "boot_alloc.h"
#include "kmem_mmio.h"

IMPLEMENT
void
Iommu_smmu_v3::init_platform()
{
  static_assert(Max_iommus >= 1, "Unexpected number of IOMMUs.");
  Address base_addr = 0x002b400000;

  // 103  71  SMMUv3 non-secure combined interrupt.
  // 104  72  SMMUv3 secure combined interrupt. Unused because there is no secure side.
  // 105  73  SMMUv3 secure event queue. Unused because there is no secure side.
  // 106  74  SMMUv3 non-secure event queue.
  // 107  75  SMMUv3 PRI queue. Unused because no PCIe device supports PRI.
  // 108  76  SMMUv3 secure command queue sync. Unused because there is no secure side.
  // 109  77  SMMUv3 non-secure command queue sync.
  // 110  78  SMMUv3 secure GERROR. Unused because there is no secure side.
  // 111  79  SMMUv3 non-secure GERROR.

  auto *smmu = new Boot_object<Iommu_smmu_v3>();
  void *v = Kmem_mmio::map(base_addr, 0x100000);
  smmu->setup(v, 106, 111);
}

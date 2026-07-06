IMPLEMENTATION [arm && iommu && pf_lx2160]:

#include "boot_alloc.h"
#include "kmem_mmio.h"

IMPLEMENT
void
Iommu_smmu_v2::init_platform()
{
  static_assert(Max_iommus >= 1, "Unexpected number of IOMMUs.");
  static constinit unsigned const nonsec_irqs[] =
  {
    // Global non-secure fault
    47,
    // Per-context non-secure interrupts (64)
    178, 179, 180, 181, 182, 183, 184, 185,
    186, 187, 188, 189, 190, 191, 192, 193,
    194, 195, 196, 197, 198, 199, 200, 201,
    202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217,
    218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 228, 229, 230, 231, 232, 233,
    234, 235, 236, 237, 238, 239, 240, 241,
  };

  auto *smmu = new Boot_object<Iommu_smmu_v2>();
  void *v = Kmem_mmio::map(0x5000000, 0x800000);
  smmu->setup(Iommu_smmu_v2::Version::Smmu_v2, v);
  smmu->setup_irqs(nonsec_irqs, cxx::size(nonsec_irqs), 1);
}

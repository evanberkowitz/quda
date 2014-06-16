#include <copy_color_spinor.cuh>

namespace quda {
  
  void copyGenericColorSpinorDD(ColorSpinorField &dst, const ColorSpinorField &src, 
				QudaFieldLocation location, void *Dst, void *Src, 
				void *dstNorm, void *srcNorm) {
    CopyGenericColorSpinor(dst, src, location, (double*)Dst, (double*)Src);
  }  

} // namespace quda

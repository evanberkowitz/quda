#ifndef _TEXTURE_H
#define _TEXTURE_H

#include <quda_internal.h>
#include <color_spinor_field.h>
#include <convert.h>

//namespace quda {

#ifdef USE_TEXTURE_OBJECTS // texture objects

template<typename OutputType, typename InputType>
class Texture {

private: 
#ifndef DIRECT_ACCESS_BLAS
  cudaTextureObject_t spinor;
#else
  const InputType *spinor; // used when textures are disabled
#endif
  
public:
  Texture() : spinor(0) { }   
#ifndef DIRECT_ACCESS_BLAS
  Texture(const cudaColorSpinorField *x) : spinor(x->Tex()) { }
#else
  Texture(const cudaColorSpinorField *x) : spinor((InputType*)(x->V())) { }
#endif
  Texture(const Texture &tex) : spinor(tex.spinor) { }
  virtual ~Texture() { }
  
  Texture& operator=(const Texture &tex) {
    if (this != &tex) spinor = tex.spinor;
    return *this;
  }
  
  // these do nothing with Texture objects
  inline void bind(const InputType*, size_t bytes){ }
  inline void unbind() { }
  
#ifndef DIRECT_ACCESS_BLAS
  __device__ inline OutputType fetch(unsigned int idx) 
  { return tex1Dfetch<OutputType>(spinor, idx); }
#else
  __device__ inline OutputType fetch(unsigned int idx) 
  { OutputType out; copyFloatN(out, spinor[idx]); return out; } 
#endif

  __device__ inline OutputType operator[](unsigned int idx) { return fetch(idx); }
};

#ifndef DIRECT_ACCESS_BLAS
__device__ inline double2 fetch_double2(int4 v)
{ return make_double2(__hiloint2double(v.y, v.x), __hiloint2double(v.w, v.z)); }

template<> __device__ inline double2 Texture<double2,double2>::fetch(unsigned int idx) 
{ double2 out; copyFloatN(out, fetch_double2(tex1Dfetch<int4>(spinor, idx))); return out; }

template<> __device__ inline float2 Texture<float2,double2>::fetch(unsigned int idx) 
{ float2 out; copyFloatN(out, fetch_double2(tex1Dfetch<int4>(spinor, idx))); return out; }
#endif

#else 

// legacy Texture references

#if (__COMPUTE_CAPABILITY__ >= 130)
  __inline__ __device__ double2 fetch_double2(texture<int4, 1> t, int i)
  {
    int4 v = tex1Dfetch(t,i);
    return make_double2(__hiloint2double(v.y, v.x), __hiloint2double(v.w, v.z));
  }
#else
  __inline__ __device__ double2 fetch_double2(texture<int4, 1> t, int i)
  {
    // do nothing
    return make_double2(0.0, 0.0);
  }
#endif

#define MAX_TEXELS (1<<27)

  template<typename OutputType, typename InputType, int tex_id=0>
    class Texture {
  private: 
#ifdef DIRECT_ACCESS_BLAS
  const InputType *spinor; // used when textures are disabled
  size_t bytes;
#endif
  static bool bound;
  static int count;

  public:
  Texture()
#ifdef DIRECT_ACCESS_BLAS
  : spinor(0), bytes(0)
#endif
  { count++; } 

  Texture(const cudaColorSpinorField *x)
#ifdef DIRECT_ACCESS_BLAS 
  : spinor((const InputType*)x->V()), bytes(x->Bytes())
#endif
  { 
    // only bind if bytes > 0
    if (x->Bytes()) { bind((const InputType*)x->V(), x->Bytes()); bound = true; } 
    count++;
  }

  Texture(const Texture &tex) 
#ifdef DIRECT_ACCESS_BLAS
  : spinor(tex.spinor), bytes(tex.bytes)
#endif
  { count++; }

  virtual ~Texture() { if (bound && !--count) { unbind(); bound = false;} }

  Texture& operator=(const Texture &tex) {
#ifdef DIRECT_ACCESS_BLAS
    spinor = tex.spinor;
    bytes = tex.bytes;
#endif
    return *this;
  }

  inline void bind(const InputType*, size_t bytes){ errorQuda("Texture id is out of range"); }
  inline void unbind() { errorQuda("Texture id is out of range"); }

  //default should only be called if a tex_id is out of range
  __device__ inline OutputType fetch(unsigned int idx) { return 0; };  
  __device__ inline OutputType operator[](unsigned int idx) { return fetch(idx); }
  };

  template<typename OutputType, typename InputType, int tex_id>  
    bool Texture<OutputType, InputType, tex_id>::bound = false;

  template<typename OutputType, typename InputType, int tex_id>  
    int Texture<OutputType, InputType, tex_id>::count = 0;

#define DECL_TEX(id)							\
  texture<short2,1,cudaReadModeNormalizedFloat> tex_short2_##id;	\
  texture<short4,1,cudaReadModeNormalizedFloat> tex_short4_##id;	\
  texture<float,1> tex_float_##id;					\
  texture<float2,1> tex_float2_##id;					\
  texture<float4,1> tex_float4_##id;					\
  texture<int4,1> tex_double2_##id;


#define DEF_BIND_UNBIND(outtype, intype, id)				\
  template<> inline void Texture<outtype,intype,id>::bind(const intype *ptr, size_t bytes) \
    { cudaBindTexture(0,tex_##intype##_##id, ptr, bytes); }		\
  template<> inline void Texture<outtype,intype,id>::unbind() { cudaUnbindTexture(tex_##intype##_##id); }


#define DEF_FETCH_TEX(outtype, intype, id)				\
  template<> __device__ inline outtype Texture<outtype,intype,id>::fetch(unsigned int idx) \
    { return tex1Dfetch(tex_##intype##_##id,idx); }


#define DEF_FETCH_DIRECT(outtype, intype, id)				\
  template<> __device__ inline outtype Texture<outtype,intype,id>::fetch(unsigned int idx) \
    { outtype out; copyFloatN(out, spinor[idx]); return out; }


#if defined(DIRECT_ACCESS_BLAS)
#define DEF_FETCH DEF_FETCH_DIRECT
#else
#define DEF_FETCH DEF_FETCH_TEX
#endif


#if defined(DIRECT_ACCESS_BLAS) || defined(FERMI_NO_DBLE_TEX)
#define DEF_FETCH_DBLE DEF_FETCH_DIRECT
#else
#define DEF_FETCH_DBLE(outtype, intype, id)				\
  template<> __device__ inline outtype Texture<outtype,double2,id>::fetch(unsigned int idx) \
    { outtype out; copyFloatN(out, fetch_double2(tex_double2_##id,idx)); return out; }
#endif


#define DEF_BIND_UNBIND_FETCH(outtype, intype, id)	\
  DEF_BIND_UNBIND(outtype, intype, id)			\
    DEF_FETCH(outtype, intype, id)


#define DEF_ALL(id)				\
  DECL_TEX(id)					\
    DEF_BIND_UNBIND_FETCH(float2, short2, id)	\
    DEF_BIND_UNBIND_FETCH(float4, short4, id)	\
    DEF_BIND_UNBIND_FETCH(float, float, id)	\
    DEF_BIND_UNBIND_FETCH(float2, float2, id)	\
    DEF_BIND_UNBIND_FETCH(float4, float4, id)	\
    DEF_BIND_UNBIND(double2, double2, id)	\
    DEF_BIND_UNBIND(float2, double2, id)	\
    DEF_FETCH_DBLE(double2, double2, id)	\
    DEF_FETCH_DBLE(float2, double2, id)


  // Declare the textures and define the member functions of the corresponding templated classes.
  DEF_ALL(0)
    DEF_ALL(1)
    DEF_ALL(2)
    DEF_ALL(3)
    DEF_ALL(4)


#undef DECL_TEX
#undef DEF_BIND_UNBIND
#undef DEF_FETCH_DIRECT
#undef DEF_FETCH_TEX
#undef DEF_FETCH
#undef DEF_FETCH_DBLE
#undef DEF_BIND_UNBIND_FETCH
#undef DEF_ALL

#endif  // USE_TEXTURE_OBJECTS


    /**
       Checks that the types are set correctly.  The precision used in the
       RegType must match that of the InterType, and the ordering of the
       InterType must match that of the StoreType.  The only exception is
       when half precision is used, in which case, RegType can be a double
       and InterType can be single (with StoreType short).

       @param RegType Register type used in kernel
       @param InterType Intermediate format - RegType precision with StoreType ordering
       @param StoreType Type used to store field in memory
    */
    template <typename RegType, typename InterType, typename StoreType>
    void checkTypes() {

    const size_t reg_size = sizeof(((RegType*)0)->x);
    const size_t inter_size = sizeof(((InterType*)0)->x);
    const size_t store_size = sizeof(((StoreType*)0)->x);

    if (reg_size != inter_size  && store_size != 2 && inter_size != 4)
      errorQuda("Precision of register (%lu) and intermediate (%lu) types must match\n",
		reg_size, inter_size);
  
    if (vecLength<InterType>() != vecLength<StoreType>()) {
      errorQuda("Vector lengths intermediate and register types must match\n");
    }

    if (vecLength<RegType>() == 0) errorQuda("Vector type not supported\n");
    if (vecLength<InterType>() == 0) errorQuda("Vector type not supported\n");
    if (vecLength<StoreType>() == 0) errorQuda("Vector type not supported\n");

  }

  // FIXME: Can we merge the Spinor and SpinorTexture objects so that
  // reading from texture is simply a constructor option?

  // the number of elements per virtual register
#define REG_LENGTH (sizeof(RegType) / sizeof(((RegType*)0)->x))

  /**
     @param RegType Register type used in kernel
     @param InterType Intermediate format - RegType precision with StoreType ordering
     @param StoreType Type used to store field in memory
     @param N Length of vector of RegType elements that this Spinor represents
     @param tex_id Which texture reference are we using
  */
  template <typename RegType, typename InterType, typename StoreType, int N, int tex_id>
    class SpinorTexture {

  private:
#if USE_TEXTURE_OBJECTS // texture objects
    Texture<InterType, StoreType> spinor;
#else
    Texture<InterType, StoreType, tex_id> spinor;
#endif
    float *norm; // direct reads for norm
    int stride;

  public:
    SpinorTexture() 
      : spinor(), norm(0), stride(0) { } // default constructor

    SpinorTexture(const cudaColorSpinorField &x) 
      : spinor(&x), norm((float*)x.Norm()),
      stride(x.Length()/(N*REG_LENGTH)) { checkTypes<RegType,InterType,StoreType>(); }

    SpinorTexture(const SpinorTexture &st) 
      : spinor(st.spinor), norm(st.norm), stride(st.stride) { }

    SpinorTexture& operator=(const SpinorTexture &src) {
      if (&src != this) {
	spinor = src.spinor;
	norm = src.norm;
	stride = src.stride;
      }
      return *this;
    }

    ~SpinorTexture() { } /* on g80 / gt200 this must not be virtual */

    __device__ inline void load(RegType x[], const int i) {
      // load data into registers first using the storage order
      const int M = (N * sizeof(RegType)) / sizeof(InterType);
      InterType y[M];

      // half precision types
      if (sizeof(InterType) == 2*sizeof(StoreType)) { 
	float xN = norm[i];
#pragma unroll
	for (int j=0; j<M; j++) {
	  y[j] = spinor[i + j*stride];
	  y[j] *= xN;
	}
      } else { // other types
#pragma unroll 
	for (int j=0; j<M; j++) copyFloatN(y[j], spinor[i + j*stride]);
      }

      // now convert into desired register order
      convert<RegType, InterType>(x, y, N);
    }

    // no save method for Textures

    QudaPrecision Precision() { 
      QudaPrecision precision = QUDA_INVALID_PRECISION;
      if (sizeof(((StoreType*)0)->x) == sizeof(double)) precision = QUDA_DOUBLE_PRECISION;
      else if (sizeof(((StoreType*)0)->x) == sizeof(float)) precision = QUDA_SINGLE_PRECISION;
      else if (sizeof(((StoreType*)0)->x) == sizeof(short)) precision = QUDA_HALF_PRECISION;
      else errorQuda("Unknown precision type\n");
      return precision;
    }

    int Stride() { return stride; }
  };

  /**
     @param RegType Register type used in kernel
     @param InterType Intermediate format - RegType precision with StoreType ordering
     @param StoreType Type used to store field in memory
     @param N Length of vector of RegType elements that this Spinor represents
  */
  template <typename RegType, typename InterType, typename StoreType, int N>
    class Spinor {

  private:
    StoreType *spinor;
    float *norm;
    const int stride;

  public:
  Spinor(cudaColorSpinorField &x) : 
    spinor((StoreType*)x.V()), norm((float*)x.Norm()),  stride(x.Length()/(N*REG_LENGTH))
      { checkTypes<RegType,InterType,StoreType>(); } 

  Spinor(const cudaColorSpinorField &x) :
    spinor((StoreType*)x.V()), norm((float*)x.Norm()), stride(x.Length()/(N*REG_LENGTH))
      { checkTypes<RegType,InterType,StoreType>(); } 
    ~Spinor() {;} /* on g80 / gt200 this must not be virtual */

    // default load used for simple fields
    __device__ inline void load(RegType x[], const int i) {
      // load data into registers first
      const int M = (N * sizeof(RegType)) / sizeof(InterType);
      InterType y[M];
#pragma unroll
      for (int j=0; j<M; j++) copyFloatN(y[j],spinor[i + j*stride]);

      convert<RegType, InterType>(x, y, N);
    }

    // default store used for simple fields
    __device__ inline void save(RegType x[], int i) {
      const int M = (N * sizeof(RegType)) / sizeof(InterType);
      InterType y[M];
      convert<InterType, RegType>(y, x, M);
#pragma unroll
      for (int j=0; j<M; j++) copyFloatN(spinor[i+j*stride], y[j]);
    }

    // used to backup the field to the host
    void save(char **spinor_h, char **norm_h, size_t bytes, size_t norm_bytes) {
      *spinor_h = new char[bytes];
      cudaMemcpy(*spinor_h, spinor, bytes, cudaMemcpyDeviceToHost);
      if (norm_bytes > 0) {
	*norm_h = new char[norm_bytes];
	cudaMemcpy(*norm_h, norm, norm_bytes, cudaMemcpyDeviceToHost);
      }
      checkCudaError();
    }

    // restore the field from the host
    void load(char **spinor_h, char **norm_h, size_t bytes, size_t norm_bytes) {
      cudaMemcpy(spinor, *spinor_h, bytes, cudaMemcpyHostToDevice);
      if (norm_bytes > 0) {
	cudaMemcpy(norm, *norm_h, norm_bytes, cudaMemcpyHostToDevice);
	delete []*norm_h;
	*norm_h = 0;
      }
      delete []*spinor_h;
      *spinor_h = 0;
      checkCudaError();
    }

    void* V() { return (void*)spinor; }
    float* Norm() { return norm; }
    QudaPrecision Precision() { 
      QudaPrecision precision = QUDA_INVALID_PRECISION;
      if (sizeof(((StoreType*)0)->x) == sizeof(double)) precision = QUDA_DOUBLE_PRECISION;
      else if (sizeof(((StoreType*)0)->x) == sizeof(float)) precision = QUDA_SINGLE_PRECISION;
      else if (sizeof(((StoreType*)0)->x) == sizeof(short)) precision = QUDA_HALF_PRECISION;
      else errorQuda("Unknown precision type\n");
      return precision;
    }

    int Stride() { return stride; }
  };

  template <typename OutputType, typename InputType, int M>
    __device__ inline void saveHalf(OutputType *x_o, float *norm, InputType x_i[M], int i, int stride) {
    float c[M];
#pragma unroll
    for (int j=0; j<M; j++) c[j] = max_fabs(x_i[j]);
#pragma unroll
    for (int j=1; j<M; j++) c[0] = fmaxf(c[j],c[0]);
  
    norm[i] = c[0]; // store norm value

    // store spinor values
    float C = __fdividef(MAX_SHORT, c[0]);  
#pragma unroll
    for (int j=0; j<M; j++) {    
      x_o[i+j*stride] = make_shortN(C*x_i[j]); 
    }
  }

  template <>
    __device__ inline void Spinor<float2, float2, short2, 3>::save(float2 x[3], int i) {
    saveHalf<short2, float2, 3>(spinor, norm, x, i, stride);
  }

  template <>
    __device__ inline void Spinor<float4, float4, short4, 6>::save(float4 x[6], int i) {
    saveHalf<short4, float4, 6>(spinor, norm, x, i, stride);
  }

  template <>
    __device__ inline void Spinor<double2, double2, short2, 3>::save(double2 x[3], int i) {
    saveHalf<short2, double2, 3>(spinor, norm, x, i, stride);
  }

  template <>
    __device__ inline void Spinor<double2, double4, short4, 12>::save(double2 x[12], int i) {
    double4 y[6];
    convert<double4, double2>(y, x, 6);
    saveHalf<short4, double4, 6>(spinor, norm, y, i, stride);
  }

//} // namespace quda

#endif // _TEXTURE_H
/// @file simd_alu.cpp
/// @brief Platform-specific SIMD ALU implementations
///
/// This file contains the actual intrinsic implementations for AVX2, AVX-512,
/// and ARM NEON. Each platform has its own section with guarded compilation.

#include "dotvm/core/simd/simd_alu.hpp"

namespace dotvm::core::simd::detail {

// ============================================================================
// x86-64 AVX2 Implementations (256-bit)
// ============================================================================

#if defined(DOTVM_HAS_AVX2_COMPILE)

// --- VADD AVX2 ---

Vector256i8 vadd_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept {
    Vector256i8 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_add_epi8(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i16 vadd_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept {
    Vector256i16 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_add_epi16(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i32 vadd_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_add_epi32(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i64 vadd_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept {
    Vector256i64 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_add_epi64(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vadd_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_add_ps(va, vb);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vadd_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_add_pd(va, vb);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VSUB AVX2 ---

Vector256i8 vsub_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept {
    Vector256i8 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_sub_epi8(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i16 vsub_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept {
    Vector256i16 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_sub_epi16(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i32 vsub_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_sub_epi32(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i64 vsub_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept {
    Vector256i64 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_sub_epi64(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vsub_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_sub_ps(va, vb);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vsub_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_sub_pd(va, vb);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VMUL AVX2 ---

Vector256i16 vmul_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept {
    Vector256i16 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_mullo_epi16(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i32 vmul_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_mullo_epi32(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vmul_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_mul_ps(va, vb);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vmul_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_mul_pd(va, vb);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VDIV AVX2 ---

Vector256f32 vdiv_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_div_ps(va, vb);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vdiv_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_div_pd(va, vb);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VDOT AVX2 ---

float vdot_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 prod = _mm256_mul_ps(va, vb);

    // Horizontal sum: reduce 8 floats to 1
    __m128 lo = _mm256_castps256_ps128(prod);
    __m128 hi = _mm256_extractf128_ps(prod, 1);
    __m128 sum = _mm_add_ps(lo, hi);  // 4 floats
    sum = _mm_hadd_ps(sum, sum);      // 2 floats
    sum = _mm_hadd_ps(sum, sum);      // 1 float

    return _mm_cvtss_f32(sum);
}

double vdot_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d prod = _mm256_mul_pd(va, vb);

    // Horizontal sum: reduce 4 doubles to 1
    __m128d lo = _mm256_castpd256_pd128(prod);
    __m128d hi = _mm256_extractf128_pd(prod, 1);
    __m128d sum = _mm_add_pd(lo, hi);  // 2 doubles
    sum = _mm_hadd_pd(sum, sum);       // 1 double

    return _mm_cvtsd_f64(sum);
}

// --- VFMA AVX2 ---

Vector256f32 vfma_avx2_f32(const Vector256f32& a, const Vector256f32& b,
                           const Vector256f32& c) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vc = _mm256_loadu_ps(c.data());
    __m256 vr = _mm256_fmadd_ps(va, vb, vc);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vfma_avx2_f64(const Vector256f64& a, const Vector256f64& b,
                           const Vector256f64& c) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vc = _mm256_loadu_pd(c.data());
    __m256d vr = _mm256_fmadd_pd(va, vb, vc);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VMIN AVX2 ---

Vector256i8 vmin_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept {
    Vector256i8 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_min_epi8(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i16 vmin_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept {
    Vector256i16 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_min_epi16(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i32 vmin_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_min_epi32(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vmin_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_min_ps(va, vb);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vmin_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_min_pd(va, vb);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VMAX AVX2 ---

Vector256i8 vmax_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept {
    Vector256i8 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_max_epi8(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i16 vmax_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept {
    Vector256i16 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_max_epi16(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i32 vmax_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_max_epi32(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vmax_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_max_ps(va, vb);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vmax_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_max_pd(va, vb);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VCMPEQ AVX2 ---

Vector256i8 vcmpeq_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept {
    Vector256i8 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpeq_epi8(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i16 vcmpeq_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept {
    Vector256i16 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpeq_epi16(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i32 vcmpeq_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpeq_epi32(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i64 vcmpeq_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept {
    Vector256i64 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpeq_epi64(va, vb);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vcmpeq_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_cmp_ps(va, vb, _CMP_EQ_OQ);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vcmpeq_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_cmp_pd(va, vb, _CMP_EQ_OQ);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VCMPLT AVX2 ---

Vector256i8 vcmplt_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept {
    Vector256i8 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpgt_epi8(vb, va);  // a < b == b > a
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i16 vcmplt_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept {
    Vector256i16 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpgt_epi16(vb, va);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i32 vcmplt_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpgt_epi32(vb, va);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256i64 vcmplt_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept {
    Vector256i64 result;
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_cmpgt_epi64(vb, va);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vcmplt_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_cmp_ps(va, vb, _CMP_LT_OQ);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vcmplt_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_cmp_pd(va, vb, _CMP_LT_OQ);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

// --- VBLEND AVX2 ---

Vector256i32 vblend_avx2_i32(const Vector256i32& mask, const Vector256i32& a,
                             const Vector256i32& b) noexcept {
    Vector256i32 result;
    __m256i vm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mask.data()));
    __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a.data()));
    __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.data()));
    __m256i vr = _mm256_blendv_epi8(vb, va, vm);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data()), vr);
    return result;
}

Vector256f32 vblend_avx2_f32(const Vector256f32& mask, const Vector256f32& a,
                             const Vector256f32& b) noexcept {
    Vector256f32 result;
    __m256 vm = _mm256_loadu_ps(mask.data());
    __m256 va = _mm256_loadu_ps(a.data());
    __m256 vb = _mm256_loadu_ps(b.data());
    __m256 vr = _mm256_blendv_ps(vb, va, vm);
    _mm256_storeu_ps(result.data(), vr);
    return result;
}

Vector256f64 vblend_avx2_f64(const Vector256f64& mask, const Vector256f64& a,
                             const Vector256f64& b) noexcept {
    Vector256f64 result;
    __m256d vm = _mm256_loadu_pd(mask.data());
    __m256d va = _mm256_loadu_pd(a.data());
    __m256d vb = _mm256_loadu_pd(b.data());
    __m256d vr = _mm256_blendv_pd(vb, va, vm);
    _mm256_storeu_pd(result.data(), vr);
    return result;
}

#endif  // DOTVM_HAS_AVX2_COMPILE

// ============================================================================
// x86-64 AVX-512 Implementations (512-bit)
// ============================================================================

#if defined(DOTVM_HAS_AVX512_COMPILE)

// --- VADD AVX-512 ---

Vector512i8 vadd_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept {
    Vector512i8 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_add_epi8(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i16 vadd_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept {
    Vector512i16 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_add_epi16(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i32 vadd_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept {
    Vector512i32 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_add_epi32(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i64 vadd_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept {
    Vector512i64 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_add_epi64(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512f32 vadd_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept {
    Vector512f32 result;
    __m512 va = _mm512_loadu_ps(a.data());
    __m512 vb = _mm512_loadu_ps(b.data());
    __m512 vr = _mm512_add_ps(va, vb);
    _mm512_storeu_ps(result.data(), vr);
    return result;
}

Vector512f64 vadd_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept {
    Vector512f64 result;
    __m512d va = _mm512_loadu_pd(a.data());
    __m512d vb = _mm512_loadu_pd(b.data());
    __m512d vr = _mm512_add_pd(va, vb);
    _mm512_storeu_pd(result.data(), vr);
    return result;
}

// --- VSUB AVX-512 ---

Vector512i8 vsub_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept {
    Vector512i8 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_sub_epi8(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i16 vsub_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept {
    Vector512i16 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_sub_epi16(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i32 vsub_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept {
    Vector512i32 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_sub_epi32(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i64 vsub_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept {
    Vector512i64 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_sub_epi64(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512f32 vsub_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept {
    Vector512f32 result;
    __m512 va = _mm512_loadu_ps(a.data());
    __m512 vb = _mm512_loadu_ps(b.data());
    __m512 vr = _mm512_sub_ps(va, vb);
    _mm512_storeu_ps(result.data(), vr);
    return result;
}

Vector512f64 vsub_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept {
    Vector512f64 result;
    __m512d va = _mm512_loadu_pd(a.data());
    __m512d vb = _mm512_loadu_pd(b.data());
    __m512d vr = _mm512_sub_pd(va, vb);
    _mm512_storeu_pd(result.data(), vr);
    return result;
}

// --- VMUL AVX-512 ---

Vector512i16 vmul_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept {
    Vector512i16 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_mullo_epi16(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i32 vmul_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept {
    Vector512i32 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_mullo_epi32(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i64 vmul_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept {
    Vector512i64 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_mullo_epi64(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512f32 vmul_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept {
    Vector512f32 result;
    __m512 va = _mm512_loadu_ps(a.data());
    __m512 vb = _mm512_loadu_ps(b.data());
    __m512 vr = _mm512_mul_ps(va, vb);
    _mm512_storeu_ps(result.data(), vr);
    return result;
}

Vector512f64 vmul_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept {
    Vector512f64 result;
    __m512d va = _mm512_loadu_pd(a.data());
    __m512d vb = _mm512_loadu_pd(b.data());
    __m512d vr = _mm512_mul_pd(va, vb);
    _mm512_storeu_pd(result.data(), vr);
    return result;
}

// --- VDIV AVX-512 ---

Vector512f32 vdiv_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept {
    Vector512f32 result;
    __m512 va = _mm512_loadu_ps(a.data());
    __m512 vb = _mm512_loadu_ps(b.data());
    __m512 vr = _mm512_div_ps(va, vb);
    _mm512_storeu_ps(result.data(), vr);
    return result;
}

Vector512f64 vdiv_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept {
    Vector512f64 result;
    __m512d va = _mm512_loadu_pd(a.data());
    __m512d vb = _mm512_loadu_pd(b.data());
    __m512d vr = _mm512_div_pd(va, vb);
    _mm512_storeu_pd(result.data(), vr);
    return result;
}

// --- VFMA AVX-512 ---

Vector512f32 vfma_avx512_f32(const Vector512f32& a, const Vector512f32& b,
                             const Vector512f32& c) noexcept {
    Vector512f32 result;
    __m512 va = _mm512_loadu_ps(a.data());
    __m512 vb = _mm512_loadu_ps(b.data());
    __m512 vc = _mm512_loadu_ps(c.data());
    __m512 vr = _mm512_fmadd_ps(va, vb, vc);
    _mm512_storeu_ps(result.data(), vr);
    return result;
}

Vector512f64 vfma_avx512_f64(const Vector512f64& a, const Vector512f64& b,
                             const Vector512f64& c) noexcept {
    Vector512f64 result;
    __m512d va = _mm512_loadu_pd(a.data());
    __m512d vb = _mm512_loadu_pd(b.data());
    __m512d vc = _mm512_loadu_pd(c.data());
    __m512d vr = _mm512_fmadd_pd(va, vb, vc);
    _mm512_storeu_pd(result.data(), vr);
    return result;
}

// --- VMIN AVX-512 ---

Vector512i8 vmin_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept {
    Vector512i8 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_min_epi8(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i16 vmin_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept {
    Vector512i16 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_min_epi16(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i32 vmin_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept {
    Vector512i32 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_min_epi32(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i64 vmin_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept {
    Vector512i64 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_min_epi64(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512f32 vmin_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept {
    Vector512f32 result;
    __m512 va = _mm512_loadu_ps(a.data());
    __m512 vb = _mm512_loadu_ps(b.data());
    __m512 vr = _mm512_min_ps(va, vb);
    _mm512_storeu_ps(result.data(), vr);
    return result;
}

Vector512f64 vmin_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept {
    Vector512f64 result;
    __m512d va = _mm512_loadu_pd(a.data());
    __m512d vb = _mm512_loadu_pd(b.data());
    __m512d vr = _mm512_min_pd(va, vb);
    _mm512_storeu_pd(result.data(), vr);
    return result;
}

// --- VMAX AVX-512 ---

Vector512i8 vmax_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept {
    Vector512i8 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_max_epi8(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i16 vmax_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept {
    Vector512i16 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_max_epi16(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i32 vmax_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept {
    Vector512i32 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_max_epi32(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512i64 vmax_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept {
    Vector512i64 result;
    __m512i va = _mm512_loadu_si512(a.data());
    __m512i vb = _mm512_loadu_si512(b.data());
    __m512i vr = _mm512_max_epi64(va, vb);
    _mm512_storeu_si512(result.data(), vr);
    return result;
}

Vector512f32 vmax_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept {
    Vector512f32 result;
    __m512 va = _mm512_loadu_ps(a.data());
    __m512 vb = _mm512_loadu_ps(b.data());
    __m512 vr = _mm512_max_ps(va, vb);
    _mm512_storeu_ps(result.data(), vr);
    return result;
}

Vector512f64 vmax_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept {
    Vector512f64 result;
    __m512d va = _mm512_loadu_pd(a.data());
    __m512d vb = _mm512_loadu_pd(b.data());
    __m512d vr = _mm512_max_pd(va, vb);
    _mm512_storeu_pd(result.data(), vr);
    return result;
}

#endif  // DOTVM_HAS_AVX512_COMPILE

// ============================================================================
// ARM NEON Implementations (128-bit)
// ============================================================================

#if defined(DOTVM_HAS_NEON_COMPILE)

// --- VADD NEON ---

Vector128i8 vadd_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept {
    Vector128i8 result;
    int8x16_t va = vld1q_s8(a.data());
    int8x16_t vb = vld1q_s8(b.data());
    int8x16_t vr = vaddq_s8(va, vb);
    vst1q_s8(result.data(), vr);
    return result;
}

Vector128i16 vadd_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept {
    Vector128i16 result;
    int16x8_t va = vld1q_s16(a.data());
    int16x8_t vb = vld1q_s16(b.data());
    int16x8_t vr = vaddq_s16(va, vb);
    vst1q_s16(result.data(), vr);
    return result;
}

Vector128i32 vadd_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    int32x4_t vr = vaddq_s32(va, vb);
    vst1q_s32(result.data(), vr);
    return result;
}

Vector128i64 vadd_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept {
    Vector128i64 result;
    int64x2_t va = vld1q_s64(a.data());
    int64x2_t vb = vld1q_s64(b.data());
    int64x2_t vr = vaddq_s64(va, vb);
    vst1q_s64(result.data(), vr);
    return result;
}

Vector128f32 vadd_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vr = vaddq_f32(va, vb);
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vadd_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vr = vaddq_f64(va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

// --- VSUB NEON ---

Vector128i8 vsub_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept {
    Vector128i8 result;
    int8x16_t va = vld1q_s8(a.data());
    int8x16_t vb = vld1q_s8(b.data());
    int8x16_t vr = vsubq_s8(va, vb);
    vst1q_s8(result.data(), vr);
    return result;
}

Vector128i16 vsub_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept {
    Vector128i16 result;
    int16x8_t va = vld1q_s16(a.data());
    int16x8_t vb = vld1q_s16(b.data());
    int16x8_t vr = vsubq_s16(va, vb);
    vst1q_s16(result.data(), vr);
    return result;
}

Vector128i32 vsub_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    int32x4_t vr = vsubq_s32(va, vb);
    vst1q_s32(result.data(), vr);
    return result;
}

Vector128i64 vsub_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept {
    Vector128i64 result;
    int64x2_t va = vld1q_s64(a.data());
    int64x2_t vb = vld1q_s64(b.data());
    int64x2_t vr = vsubq_s64(va, vb);
    vst1q_s64(result.data(), vr);
    return result;
}

Vector128f32 vsub_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vr = vsubq_f32(va, vb);
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vsub_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vr = vsubq_f64(va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

// --- VMUL NEON ---

Vector128i8 vmul_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept {
    Vector128i8 result;
    int8x16_t va = vld1q_s8(a.data());
    int8x16_t vb = vld1q_s8(b.data());
    int8x16_t vr = vmulq_s8(va, vb);
    vst1q_s8(result.data(), vr);
    return result;
}

Vector128i16 vmul_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept {
    Vector128i16 result;
    int16x8_t va = vld1q_s16(a.data());
    int16x8_t vb = vld1q_s16(b.data());
    int16x8_t vr = vmulq_s16(va, vb);
    vst1q_s16(result.data(), vr);
    return result;
}

Vector128i32 vmul_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    int32x4_t vr = vmulq_s32(va, vb);
    vst1q_s32(result.data(), vr);
    return result;
}

Vector128f32 vmul_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vr = vmulq_f32(va, vb);
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vmul_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vr = vmulq_f64(va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

// --- VDIV NEON ---

Vector128f32 vdiv_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vr = vdivq_f32(va, vb);
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vdiv_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vr = vdivq_f64(va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

// --- VDOT NEON ---

float vdot_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t prod = vmulq_f32(va, vb);

    // Horizontal sum
    float32x2_t sum_pairs = vadd_f32(vget_low_f32(prod), vget_high_f32(prod));
    float32x2_t sum_final = vpadd_f32(sum_pairs, sum_pairs);

    return vget_lane_f32(sum_final, 0);
}

double vdot_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t prod = vmulq_f64(va, vb);

    return vgetq_lane_f64(prod, 0) + vgetq_lane_f64(prod, 1);
}

// --- VFMA NEON ---

Vector128f32 vfma_neon_f32(const Vector128f32& a, const Vector128f32& b,
                           const Vector128f32& c) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vc = vld1q_f32(c.data());
    float32x4_t vr = vfmaq_f32(vc, va, vb);  // c + a*b
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vfma_neon_f64(const Vector128f64& a, const Vector128f64& b,
                           const Vector128f64& c) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vc = vld1q_f64(c.data());
    float64x2_t vr = vfmaq_f64(vc, va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

// --- VMIN NEON ---

Vector128i8 vmin_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept {
    Vector128i8 result;
    int8x16_t va = vld1q_s8(a.data());
    int8x16_t vb = vld1q_s8(b.data());
    int8x16_t vr = vminq_s8(va, vb);
    vst1q_s8(result.data(), vr);
    return result;
}

Vector128i16 vmin_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept {
    Vector128i16 result;
    int16x8_t va = vld1q_s16(a.data());
    int16x8_t vb = vld1q_s16(b.data());
    int16x8_t vr = vminq_s16(va, vb);
    vst1q_s16(result.data(), vr);
    return result;
}

Vector128i32 vmin_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    int32x4_t vr = vminq_s32(va, vb);
    vst1q_s32(result.data(), vr);
    return result;
}

Vector128f32 vmin_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vr = vminq_f32(va, vb);
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vmin_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vr = vminq_f64(va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

// --- VMAX NEON ---

Vector128i8 vmax_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept {
    Vector128i8 result;
    int8x16_t va = vld1q_s8(a.data());
    int8x16_t vb = vld1q_s8(b.data());
    int8x16_t vr = vmaxq_s8(va, vb);
    vst1q_s8(result.data(), vr);
    return result;
}

Vector128i16 vmax_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept {
    Vector128i16 result;
    int16x8_t va = vld1q_s16(a.data());
    int16x8_t vb = vld1q_s16(b.data());
    int16x8_t vr = vmaxq_s16(va, vb);
    vst1q_s16(result.data(), vr);
    return result;
}

Vector128i32 vmax_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    int32x4_t vr = vmaxq_s32(va, vb);
    vst1q_s32(result.data(), vr);
    return result;
}

Vector128f32 vmax_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vr = vmaxq_f32(va, vb);
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vmax_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vr = vmaxq_f64(va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

// --- VCMPEQ NEON ---

Vector128i8 vcmpeq_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept {
    Vector128i8 result;
    int8x16_t va = vld1q_s8(a.data());
    int8x16_t vb = vld1q_s8(b.data());
    uint8x16_t vr = vceqq_s8(va, vb);
    vst1q_s8(result.data(), vreinterpretq_s8_u8(vr));
    return result;
}

Vector128i16 vcmpeq_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept {
    Vector128i16 result;
    int16x8_t va = vld1q_s16(a.data());
    int16x8_t vb = vld1q_s16(b.data());
    uint16x8_t vr = vceqq_s16(va, vb);
    vst1q_s16(result.data(), vreinterpretq_s16_u16(vr));
    return result;
}

Vector128i32 vcmpeq_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    uint32x4_t vr = vceqq_s32(va, vb);
    vst1q_s32(result.data(), vreinterpretq_s32_u32(vr));
    return result;
}

Vector128i64 vcmpeq_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept {
    Vector128i64 result;
    int64x2_t va = vld1q_s64(a.data());
    int64x2_t vb = vld1q_s64(b.data());
    uint64x2_t vr = vceqq_s64(va, vb);
    vst1q_s64(result.data(), vreinterpretq_s64_u64(vr));
    return result;
}

Vector128f32 vcmpeq_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    uint32x4_t vr = vceqq_f32(va, vb);
    vst1q_f32(result.data(), vreinterpretq_f32_u32(vr));
    return result;
}

Vector128f64 vcmpeq_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    uint64x2_t vr = vceqq_f64(va, vb);
    vst1q_f64(result.data(), vreinterpretq_f64_u64(vr));
    return result;
}

// --- VCMPLT NEON ---

Vector128i8 vcmplt_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept {
    Vector128i8 result;
    int8x16_t va = vld1q_s8(a.data());
    int8x16_t vb = vld1q_s8(b.data());
    uint8x16_t vr = vcltq_s8(va, vb);
    vst1q_s8(result.data(), vreinterpretq_s8_u8(vr));
    return result;
}

Vector128i16 vcmplt_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept {
    Vector128i16 result;
    int16x8_t va = vld1q_s16(a.data());
    int16x8_t vb = vld1q_s16(b.data());
    uint16x8_t vr = vcltq_s16(va, vb);
    vst1q_s16(result.data(), vreinterpretq_s16_u16(vr));
    return result;
}

Vector128i32 vcmplt_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    uint32x4_t vr = vcltq_s32(va, vb);
    vst1q_s32(result.data(), vreinterpretq_s32_u32(vr));
    return result;
}

Vector128i64 vcmplt_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept {
    Vector128i64 result;
    int64x2_t va = vld1q_s64(a.data());
    int64x2_t vb = vld1q_s64(b.data());
    uint64x2_t vr = vcltq_s64(va, vb);
    vst1q_s64(result.data(), vreinterpretq_s64_u64(vr));
    return result;
}

Vector128f32 vcmplt_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    uint32x4_t vr = vcltq_f32(va, vb);
    vst1q_f32(result.data(), vreinterpretq_f32_u32(vr));
    return result;
}

Vector128f64 vcmplt_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    uint64x2_t vr = vcltq_f64(va, vb);
    vst1q_f64(result.data(), vreinterpretq_f64_u64(vr));
    return result;
}

// --- VBLEND NEON ---

Vector128i32 vblend_neon_i32(const Vector128i32& mask, const Vector128i32& a,
                             const Vector128i32& b) noexcept {
    Vector128i32 result;
    int32x4_t vm = vld1q_s32(mask.data());
    int32x4_t va = vld1q_s32(a.data());
    int32x4_t vb = vld1q_s32(b.data());
    int32x4_t vr = vbslq_s32(vreinterpretq_u32_s32(vm), va, vb);
    vst1q_s32(result.data(), vr);
    return result;
}

Vector128f32 vblend_neon_f32(const Vector128f32& mask, const Vector128f32& a,
                             const Vector128f32& b) noexcept {
    Vector128f32 result;
    float32x4_t vm = vld1q_f32(mask.data());
    float32x4_t va = vld1q_f32(a.data());
    float32x4_t vb = vld1q_f32(b.data());
    float32x4_t vr = vbslq_f32(vreinterpretq_u32_f32(vm), va, vb);
    vst1q_f32(result.data(), vr);
    return result;
}

Vector128f64 vblend_neon_f64(const Vector128f64& mask, const Vector128f64& a,
                             const Vector128f64& b) noexcept {
    Vector128f64 result;
    float64x2_t vm = vld1q_f64(mask.data());
    float64x2_t va = vld1q_f64(a.data());
    float64x2_t vb = vld1q_f64(b.data());
    float64x2_t vr = vbslq_f64(vreinterpretq_u64_f64(vm), va, vb);
    vst1q_f64(result.data(), vr);
    return result;
}

#endif  // DOTVM_HAS_NEON_COMPILE

}  // namespace dotvm::core::simd::detail

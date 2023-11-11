// Copyright (c) 2023 Dan Fu, Hermann Kumbong

#include <torch/extension.h>

#include <vector>
#include <stdio.h>
#include <mma.h>
#include <cuda_bf16.h>
#include <cub/block/block_load.cuh>
#include <cub/block/block_store.cuh>
#include "shared/monarch_cuda_shared_bf16_complex_mul.h"
using namespace nvcuda;

using complex_bfloat16_t = typename c10::complex<at::BFloat16>;

#ifndef MONARCH_CUDA_SHARED_R2R_BF16_
#define MONARCH_CUDA_SHARED_R2R_BF16_

__device__ __forceinline__ void negate_twid(
  complex_bfloat16_t *twid_input_data,
  complex_bfloat16_t *twid_output_data,
  int items_per_thread
) {
  for (int i = 0; i < items_per_thread; i++) {
    twid_output_data[i] = conj(twid_input_data[i]);
  }
}

__device__ __forceinline__ void load_input(
  at::BFloat16 *a_real,
  at::BFloat16 *a_imag,
  at::BFloat16 *x_input_data,
  int items_per_thread_input,
  int num_threads,
  int thread_id
) {
  int a_idx;
  for (int i = 0; i < items_per_thread_input / 4; i++)
  {
    a_idx = i * num_threads + thread_id;

    reinterpret_cast<__nv_bfloat162 *>(a_real)[a_idx] = __nv_bfloat162(
      __nv_bfloat16(x_input_data[4 * i]),
      __nv_bfloat16(x_input_data[4 * i + 2])
    );
    reinterpret_cast<__nv_bfloat162 *>(a_imag)[a_idx] = __nv_bfloat162(
      __nv_bfloat16(x_input_data[4 * i + 1]),
      __nv_bfloat16(x_input_data[4 * i + 3])
    );
    // a_imag[a_idx] = x_input_data[2 * i + 1];
  }
}

__device__ __forceinline__ void load_output(
  at::BFloat16 *a_real,
  at::BFloat16 *a_imag,
  at::BFloat16 *x_input_data,
  int items_per_thread_input,
  int num_threads,
  int thread_id
) {
  int a_idx;
  for (int i = 0; i < items_per_thread_input / 4; i++)
  {
    a_idx = i * num_threads + thread_id;

    x_input_data[4 * i] = reinterpret_cast<__nv_bfloat162 *>(a_real)[a_idx].x;
    x_input_data[4 * i + 2] = reinterpret_cast<__nv_bfloat162 *>(a_real)[a_idx].y;
    x_input_data[4 * i + 1] = reinterpret_cast<__nv_bfloat162 *>(a_imag)[a_idx].x;
    x_input_data[4 * i + 3] = reinterpret_cast<__nv_bfloat162 *>(a_imag)[a_idx].y;
  }
}

__device__ __forceinline__ void store_z_data(
  at::BFloat16 *a_real,
  at::BFloat16 *a_imag,
  complex_bfloat16_t *z_data,
  int items_per_thread_input,
  int num_threads,
  int thread_id
) {
  int a_idx;
  for (int i = 0; i < items_per_thread_input; i++)
  {
    a_idx = i * num_threads + thread_id;

    a_real[a_idx] = z_data[i].real();
    a_imag[a_idx] = z_data[i].imag();
  }
}

__device__ __forceinline__ void multiply_kf(
  complex_bfloat16_t *z_data,
  complex_bfloat16_t *kf_data,
  complex_bfloat16_t *out_data,
  int items_per_thread,
  int num_threads,
  int thread_id
) {
  __nv_bfloat162 scratch;
  for (int i = 0; i < items_per_thread / 2; i++) {
    // z_data[2*i] corresponds to a_real[a_idx], a_imag[a_idx]
    // z_data[2*i + 1] corresponds to a_real[a_idx + 1], a_imag[a_idx + 1]

    if (thread_id == 0 && i == 0) {
      // special case
      // do pointwise
      scratch = __hmul2(
        __nv_bfloat162(__nv_bfloat16(z_data[0].real()), __nv_bfloat16(z_data[0].imag())),
        __nv_bfloat162(__nv_bfloat16(kf_data[0].real()), __nv_bfloat16(kf_data[0].imag()))
      );
      out_data[0] = complex_bfloat16_t(scratch.x, scratch.y);
      complex_mul(
        z_data[1], kf_data[1],
        &out_data[1]
      );
    } else {
      complex_mul_bfloat162(
        z_data[2*i], z_data[2*i+1],
        kf_data[2*i], kf_data[2*i+1],
        &out_data[2*i], &out_data[2*i+1]
      );
    }
  }
}

__device__ __forceinline__ void multiply_kf_conj(
  complex_bfloat16_t *z_data,
  complex_bfloat16_t *kf_data,
  complex_bfloat16_t *out_data,
  int items_per_thread,
  int num_threads,
  int thread_id
) {
  __nv_bfloat162 scratch;
  for (int i = 0; i < items_per_thread / 2; i++) {
    // z_data[2*i] corresponds to a_real[a_idx], a_imag[a_idx]
    // z_data[2*i + 1] corresponds to a_real[a_idx + 1], a_imag[a_idx + 1]

    if (thread_id == 0 && i == 0) {
      // special case
      // do pointwise
      scratch = __hmul2(
        __nv_bfloat162(__nv_bfloat16(z_data[0].real()), __nv_bfloat16(z_data[0].imag())),
        __nv_bfloat162(__nv_bfloat16(kf_data[0].real()), __nv_bfloat16(kf_data[0].imag()))
      );
      out_data[0] = complex_bfloat16_t(scratch.x, scratch.y);
      complex_mul_conj(
        z_data[1], kf_data[1],
        &out_data[1]
      );
    } else {
      complex_mul_conj_bfloat162(
        z_data[2*i], z_data[2*i+1],
        kf_data[2*i], kf_data[2*i+1],
        &out_data[2*i], &out_data[2*i+1]
      );
    }
  }
}

__device__ __forceinline__ void process_zf(
  at::BFloat16 *a_real,
  at::BFloat16 *a_imag,
  complex_bfloat16_t *z_data,
  complex_bfloat16_t *twid_input_data,
  int items_per_thread,
  int num_threads,
  int thread_id,
  int N
) {
  int a_idx1, a_idx2;
  complex_bfloat16_t scratch_complex1, scratch_complex2, xe, xo;
  __nv_bfloat162 xe_real2, xe_imag2, xo_real2, xo_imag2, a1_real2, a1_imag2, a2_real2, a2_imag2, z_real2, z_imag2;
  for (int i = 0; i < items_per_thread / 2; i++) {
    a_idx1 = (2 * i * num_threads + thread_id);
    a_idx2 = ((2 * i + 1) * num_threads + thread_id);

    // z_data[2*i] corresponds to a_real[a_idx], a_imag[a_idx]
    // z_data[2*i + 1] corresponds to a_real[a_idx + 1], a_imag[a_idx + 1]

    if (thread_id == 0 && i == 0) {
      // special case
      // xe = a_real[0]
      // xo = a_imag[0]
      // z.real = xe + xo * twid_real[0] = xe + xo
      // z.imag = xe - xo
      z_data[0] = complex_bfloat16_t(
        a_real[0] + a_imag[0],
        a_real[0] - a_imag[0]
      );
      scratch_complex1 = complex_bfloat16_t(a_real[a_idx2], a_imag[a_idx2]);
      scratch_complex2 = complex_bfloat16_t(a_real[N-a_idx2], -a_imag[N-a_idx2]);

      xe = (scratch_complex1 + scratch_complex2) * complex_bfloat16_t(__float2bfloat16(0.5), __float2bfloat16(0.0));
      xo = (scratch_complex1 - scratch_complex2) * complex_bfloat16_t(__float2bfloat16(0.0), __float2bfloat16(-0.5));
      z_data[1] = xe + xo * twid_input_data[1];
    } else {
      // to compute z[i], we need a[a_idx], a[N - a_idx], and twid[a_idx]
      // xe = (a[a_idx] + a[N - a_idx]) / 2
      // xo = (a[a_idx] - a[N - a_idx]) / 2j
      // z[i] = xe + xo * twid[a_idx]
      a1_real2 = __nv_bfloat162(__nv_bfloat16(a_real[a_idx1]), __nv_bfloat16(a_real[a_idx2]));
      a1_imag2 = __nv_bfloat162(__nv_bfloat16(a_imag[a_idx1]), __nv_bfloat16(a_imag[a_idx2]));
      a2_real2 = __nv_bfloat162(__nv_bfloat16(a_real[N-a_idx1]), __nv_bfloat16(a_real[N-a_idx2]));
      a2_imag2 = __nv_bfloat162(__nv_bfloat16(-a_imag[N-a_idx1]), __nv_bfloat16(-a_imag[N-a_idx2]));

      complex_mul_bfloat162(
        __hadd2(a1_real2, a2_real2),
        __hadd2(a1_imag2, a2_imag2),
        __nv_bfloat162(__float2bfloat16(0.5), __float2bfloat16(0.5)),
        __nv_bfloat162(__float2bfloat16(0.0), __float2bfloat16(0.0)),
        &xe_real2, &xe_imag2
      );
      complex_mul_bfloat162(
        __hsub2(a1_real2, a2_real2),
        __hsub2(a1_imag2, a2_imag2),
        __nv_bfloat162(__float2bfloat16(0.0), __float2bfloat16(0.0)),
        __nv_bfloat162(__float2bfloat16(-0.5), __float2bfloat16(-0.5)),
        &xo_real2, &xo_imag2
      );

      complex_mul_bfloat162(
        xo_real2, xo_imag2,
        __nv_bfloat162(__nv_bfloat16(twid_input_data[2*i].real()), __nv_bfloat16(twid_input_data[2*i + 1].real())),
        __nv_bfloat162(__nv_bfloat16(twid_input_data[2*i].imag()), __nv_bfloat16(twid_input_data[2*i + 1].imag())),
        &z_real2, &z_imag2
      );

      z_real2 = __hadd2(xe_real2, z_real2);
      z_imag2 = __hadd2(xe_imag2, z_imag2);
      
      z_data[2*i] = complex_bfloat16_t(z_real2.x, z_imag2.x);
      z_data[2*i + 1] = complex_bfloat16_t(z_real2.y, z_imag2.y);
    }
  }
}

__device__ __forceinline__ void process_yf(
  at::BFloat16 *a_real,
  at::BFloat16 *a_imag,
  complex_bfloat16_t *z_data,
  complex_bfloat16_t *twid_input_data_conj,
  int items_per_thread,
  int num_threads,
  int thread_id,
  int N
) {
  int a_idx1, a_idx2;
  complex_bfloat16_t scratch_complex1, scratch_complex2, xe, xo;

  __nv_bfloat162 xe_real2, xe_imag2, xo_real2, xo_imag2, a1_real2, a1_imag2, a2_real2, a2_imag2, z_real2, z_imag2;
  for (int i = 0; i < items_per_thread / 2; i++) {
    a_idx1 = (2 * i * num_threads + thread_id);
    a_idx2 = ((2 * i + 1) * num_threads + thread_id);
    // to compute z[i], we need a[a_idx], a[N - a_idx], and twid[a_idx]
    // xe = (a[a_idx] + a[N - a_idx]) / 2
    // xo = (a[a_idx] - a[N - a_idx]) / 2 * twid[i].conj()
    // z[i] = xe + xo * 1j
    if (thread_id == 0 && i == 0) {
      // special case
      xe = complex_bfloat16_t(
        (a_real[0] + a_imag[0]) / 2,
        0.
      );
      xo = complex_bfloat16_t(
        (a_real[0] - a_imag[0]) / 2,
        0.
      );
      z_data[0] = xe + xo * complex_bfloat16_t(0., 1.);

      scratch_complex1 = complex_bfloat16_t(a_real[a_idx2], a_imag[a_idx2]);
      scratch_complex2 = complex_bfloat16_t(a_real[N-a_idx2], -a_imag[N-a_idx2]);
      xe = (scratch_complex1 + scratch_complex2) * complex_bfloat16_t(__float2bfloat16(0.5), __float2bfloat16(0.0));
      xo = ((scratch_complex1 - scratch_complex2) * complex_bfloat16_t(__float2bfloat16(0.0), __float2bfloat16(0.5))) * twid_input_data_conj[1];

      // z_data[1] = xe + xo * complex_bfloat16_t(0., 1.);
      z_data[1] = xe + xo;
    } else {
      a1_real2 = __nv_bfloat162(__nv_bfloat16(a_real[a_idx1]), __nv_bfloat16(a_real[a_idx2]));
      a1_imag2 = __nv_bfloat162(__nv_bfloat16(a_imag[a_idx1]), __nv_bfloat16(a_imag[a_idx2]));
      a2_real2 = __nv_bfloat162(__nv_bfloat16(a_real[N-a_idx1]), __nv_bfloat16(a_real[N-a_idx2]));
      a2_imag2 = __nv_bfloat162(__nv_bfloat16(-a_imag[N-a_idx1]), __nv_bfloat16(-a_imag[N-a_idx2]));

      complex_mul_bfloat162(
        __hadd2(a1_real2, a2_real2),
        __hadd2(a1_imag2, a2_imag2),
        __nv_bfloat162(__float2bfloat16(0.5), __float2bfloat16(0.5)),
        __nv_bfloat162(__float2bfloat16(0.0), __float2bfloat16(0.0)),
        &xe_real2, &xe_imag2
      );
      complex_mul_bfloat162(
        __hsub2(a1_real2, a2_real2),
        __hsub2(a1_imag2, a2_imag2),
        __nv_bfloat162(__float2bfloat16(0.0), __float2bfloat16(0.0)),
        __nv_bfloat162(__float2bfloat16(0.5), __float2bfloat16(0.5)),
        &xo_real2, &xo_imag2
      );

      complex_mul_bfloat162(
        xo_real2, xo_imag2,
        __nv_bfloat162(__nv_bfloat16(twid_input_data_conj[2*i].real()), __nv_bfloat16(twid_input_data_conj[2*i + 1].real())),
        __nv_bfloat162(__nv_bfloat16(twid_input_data_conj[2*i].imag()), __nv_bfloat16(twid_input_data_conj[2*i + 1].imag())),
        &z_real2, &z_imag2
      );
      
      z_real2 = __hadd2(xe_real2, z_real2);
      z_imag2 = __hadd2(xe_imag2, z_imag2);

      z_data[2*i] = complex_bfloat16_t(z_real2.x, z_imag2.x);
      z_data[2*i + 1] = complex_bfloat16_t(z_real2.y, z_imag2.y);
    }
  }
}

#endif
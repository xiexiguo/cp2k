/*****************************************************************************
 *  CP2K: A general program to perform molehiplar dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

/******************************************************************************
 *  Authors: Benjamin G Levine, Andreas Gloess
 *
 *  2012/05/18                 Refacturing - original files:
 *                              - hip_tools/hipfft.h
 *                              - hip_tools/hipfft.hip
 *
 *****************************************************************************/
#if defined ( __PW_HIP )

// global dependencies
#include <hip/hip_runtime.h>
#include <hipfft.h>
#include <hipblas.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

// local dependencies
#include "pw_hip_utils.h"
#include "fft_hip_utils.h"
#include "fft_hip_internal.h"

// debug flag
#define CHECK 1
#define VERBOSE 0

// dimensions
static int         n_plans = 0;
static hipfftHandle saved_plans[max_plans];
static int         iplandims[max_plans][5];

// --- CODE -------------------------------------------------------------------


/******************************************************************************
 * \brief   Sets up and save a double precision complex 3D-FFT plan on the GPU.
 *          Saved plans are reused if they fit the requirements.
 * \author  Andreas Gloess
 * \date    2012-05-18
 * \version 0.01
 *****************************************************************************/
void ffthip_plan3d_z(      hipfftHandle  &plan,
                          int          &ioverflow,
                    const int          *n,
                    const hipStream_t  hip_stream) {

  int i;
  hipfftResult_t cErr;

  ioverflow = 0;
  for (i = 0; i < n_plans; i++) {
    if ( iplandims[i][0] == 3 && 
         iplandims[i][1] == n[0] && 
         iplandims[i][2] == n[1] && 
         iplandims[i][3] == n[2] &&
         iplandims[i][4] == 0 ) {
      plan = saved_plans[i];
      return;
    }
  }

  if (VERBOSE) printf("FFT 3D (%d-%d-%d)\n", n[0], n[1], n[2]);
  cErr = hipfftPlan3d(&plan, n[2], n[1], n[0], HIPFFT_Z2Z);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
  cErr = hipfftSetStream(plan, hip_stream);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
#if (__CUDACC_VER_MAJOR__<8)
  cErr = hipfftSetCompatibilityMode(plan, FFT_ALIGNMENT);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
#endif

  if ( n_plans < max_3d_plans ) {
    saved_plans[n_plans] = plan;
    iplandims[n_plans][0] = 3;
    iplandims[n_plans][1] = n[0];
    iplandims[n_plans][2] = n[1];
    iplandims[n_plans][3] = n[2];
    iplandims[n_plans][4] = 0;
    n_plans++;
    return;
  }
  ioverflow=1;
}

/******************************************************************************
 * \brief   Sets up and save a double precision complex 2D-FFT plan on the GPU.
 *          Saved plans are reused if they fit the requirements.
 * \author  Andreas Gloess
 * \date    2012-07-16
 * \version 0.01
 *****************************************************************************/
void ffthip_plan2dm_z(      hipfftHandle  &plan,
                           int          &ioverflow,
                     const int          *n,
                     const int           fsign,
                     const hipStream_t  hip_stream) {

  int i, istride, idist, ostride, odist, batch;
  int nsize[2], inembed[2], onembed[2];
  hipfftResult_t cErr;

  ioverflow = 0;
  for (i = 0; i < n_plans; i++) {
    if ( iplandims[i][0] == 2 &&
         iplandims[i][1] == n[0] &&
         iplandims[i][2] == n[1] &&
         iplandims[i][3] == n[2] &&
         iplandims[i][4] == fsign ) {
      plan = saved_plans[i];
      return;
    }
  }

  nsize[0] = n[2];
  nsize[1] = n[1];
  inembed[0] = n[2];
  inembed[1] = n[1];
  onembed[0] = n[2];
  onembed[1] = n[1];
  batch = n[0];
  if ( fsign == +1 ) {
    istride = n[0];
    idist = 1;
    ostride = 1;
    odist = n[1]*n[2];
  } else {
    istride = 1;
    idist = n[1]*n[2];
    ostride = n[0];
    odist = 1;
  }

  if (VERBOSE) printf("FFT 2D (%d) (%d-%d-%d) %d %d %d %d\n", fsign, n[0], n[1], n[2], istride, idist, ostride, odist);
  cErr = hipfftPlanMany(&plan, 2, nsize, inembed, istride, idist, onembed, ostride, odist, HIPFFT_Z2Z, batch);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
  cErr = hipfftSetStream(plan, hip_stream);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
#if (__CUDACC_VER_MAJOR__<8)
  cErr = hipfftSetCompatibilityMode(plan, FFT_ALIGNMENT);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
#endif

  if ( n_plans < max_2d_plans ) {
    saved_plans[n_plans] = plan;
    iplandims[n_plans][0] = 2;
    iplandims[n_plans][1] = n[0];
    iplandims[n_plans][2] = n[1];
    iplandims[n_plans][3] = n[2];
    iplandims[n_plans][4] = fsign;
    n_plans++;
    return;
  }

  ioverflow=1;
}

/******************************************************************************
 * \brief   Sets up and save a double precision complex 1D-FFT plan on the GPU.
 *          Saved plans are reused if they fit the requirements.
 * \author  Andreas Gloess
 * \date    2012-07-04
 * \version 0.01
 *****************************************************************************/
void ffthip_plan1dm_z(      hipfftHandle  &plan,
                           int          &ioverflow,
                     const int           n,
                     const int           m,
                     const int           fsign,
                     const hipStream_t  hip_stream) {

  int i, istride, idist, ostride, odist, batch;
  int nsize[1], inembed[1], onembed[1];
  hipfftResult_t cErr;

  ioverflow = 0;
  for (i = 0; i<n_plans; i++) {
    if ( iplandims[i][0] == 1 &&
         iplandims[i][1] == n &&
         iplandims[i][2] == m &&
         iplandims[i][3] == 0 &&
         iplandims[i][4] == fsign ) {
      plan = saved_plans[i];
      return;
    }
  }

  nsize[0] = n;
  inembed[0] = 0; // is ignored, but is not allowed to be NULL pointer (for adv. strided I/O)
  onembed[0] = 0; // is ignored, but is not allowed to be NULL pointer (for adv. strided I/O)
  batch = m;
  if ( fsign == +1 ) {
    istride = m;
    idist = 1;
    ostride = 1;
    odist = n;
  } else {
    istride = 1;
    idist = n;
    ostride = m;
    odist = 1;
  }

  if (VERBOSE) printf("FFT 1D (%d) (%d-%d) %d %d %d %d\n", fsign, n, m, istride, idist, ostride, odist);
  cErr = hipfftPlanMany(&plan, 1, nsize, inembed, istride, idist, onembed, ostride, odist, HIPFFT_Z2Z, batch);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
  cErr = hipfftSetStream(plan, hip_stream);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
#if (__CUDACC_VER_MAJOR__<8)
  cErr = hipfftSetCompatibilityMode(plan, FFT_ALIGNMENT);
  if (CHECK) hipfft_error_check(cErr, __LINE__);
#endif

  if ( n_plans < max_1d_plans ) {
    saved_plans[n_plans] = plan;
    iplandims[n_plans][0] = 1;
    iplandims[n_plans][1] = n;
    iplandims[n_plans][2] = m;
    iplandims[n_plans][3] = 0;
    iplandims[n_plans][4] = fsign;
    n_plans++;
    return;
  }

  ioverflow=1;
}

/******************************************************************************
 * \brief   Performs a scaled double precision complex 3D-FFT on the GPU.
 *          Input/output is a DEVICE pointer (data).
 * \author  Andreas Gloess
 * \date    2012-05-18
 * \version 0.01
 *****************************************************************************/
extern "C" void ffthip_run_3d_z_(const int                 fsign,
                                const int                *n,
                                const double              scale,
                                      hipfftDoubleComplex *data,
                                const hipStream_t        hip_stream) {

  int ioverflow, lmem;
  hipfftHandle   plan;
  hipfftResult_t cErr;
  hipError_t  hipErr;

  lmem = n[0] * n[1] * n[2];

  ffthip_plan3d_z(plan, ioverflow, n, hip_stream);
  if ( fsign < 0  ) {
    cErr = hipfftExecZ2Z(plan, data, data, HIPFFT_INVERSE);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }
  else {
    cErr = hipfftExecZ2Z(plan, data, data, HIPFFT_FORWARD);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }

  if (scale != 1.0e0) {
    hipErr = hipStreamSynchronize(hip_stream);
    if (CHECK) pw_hip_error_check(hipErr, __LINE__);
    hipblasDscal(2*lmem, scale, (double *) data, 1);
  }

  if (ioverflow) {
    hipErr = hipStreamSynchronize(hip_stream);
    if (CHECK) pw_hip_error_check(hipErr, __LINE__);
    cErr = hipfftDestroy(plan);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }
}

/******************************************************************************
 * \brief   Performs a scaled double precision complex 2D-FFT many times on
 *          the GPU.
 *          Input/output are DEVICE pointers (data_in, date_out).
 * \author  Andreas Gloess
 * \date    2012-07-16
 * \version 0.01
 *****************************************************************************/
extern "C" void ffthip_run_2dm_z_(const int                 fsign,
                                 const int                *n,
                                 const double              scale,
                                       hipfftDoubleComplex *data_in,
                                       hipfftDoubleComplex *data_out,
                                 const hipStream_t        hip_stream) {

  int ioverflow, lmem;
  hipfftHandle   plan;
  hipfftResult_t cErr;
  hipError_t  hipErr;
  
  lmem = n[0] * n[1] * n[2];

  ffthip_plan2dm_z(plan, ioverflow, n, fsign, hip_stream);
  if ( fsign < 0 ) {
    cErr = hipfftExecZ2Z(plan, data_in, data_out, HIPFFT_INVERSE);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }
  else {
    cErr = hipfftExecZ2Z(plan, data_in, data_out, HIPFFT_FORWARD);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }

  if (scale != 1.0e0) {
    hipErr = hipStreamSynchronize(hip_stream);
    if (CHECK) pw_hip_error_check(hipErr, __LINE__);
    hipblasDscal(2 * lmem, scale, (double *) data_out, 1);
  }

  if (ioverflow) {
    hipErr = hipStreamSynchronize(hip_stream);
    if (CHECK) pw_hip_error_check(hipErr, __LINE__);
    cErr = hipfftDestroy(plan);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }
}

/******************************************************************************
 * \brief   Performs a scaled double precision complex 1D-FFT many times on
 *          the GPU.
 *          Input/output are DEVICE pointers (data_in, date_out).
 * \author  Andreas Gloess
 * \date    2012-05-18
 * \version 0.01
 *****************************************************************************/
extern "C" void ffthip_run_1dm_z_(const int                 fsign,
                                 const int                 n,
                                 const int                 m,
                                 const double              scale,
                                       hipfftDoubleComplex *data_in,
                                       hipfftDoubleComplex *data_out,
                                 const hipStream_t        hip_stream) {

  int ioverflow, lmem;
  hipfftHandle   plan;
  hipfftResult_t cErr;
  hipError_t  hipErr;
  
  lmem = n * m;

  ffthip_plan1dm_z(plan, ioverflow, n, m, fsign, hip_stream);
  if ( fsign < 0 ) {
    cErr = hipfftExecZ2Z(plan, data_in, data_out, HIPFFT_INVERSE);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }
  else {
    cErr = hipfftExecZ2Z(plan, data_in, data_out, HIPFFT_FORWARD);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }

  if (scale != 1.0e0) {
    hipErr = hipStreamSynchronize(hip_stream);
    if (CHECK) pw_hip_error_check(hipErr, __LINE__);
    hipblasDscal(2 * lmem, scale, (double *) data_out, 1);
  }

  if (ioverflow) {
    hipErr = hipStreamSynchronize(hip_stream);
    if (CHECK) pw_hip_error_check(hipErr, __LINE__);
    cErr = hipfftDestroy(plan);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }
}

/******************************************************************************
 * \brief   Release all stored plans.
 *
 * \author  Andreas Gloess
 * \date    2013-04-23
 * \version 0.01
 *****************************************************************************/
extern "C" void ffthip_release_() {
  int           i;
  hipfftHandle   plan;
  hipfftResult_t cErr;
  hipError_t   hipErr;

  for (i = 0; i < n_plans; i++) {
    plan = saved_plans[i];
    hipErr = hipDeviceSynchronize();
    if (CHECK) pw_hip_error_check(hipErr, __LINE__);
    cErr = hipfftDestroy(plan);
    if (CHECK) hipfft_error_check(cErr, __LINE__);
  }
  n_plans = 0;
}

#endif

/*****************************************************************************
 *  CP2K: A general program to perform molehiplar dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

/******************************************************************************
 *  Authors: Benjamin G Levine, Andreas Gloess
 *
 *  2012/05/18                 Refacturing - original files:
 *                              - hip_tools/hip_pw_hip.hip
 *
 *****************************************************************************/
#if defined ( __PW_HIP )

// global dependencies
#include <hip/hip_runtime.h>
#include <hipfft.h>
#include <hipblas.h>
#include <stdio.h>

// local dependencies
#include "pw_hip_utils.h"
#include "fft_hip.h"

// debug flag
#define CHECK 1

// configuration(s)
#define NTHREADS 32
#define MAXTHREADS 1024
#define MAXGRIDX 65535

// helper routine(s)
void get_grid_params(const int   ngpts,
                     const int   blocksize,
                           dim3 &threadsPerBlock,
                           dim3 &blocksPerGrid) {
  int blocks;
  if (blocksize <= MAXTHREADS) {
    threadsPerBlock.x = blocksize;
  } else {
    threadsPerBlock.x = MAXTHREADS;
    printf("WARNING: Number of threads per block (x) is too large!\n");
    printf("WARNING: Number of threads per block (x) is set to: %d.\n", MAXTHREADS);
  }
  threadsPerBlock.y = 1;
  threadsPerBlock.z = 1;
  blocks = (ngpts + threadsPerBlock.x - 1) / threadsPerBlock.x;
  blocksPerGrid.x = (int) ceil(sqrt((double) blocks));
  if (blocksPerGrid.x > MAXGRIDX) {
    printf("CUDA: Not allowed grid dimensions!\n");
    exit(1);
  }
  blocksPerGrid.y = (int) rint(sqrt((double) blocks));
  blocksPerGrid.z = 1;
}

void hipStreamBarrier(hipStream_t hip_stream) {
  hipError_t cErr;

  // stop on previous errors
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  // might result in endless loop
  do {
    cErr = hipStreamQuery(hip_stream); 
  } while (cErr != hipSuccess);
}

// --- CODE -------------------------------------------------------------------

/******************************************************************************
 * \brief   Performs a out-of-place copy of a double precision vector (first
 *          half filled) into a double precision complex vector on the GPU.
 *          It requires a global double precision vector 'zout' of lenght '2n'.
 *          [memory (shared):  none Byte
 *           memory (private): 4 Byte
 *           memory (global):  16*n Byte]
 *          n - size of double precision input vector
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
__global__ void pw_copy_rc_hip_z(const double *din,
                                      double *zout,
                                const int     n) {
  const int igpt = (gridDim.x * blockIdx.y + blockIdx.x) * blockDim.x + threadIdx.x;

  if (igpt < n) {
     zout[2 * igpt    ] = din[igpt];
     zout[2 * igpt + 1] = 0.0e0;
  }
}


/******************************************************************************
 * \brief   Performs a out-of-place copy of a double precision complex vector
 *          (real part) into a double precision vector on the GPU.
 *          It requires a global double precision vector 'dout' of lenght 'n'.
 *          [memory (shared):  none Byte
 *           memory (private): 4 Byte
 *           memory (global):  16*n Byte]
 *          n - size of double precision output vector
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
__global__ void pw_copy_cr_hip_z(const double *zin,
                                      double *dout,
                                const int     n) {
  const int igpt = (gridDim.x * blockIdx.y + blockIdx.x) * blockDim.x + threadIdx.x;

  if (igpt < n) {
     dout[igpt] = zin[2 * igpt];
  }
}


/******************************************************************************
 * \brief   Performs a (double precision complex) gather and scale on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
__global__ void pw_gather_hip_z(      double *pwcc,
                               const double *c,
                               const double  scale,
                               const int     ngpts,
                               const int    *ghatmap) {

  const int igpt = (gridDim.x * blockIdx.y + blockIdx.x) * blockDim.x + threadIdx.x;

  if (igpt < ngpts) {
    pwcc[2 * igpt    ] = scale * c[2 * ghatmap[igpt]    ];
    pwcc[2 * igpt + 1] = scale * c[2 * ghatmap[igpt] + 1];
  }
}


/******************************************************************************
 * \brief   Performs a (double precision complex) scatter and scale on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
__global__ void pw_scatter_hip_z(      double *c,
                                const double *pwcc,
                                const double  scale,
                                const int     ngpts,
                                const int     nmaps,
                                const int    *ghatmap) {

  const int igpt = (gridDim.x * blockIdx.y + blockIdx.x) * blockDim.x + threadIdx.x;

  if (igpt < ngpts) {
    c[2 * ghatmap[igpt]    ] = scale * pwcc[2 * igpt    ];
    c[2 * ghatmap[igpt] + 1] = scale * pwcc[2 * igpt + 1];
    if (nmaps == 2) {
      c[2 * ghatmap[igpt + ngpts]    ] =   scale * pwcc[2 * igpt    ];
      c[2 * ghatmap[igpt + ngpts] + 1] = - scale * pwcc[2 * igpt + 1];
    }
  }
}


/******************************************************************************
 * \brief   Performs a (double precision complex) FFT, followed by a (double
 *          precision complex) gather, on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_cfffg_z_(const double          *din,
                                       hipDoubleComplex *zout,
                                 const int             *ghatmap,
                                 const int             *npts,
                                 const int              ngpts,
                                 const double           scale) {
  double *ptr_1, *ptr_2;
  int    *ghatmap_dev;
  int     nrpts;
  dim3    blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts = npts[0] * npts[1] * npts[2];
  if (nrpts == 0 || ngpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers
  pw_hip_device_mem_alloc(&ptr_1,       2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2,       2 * nrpts); //? (ngpts)
  pw_hip_device_mem_alloc(&ghatmap_dev,     ngpts);

  // convert the real (host) pointer 'din' into a complex (device)
  // pointer 'ptr_1'
  // copy to device (NOTE: only first half of ptr_1 is written!)
  cErr = hipMemcpyAsync(ptr_1, din, sizeof(double) * nrpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // CUDA blocking for pw_copy_rc (currently only 2-D grid)
  get_grid_params(nrpts, MAXTHREADS, threadsPerBlock, blocksPerGrid);

  // real to complex blow-up
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  hipLaunchKernelGGL((pw_copy_rc_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_1, ptr_2, nrpts);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // copy gather map array from host to the device
  cErr = hipMemcpyAsync(ghatmap_dev, ghatmap, sizeof(int) * ngpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_streams[1])
  ffthip_run_3d_z_(+1, npts, 1.0e0, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);

  // CUDA blocking for gather (currently only 2-D grid)
  get_grid_params(ngpts, NTHREADS, threadsPerBlock, blocksPerGrid);

  // gather on the GPU
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  hipLaunchKernelGGL((pw_gather_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_1, ptr_2, scale, ngpts, ghatmap_dev);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(zout, ptr_1, sizeof(hipDoubleComplex) * ngpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
  pw_hip_device_mem_free(&ghatmap_dev);
}


/******************************************************************************
 * \brief   Performs a (double precision complex) scatter, followed by a
 *          (double precision complex) FFT, on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_sfffc_z_(const hipDoubleComplex *zin,
                                       double          *dout,
                                 const int             *ghatmap,
                                 const int             *npts,
                                 const int              ngpts,
                                 const int              nmaps,
                                 const double           scale) {
  double *ptr_1, *ptr_2;
  int    *ghatmap_dev;
  int    nrpts;
  dim3   blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts = npts[0] * npts[1] * npts[2];
  if (nrpts == 0 || ngpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers
  pw_hip_device_mem_alloc(&ptr_1,       2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2,       2 * nrpts);
  pw_hip_device_mem_alloc(&ghatmap_dev, nmaps * ngpts);
  
  // copy all arrays from host to the device
  cErr = hipMemcpyAsync(ptr_1, zin, sizeof(hipDoubleComplex) * ngpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(ghatmap_dev, ghatmap, sizeof(int) * nmaps * ngpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // CUDA blocking for scatter (currently only 2-D grid)
  get_grid_params(ngpts, NTHREADS, threadsPerBlock, blocksPerGrid);

  // scatter on the GPU
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemsetAsync(ptr_2, 0, sizeof(double) * 2 * nrpts, hip_streams[1]); // we need to do this only if spherical hipt-off is used!
  if (CHECK) pw_hip_error_check(cErr, __LINE__);                                  // but it turns out to be performance irrelevant
  hipLaunchKernelGGL((pw_scatter_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_2, ptr_1, scale, ngpts, nmaps, ghatmap_dev);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_streams[1])
  ffthip_run_3d_z_(-1, npts, 1.0e0, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);

  // CUDA blocking for pw_copy_cr (currently only 2-D grid)
  get_grid_params(nrpts, MAXTHREADS, threadsPerBlock, blocksPerGrid);

  // convert the complex (device) pointer 'ptr_2' into a real (host)
  // pointer 'dout' (NOTE: Only first half of ptr_1 is written!)
  hipLaunchKernelGGL((pw_copy_cr_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_2, ptr_1, nrpts);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(dout, ptr_1, sizeof(double) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
  pw_hip_device_mem_free(&ghatmap_dev);
}


/******************************************************************************
 * \brief   Performs a (double to complex double) blow-up and a (double
 *          precision complex) 2D-FFT on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_cff_z_(const double          *din,
                                     hipDoubleComplex *zout,
                               const int             *npts) {
  double *ptr_1, *ptr_2;
  int     nrpts;
  dim3    blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts  = npts[0] * npts[1] * npts[2];
  if (nrpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers for:
  pw_hip_device_mem_alloc(&ptr_1, 2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2, 2 * nrpts);

  // convert the real (host) pointer 'din' into a complex (device)
  // pointer 'ptr_in'
  // copy all arrays from host to the device (NOTE: Only first half of ptr_1 is written!)
  cErr = hipMemcpyAsync(ptr_1, din, sizeof(double) * nrpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // CUDA blocking for pw_copy_rc (currently only 2-D grid)
  get_grid_params(nrpts, MAXTHREADS, threadsPerBlock, blocksPerGrid);

  // real to complex blow-up
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  hipLaunchKernelGGL((pw_copy_rc_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_1, ptr_2, nrpts);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_streams[1])
  //NOTE: the following works, but CUDA does 2D-FFT in C-shaped (not optimal) order
  //ffthip_run_2dm_z_(1, npts, 1.0e0, (hipfftDoubleComplex *) ptr_2, (hipfftDoubleComplex *) ptr_1, hip_streams[1]);
  ffthip_run_1dm_z_(1, npts[2], npts[0]*npts[1], 1.0e0, (hipfftDoubleComplex *) ptr_2, (hipfftDoubleComplex *) ptr_1, hip_streams[1]);
  ffthip_run_1dm_z_(1, npts[1], npts[0]*npts[2], 1.0e0, (hipfftDoubleComplex *) ptr_1, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  //cErr = hipMemcpyAsync(zout, ptr_1, sizeof(hipDoubleComplex) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  cErr = hipMemcpyAsync(zout, ptr_2, sizeof(hipDoubleComplex) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
}


/******************************************************************************
 * \brief   Performs a (double precision complex) 2D-FFT and a (double complex
 *          to double) shrink-down on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_ffc_z_(const hipDoubleComplex *zin,
                                     double          *dout,
                               const int             *npts) {
  double *ptr_1, *ptr_2;
  int     nrpts;
  dim3    blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts  = npts[0] * npts[1] * npts[2];
  if (nrpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers for:
  pw_hip_device_mem_alloc(&ptr_1, 2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2, 2 * nrpts);
  
  // copy input data from host to the device
  cErr = hipMemcpyAsync(ptr_1, zin, sizeof(hipDoubleComplex) * nrpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_stream[1])
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  //ffthip_run_2dm_z_(-1, npts, 1.0e0, (hipfftDoubleComplex *) ptr_1, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);
  ffthip_run_1dm_z_(-1, npts[1], npts[0]*npts[2], 1.0e0, (hipfftDoubleComplex *) ptr_1, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);
  ffthip_run_1dm_z_(-1, npts[2], npts[0]*npts[1], 1.0e0, (hipfftDoubleComplex *) ptr_2, (hipfftDoubleComplex *) ptr_1, hip_streams[1]);

  // CUDA blocking for pw_copy_cr (currently only 2-D grid)
  get_grid_params(nrpts, MAXTHREADS, threadsPerBlock, blocksPerGrid);

  // convert the complex (device) pointer 'ptr_1' into a real (host)
  // pointer 'dout' (NOTE: Only first half of ptr_2 is written!)
  //hipLaunchKernelGGL((pw_copy_cr_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_2, ptr_1, nrpts);
  hipLaunchKernelGGL((pw_copy_cr_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_1, ptr_2, nrpts);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  //cErr = hipMemcpyAsync(dout, ptr_1, sizeof(double) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  cErr = hipMemcpyAsync(dout, ptr_2, sizeof(double) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
}


/******************************************************************************
 * \brief   Performs a (double to complex double) blow-up and a (double
 *          precision complex) 1D-FFT on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_cf_z_(const double          *din,
                                    hipDoubleComplex *zout,
                              const int             *npts) {
  double *ptr_1, *ptr_2;
  int     nrpts;
  dim3    blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts  = npts[0] * npts[1] * npts[2];
  if (nrpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers for:
  pw_hip_device_mem_alloc(&ptr_1, 2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2, 2 * nrpts);

  // convert the real (host) pointer 'din' into a complex (device)
  // pointer 'ptr_2' (NOTE: Only first half of ptr_1 is written!)
  // copy all arrays from host to the device
  cErr = hipMemcpyAsync(ptr_1, din, sizeof(double) * nrpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // CUDA blocking for pw_copy_rc (currently only 2-D grid)
  get_grid_params(nrpts, MAXTHREADS, threadsPerBlock, blocksPerGrid);

  // real to complex blow-up
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  hipLaunchKernelGGL((pw_copy_rc_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_1, ptr_2, nrpts);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_streams[1])
  ffthip_run_1dm_z_(1, npts[2], npts[0]*npts[1], 1.0e0, (hipfftDoubleComplex *) ptr_2, (hipfftDoubleComplex *) ptr_1, hip_streams[1]);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(zout, ptr_1, sizeof(hipDoubleComplex) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
}


/******************************************************************************
 * \brief   Performs a (double precision complex) 1D-FFT and a (double complex
 *          to double) shrink-down on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_fc_z_(const hipDoubleComplex *zin,
                                    double          *dout,
                              const int             *npts) {
  double *ptr_1, *ptr_2;
  int     nrpts;
  dim3    blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts  = npts[0] * npts[1] * npts[2];
  if (nrpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers for:
  pw_hip_device_mem_alloc(&ptr_1, 2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2, 2 * nrpts);
  
  // copy input data from host to the device
  cErr = hipMemcpyAsync(ptr_1, zin, sizeof(hipDoubleComplex) * nrpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_stream[1])
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  ffthip_run_1dm_z_(-1, npts[2], npts[0]*npts[1], 1.0e0, (hipfftDoubleComplex *) ptr_1, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);

  // CUDA blocking for pw_copy_cr (currently only 2-D grid)
  get_grid_params(nrpts, MAXTHREADS, threadsPerBlock, blocksPerGrid);

  // convert the complex (device) pointer 'ptr_2' into a real (host)
  // pointer 'dout' (NOTE: Only first half of ptr_1 is written!)
  hipLaunchKernelGGL((pw_copy_cr_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_2, ptr_1, nrpts);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(dout, ptr_1, sizeof(double) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
}


/******************************************************************************
 * \brief   Performs a (double precision complex) 1D-FFT on the GPU.
 * \author  Andreas Gloess
 * \date    2013-05-01
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_f_z_(const hipDoubleComplex *zin,
                                   hipDoubleComplex *zout,
                             const int              dir,
                             const int              n,
                             const int              m) {
  double *ptr_1, *ptr_2;
  int     nrpts;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of complex arrays
  nrpts  = n * m;
  if (nrpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers for:
  pw_hip_device_mem_alloc(&ptr_1, 2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2, 2 * nrpts);
  
  // copy input data from host to the device
  cErr = hipMemcpyAsync(ptr_1, zin, sizeof(hipDoubleComplex) * nrpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_stream[1])
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  ffthip_run_1dm_z_(dir, n, m, 1.0e0, (hipfftDoubleComplex *) ptr_1, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(zout, ptr_2, sizeof(hipDoubleComplex) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
}


/******************************************************************************
 * \brief   Performs a (double precision complex) 1D-FFT, followed by a (double
 *          precision complex) gather, on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_fg_z_(const hipDoubleComplex *zin,
                                    hipDoubleComplex *zout,
                              const int             *ghatmap,
                              const int             *npts,
                              const int              mmax,
                              const int              ngpts,
                              const double           scale) {
  double *ptr_1, *ptr_2;
  int    *ghatmap_dev;
  int     nrpts;
  dim3    blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts = npts[0] * mmax;
  if (nrpts == 0 || ngpts == 0) return;

  // get streams and events
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers
  pw_hip_device_mem_alloc(&ptr_1,      2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2,      2 * nrpts);
  pw_hip_device_mem_alloc(&ghatmap_dev,    ngpts);

  // transfer gather data from host to device
  cErr = hipMemcpyAsync(ghatmap_dev, ghatmap, sizeof(int) * ngpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // transfer input data from host to device
  cErr = hipMemcpyAsync(ptr_1, zin, sizeof(hipDoubleComplex) * nrpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_streams[1])
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  ffthip_run_1dm_z_(1, npts[0], mmax, 1.0e0, (hipfftDoubleComplex *) ptr_1, (hipfftDoubleComplex *) ptr_2, hip_streams[1]);

  // CUDA blocking for gather (currently only 2-D grid)
  get_grid_params(ngpts, NTHREADS, threadsPerBlock, blocksPerGrid);

  // gather on the GPU
  hipLaunchKernelGGL((pw_gather_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_1, ptr_2, scale, ngpts, ghatmap_dev);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // transfer results from device to host
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(zout, ptr_1, sizeof(hipDoubleComplex) * ngpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
  pw_hip_device_mem_free(&ghatmap_dev);
}


/******************************************************************************
 * \brief   Performs a (double precision complex) scatter, followed by a
 *          (double precision complex) 1D-FFT, on the GPU.
 * \author  Andreas Gloess
 * \date    2013-03-07
 * \version 0.01
 *****************************************************************************/
extern "C" void pw_hip_sf_z_(const hipDoubleComplex *zin,
                                    hipDoubleComplex *zout,
                              const int             *ghatmap,
                              const int             *npts,
                              const int              mmax,
                              const int              ngpts,
                              const int              nmaps,
                              const double           scale) {
  double *ptr_1, *ptr_2;
  int    *ghatmap_dev;
  int    nrpts;
  dim3   blocksPerGrid, threadsPerBlock;
  hipStream_t *hip_streams;
  hipEvent_t  *hip_events;
  hipError_t   cErr;

  // dimensions of double and complex arrays
  nrpts = npts[0] * mmax;
  if (nrpts == 0 || ngpts == 0) return;

  // get streams
  pw_hip_get_streams(&hip_streams);
  pw_hip_get_events(&hip_events);

  // get device memory pointers
  pw_hip_device_mem_alloc(&ptr_1,       2 * nrpts);
  pw_hip_device_mem_alloc(&ptr_2,       2 * nrpts);
  pw_hip_device_mem_alloc(&ghatmap_dev, nmaps * ngpts);

  // transfer input data from host to the device
  cErr = hipMemcpyAsync(ptr_1, zin, sizeof(hipDoubleComplex) * ngpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // transfer scatter data from host to device
  cErr = hipMemcpyAsync(ghatmap_dev, ghatmap, sizeof(int) * nmaps * ngpts, hipMemcpyHostToDevice, hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[0], hip_streams[0]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // CUDA blocking for scatter (currently only 2-D grid)
  get_grid_params(ngpts, NTHREADS, threadsPerBlock, blocksPerGrid);

  // scatter on the GPU
  cErr = hipStreamWaitEvent(hip_streams[1], hip_events[0], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemsetAsync(ptr_2, 0, sizeof(double) * 2 * nrpts, hip_streams[1]); // we need to do this only if spherical hipt-off is used!
  if (CHECK) pw_hip_error_check(cErr, __LINE__);                                  // but it turns out to be performance irrelevant
  hipLaunchKernelGGL((pw_scatter_hip_z), dim3(blocksPerGrid), dim3(threadsPerBlock), 0, hip_streams[1], ptr_2, ptr_1, scale, ngpts, nmaps, ghatmap_dev);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // fft on the GPU (hip_streams[1])
  ffthip_run_1dm_z_(-1, npts[0], mmax, 1.0e0, (hipfftDoubleComplex *) ptr_2, (hipfftDoubleComplex *) ptr_1, hip_streams[1]);
  cErr = hipGetLastError();
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipEventRecord(hip_events[1], hip_streams[1]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // get results from device memory
  cErr = hipStreamWaitEvent(hip_streams[2], hip_events[1], 0);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);
  cErr = hipMemcpyAsync(zout, ptr_1, sizeof(hipDoubleComplex) * nrpts, hipMemcpyDeviceToHost, hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // synchronize with respect to host
  cErr = hipStreamSynchronize(hip_streams[2]);
  if (CHECK) pw_hip_error_check(cErr, __LINE__);

  // release memory stack
  pw_hip_device_mem_free(&ptr_1);
  pw_hip_device_mem_free(&ptr_2);
  pw_hip_device_mem_free(&ghatmap_dev);
}
#endif

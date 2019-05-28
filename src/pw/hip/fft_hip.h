/*****************************************************************************
 *  CP2K: A general program to perform molecuplar dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

#ifndef FFT_HIP_H
#define FFT_HIP_H
/******************************************************************************
 *
 *                              - hip_tools/hipfft.h
 *                              - hip_tools/hipfft.hip
 *
 *****************************************************************************/
#if defined ( __PW_HIP )
#include <hipfft.h>

// Double precision complex procedures
extern "C" void ffthip_run_3d_z_  (const int                 fsign,
                                  const int                *n,
                                  const double              scale,
                                        hipfftDoubleComplex *data,
                                  const hipStream_t        hip_stream);


extern "C" void ffthip_run_2dm_z_ (const int                 fsign,
                                  const int                *n,
                                  const double              scale,
                                        hipfftDoubleComplex *data_in,
                                        hipfftDoubleComplex *data_out,
                                  const hipStream_t        hip_stream);


extern "C" void ffthip_run_1dm_z_ (const int                 fsign,
                                  const int                 n,
                                  const int                 m,
                                  const double              scale,
                                        hipfftDoubleComplex *data_in,
                                        hipfftDoubleComplex *data_out,
                                  const hipStream_t        hip_stream);


extern "C" void ffthip_release_   ();
#endif
#endif

/*****************************************************************************
 *  CP2K: A general program to perform molecular dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

#if defined ( __PW_HIP )

/* This file contains memory management routines for device memory. */ 
// Revised: Sept. 2012, Andreas Gloess
// Author:  Benjamin G Levine

// global dependencies
#include <hip/hip_runtime.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

// local dependencies
#include "fft_hip.h"
#include "fft_hip_utils.h"

// debug flag
#define CHECK 1
#define VERBOSE 0

// --- CODE --------------------------------------------------------------------
static const int    nstreams      = 3;
static const int    nevents       = 2;

hipError_t          cErr;
static hipStream_t *hip_streams;
static hipEvent_t  *hip_events;
static int           is_configured = 0;

extern void pw_hip_error_check (hipError_t hipError, int line) {
  int         pid;
  size_t      free, total;
  hipError_t cErr2;

  cErr2 = hipGetLastError();
  if (hipError != hipSuccess || cErr2 != hipSuccess) {
    pid = getpid();
    printf("%d HIP RT Error line %d\n", pid, line);
    printf("%d HIP RT1 Error: %s\n", pid, hipGetErrorString(hipError));
    printf("%d HIP RT2 Error: %s\n", pid, hipGetErrorString(cErr2));
    hipMemGetInfo(&free,&total);
    printf("%d Free: %zu , Total: %zu\n", pid, free, total);
    fflush(stdout);
    exit(-1);
  }
}

// STREAMS INIT/GET/RELEASE
void pw_hip_device_streams_alloc (hipStream_t **streams) {
  hipStream_t *hip_streams_ptr;
  hip_streams_ptr = (hipStream_t *) malloc(nstreams * sizeof(hipStream_t));
  for (int i = 0; i < nstreams; i++) {
    cErr = hipStreamCreateWithFlags(&hip_streams_ptr[i], hipStreamNonBlocking);
    if (CHECK) pw_hip_error_check (cErr, __LINE__);
  }
  *streams = hip_streams_ptr;
}

extern void pw_hip_get_streams (hipStream_t **streams) {
  *streams = hip_streams;
}

void pw_hip_device_streams_release (hipStream_t **streams) {
  hipStream_t *hip_streams_ptr;
  hip_streams_ptr = *streams;
  for (int i = 0; i < nstreams; i++) {
     cErr = hipStreamDestroy(hip_streams_ptr[i]);
     if (CHECK) pw_hip_error_check (cErr, __LINE__);
  }
  free(hip_streams_ptr);
  hip_streams_ptr = NULL;
}

// EVENTS INIT/GET/RELEASE
void pw_hip_device_events_alloc (hipEvent_t **events) {
  hipEvent_t *hip_events_ptr;
  hip_events_ptr = (hipEvent_t *) malloc(nevents * sizeof(hipEvent_t));
  for (int i =0; i < nevents; i++) {
    cErr = hipEventCreateWithFlags(&hip_events_ptr[i], hipEventDisableTiming);
    //cErr = hipEventCreateWithFlags(&(hip_events_ptr[i]), hipEventDefault);
    //cErr = hipEventCreateWithFlags(&(hip_events_ptr[i]), hipEventBlockingSync);
    if (CHECK) pw_hip_error_check (cErr, __LINE__);
  }
  *events = hip_events_ptr;
}

extern void pw_hip_get_events (hipEvent_t **events) {
  *events = hip_events;
}

void pw_hip_device_events_release (hipEvent_t **events) {
  hipEvent_t *hip_events_ptr;
  hip_events_ptr = *events;
  for (int i = 0; i < nevents; i++) {
    cErr = hipEventDestroy(hip_events_ptr[i]);
    if (CHECK) pw_hip_error_check (cErr, __LINE__);
  }
  free(hip_events_ptr);
  hip_events_ptr = NULL;
}

// MEMORY ALLOC/RELEASE
extern void pw_hip_device_mem_alloc (int **ptr, int n) {
  cErr = hipMalloc((void **) ptr, (size_t) sizeof(int)*n);
  if (CHECK) pw_hip_error_check (cErr, __LINE__);
}

extern void pw_hip_device_mem_alloc (float **ptr, int n) {
  cErr = hipMalloc((void **) ptr, (size_t) sizeof(float)*n);
  if (CHECK) pw_hip_error_check (cErr, __LINE__);
}

extern void pw_hip_device_mem_alloc (double **ptr, int n) {
  cErr = hipMalloc((void **) ptr, (size_t) sizeof(double)*n);
  if (CHECK) pw_hip_error_check (cErr, __LINE__);
}

extern void pw_hip_device_mem_free (int **ptr) {
  cErr = hipFree((void *) *ptr);
  if (CHECK) pw_hip_error_check (cErr, __LINE__);
  *ptr = NULL;
}

extern void pw_hip_device_mem_free (float **ptr) {
  cErr = hipFree((void *) *ptr);
  if (CHECK) pw_hip_error_check (cErr, __LINE__);
  *ptr = NULL;
}

extern void pw_hip_device_mem_free (double **ptr) {
  cErr = hipFree((void *) *ptr);
  if (CHECK) pw_hip_error_check (cErr, __LINE__);
  *ptr = NULL;
}

// INIT/RELEASE
extern "C" int pw_hip_init () {
  if ( is_configured == 0 ) {
    int version;
    hipfftResult_t hipfftErr;
    pw_hip_device_streams_alloc (&hip_streams);
    pw_hip_device_events_alloc (&hip_events);
    is_configured = 1;
    hipfftErr = hipfftGetVersion(&version);
    if (CHECK) hipfft_error_check(hipfftErr, __LINE__);
/*
    if (version == 7000){
#if defined ( __HAS_PATCHED_CUFFT_70 )
//      message would be printed to all ranks, if the user has __HAS_PATCHED_CUFFT_70 she's supposed to know what she's doing
//      printf("CUFFT 7.0 enabled on user request (-D__HAS_PATCHED_CUFFT_70).\n");
//      printf("Please ensure that CUFFT is patched (libhipfft.so.x.y.z, libhipfftw.so.x,y,z; x.y.z >= 7.0.35).\n");
#else
       printf("CUFFT 7.0 disabled due to an unresolved bug.\n");
       printf("Please upgrade to CUDA 7.5 or later or apply CUFFT patch (see -D__HAS_PATCHED_CUFFT_70).\n");
       return -1;
#endif
    }
*/
  }
  return 0;
}

extern "C" void pw_hip_finalize () {
  if ( is_configured == 1 ) {
    ffthip_release_();
    pw_hip_device_streams_release (&hip_streams);
    pw_hip_device_events_release (&hip_events);
    is_configured = 0;
  }
}

#endif

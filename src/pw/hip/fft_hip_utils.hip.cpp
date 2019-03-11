/*****************************************************************************
 *  CP2K: A general program to perform molecular dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

/******************************************************************************
 *  Author: Andreas Gloess
 *
 *****************************************************************************/
#if defined ( __PW_HIP )

// global dependencies
#include <hip/hip_runtime.h>
#include <hipfft.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

// debug flag
#define CHECK 1
#define VERBOSE 0

// --- CODE -------------------------------------------------------------------


extern void hipfft_error_check (hipfftResult_t hipfftError, int line) {
  int         pid;
  size_t      free, total;
  hipError_t cErr2;

  cErr2 = hipGetLastError();
  if (hipfftError != HIPFFT_SUCCESS || cErr2 != hipSuccess) {
    pid = getpid();
    printf("%d CUDA FFT Error line: %d \n", pid, line);
    switch (hipfftError) {
      case HIPFFT_INVALID_PLAN:   printf("%d CUDA FFT1 Error (HIPFFT_INVALID_PLAN)\n", pid); break;
      case HIPFFT_ALLOC_FAILED:   printf("%d CUDA FFT1 Error (HIPFFT_ALLOC_FAILED)\n", pid); break;
      case HIPFFT_INVALID_VALUE:  printf("%d CUDA FFT1 Error (HIPFFT_INVALID_VALUE)\n", pid); break;
      case HIPFFT_INTERNAL_ERROR: printf("%d CUDA FFT1 Error (HIPFFT_INTERNAL_ERROR)\n", pid); break;
      case HIPFFT_EXEC_FAILED:    printf("%d CUDA FFT1 Error (HIPFFT_EXEC_FAILED)\n", pid); break;
      case HIPFFT_INVALID_SIZE:   printf("%d CUDA FFT1 Error (HIPFFT_INVALID_SIZE)\n", pid); break;
      default: printf("%d CUDA FFT1 Error (--unimplemented--) %d %d\n", pid, hipfftError, cErr2); break;
    }
    printf("%d CUDA FFT2 Error %s \n", pid, hipGetErrorString(cErr2));
    hipMemGetInfo(&free,&total);
    printf("%d Free: %zu , Total: %zu\n", pid, free, total);
    fflush(stdout);
    exit(-1);
  }
}

#endif

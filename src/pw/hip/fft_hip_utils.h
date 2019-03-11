/*****************************************************************************
 *  CP2K: A general program to perform molecular dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

#ifndef FFT_HIP_UTILS_H
#define FFT_HIP_UTILS_H
#include <hipfft.h>

extern void hipfft_error_check (hipfftResult_t hipfftError, int line);

#endif

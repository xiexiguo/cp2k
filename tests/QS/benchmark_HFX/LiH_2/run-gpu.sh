#!/bin/bash

export LD_PRELOAD=/home/xiexg/rocfft/lib/librocfft.so:/home/xiexg/rocfft/lib/librocfft-device.so

CP2K=/home/xiexg/cp2k/exe/Linux-x86-64-hip-gfortran-mkl/cp2k.psmp
mpirun -np 1 $CP2K input_bulk_B88_3.inp

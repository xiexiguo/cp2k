#!/bin/bash

export LD_PRELOAD=/home/xiexg/rocfft/librocfft.so.0.9.0.0:/home/xiexg/rocfft/librocfft-device.so.0.9.0.0

CP2K=/home/xiexg/cp2k/exe/Linux-x86-64-gfortran-mkl/cp2k.psmp
mpirun -np 1 $CP2K input_bulk_B88_3.inp

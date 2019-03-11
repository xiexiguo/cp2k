/*****************************************************************************
 *  CP2K: A general program to perform molecular dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

#ifndef PW_HIP_UTILS_H
#define PW_HIP_UTILS_H

extern void pw_hip_error_check (hipError_t hipError_t, int line);

// STREAMS INIT/GET/RELEASE
extern void pw_hip_device_streams_alloc (hipStream_t **streams);
extern void pw_hip_get_streams (hipStream_t **streams);
extern void pw_hip_device_streams_release (hipStream_t **streams);

// EVENTS INIT/GET/RELEASE
extern void pw_hip_device_events_alloc (hipEvent_t **events);
extern void pw_hip_get_events (hipEvent_t **events);
extern void pw_hip_device_events_release (hipEvent_t **events);

// MEMORY ALLOC/RELEASE
extern void pw_hip_device_mem_alloc (int **ptr, int n);
extern void pw_hip_device_mem_alloc (float **ptr, int n);
extern void pw_hip_device_mem_alloc (double **ptr, int n);
extern void pw_hip_device_mem_free (int **ptr);
extern void pw_hip_device_mem_free (float **ptr);
extern void pw_hip_device_mem_free (double **ptr);

// DEVICE INIT/RELEASE
extern "C" int  pw_hip_init ();
extern "C" void pw_hip_release ();

#endif

/*****************************************************************************
 *  CP2K: A general program to perform molecular dynamics simulations        *
 *  Copyright (C) 2000 - 2019  CP2K developers group                         *
 *****************************************************************************/

#ifndef PW_HIP_H
#define PW_HIP_H
/******************************************************************************
 *  Authors: Benjamin G Levine, Andreas Gloess
 *
 *  2012/05/18                 Refacturing - original files:
 *                              - hip_tools/hip_pw_hip.hip
 *
 *****************************************************************************/
#if defined ( __PW_HIP )

/* Double precision complex procedures */
extern "C" void pw_hip_cfffg_z_ (const double          *din,
                                        hipDoubleComplex *zout,
                                  const int             *ghatmap,
                                  const int             *npts,
                                  const int              ngpts,
                                  const double           scale);


extern "C" void pw_hip_sfffc_z_ (const hipDoubleComplex *zin,
                                        double          *dout,
                                  const int             *ghatmap,
                                  const int             *npts,
                                  const int              ngpts,
                                  const int              nmaps,
                                  const double           scale);


extern "C" void pw_hip_cff_z_   (const double          *din,
                                        hipDoubleComplex *zout,
                                  const int             *npts);


extern "C" void pw_hip_ffc_z_   (const hipDoubleComplex *zin,
                                        double          *dout,
                                  const int             *npts);


extern "C" void pw_hip_cf_z_    (const double          *din,
                                        hipDoubleComplex *zout,
                                  const int             *npts);


extern "C" void pw_hip_fc_z_    (const hipDoubleComplex *zin,
                                        double          *dout,
                                  const int             *npts);


extern "C" void pw_hip_f_z_     (const hipDoubleComplex *zin,
                                        hipDoubleComplex *zout,
                                  const int              dir,
                                  const int              n,
                                  const int              m);


extern "C" void pw_hip_fg_z_    (const hipDoubleComplex *zin,
                                        hipDoubleComplex *zout,
                                  const int             *ghatmap,
                                  const int             *npts,
                                  const int              mmax,
                                  const int              ngpts,
                                  const double           scale);


extern "C" void pw_hip_sf_z_    (const hipDoubleComplex *zin,
                                        hipDoubleComplex *zout,
                                  const int             *ghatmap,
                                  const int             *npts,
                                  const int              mmax,
                                  const int              ngpts,
                                  const int              nmaps,
                                  const double           scale);
#endif
#endif

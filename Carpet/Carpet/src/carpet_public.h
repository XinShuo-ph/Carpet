/* $Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/carpet_public.h,v 1.10 2003/07/20 21:03:43 schnetter Exp $ */

#ifndef CARPET_PUBLIC_H
#define CARPET_PUBLIC_H

#include <mpi.h>

#include "cctk_Arguments.h"



/* Tell thorns that the Carpet routines exist */
#define HAVE_CARPET



#ifdef __cplusplus
namespace Carpet {
  extern "C" {
#endif
    
    /* Scheduled functions */
    int CarpetParamCheck (CCTK_ARGUMENTS);
    int CarpetStartup (void);
    
    
    
    /* Prolongation management */
    int CarpetEnableProlongating (const int flag);
    
    
    
    /* Call a schedule group */
    int CallScheduleGroup (cGH * const cgh, const char * const group);
    
    /* Call a local function */
    int CallLocalFunction (cGH * const cgh,
                           void (* const function) (cGH * const cgh));
    int CallLevelFunction (cGH * const cgh,
                           void (* const function) (cGH * const cgh));
    int CallGlobalFunction (cGH * const cgh,
                            void (* const function) (cGH * const cgh));
    
    
    
    /* Helper functions */
    MPI_Comm CarpetMPIComm (void);
    MPI_Datatype CarpetMPIDatatype (int vartype);
    
#ifdef __cplusplus
  } /* extern "C" */
} /* namespace Carpet */
#endif

#endif /* !defined(CARPET_PUBLIC_H) */

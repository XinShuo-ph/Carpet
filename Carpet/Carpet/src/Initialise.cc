#include <assert.h>
#include <stdlib.h>

#include "cctk.h"
#include "cctk_Parameters.h"
#include "cctki_GHExtensions.h"
#include "cctki_ScheduleBindings.h"
#include "cctki_WarnLevel.h"

#include "carpet.hh"

static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/Initialise.cc,v 1.3 2001/12/18 10:29:35 schnetter Exp $";



namespace Carpet {
  
  using namespace std;
  
  
  
  int Initialise (tFleshConfig* fc)
  {
    DECLARE_CCTK_PARAMETERS;
    
    // Initialise stuff
    const int convlev = 0;
    cGH* const cgh = CCTK_SetupGH (fc, convlev);
    CCTKi_AddGH (fc, convlev, cgh);
    
    // Delay checkpoint until MPI has been initialised
    Waypoint ("starting Initialise...");
    
    // Initialise stuff
    cgh->cctk_iteration = 0;
    cgh->cctk_time = cctk_initial_time;
    
    // Enable storage and communtication
    CCTKi_ScheduleGHInit (cgh);
    
    // Initialise stuff
    CCTKi_InitGHExtensions (cgh);
    
    // Check parameters
    Waypoint ("PARAMCHECK");
    CCTK_ScheduleTraverse ("CCTK_PARAMCHECK", cgh, CallFunction);
    CCTKi_FinaliseParamWarn();
    
    BEGIN_REFLEVEL_LOOP(cgh) {
      
      // Checking
      Poison (cgh, alltimes);
      
      // Set up the grid
      Waypoint ("%*sScheduling BASEGRID", 2*reflevel, "");
      CCTK_ScheduleTraverse ("CCTK_BASEGRID", cgh, CallFunction);
      if (reflevel==0) {
	base_delta_time = cgh->cctk_delta_time;
      } else {
// 	assert (abs(cgh->cctk_delta_time - base_delta_time / reflevelfactor)
// 		< 1e-6 * base_delta_time);
	// This circumvents a bug in CactusBase/Time
	cgh->cctk_delta_time = base_delta_time / reflevelfact;
      }
      
      // Set up the initial data
      Waypoint ("%*sScheduling INITIAL", 2*reflevel, "");
      CCTK_ScheduleTraverse ("CCTK_INITIAL", cgh, CallFunction);
      Waypoint ("%*sScheduling POSTINITIAL", 2*reflevel, "");
      CCTK_ScheduleTraverse ("CCTK_POSTINITIAL", cgh, CallFunction);
      
      // Poststep
      Waypoint ("%*sScheduling POSTSTEP", 2*reflevel, "");
      CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cgh, CallFunction);
      
      // Checking
      PoisonCheck (cgh, alltimes);
      
      // Recompose grid hierarchy
      Recompose (cgh);
      
    } END_REFLEVEL_LOOP(cgh);
    
    BEGIN_REVERSE_REFLEVEL_LOOP(cgh) {
      
      // Restrict
      Restrict (cgh);
      
      // Checking
      CalculateChecksums (cgh, allbutcurrenttime);
      
      // Recover
      Waypoint ("%*sScheduling RECOVER_VARIABLES", 2*reflevel, "");
      CCTK_ScheduleTraverse ("CCTK_RECOVER_VARIABLES", cgh, CallFunction);
      Waypoint ("%*sScheduling CPINITIAL", 2*reflevel, "");
      CCTK_ScheduleTraverse ("CCTK_CPINITIAL", cgh, CallFunction);
      
      // Analysis
      Waypoint ("%*sScheduling ANALYSIS", 2*reflevel, "");
      CCTK_ScheduleTraverse ("CCTK_ANALYSIS", cgh, CallFunction);
      
      // Output
      Waypoint ("%*sOutputGH", 2*reflevel, "");
      CCTK_OutputGH (cgh);
      
      // Checking
      CheckChecksums (cgh, allbutcurrenttime);
      
    } END_REVERSE_REFLEVEL_LOOP(cgh);
    
    Waypoint ("done with Initialise.");
    
    return 0;
  }
  
} // namespace Carpet

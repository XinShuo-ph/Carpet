#include <assert.h>
#include <stdlib.h>

#include "cctk.h"
#include "cctk_Parameters.h"
#include "cctk_Termination.h"

#include "Carpet/CarpetLib/src/th.hh"

#include "carpet.hh"

static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/Evolve.cc,v 1.7 2002/01/09 17:45:39 schnetter Exp $";



namespace Carpet {
  
  using namespace std;
  
  
  
  static bool do_terminate (const cGH *cgh, CCTK_REAL time, int iteration)
  {
    DECLARE_CCTK_PARAMETERS;
    
    // Early shortcut
    if (terminate_next || CCTK_TerminationReached(cgh)) return false;
    
    bool term_iter = iteration >= cctk_itlast;
    bool term_time = (cctk_initial_time < cctk_final_time
		     ? time >= cctk_final_time
		     : time <= cctk_final_time);
    
    if (CCTK_Equals(terminate, "never")) {
      return true;
    } else if (CCTK_Equals(terminate, "iteration")) {
      return term_iter;
    } else if (CCTK_Equals(terminate, "time")) {
      return term_time;
    } else if (CCTK_Equals(terminate, "either")) {
      return term_iter || term_time;
    } else if (CCTK_Equals(terminate, "both")) {
      return term_iter && term_time;
    } else {
      abort();
    }
  }
  
  
  
  int Evolve (tFleshConfig* fc)
  {
    DECLARE_CCTK_PARAMETERS;
    
    Waypoint ("starting Evolve...");
    
    const int convlev = 0;
    cGH* cgh = fc->GH[convlev];
    
    // Main loop
    while (! do_terminate(cgh, cgh->cctk_time, cgh->cctk_iteration)) {
      
      // Advance time
      ++cgh->cctk_iteration;
      cgh->cctk_time += base_delta_time / maxreflevelfact;
      
      Waypoint ("Evolving iteration %d...", cgh->cctk_iteration);
      
      BEGIN_MGLEVEL_LOOP(cgh) {
	if ((cgh->cctk_iteration-1) % mglevelfact == 0) {
	  
	  Waypoint ("Evolving multigrid level %d...", mglevel);
	  
	  BEGIN_REFLEVEL_LOOP(cgh) {
	    const int do_every = mglevelfact * maxreflevelfact/reflevelfact;
	    if ((cgh->cctk_iteration-1) % do_every == 0) {
	      
	      // Cycle time levels
	      CycleTimeLevels (cgh);
	      
	      // Advance level times
	      tt->advance_time (reflevel, mglevel);
	      tt0->advance_time (reflevel, mglevel);
	      for (int group=0; group<CCTK_NumGroups(); ++group) {
		switch (CCTK_GroupTypeI(group)) {
		case CCTK_SCALAR:
		  break;
		case CCTK_ARRAY:
		  arrdata[group].tt->advance_time (reflevel, mglevel);
		  break;
		case CCTK_GF:
		  break;
		default:
		  abort();
		}
	      }
	      
	      // Checking
	      CalculateChecksums (cgh, allbutcurrenttime);
	      Poison (cgh, currenttimebutnotifonly);
	      
	      // Evolve
	      Waypoint ("%*sScheduling PRESTEP", 2*reflevel, "");
	      CCTK_ScheduleTraverse ("CCTK_PRESTEP", cgh, CallFunction);
	      Waypoint ("%*sScheduling EVOL", 2*reflevel, "");
	      CCTK_ScheduleTraverse ("CCTK_EVOL", cgh, CallFunction);
	      Waypoint ("%*sScheduling POSTSTEP", 2*reflevel, "");
	      CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cgh, CallFunction);
	      
	      // Checking
	      PoisonCheck (cgh, currenttimebutnotifonly);
	      
	      // Recompose grid hierarchy
	      Recompose (cgh);
	      
	    }
	  } END_REFLEVEL_LOOP(cgh);
	  
	}
      } END_MGLEVEL_LOOP(cgh);
      
      BEGIN_MGLEVEL_LOOP(cgh) {
	if (cgh->cctk_iteration % mglevelfact == 0) {
	  
	  BEGIN_REVERSE_REFLEVEL_LOOP(cgh) {
	    const int do_every = mglevelfact * maxreflevelfact/reflevelfact;
	    if (cgh->cctk_iteration % do_every == 0) {
	      
	      // Restrict
	      Restrict (cgh);
	      
	      // Checking
	      CalculateChecksums (cgh, currenttime);
	      
	      // Waypoint
	      Waypoint ("%*sScheduling CHECKPOINT", 2*reflevel, "");
	      CCTK_ScheduleTraverse ("CCTK_CHECKPOINT", cgh, CallFunction);
	      
	      // Analysis
	      Waypoint ("%*sScheduling ANALYSIS", 2*reflevel, "");
	      CCTK_ScheduleTraverse ("CCTK_ANALYSIS", cgh, CallFunction);
	      
	      // Output
	      Waypoint ("%*sOutputGH", 2*reflevel, "");
	      CCTK_OutputGH (cgh);
	      
	      // Checking
	      CheckChecksums (cgh, alltimes);
	      
	    }
	  } END_REVERSE_REFLEVEL_LOOP(cgh);
	  
	}
      } END_MGLEVEL_LOOP(cgh);
      
    } // main loop
    
    Waypoint ("done with Evolve.");
    
    return 0;
  }
  
} // namespace Carpet

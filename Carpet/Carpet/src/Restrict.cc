#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "cctk.h"

#include "Carpet/CarpetLib/src/ggf.hh"
#include "Carpet/CarpetLib/src/gh.hh"

#include "carpet.hh"

static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/Restrict.cc,v 1.10 2002/09/25 19:55:06 schnetter Exp $";

CCTK_FILEVERSION(Carpet_Restrict_cc)



namespace Carpet {
  
  using namespace std;
  
  
  
  void Restrict (const cGH* cgh)
  {
    assert (component == -1);
    
    Checkpoint ("%*sRestrict", 2*reflevel, "");
    
    // Loop over grid functions with storage
    for (int group=0; group<CCTK_NumGroups(); ++group) {
      if (CCTK_GroupTypeI(group) == CCTK_GF
	  && CCTK_QueryGroupStorageI(cgh, group)) {
	for (int var=0; var<(int)arrdata[group].data.size(); ++var) {
	  
	  const int tl = 0;
	  
	  // use background time here (which may not be modified by
	  // the user)
	  const CCTK_REAL time = tt->time (tl, reflevel, mglevel);
	  if (tl==0) {
	    const CCTK_REAL time1 = tt->time (tl, reflevel, mglevel);
	    const CCTK_REAL time2 = cgh->cctk_time / base_delta_time;
	    assert (fabs(time1 - time2) < 1e-10);
	  }
	  
	  if (mglevel > 0) {
	    
	    for (int c=0; c<hh->components(reflevel); ++c) {
	      arrdata[group].data[var]->mg_restrict
		(tl, reflevel, c, mglevel, time);
	    }
	    for (int c=0; c<arrdata[group].hh->components(reflevel); ++c) {
	      arrdata[group].data[var]->sync (tl, reflevel, c, mglevel);
	    }
	    
	  } // if not finest multigrid level
	  
	  if (reflevel < hh->reflevels()-1) {
	    
	    for (int c=0; c<hh->components(reflevel); ++c) {
	      arrdata[group].data[var]->ref_restrict
		(tl, reflevel, c, mglevel, time);
	    }
	    for (int c=0; c<arrdata[group].hh->components(reflevel); ++c) {
	      arrdata[group].data[var]->sync (tl, reflevel, c, mglevel);
	    }
	    
	    for (int c=0; c<arrdata[group].hh->components(reflevel+1); ++c) {
	      arrdata[group].data[var]->ref_bnd_prolongate
		(tl, reflevel+1, c, mglevel, time);
	    }
	    // TODO: is this necessary?
	    for (int c=0; c<arrdata[group].hh->components(reflevel+1); ++c) {
	      arrdata[group].data[var]->sync (tl, reflevel+1, c, mglevel);
	    }
	    
	  } // if not finest refinement level
	  
	} // loop over variables
      } // if group has storage
    } // loop over groups
    
  }
  
} // namespace Carpet

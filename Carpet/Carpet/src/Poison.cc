#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "carpet.hh"

static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/Poison.cc,v 1.3 2001/11/02 10:58:58 schnetter Exp $";



namespace Carpet {
  
  using namespace std;
  
  
  
  void Poison (cGH* cgh, const checktimes where)
  {
    DECLARE_CCTK_PARAMETERS;
    
    if (! poison_new_timelevels) return;
    
    for (int group=0; group<CCTK_NumGroups(); ++group) {
      if (CCTK_QueryGroupStorageI((cGH*)cgh, group)) {
	PoisonGroup (cgh, group, where);
      } // if has storage
    } // for group
  }
  
  
  
  void PoisonGroup (cGH* cgh, const int group, const checktimes where)
  {
    DECLARE_CCTK_PARAMETERS;
    
    assert (group>=0 && group<CCTK_NumGroups());
    
    if (! poison_new_timelevels) return;
    
    Checkpoint ("%*sPoisonGroup %s", 2*reflevel, "", CCTK_GroupName(group));
    
    if (! CCTK_QueryGroupStorageI((cGH*)cgh, group)) {
      CCTK_VWarn (2, __LINE__, __FILE__, CCTK_THORNSTRING,
		  "Cannot poison group \"%s\" because it has no storage",
		  CCTK_GroupName(group));
      return;
    }
    
    for (int var=0; var<CCTK_NumVarsInGroupI(group); ++var) {
      
      const int n = CCTK_FirstVarIndexI(group) + var;
      const int sz = CCTK_VarTypeSize(CCTK_VarTypeI(n));
      assert (sz>0);
      
      const int num_tl = CCTK_NumTimeLevelsFromVarI(n);
      assert (num_tl>0);
      const int min_tl = mintl(where, num_tl);
      const int max_tl = maxtl(where, num_tl);
      
      for (int tl=min_tl; tl<=max_tl; ++tl) {
	
	BEGIN_COMPONENT_LOOP(cgh) {
	  if (hh->is_local(reflevel,component)) {
	    int np = 1;
	    const int gpdim = arrdata[group].info.dim;
	    for (int d=0; d<gpdim; ++d) {
	      np *= *CCTK_ArrayGroupSizeI((cGH*)cgh, d, group);
	    }
	    memset (cgh->data[n][tl], poison_value, np*sz);
	  }
	} END_COMPONENT_LOOP(cgh);
	
      } // for tl
      
    } // for var
  }
  
  
  
  void PoisonCheck (cGH* cgh, const checktimes where)
  {
    DECLARE_CCTK_PARAMETERS;
    
    if (! check_for_poison) return;
    
    Checkpoint ("%*sPoisonCheck", 2*reflevel, "");
    
    for (int group=0; group<CCTK_NumGroups(); ++group) {
      if (CCTK_QueryGroupStorageI((cGH*)cgh, group)) {
	for (int var=0; var<CCTK_NumVarsInGroupI(group); ++var) {
	  
	  const int n = CCTK_FirstVarIndexI(group) + var;
	  
	  const int num_tl = CCTK_NumTimeLevelsFromVarI(n);
	  assert (num_tl>0);
	  const int min_tl = mintl(where, num_tl);
	  const int max_tl = maxtl(where, num_tl);
	  
	  for (int tl=min_tl; tl<=max_tl; ++tl) {
	    
	    BEGIN_COMPONENT_LOOP(cgh) {
	      if (hh->is_local(reflevel,component)) {
		vect<int,dim> size(1);
		const int gpdim = arrdata[group].info.dim;
		for (int d=0; d<gpdim; ++d) {
		  size[d] = *CCTK_ArrayGroupSizeI((cGH*)cgh, d, group);
		}
		const int tp = CCTK_VarTypeI(n);
		const void* const data = cgh->data[n][tl];
		int numpoison=0;
		for (int k=0; k<size[2]; ++k) {
		  for (int j=0; j<size[1]; ++j) {
		    for (int i=0; i<size[0]; ++i) {
		      const int idx = CCTK_GFINDEX3D(cgh,i,j,k);
		      bool poisoned=false;
		      switch (tp) {
#define TYPECASE(N,T)							\
		      case N: {						\
			T worm;						\
			memset (&worm, poison_value, sizeof(worm));	\
			poisoned = ((const T*)data)[idx] == worm;	\
			break;						\
		      }
#include "typecase"
#undef TYPECASE
		      default:
			UnsupportedVarType(n);
		      }
		      if (poisoned) {
			++numpoison;
			if (numpoison<=10) {
			  char* fullname = CCTK_FullName(n);
			  CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
				      "The variable \"%s\" contains poison at [%d,%d,%d] in timelevel %d",
				      fullname, i,j,k, tl);
			  free (fullname);
			}
		      } // if poisoned
		    } // for i
		  } // for j
		} // for k
		if (numpoison>10) {
		  char* fullname = CCTK_FullName(n);
		  CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
			      "The variable \"%s\" contains poison at %d locations in timelevel %d; not all locations were printed.",
			      fullname, numpoison, tl);
		  free (fullname);
		}
	      } // if is local
	    } END_COMPONENT_LOOP(cgh);
	    
	  } // for tl
	  
	} // for var
      } // if has storage
    } // for group
  }
  
} // namespace Carpet

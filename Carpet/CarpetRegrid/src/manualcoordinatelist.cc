#include <cassert>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "gh.hh"

#include "carpet.hh"
#include "regrid.hh"

extern "C" {
  static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetRegrid/src/manualcoordinatelist.cc,v 1.9 2004/07/02 10:14:51 tradke Exp $";
  CCTK_FILEVERSION(Carpet_CarpetRegrid_manualcoordinatelist_cc);
}



namespace CarpetRegrid {
  
  using namespace std;
  using namespace Carpet;
  
  
  
  int ManualCoordinateList (cGH const * const cctkGH,
                            gh<dim> const & hh,
                            gh<dim>::rexts  & bbsss,
                            gh<dim>::rbnds  & obss,
                            gh<dim>::rprocs & pss)
  {
    DECLARE_CCTK_PARAMETERS;
    int ierr;
    
    assert (refinement_levels >= 1);
    
    // do nothing if the levels already exist
    if (reflevel == refinement_levels) return 0;
    
    assert (bbsss.size() >= 1);
    
    jjvect nboundaryzones, is_internal, is_staggered, shiftout;
    ierr = GetBoundarySpecification
      (2*dim, &nboundaryzones[0][0], &is_internal[0][0],
       &is_staggered[0][0], &shiftout[0][0]);
    assert (!ierr);
    rvect physical_min, physical_max;
    rvect interior_min, interior_max;
    rvect exterior_min, exterior_max;
    rvect base_spacing;
    ierr = GetDomainSpecification
      (dim, &physical_min[0], &physical_max[0],
       &interior_min[0], &interior_max[0],
       &exterior_min[0], &exterior_max[0], &base_spacing[0]);
    assert (!ierr);
    
    bbsss.resize (refinement_levels);
    obss.resize (refinement_levels);
    pss.resize (refinement_levels);
    
    vector<vector<rbbox> > newbbss;
    if (strcmp(coordinates, "") != 0) {
      istringstream gp_str(coordinates);
      try {
        gp_str >> newbbss;
      } catch (input_error) {
        CCTK_WARN (0, "Could not parse parameter \"coordinates\"");
      }
    }

    vector<vector<bbvect> > newobss;
    if (smart_outer_boundaries) {
      // TODO:
      // assert (domain_from_coordbase);
      
      newobss.resize(newbbss.size());
      for (size_t rl=0; rl<newobss.size(); ++rl) {
        newobss.at(rl).resize(newbbss.at(rl).size());
        for (size_t c=0; c<newobss.at(rl).size(); ++c) {
          for (int d=0; d<dim; ++d) {
            assert (mglevel==0);
            rvect const spacing = base_spacing * ipow(mgfact, basemglevel) / ipow(reffact, rl+1);
            ierr = ConvertFromPhysicalBoundary
              (dim, &physical_min[0], &physical_max[0],
               &interior_min[0], &interior_max[0],
               &exterior_min[0], &exterior_max[0], &spacing[0]);
            assert (!ierr);
            newobss.at(rl).at(c)[d][0] = abs(newbbss.at(rl).at(c).lower()[d] - physical_min[d]) < 1.0e-6 * spacing[d];
            if (newobss.at(rl).at(c)[d][0]) {
              rvect lo = newbbss.at(rl).at(c).lower();
              rvect up = newbbss.at(rl).at(c).upper();
              rvect str = newbbss.at(rl).at(c).stride();
              lo[d] = exterior_min[d];
              newbbss.at(rl).at(c) = rbbox(lo, up, str);
            }
            newobss.at(rl).at(c)[d][1] = abs(newbbss.at(rl).at(c).upper()[d] - physical_max[d]) < 1.0e-6 * base_spacing[d] / ipow(reffact, rl);
            if (newobss.at(rl).at(c)[d][1]) {
              rvect lo = newbbss.at(rl).at(c).lower();
              rvect up = newbbss.at(rl).at(c).upper();
              rvect str = newbbss.at(rl).at(c).stride();
              up[d] = exterior_max[d];
              newbbss.at(rl).at(c) = rbbox(lo, up, str);
            }
          }
        }
      }
      
    } else {                    // if ! smart_outer_boundaries
      
      if (strcmp(outerbounds, "") !=0 ) {
        istringstream ob_str (outerbounds);
        try {
          ob_str >> newobss;
        } catch (input_error) {
          CCTK_WARN (0, "Could not parse parameter \"outerbounds\"");
        }
        bool good = newobss.size() == newbbss.size();
        if (good) {
          for (size_t rl=0; rl<newobss.size(); ++rl) {
            good = good && newobss.at(rl).size() == newbbss.at(rl).size();
          }
        }
        if (! good) {
          cout << "coordinates: " << newbbss << endl;
          cout << "outerbounds: " << newobss << endl;
          CCTK_WARN (0, "The parameters \"outerbounds\" and \"coordinates\" must have the same structure");
        }
      } else {
        newobss.resize(newbbss.size());
        for (size_t rl=0; rl<newobss.size(); ++rl) {
          newobss.at(rl).resize(newbbss.at(rl).size());
          for (size_t c=0; c<newobss.at(rl).size(); ++c) {
            newobss.at(rl).at(c) = bbvect(false);
          }
        }
      }

    } // if ! smart_outer_boundaries
    
    if (newbbss.size() < refinement_levels-1) {
      CCTK_VWarn (0, __LINE__, __FILE__, CCTK_THORNSTRING,
                  "The parameter \"coordinates\" must contain at least \"refinement_levels-1\" (here: %d) levels", (int)refinement_levels-1);
    }
    
    for (size_t rl=1; rl<refinement_levels; ++rl) {
      
      vector<ibbox> bbs;
      gh<dim>::cbnds obs;
      
      bbs.reserve (newbbss.at(rl-1).size());
      obs.reserve (newbbss.at(rl-1).size());
      
      for (size_t c=0; c<newbbss.at(rl-1).size(); ++c) {
        rbbox const & ext = newbbss.at(rl-1).at(c);
        bbvect const & ob = newobss.at(rl-1).at(c);
        rvect const spacing = base_spacing / ipow(reffact, rl);
        assert (all(abs(ext.stride() - spacing) < spacing * 1.0e-10));
        ManualCoordinates_OneLevel
          (cctkGH, hh, rl, refinement_levels,
           ext.lower(), ext.upper(), ob, bbs, obs);
      }
      
      // make multiprocessor aware
      gh<dim>::cprocs ps;
      SplitRegions (cctkGH, bbs, obs, ps);
      
      // make multigrid aware
      vector<vector<ibbox> > bbss;
      MakeMultigridBoxes (cctkGH, bbs, obs, bbss);
      
      bbsss.at(rl) = bbss;
      obss.at(rl) = obs;
      pss.at(rl) = ps;
      
    } // for rl
    
    return 1;
  }
  
} // namespace CarpetRegrid

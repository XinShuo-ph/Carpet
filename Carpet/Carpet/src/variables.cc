#include <vector>
#include "dh.hh"
#include "gh.hh"
#include "th.hh"

#include "carpet.hh"

extern "C" {
  static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/variables.cc,v 1.17 2003/07/20 21:03:43 schnetter Exp $";
  CCTK_FILEVERSION(Carpet_Carpet_variables_cc);
}



namespace Carpet {
  
  using namespace std;
  
  
  
  // Handle from CCTK_RegisterGHExtension
  int GHExtension;
  
  // Maximum number of refinement levels
  int maxreflevels;
  
  // Refinement factor
  int reffact;
  
  // Refinement factor on finest grid
  int maxreflevelfact;
  
  // Multigrid levels
  int mglevels;
  
  // Multigrid factor
  int mgfact;
  
  // Multigrid factor on coarsest grid
  int maxmglevelfact;
  
  
  
  // Current position on the grid hierarchy
  int reflevel;
  int mglevel;
  int component;
  
  // refinement factor of current level: ipow(refinement_factor, reflevel)
  int reflevelfact;
  
  // multigrid factor of current level: ipow(multigrid_factor, mglevel)
  int mglevelfact;
  
  // Is this the time for a global mode call?
  bool do_global_mode;
  
  // Is prolongation enabled?
  bool do_prolongate;
  
  // Current times on the refinement levels
  vector<CCTK_REAL> refleveltimes;
  CCTK_REAL delta_time;
  
  
  
  // Data for grid functions
  
  // The grid hierarchy
  gh<dim>* hh;
  th<dim>* tt;
  dh<dim>* dd;

  // Data for everything
  vector<arrdesc> arrdata;	// [group]
  
  // Checksums
  vector<vector<vector<vector<ckdesc> > > > checksums; // [n][rl][tl][c]
  
} // namespace Carpet


#include "variables.hh"

extern "C" {
  static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/variables.cc,v 1.22 2004/05/21 18:16:23 schnetter Exp $";
  CCTK_FILEVERSION(Carpet_Carpet_variables_cc);
}

namespace Carpet {
  
  using namespace std;
  
  
  
  // Handle from CCTK_RegisterGHExtension
  int GHExtension;
  
  // Maximum number of refinement levels
  int maxreflevels;
  
  // Refinement levels
  int reflevels;
  
  // Refinement factor
  int reffact;
  
  // Refinement factor on finest grid
  int maxreflevelfact;
  
  // Base multigrid level
  int basemglevel;
  
  // Multigrid levels
  int mglevels;
  
  // Multigrid factor
  int mgfact;
  
  // Multigrid factor on coarsest grid
  int maxmglevelfact;
  
  // Maps
  int maps;
  
  
  
  // Current position on the grid hierarchy
  int reflevel;
  int mglevel;
  int map;
  int component;
  
  // refinement factor of current level: ipow(refinement_factor, reflevel)
  int reflevelfact;
  
  // multigrid factor of current level: ipow(multigrid_factor, mglevel)
  int mglevelfact;
  
  
  
  // Carpet's GH
  CarpetGH carpetGH;
  
  
  
  // Times and spaces on the refinement levels
  CCTK_REAL global_time;
  vector<vector<CCTK_REAL> > leveltimes; // [mglevel][reflevel]
  CCTK_REAL delta_time;
  
  vector<vect<CCTK_REAL,dim> > origin_space; // [mglevel]
  vect<CCTK_REAL,dim> delta_space;
  
  
  
  // Is this the time for a global mode call?
  bool do_meta_mode;
  bool do_global_mode;
  
  // Is prolongation enabled?
  bool do_prolongate;
  
  
  
  // Data for grid functions
  
  // The grid hierarchy
  vector<gh<dim>*> vhh;         // [map]
  vector<dh<dim>*> vdd;         // [map]
  vector<th<dim>*> vtt;         // [map]

  // Data for the groups
  vector<groupdesc> groupdata;  // [group]
  
  // Data for everything
  vector<vector<arrdesc> > arrdata; // [group][map]
  
} // namespace Carpet

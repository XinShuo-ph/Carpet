#include <cassert>
#include <cstdlib>

#include "cctk.h"

#include "ggf.hh"
#include "gh.hh"

#include "carpet.hh"



namespace Carpet {
  
  using namespace std;
  
  
  
  void CycleTimeLevels (const cGH* cgh)
  {
    Checkpoint ("CycleTimeLevels");
    assert (is_level_mode());
    
    for (int group=0; group<CCTK_NumGroups(); ++group) {
      assert (group<(int)arrdata.size());
      if (CCTK_QueryGroupStorageI(cgh, group)) {
        switch (CCTK_GroupTypeI(group)) {
          
        case CCTK_GF:
          assert (reflevel>=0 and reflevel<reflevels);
          for (int var=0; var<CCTK_NumVarsInGroupI(group); ++var) {
            for (int m=0; m<(int)arrdata.at(group).size(); ++m) {
              assert (m<(int)arrdata.at(group).size());
              assert (var<(int)arrdata.at(group).at(m).data.size());
              for (int c=0; c<arrdata.at(group).at(m).hh->components(reflevel); ++c) {
                arrdata.at(group).at(m).data.at(var)->cycle (reflevel, c, mglevel);
              }
            }
          }
          break;
          
        case CCTK_SCALAR:
        case CCTK_ARRAY:
          if (do_global_mode) {
            for (int var=0; var<CCTK_NumVarsInGroupI(group); ++var) {
              assert (var<(int)arrdata.at(group).at(0).data.size());
              for (int c=0; c<arrdata.at(group).at(0).hh->components(0); ++c) {
                arrdata.at(group).at(0).data.at(var)->cycle (0, c, mglevel);
              }
            }
          }
          break;
          
        default:
          assert (0);
        } // switch grouptype
      } // if storage
    } // for group
  }
  
  
  
  void FlipTimeLevels (const cGH* cgh)
  {
    Checkpoint ("FlipTimeLevels");
    assert (is_level_mode());
    
    for (int group=0; group<CCTK_NumGroups(); ++group) {
      assert (group<(int)arrdata.size());
      if (CCTK_QueryGroupStorageI(cgh, group)) {
        const int num_vars = CCTK_NumVarsInGroupI(group);
        if (num_vars>0) {
          const int var0 = CCTK_FirstVarIndexI(group);
          assert (var0>=0);
          
          switch (CCTK_GroupTypeI(group)) {
            
          case CCTK_GF:
            for (int m=0; m<(int)arrdata.at(group).size(); ++m) {
              for (int var=0; var<CCTK_NumVarsInGroupI(group); ++var) {
                assert (var<(int)arrdata.at(group).at(m).data.size());
                for (int c=0; c<arrdata.at(group).at(m).hh->components(reflevel); ++c) {
                  arrdata.at(group).at(m).data.at(var)->flip (reflevel, c, mglevel);
                }
              }
            }
            break;
            
          case CCTK_SCALAR:
          case CCTK_ARRAY:
            if (do_global_mode) {
              for (int var=0; var<CCTK_NumVarsInGroupI(group); ++var) {
                assert (var<(int)arrdata.at(group).at(0).data.size());
                for (int c=0; c<arrdata.at(group).at(0).hh->components(0); ++c) {
                  arrdata.at(group).at(0).data.at(var)->flip (0, c, mglevel);
                }
              }
            }
            break;
            
          default:
            assert (0);
          } // switch grouptype
          
        } // if num_vars>0
      } // if storage
    } // for group
  }
  
} // namespace Carpet

#include <Requirements.hh>

#include <defs.hh>

#include <cctk.h>
#include <cctk_Parameters.h>
#include <cctk_Functions.h>
#include <cctk_Schedule.h>
#include <cctki_GHExtensions.h>
#include <cctki_Schedule.h>
#include <util_String.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include <clause.hh>
#include <clauses.hh>
#include <util.hh>

using namespace std;

namespace Requirements {
   
  // Rules:
  //
  // 1. Everything that is required by a routine must be provided by
  //    another routine which is scheduled earlier.
  //
  // 2. Things can be provided only once, not multiple times.
  //    Except when they are also required.
  
  class all_clauses_t {
    // TODO: Represent I/O as well?
    typedef std::map<cFunctionData const*, clauses_t const*> clauses_map_t;
    clauses_map_t clauses_map;
    // Singleton
    all_clauses_t(all_clauses_t const&);
    all_clauses_t& operator=(all_clauses_t const&);
  public:
    all_clauses_t() {}
    clauses_t const& get_clauses(cFunctionData const* function_data);
    void remove_clauses(cFunctionData const* function_data);

    // Input/Output helpers
    void input (istream& is);
    void output (ostream& os) const;
  };
  
  clauses_t const& all_clauses_t::
  get_clauses(cFunctionData const* const function_data)
  {
    clauses_map_t::const_iterator const iclauses =
      clauses_map.find(function_data);
    if (iclauses != clauses_map.end()) return *iclauses->second;
    clauses_t* const clauses = new clauses_t;
    clauses->setup(function_data);
    pair<clauses_map_t::const_iterator, bool> const ret =
      clauses_map.insert(clauses_map_t::value_type(function_data, clauses));
    assert(ret.second);
    return *ret.first->second;
  }
  
  void all_clauses_t::
  remove_clauses(cFunctionData const* const function_data)
  {
    clauses_map_t::iterator const iclauses =
      clauses_map.find(function_data);
    if (iclauses != clauses_map.end()) {
      clauses_map.erase(iclauses);
    }
    return;
  }
  
  inline ostream& operator<< (ostream& os, const all_clauses_t& a) {
    a.output(os);
    return os;
  }
      
  void all_clauses_t::output(ostream& os) const
  {
    os << "all_clauses: {" << std::endl;
    for (std::map<cFunctionData const*, clauses_t const*>::const_iterator ti=clauses_map.begin();
         ti!=clauses_map.end();
         ++ti)
    {
      if (ti!=clauses_map.begin()) os << ",";
      os << ti->first->thorn << "::" 
         << ti->first->routine << " in " 
         << ti->first->where << ": " << *ti->second << std::endl;
    }
    os << "}";
  }
  
  all_clauses_t all_clauses;
  
  
  
  // Keep track of which time levels contain good data; modify this
  // while time level cycling; routines should specify how many time
  // levels they require/provide
  
  bool there_was_an_error = false;
  bool there_was_a_warning = false;

  // Represents which have valid information and which do not.
  // This will later be indexed by rl, map etc.
  // Currently only works with unigrid.
  struct gridpoint_t {
    bool interior, boundary, ghostzones, boundary_ghostzones;
    gridpoint_t():
      interior(false), boundary(false), ghostzones(false),
      boundary_ghostzones(false)
    {}

    // Construct an object with information about which points are
    // valid, assuming that a function with the given clause has just
    // been run
    gridpoint_t(clause_t const& clause):
      interior(clause.everywhere or clause.interior),
      boundary(clause.everywhere or clause.boundary),
      ghostzones(clause.everywhere),
      boundary_ghostzones(clause.everywhere or clause.boundary_ghostzones)
    {}
    void check_state(clause_t const& clause,
                     cFunctionData const* function_data,
                     int vi, int rl, int m, int tl) const;
    void report_error(cFunctionData const* function_data,
                      int vi, int rl, int m, int tl,
                      char const* what, char const* where) const;
    void report_warning(cFunctionData const* function_data,
                        int vi, int rl, int m, int tl,
                        char const* what, char const* where) const;
    void update_state(clause_t const& clause);

    // Input/Output helpers
    void input (istream& is);
    void output (ostream& os) const;
  };
  
  inline ostream& operator<< (ostream& os, const gridpoint_t& a) {
    a.output(os);
    return os;
  }


  // Check that all the parts of the grid variables read by a function
  // are valid.  This will be called before the function is executed.
  void gridpoint_t::check_state(clause_t const& clause,
                                cFunctionData const* const function_data,
                                int const vi,
                                int const rl, int const m, int const tl)
    const
  {
    if (not interior) {
      if (clause.everywhere or clause.interior) {
        report_error(function_data, vi, rl, m, tl,
                     "calling function", "interior");
      }
    }
    if (not boundary) {
      if (clause.everywhere or clause.boundary) {
        report_error(function_data, vi, rl, m, tl,
                     "calling function", "boundary");
      }
    }
    if (not ghostzones) {
      if (clause.everywhere) {
        report_error(function_data, vi, rl, m, tl,
                     "calling function", "ghostzones");
      }
    }
    if (not boundary_ghostzones) {
      if (clause.everywhere or clause.boundary_ghostzones) {
        report_error(function_data, vi, rl, m, tl,
                     "calling", "boundary-ghostzones");
      }
    }
  }
  
  void gridpoint_t::report_error(cFunctionData const* const function_data,
                                 int const vi,
                                 int const rl, int const m, int const tl,
                                 char const* const what,
                                 char const* const where) const
  {
    char* const fullname = CCTK_FullName(vi);
    ostringstream state;
    state << "current state: " << *this << std::endl;
    if (function_data) {
      // The error is related to a scheduled function
      CCTK_VWarn(CCTK_WARN_ALERT, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Schedule READS clause not satisfied: "
                 "Function %s::%s in %s: "
                 "Variable %s reflevel=%d map=%d timelevel=%d: "
                 "%s not valid for %s. %s",
                 function_data->thorn, function_data->routine,
                 function_data->where,
                 fullname, rl, m, tl,
                 where, what, state.str().c_str());
    } else {
      // The error is not related to a scheduled function
      CCTK_VWarn(CCTK_WARN_ALERT, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Schedule READS clause not satisfied: "
                 "Variable %s reflevel=%d map=%d timelevel=%d: "
                 "%s not valid for %s. %s",
                 fullname, rl, m, tl,
                 where, what, state.str().c_str());
    }
    free(fullname);
    there_was_an_error = true;
  }
  
  void gridpoint_t::report_warning(cFunctionData const* const function_data,
                                   int const vi,
                                   int const rl, int const m, int const tl,
                                   char const* const what,
                                   char const* const where) const
  {
    char* const fullname = CCTK_FullName(vi);
    ostringstream state;
    state << "current state: " << *this << std::endl;
    if (function_data) {
      // The error is related to a scheduled function
      CCTK_VWarn(CCTK_WARN_ALERT, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Schedule WRITES clause is superfluous: "
                 "Function %s::%s in %s: "
                 "Variable %s reflevel=%d map=%d timelevel=%d: "
                 "%s already valid for %s. %s",
                 function_data->thorn, function_data->routine,
                 function_data->where,
                 fullname, rl, m, tl,
                 where, what, state.str().c_str());
    } else {
      // The error is not related to a scheduled function
      CCTK_VWarn(CCTK_WARN_ALERT, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Schedule WRITES clause already satisfied: "
                 "Variable %s reflevel=%d map=%d timelevel=%d: "
                 "%s already valid for %s. %s",
                 fullname, rl, m, tl,
                 where, what, state.str().c_str());
    }
    free(fullname);
    there_was_a_warning = true;
  }
  
  // Update this object to reflect the fact that some parts of some
  // variables are now valid after a function has been called
  void gridpoint_t::update_state(clause_t const& clause)
  {
    if (clause.everywhere or clause.interior) {
      interior = true;
    }
    if (clause.everywhere or clause.boundary) {
      boundary = true;
    }
    if (clause.everywhere) {
      ghostzones = true;
    }
    if (clause.everywhere or clause.boundary_ghostzones) {
      boundary_ghostzones = true;
    }
  }
  
  void gridpoint_t::output(ostream& os) const
  {
    os << "(";
    if (interior) os << "interior;";
    if (boundary) os << "boundary;";
    if (ghostzones) os << "ghostzones;";
    if (boundary_ghostzones) os << "boundary_ghostzones;";
    os << ")";
  }
  
  
  // The state (valid/invalid) of parts of the grid for all
  // timelevels, maps, refinement levels and variables
  class all_state_t {
    typedef vector<gridpoint_t> timelevels_t;
    typedef vector<timelevels_t> maps_t;
    typedef vector<maps_t> reflevels_t;
    typedef vector<reflevels_t> variables_t;
    variables_t vars;
    variables_t old_vars;     // for regridding
  public:
    void setup(int maps);
    void change_storage(vector<int> const& groups,
                        vector<int> const& timelevels,
                        int reflevel);
    void regrid(int reflevels);
    void recompose(int reflevel, valid::valid_t where);
    void regrid_free();
    void cycle(int reflevel);
    void before_routine(cFunctionData const* function_data,
                        int reflevel, int map, int timelevel) const;
    void after_routine(cFunctionData const* function_data,
                       int reflevel, int map, int timelevel);
    void sync(cFunctionData const* function_data,
              vector<int> const& groups, int reflevel, int timelevel);
    void restrict1(vector<int> const& groups, int reflevel);
    void invalidate(vector<int> const& vars,
                    int reflevel, int map, int timelevel);
    
    // Input/Output helpers
    void input (istream& is);
    void output (ostream& os) const;
  };
  
  all_state_t all_state;
  
  // Ignore requirements in these variables; these variables are
  // always considered valid
  std::vector<bool> ignored_variables;
  
  
  
  static void add_ignored_variable(int const id, const char *const opstring,
                                   void *const callback_arg)
  {
    std::vector<bool>& ivs = *static_cast<std::vector<bool>*>(callback_arg);
    
    ivs.at(id) = true;
  }
  
  void Setup(int const maps)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: Setup maps=%d", maps);
      }
      all_state.setup(maps);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  void all_state_t::setup(int const maps)
  {
    DECLARE_CCTK_PARAMETERS;
    assert(vars.empty());
    vars.resize(CCTK_NumVars());
    for (variables_t::iterator
           ivar = vars.begin(); ivar != vars.end(); ++ivar)
    {
      reflevels_t& rls = *ivar;
      int const vi = &*ivar - &*vars.begin();
      assert(rls.empty());
      // Allocate one refinement level initially
      int const nrls = 1;
      rls.resize(nrls);
      for (reflevels_t::iterator irl = rls.begin(); irl != rls.end(); ++irl) {
        maps_t& ms = *irl;
        assert(ms.empty());
        int const group_type = CCTK_GroupTypeFromVarI(vi);
        int const nms = group_type==CCTK_GF ? maps : 1;
        if (verbose) {
          char* const fullname = CCTK_FullName(vi);
          int const rl = &*irl - &*rls.begin();
          CCTK_VInfo(CCTK_THORNSTRING,
                     "Requirements: Setting up %d maps for variable %s(rl=%d)",
                     nms, fullname, rl);
          free(fullname);
        }
        ms.resize(nms);
        for (maps_t::iterator im = ms.begin(); im != ms.end(); ++im) {
          timelevels_t& tls = *im;
          assert(tls.empty());
          // Not allocating any time levels here
        }
      }
    }
    assert(ignored_variables.empty());
    ignored_variables.resize(CCTK_NumVars());
    CCTK_TraverseString(ignore_these_variables, add_ignored_variable,
                        (void*)&ignored_variables,
                        CCTK_GROUP_OR_VAR);
  }
  
  
  
  void ChangeStorage(vector<int> const& groups,
                     vector<int> const& timelevels,
                     int const reflevel)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        std::ostringstream stream;
        stream << "groups: " << groups << " timelevels: " << timelevels;
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: ChangeStorage reflevel=%d %s", reflevel, stream.str().c_str());
      }
      all_state.change_storage(groups, timelevels, reflevel);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }

  // Update internal data structures when Carpet changes the number of
  // active timelevels for a group
  void all_state_t::change_storage(vector<int> const& groups,
                                   vector<int> const& timelevels,
                                   int const reflevel)
  {
    DECLARE_CCTK_PARAMETERS;
    assert(groups.size() == timelevels.size());
    for (vector<int>::const_iterator
           igi = groups.begin(), itl = timelevels.begin();
         igi != groups.end(); ++igi, ++itl)
    {
      int const gi = *igi;
      int const tl = *itl;
      bool const is_array = CCTK_GroupTypeI(gi) != CCTK_GF;
      int const v0 = CCTK_FirstVarIndexI(gi);
      int const nv = CCTK_NumVarsInGroupI(gi);
      for (int vi=v0; vi<v0+nv; ++vi) {
        reflevels_t& rls = vars.AT(vi);
        int const reflevels = int(rls.size());
        bool const all_rl = reflevel==-1;
        int const min_rl = is_array ? 0 : all_rl ? 0 : reflevel;
        int const max_rl = is_array ? 1 : all_rl ? reflevels : reflevel+1;
        assert(min_rl>=0 and max_rl<=reflevels);
        for (int rl=min_rl; rl<max_rl; ++rl) {
          maps_t& ms = rls.AT(rl);
          for (maps_t::iterator im = ms.begin(); im != ms.end(); ++im) {
            timelevels_t& tls = *im;
            int const ntls = int(tls.size());
            if (tl < ntls) {
              // Free some storage
              if (verbose) {
                char* const fullname = CCTK_FullName(vi);
                int const m = &*im - &*ms.begin();
                CCTK_VInfo(CCTK_THORNSTRING,
                           "Requirements: Decreasing storage to %d time levels for variable %s(rl=%d,m=%d)",
                           tl, fullname, rl, m);
                free(fullname);
              }
              tls.resize(tl);
            } else if (tl > ntls) {
              // Allocate new storage
              if (verbose) {
                char* const fullname = CCTK_FullName(vi);
                int const m = &*im - &*ms.begin();
                CCTK_VInfo(CCTK_THORNSTRING,
                           "Requirements: Increasing storage to %d time levels for variable %s(rl=%d,m=%d)",
                           tl, fullname, rl, m);
                free(fullname);
              }
              // The default constructor for gridpoint_t sets all
              // data to "invalid"
              tls.resize(tl);
            }
          }
        }
      }
    }
  }
  
  
  
  void Regrid(int const reflevels)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: Regrid reflevels=%d", reflevels);
      }
      all_state.regrid(reflevels);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  // Update internal data structures when Carpet regrids
  void all_state_t::regrid(int const reflevels)
  {
    DECLARE_CCTK_PARAMETERS;
    assert(old_vars.empty());
    old_vars.resize(vars.size());
    
    int const ng = CCTK_NumGroups();
    for (int gi=0; gi<ng; ++gi) {
      int const group_type = CCTK_GroupTypeI(gi);
      switch (group_type) {
      case CCTK_SCALAR:
      case CCTK_ARRAY:
        // Grid arrays remain unchanged
        break;
      case CCTK_GF: {
        // Only grid functions are regridded
        int const v0 = CCTK_FirstVarIndexI(gi);
        int const nv = CCTK_NumVarsInGroupI(gi);
        for (int vi=v0; vi<v0+nv; ++vi) {
          reflevels_t& rls = vars.AT(vi);
          reflevels_t& old_rls = old_vars.AT(vi);
          assert(old_rls.empty());
          swap(rls, old_rls);
          // Delete (unused) old refinement levels
          int const old_reflevels = int(old_rls.size());
          for (int rl=reflevels; rl<old_reflevels; ++rl) {
            maps_t& old_ms = old_rls.AT(rl);
            if (verbose) {
              char* const fullname = CCTK_FullName(vi);
              CCTK_VInfo(CCTK_THORNSTRING,
                         "Requirements: Deleting unused refinement level %d of variable %s",
                         rl, fullname);
              free(fullname);
            }
            old_ms.clear();
          }
          // Allocate new refinement levels
          rls.resize(reflevels);
          maps_t const& old_ms = old_rls.AT(0);
          int const old_maps = int(old_ms.size());
          int const maps = old_maps;
          for (int rl=old_reflevels; rl<reflevels; ++rl) {
            maps_t& ms = rls.AT(rl);
            if (verbose) {
              char* const fullname = CCTK_FullName(vi);
              CCTK_VInfo(CCTK_THORNSTRING,
                         "Requirements: Allocating new refinement level %d for variable %s",
                         rl, fullname);
              free(fullname);
            }
            ms.resize(maps);
            for (maps_t::iterator im = ms.begin(); im != ms.end(); ++im) {
              int const crl = 0;
              int const m = &*im - &*ms.begin();
              timelevels_t& tls = *im;
              assert(tls.empty());
              int const ntls = int(old_rls.AT(crl).AT(m).size());
              // Allocate undefined timelevels
              tls.resize(ntls);
            }
          }
        }
        break;
      }
      default:
        assert(0);
      }
    }
  }
  
  
  
  void Recompose(int const reflevel, valid::valid_t const where)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: Recompose reflevel=%d where=%s",
                   reflevel,
                   where == valid::nowhere    ? "nowhere"    :
                   where == valid::interior   ? "interior"   :
                   where == valid::everywhere ? "everywhere" :
                   NULL);
      }
      all_state.recompose(reflevel, where);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  // Update internal data structures when Carpet recomposes
  void all_state_t::recompose(int const reflevel, valid::valid_t const where)
  {
    DECLARE_CCTK_PARAMETERS;
    int const ng = CCTK_NumGroups();
    for (int gi=0; gi<ng; ++gi) {
      int const group_type = CCTK_GroupTypeI(gi);
      switch (group_type) {
      case CCTK_SCALAR:
      case CCTK_ARRAY:
        // Grid arrays remain unchanged
        break;
      case CCTK_GF: {
        // Only grid functions are regridded
        int const v0 = CCTK_FirstVarIndexI(gi);
        int const nv = CCTK_NumVarsInGroupI(gi);
        for (int vi=v0; vi<v0+nv; ++vi) {
          reflevels_t& rls = vars.AT(vi);
          maps_t& ms = rls.AT(reflevel);
          reflevels_t& old_rls = old_vars.AT(vi);
          int const old_reflevels = int(old_rls.size());
          if (reflevel < old_reflevels) {
            // This refinement level is regridded
            maps_t& old_ms = old_rls.AT(reflevel);
            assert(not old_ms.empty());
            assert(ms.empty());
            swap(ms, old_ms);
            for (maps_t::iterator
                   im = ms.begin(), old_im = old_ms.begin();
                 im != ms.end(); ++im, ++old_im)
            {
              timelevels_t& tls = *im;
              if (verbose) {
                char* const fullname = CCTK_FullName(vi);
                int const m = &*im - &*ms.begin();
                CCTK_VInfo(CCTK_THORNSTRING,
                           "Requirements: Recomposing variable %s(rl=%d,m=%d)",
                           fullname, reflevel, m);
                free(fullname);
              }
              for (timelevels_t::iterator
                     itl = tls.begin(); itl != tls.end(); ++itl)
              {
                gridpoint_t& gp = *itl;
                switch (where) {
                case valid::nowhere:
                  gp.interior = false;
                  // fall through
                case valid::interior:
                  // Recomposing sets only the interior
                  gp.boundary = false;
                  gp.ghostzones = false;
                  gp.boundary_ghostzones = false;
                  // fall through
                case valid::everywhere:
                  // do nothing
                  break;
                default:
                  assert(0);
                }
              }
            }
            assert(old_ms.empty());
          } else {
            // This refinement level is new
            assert(where == valid::nowhere);
          }
        }
        break;
      }
      default:
        assert(0);
      }
    }
  }
  
  
  
  void RegridFree()
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: RegridFree");
      }
      all_state.regrid_free();
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  void all_state_t::regrid_free()
  {
    // Ensure all old maps have been recomposed
    for (variables_t::const_iterator
           ivar = old_vars.begin(); ivar != old_vars.end(); ++ivar)
    {
      reflevels_t const& old_rls = *ivar;
      for (reflevels_t::const_iterator
             irl = old_rls.begin(); irl != old_rls.end(); ++irl)
      {
        maps_t const& old_ms = *irl;
        assert(old_ms.empty());
      }
    }
    old_vars.clear();
  }
  
  
  
  void Cycle(int const reflevel)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: Cycle reflevel=%d", reflevel);
      }
      all_state.cycle(reflevel);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  // Update internal data structures when Carpet cycles timelevels
  void all_state_t::cycle(int const reflevel)
  {
    int const ng = CCTK_NumGroups();
    for (int gi=0; gi<ng; ++gi) {
      int const group_type = CCTK_GroupTypeI(gi);
      bool do_cycle;
      switch (group_type) {
      case CCTK_SCALAR:
      case CCTK_ARRAY:
        // Grid arrays are cycled in global mode
        do_cycle = reflevel == -1;
        break;
      case CCTK_GF:
        // Grid functions are cycled in level mode
        do_cycle = reflevel >= 0;
        break;
      default:
        assert(0);
      }
      if (do_cycle) {
        // Translate global mode to refinement level 0
        int const rl = reflevel >= 0 ? reflevel : 0;
        int const v0 = CCTK_FirstVarIndexI(gi);
        int const nv = CCTK_NumVarsInGroupI(gi);
        for (int vi=v0; vi<v0+nv; ++vi) {
          reflevels_t& rls = vars.AT(vi);
          maps_t& ms = rls.AT(rl);
          for (maps_t::iterator im = ms.begin(); im != ms.end(); ++im) {
            timelevels_t& tls = *im;
            int const ntl = int(tls.size());
            if (ntl >= 1) {
              // Only cycle variables with sufficient storage
              for (int tl=ntl-1; tl>0; --tl) {
                tls.AT(tl) = tls.AT(tl-1);
              }
              // The new time level is uninitialised
              // TODO: keep it valid to save time, since MoL will
              // copy it anyway?
              tls.AT(0) = gridpoint_t();
            }
          }
        }
      }
    }
  }
  
  
   
  void BeforeRoutine(cFunctionData const* const function_data,
                     int const reflevel, int const map, int const timelevel)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      all_state.before_routine(function_data, reflevel, map, timelevel);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  // Check that the grid is in the required state before a given
  // function is executed
  void all_state_t::before_routine(cFunctionData const* const function_data,
                                   int const reflevel, int const map,
                                   int const timelevel)
    const
  {
    // Loop over all clauses
    clauses_t const& clauses = all_clauses.get_clauses(function_data);
    for (vector<clause_t>::const_iterator iclause = clauses.reads.begin();
         iclause != clauses.reads.end();
         ++iclause)
    {
      clause_t const& clause = *iclause;
      for (vector<int>::const_iterator ivar = clause.vars.begin();
           ivar != clause.vars.end();
           ++ivar)
      {
        int const vi = *ivar;
        
        if (ignored_variables.at(vi))
            continue;
        
        // Loop over all (refinement levels, maps, time levels)
        reflevels_t const& rls = vars.AT(vi);
        int const reflevels = int(rls.size());
        int min_rl, max_rl;
        if (clause.all_reflevels or reflevel==-1) {
          min_rl = 0; max_rl = reflevels;
        } else {
          min_rl = reflevel; max_rl = min_rl+1;
        }
        for (int rl=min_rl; rl<max_rl; ++rl) {
          
          maps_t const& ms = rls.AT(rl);
          int const maps = int(ms.size());
          int min_m, max_m;
          if (clause.all_maps or map==-1) {
            min_m = 0; max_m = maps;
          } else {
            min_m = map; max_m = min_m+1;
          }
          for (int m=min_m; m<max_m; ++m) {
            
            timelevels_t const& tls = ms.AT(m);
            int const timelevels = int(tls.size());
            assert(timelevel != -1);
            assert(timelevels >= clause.min_num_timelevels());
            // TODO: properly handle timelevels the way enter_local_mode() does
            const int mintl = timelevel == 0 || timelevels == 1 ? 0 : timelevel;
            const int maxtl = timelevel == 0 || timelevels == 1 ? timelevels-1 : timelevel;
            const int tl_of = timelevels > 1 ? timelevel : 0;
            for (int tl=mintl; tl<=maxtl; ++tl) {
              if (timelevel==-1 or clause.active_on_timelevel(tl-tl_of)) {
                gridpoint_t const& gp = tls.AT(tl);
                gp.check_state(clause, function_data, vi, rl, m, tl);
              }
            }
            
          }
        }
        
      }
    }
  }
  
  
  
  void AfterRoutine(cFunctionData const* const function_data,
                    int const reflevel, int const map, int const timelevel)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      all_state.after_routine(function_data, reflevel, map, timelevel);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  // Update internal data structures after a function has been
  // executed to reflect the fact that some variables are now valid
  void all_state_t::after_routine(cFunctionData const* const function_data,
                                  int const reflevel, int const map,
                                  int const timelevel)
  {
    // Loop over all clauses
    clauses_t const& clauses = all_clauses.get_clauses(function_data);
    for (vector<clause_t>::const_iterator iclause = clauses.writes.begin();
         iclause != clauses.writes.end();
         ++iclause)
    {
      clause_t const& clause = *iclause;
      for (vector<int>::const_iterator ivar = clause.vars.begin();
           ivar != clause.vars.end();
           ++ivar)
      {
        int const vi = *ivar;
        
        // Loop over all (refinement levels, maps, time levels)
        reflevels_t& rls = vars.AT(vi);
        int const reflevels = int(rls.size());
        int min_rl, max_rl;
        if (clause.all_reflevels or reflevel==-1) {
          min_rl = 0; max_rl = reflevels;
        } else {
          min_rl = reflevel; max_rl = min_rl+1;
        }
        for (int rl=min_rl; rl<max_rl; ++rl) {
          
          maps_t& ms = rls.AT(rl);
          int const maps = int(ms.size());
          int min_m, max_m;
          if (clause.all_maps or map==-1) {
            min_m = 0; max_m = maps;
          } else {
            min_m = map; max_m = min_m+1;
          }
          for (int m=min_m; m<max_m; ++m) {
            
            timelevels_t& tls = ms.AT(m);
            int const timelevels = int(tls.size());
            assert(timelevel != -1);
            assert(timelevels >= clause.min_num_timelevels());
            // TODO: properly handle timelevels the way enter_local_mode() does
            const int mintl = timelevel == 0 || timelevels == 1 ? 0 : timelevel;
            const int maxtl = timelevel == 0 || timelevels == 1 ? timelevels-1 : timelevel;
            const int tl_of = timelevels > 1 ? timelevel : 0;
            for (int tl=mintl; tl<=maxtl; ++tl) {
              if (timelevel==-1 or clause.active_on_timelevel(tl-tl_of)) {
                gridpoint_t& gp = tls.AT(tl);
                gp.update_state(clause);
              }
            }
            
          }
        }
        
      }
    }
  }
  
  
  
  void Sync(cFunctionData const* const function_data,
            vector<int> const& groups,
            int const reflevel, int const timelevel)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: Sync reflevel=%d timelevel=%d",
                   reflevel, timelevel);
      }
      all_state.sync(function_data, groups, reflevel, timelevel);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  // Update internal data structures when Carpet syncs
  void all_state_t::sync(cFunctionData const* const function_data,
                         vector<int> const& groups,
                         int const reflevel, int const timelevel)
  {
    // Loop over all variables
    for (vector<int>::const_iterator
           igi = groups.begin(); igi != groups.end(); ++igi)
    {
      int const gi = *igi;
      bool do_sync;
      int const group_type = CCTK_GroupTypeI(gi);
      switch (group_type) {
      case CCTK_SCALAR:
      case CCTK_ARRAY:
        // Grid arrays are synced in global mode
        do_sync = reflevel == -1;
        break;
      case CCTK_GF:
        // Grid functions are synced in level mode
        do_sync = reflevel >= 0;
        break;
      default:
        assert(0);
      }
      if (do_sync) {
        // Translate global mode to refinement level 0
        int const rl = reflevel >= 0 ? reflevel : 0;
        int const v0 = CCTK_FirstVarIndexI(gi);
        int const nv = CCTK_NumVarsInGroupI(gi);
        for (int vi=v0; vi<v0+nv; ++vi) {
          if (ignored_variables.at(vi))
            continue;
          
          reflevels_t& rls = vars.AT(vi);
          maps_t& ms = rls.AT(rl);
          int const maps = int(ms.size());
          for (int m=0; m<maps; ++m) {
            timelevels_t& tls = ms.AT(m);
            int const tl = timelevel;
            gridpoint_t& gp = tls.AT(tl);
            
            // Synchronising requires a valid interior
            if (not gp.interior) {
              gp.report_error
                (function_data, vi, rl, m, tl, "synchronising", "interior");
            }
            
            // Synchronising (i.e. prolongating) requires valid data
            // on all time levels of the same map of the next
            // coarser refinement level
            if (rl > 0) {
              int const crl = rl-1;
              maps_t const& cms = rls.AT(crl);
              timelevels_t const& ctls = cms.AT(m);
              // TODO: use prolongation_order_time instead?
              int const ctimelevels = int(ctls.size());
              for (int ctl=0; ctl<ctimelevels; ++ctl) {
                gridpoint_t const& cgp = ctls.AT(ctl);
                if (not (cgp.interior and cgp.boundary and cgp.ghostzones and
                         cgp.boundary_ghostzones))
                {
                  cgp.report_error
                    (function_data, vi, crl, m, ctl,
                     "prolongating", "everywhere");
                }
              }
            }
            
            // Synchronising sets all ghost zones, and sets boundary
            // ghost zones if boundary zones are set
            if (gp.boundary ) {
              if (gp.ghostzones and gp.boundary_ghostzones) {
                gp.report_warning
                  (function_data, vi, rl, m, tl,
                   "synchronising", "ghostzones+boundary_ghostzones");
              }
            } else {
              if (gp.ghostzones) {
                gp.report_warning
                  (function_data, vi, rl, m, tl,
                   "synchronising", "ghostzones");
              }
            }
            gp.ghostzones = true;
            gp.boundary_ghostzones = gp.boundary;
          }
        }
      }
    }
  }
  
  
  
  void Restrict(vector<int> const& groups, int const reflevel)
  {
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      if (verbose) {
        CCTK_VInfo(CCTK_THORNSTRING,
                   "Requirements: Restrict reflevel=%d",
                   reflevel);
      }
      all_state.restrict1(groups, reflevel);
    }
    if (inconsistencies_are_fatal and there_was_an_error) {
      CCTK_WARN(CCTK_WARN_ABORT,
                "Aborting because schedule clauses were not satisfied");
    }
  }
  
  // Update internal data structures when Carpet restricts
  void all_state_t::restrict1(vector<int> const& groups, int const reflevel)
  {
    // Loop over all variables
    for (vector<int>::const_iterator
           igi = groups.begin(); igi != groups.end(); ++igi)
    {
      int const gi = *igi;
      bool do_restrict;
      int const group_type = CCTK_GroupTypeI(gi);
      switch (group_type) {
      case CCTK_SCALAR:
      case CCTK_ARRAY:
        // Grid arrays are synced in global mode
        do_restrict = reflevel == -1;
        break;
      case CCTK_GF:
        // Grid functions are synced in level mode
        do_restrict = reflevel >= 0;
        break;
      default:
        assert(0);
      }
      if (do_restrict) {
        // Translate global mode to refinement level 0
        int const rl = reflevel >= 0 ? reflevel : 0;
        int const v0 = CCTK_FirstVarIndexI(gi);
        int const nv = CCTK_NumVarsInGroupI(gi);
        for (int vi=v0; vi<v0+nv; ++vi) {
          if (ignored_variables.at(vi))
            continue;
          
          reflevels_t& rls = vars.AT(vi);
          int const reflevels = int(rls.size());
          maps_t& ms = rls.AT(rl);
          int const maps = int(ms.size());
          for (int m=0; m<maps; ++m) {
            timelevels_t& tls = ms.AT(m);
            int const tl = 0;
            gridpoint_t& gp = tls.AT(tl);
            
            // Restricting requires a valid interior (otherwise we
            // cannot be sure that all of the interior is valid
            // afterwards)
            if (not gp.interior) {
              gp.report_error
                (NULL, vi, rl, m, tl, "restricting", "interior");
            }
            
            // Restricting requires valid data on the current time
            // level of the same map of the next finer refinement
            // level
            if (rl < reflevels-1) {
              int const frl = rl+1;
              maps_t const& fms = rls.AT(frl);
              timelevels_t const& ftls = fms.AT(m);
              int const ftl = 0;
              gridpoint_t const& fgp = ftls.AT(ftl);
              if (not (fgp.interior and fgp.boundary and fgp.ghostzones and
                       fgp.boundary_ghostzones))
              {
                fgp.report_error
                  (NULL, vi, frl, m, ftl, "restricting", "everywhere");
              }
            }
            
            // Restricting fills (part of) the interior, but leaves
            // ghost zones and boundary zones undefined
            gp.boundary = false;
            gp.ghostzones = false;
            gp.boundary_ghostzones = false;
          }
        }
      }
    }
  }
  
  inline ostream& operator<< (ostream& os, const all_state_t& a) {
    a.output(os);
    return os;
  }

  void all_state_t::output(ostream& os) const
  {
    os << "all_state:" << std::endl;
    os << "vars:" << std::endl;
    os << vars << std::endl;
    os << "old_vars:" << std::endl;
    os << old_vars << std::endl;
  }
  
  template ostream& output (ostream& os, const vector<all_state_t::timelevels_t>& v);
  template ostream& output (ostream& os, const vector<all_state_t::maps_t>& v);
  template ostream& output (ostream& os, const vector<all_state_t::reflevels_t>& v);
  template ostream& output (ostream& os, const vector<all_state_t::variables_t>& v);
  
  
  
  ////////////////////////////////////////////////////////////////////////////
  
  
  // Check that the grid is in the correct state, i.e. all necessary
  // parts are valid, for the "current" function. 
  extern "C" 
  void Carpet_Requirements_CheckReads(CCTK_POINTER_TO_CONST const cctkGH_,
                                      CCTK_INT const nvars,
                                      CCTK_INT const* const varinds,
                                      char const* const clause)
  {
    cGH const* const cctkGH = static_cast<cGH const*>(cctkGH_);
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      // TODO: come up with a scheme to avoid constructing and destroying clauses
      cFunctionData const* const function_data = 
          CCTK_ScheduleQueryCurrentFunction(cctkGH);
      int const reflevel = GetRefinementLevel(cctkGH);
      int const map = GetMap(cctkGH);
      int const timelevel = GetTimeLevel(cctkGH);
      // TODO: design an interface to all_state.before_routine that operates
      //       on indices and clauses directly
      for (int v=0; v<nvars; ++v) { 
        cFunctionData temp_function_data = *function_data;
        char* const fullname = CCTK_FullName(varinds[v]);
        char* reads;
        int const len_written =
          Util_asprintf(&reads, "%s(%s)", fullname, clause);
        assert(len_written > 0);
        temp_function_data.n_WritesClauses = 0;
        temp_function_data.WritesClauses = NULL;
        temp_function_data.n_ReadsClauses = 1;
        temp_function_data.ReadsClauses = (char const**)&reads;
        all_clauses.get_clauses(&temp_function_data);
        BeforeRoutine(&temp_function_data, reflevel, map, timelevel);
        all_clauses.remove_clauses(&temp_function_data);
        free(fullname);
        free(reads);
      }
    }
  }
  
  // Register the fact that certain parts of the grid have been
  // written in certain variables due to executing the "current"
  // function.
  extern "C" 
  void Carpet_Requirements_NotifyWrites(CCTK_POINTER_TO_CONST const cctkGH_,
                                        CCTK_INT const nvars,
                                        CCTK_INT const* const varinds,
                                        char const* const clause)
  {
    cGH const* const cctkGH = static_cast<cGH const*>(cctkGH_);
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      // TODO: come up with a scheme to avoid constructing and destroying clauses
      cFunctionData const* const function_data = 
          CCTK_ScheduleQueryCurrentFunction(cctkGH);
      int const reflevel = GetRefinementLevel(cctkGH);
      int const map = GetMap(cctkGH);
      int const timelevel = GetTimeLevel(cctkGH);
      // TODO: design an interface to all_state.before_routine that operates
      //       on indices and claues directly
      for (int v=0; v<nvars; ++v) { 
        cFunctionData temp_function_data = *function_data;
        char* const fullname = CCTK_FullName(varinds[v]);
        char* writes;
        int const len_written =
          Util_asprintf(&writes, "%s(%s)", fullname, clause);
        assert(len_written > 0);
        temp_function_data.n_WritesClauses = 1;
        temp_function_data.WritesClauses = (char const**)&writes;
        temp_function_data.n_ReadsClauses = 0;
        temp_function_data.ReadsClauses = NULL;
        all_clauses.get_clauses(&temp_function_data);
        AfterRoutine(&temp_function_data, reflevel, map, timelevel);
        all_clauses.remove_clauses(&temp_function_data);
        free(fullname);
        free(writes);
      }
    }
  }
  
  extern "C"
  void Carpet_Requirements_Invalidate(CCTK_POINTER_TO_CONST const cctkGH_,
                                      CCTK_INT const nvars,
                                      CCTK_INT const* const varinds)
  {
    cGH const* const cctkGH = static_cast<cGH const*>(cctkGH_);
    DECLARE_CCTK_PARAMETERS;
    if (check_requirements) {
      vector<int> vars(nvars);
      for (int v=0; v<nvars; ++v) {
        vars.AT(v) = varinds[v];
      }
      int const reflevel = GetRefinementLevel(cctkGH);
      int const map = GetMap(cctkGH);
      int const timelevel = GetTimeLevel(cctkGH);
      all_state.invalidate(vars, reflevel, map, timelevel);
    }
  }
  
  void all_state_t::invalidate(vector<int> const& vars1,
                               int const reflevel, int const map,
                               int const timelevel)
  {
    // Loop over all variables
    for (vector<int>::const_iterator
           ivi = vars1.begin(); ivi != vars1.end(); ++ivi)
    {
      int const vi = *ivi;
      reflevels_t& rls = vars.AT(vi);
      maps_t& ms = rls.AT(reflevel);
      timelevels_t& tls = ms.AT(map);
      // This time level is uninitialised
      tls.AT(timelevel) = gridpoint_t();
    }
  }
  
  
  
  ////////////////////////////////////////////////////////////////////////////
  
  
  
  // scheduled routines to handle boundary and symmetry conditions
  extern "C"
  void CarpetCheckReadsBeforeBoundary(CCTK_ARGUMENTS)
  {
    DECLARE_CCTK_ARGUMENTS;
    int num_vars, err;
    vector<CCTK_INT> vars, faces, widths, tables;

    num_vars = Boundary_SelectedGVs(cctkGH, 0, NULL, NULL, NULL, NULL, NULL);
    if (num_vars < 0) {
      CCTK_VWarn(0, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Error retrieving number of selected GVs: %d", num_vars);
    }
    vars.resize(num_vars);
    faces.resize(num_vars);
    widths.resize(num_vars);
    tables.resize(num_vars);

    /* get selected vars for all bc */
    err = Boundary_SelectedGVs(cctkGH, num_vars, &vars[0], &faces[0], &widths[0], &tables[0],
                                    NULL);
    if (err<0) {
      CCTK_VWarn(0, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Error in Boundary_SelectedGVs for all boundary conditions");
    } else if (err != num_vars) {
      CCTK_VWarn(0, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Boundary_SelectedGVs returned %d selected variables for "
                 "all boundary conditions, but %d expected\n", err,
                 num_vars);
    }

    Requirements_CheckReads(cctkGH, num_vars, &vars[0], "interior");
  }

  extern "C"
  void CarpetNotifyWritesAfterBoundary(CCTK_ARGUMENTS)
  {
    DECLARE_CCTK_ARGUMENTS;
    int num_vars, err;
    vector<CCTK_INT> vars, faces, widths, tables;

    num_vars = Boundary_SelectedGVs(cctkGH, 0, NULL, NULL, NULL, NULL, NULL);
    if (num_vars < 0) {
      CCTK_VWarn(0, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Error retrieving number of selected GVs: %d", num_vars);
    }
    vars.resize(num_vars);
    faces.resize(num_vars);
    widths.resize(num_vars);
    tables.resize(num_vars);

    /* get selected vars for all bc */
    err = Boundary_SelectedGVs(cctkGH, num_vars, &vars[0], &faces[0], &widths[0], &tables[0],
                                    NULL);
    if (err<0) {
      CCTK_VWarn(0, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Error in Boundary_SelectedGVs for all boundary conditions");
    } else if (err != num_vars) {
      CCTK_VWarn(0, __LINE__, __FILE__, CCTK_THORNSTRING,
                 "Boundary_SelectedGVs returned %d selected variables for "
                 "all boundary conditions, but %d expected\n", err,
                 num_vars);
    }

    Requirements_NotifyWrites(cctkGH, num_vars, &vars[0], "boundary;boundary_ghostzones");
  }
  
  
  
} // namespace Requirements
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include "cctk.h"
#include "cctk_Arguments.h"
#include "cctk_Functions.h"
#include "cctk_Parameters.h"

#include "CactusBase/IOUtil/src/ioGH.h"
#include "CactusBase/IOUtil/src/ioutil_Utils.h"

#include "carpet.hh"



// That's a hack
namespace Carpet {
  void UnsupportedVarType (const int vindex);
}



namespace CarpetIOBasic {

  using namespace std;
  using namespace Carpet;



  // Output field widths and precisions
  int const iter_width    =  9;
  int const time_width    =  9;
  int const time_prec     =  3;
  
  int const int_width     = 11;
  int const real_width    = 13;
  int const real_prec     =  8;
  int const real_prec_sci =  6;

  double const real_min = 1.0e-8;
  double const real_max = 1.0e+3;



  // Definition of local types
  struct info {
    string reduction;
    int handle;
  };



  // Registered functions
  void* SetupGH (tFleshConfig* fc, int convLevel, cGH* cctkGH);
  int OutputGH (const cGH* cctkGH);

  // Internal functions
  bool TimeToOutput (const cGH* cctkGH);
  void OutputHeader (const cGH* cctkGH);
  void OutputVar (const cGH* cctkGH, int vindex);
  void CheckSteerableParameters (const cGH * cctkGH);

  void
  ExamineVariable (int vindex, bool & isint, int & numcomps, bool & isscalar);
  vector<string> ParseReductions (char const * credlist);
  template <typename T> bool UseScientificNotation (T const & x);



  // Definition of members
  int output_count;
  int last_output;

  /* CarpetBasic GH extension structure */
  struct
  {
    /* list of variables to output */
    char *out_vars;

    /* stop on I/O parameter parsing errors ? */
    int stop_on_parse_errors;

    /* I/O request description list (for all variables) */
    ioRequest **requests;
  } IOparameters;



  // Special numeric and output routines for complex numbers

  template <typename T> T myabs (const T& val) { return abs(val); }

#ifdef HAVE_CCTK_COMPLEX8
  CCTK_REAL4 myabs (const CCTK_COMPLEX8& val)
  {
    return CCTK_Cmplx8Abs(val);
  }

  ostream& operator<< (ostream& os, const CCTK_COMPLEX8& val)
  {
    int const w = os.width();
    os << CCTK_Cmplx8Real(val);
    os << " ";
    os.width(w);
    os << CCTK_Cmplx8Imag(val);
    return os;
  }
#endif

#ifdef HAVE_CCTK_COMPLEX16
  CCTK_REAL8 myabs (const CCTK_COMPLEX16& val)
  {
    return CCTK_Cmplx16Abs(val);
  }

  ostream& operator<< (ostream& os, const CCTK_COMPLEX16& val)
  {
    int const w = os.width();
    os << CCTK_Cmplx16Real(val);
    os << " ";
    os.width(w);
    os << CCTK_Cmplx16Imag(val);
    return os;
  }
#endif

#ifdef HAVE_CCTK_COMPLEX32
  CCTK_REAL16 myabs (const CCTK_COMPLEX32& val)
  {
    return CCTK_Cmplx32Abs(val);
  }

  ostream& operator<< (ostream& os, const CCTK_COMPLEX32& val)
  {
    int const w = os.width();
    os << CCTK_Cmplx32Real(val);
    os << " ";
    os.width(w);
    os << CCTK_Cmplx32Imag(val);
    return os;
  }
#endif



  extern "C" void
  CarpetIOBasicStartup ()
  {
    CCTK_RegisterBanner ("AMR info I/O provided by CarpetIOBasic");

    int GHExtension = CCTK_RegisterGHExtension("CarpetIOBasic");
    CCTK_RegisterGHExtensionSetupGH (GHExtension, SetupGH);

    int IOMethod = CCTK_RegisterIOMethod ("CarpetIOBasic");
    CCTK_RegisterIOMethodOutputGH (IOMethod, OutputGH);
  }



  extern "C" void
  CarpetIOBasicInit (CCTK_ARGUMENTS)
  {
    DECLARE_CCTK_ARGUMENTS;

    *this_iteration = 0;
    *last_output_iteration = 0;
    *last_output_time = cctkGH->cctk_time;
  }



  void*
  SetupGH (tFleshConfig* const fc, int const convLevel, cGH* const cctkGH)
  {
    DECLARE_CCTK_PARAMETERS;
    const void *dummy;

    dummy = &fc;
    dummy = &convLevel;
    dummy = &cctkGH;
    dummy = &dummy;

    // Truncate all files if this is not a restart
    const int numvars = CCTK_NumVars ();
    output_count = 0;

    // No iterations have yet been output
    last_output = -1;

    IOparameters.requests = (ioRequest **) calloc (numvars, sizeof(ioRequest*));
    IOparameters.out_vars = strdup ("");

    // initial I/O parameter check
    IOparameters.stop_on_parse_errors = strict_io_parameter_check;
    CheckSteerableParameters (cctkGH);
    IOparameters.stop_on_parse_errors = 0;

    // We register only once, ergo we get only one handle.  We store
    // that statically, so there is no need to pass anything to
    // Cactus.
    return NULL;
  }



  int
  OutputGH (const cGH * const cctkGH)
  {
    DECLARE_CCTK_ARGUMENTS;
    DECLARE_CCTK_PARAMETERS;
    
    if (TimeToOutput (cctkGH)) {
      
      int oldprec;
      ios_base::fmtflags oldflags;
      if (CCTK_MyProc(cctkGH) == 0) {
        oldprec = cout.precision();
        oldflags = cout.flags();
      }
      
      if (output_count ++ % 20 == 0) {
        // Print the header
        OutputHeader (cctkGH);
      }
      
      if (CCTK_MyProc(cctkGH) == 0) {
        cout << setw(iter_width) << setfill(' ') << cctk_iteration
             << " "
             << setw(time_width) << setfill(' ')
             << fixed << setprecision(time_prec) << cctk_time;
      }
      
      int const numvars = CCTK_NumVars();
      for (int vindex=0; vindex<numvars; ++vindex) {
        if (IOparameters.requests[vindex]) {
          OutputVar (cctkGH, vindex);
        }
      }
      
      if (CCTK_MyProc(cctkGH) == 0) {
        cout << endl << flush;
      }
      
      last_output = cctk_iteration;
      
      if (CCTK_MyProc(cctkGH) == 0) {
        cout.precision (oldprec);
        cout.setf (oldflags);
      }
      
    } // if time to output
    
    return 0;
  }
  
  
  
  void
  OutputHeader (const cGH * const cctkGH)
  {
    DECLARE_CCTK_PARAMETERS;
    
    if (CCTK_MyProc(cctkGH) == 0) {
      
      // Find the set of desired reductions
      vector<string> const reductions = ParseReductions (outInfo_reductions);
      int const numreds = reductions.size();
      
      int const numvars = CCTK_NumVars();
      
      // The header consists of four lines:
      //    pass 0: print separator
      //    pass 1: print variable name
      //    pass 2: print reduction operation
      //    pass 3: print separator
      for (int pass=0; pass<4; ++pass) {
        
        // Print iteration and time
        switch (pass) {
        case 0:
        case 3:
          cout << "-------------------";
          break;
        case 1:
          cout << "Iteration      Time";
          break;
        case 2:
          cout << "                   ";
          break;
        default:
          assert (0);
        }
        
        // Loop over all variables that should be output
        for (int vindex=0; vindex<numvars; ++vindex) {
          if (IOparameters.requests[vindex]) {
            
            bool isint;
            int numcomps;
            bool isscalar;
            ExamineVariable (vindex, isint, numcomps, isscalar);
            
            char * cfullname = CCTK_FullName (vindex);
            string const fullname (cfullname);
            free (cfullname);
            
            // Print a vertical separator
            switch (pass) {
            case 0:
            case 3:
              cout << "--";
              break;
            case 1:
            case 2:
              cout << " |";
              break;
            default:
              assert (0);
            }
            
            int const width = isint ? int_width : real_width;
            
            int const mynumreds = isscalar ? 1 : numreds;
            
            // Print the entry
            switch (pass) {
            case 0:
            case 3:
              {
                size_t const numchars = (width+1)*numcomps*numreds;
                cout << setw(numchars) << setfill('-') << "";
              }
              break;
            case 1:
              {
                size_t const numchars = (width+1)*numcomps*numreds-1;
                if (fullname.length() > numchars) {
                  int begin = fullname.length() - (numchars-1);
                  cout << " *" << fullname.substr(begin);
                } else {
                  cout << " " << setw(numchars) << setfill(' ') << fullname;
                }
              }
              break;
            case 2:
              if (isscalar) {
                int const numchars = (width+1)*numcomps;
                cout << setw(numchars) << setfill(' ') << "";
              } else {
                for (int red=0; red<mynumreds; ++red) {
                  int const numchars = (width+1)*numcomps-1;
                  cout << " " << setw(numchars) << setfill(' ')
                       << reductions.at(red).substr(0,numchars);
                }
              }
              break;
            default:
              assert (0);
            }
            
          } // if want output
        }   // for vindex
        
        cout << endl;
        
      } // pass
      
    } // if on root processor
  }
  
  
  
  void
  OutputVar (const cGH * const cctkGH,
             int const n)
  {
    DECLARE_CCTK_ARGUMENTS;
    DECLARE_CCTK_PARAMETERS;
    
    assert (is_level_mode());
    
    const int group = CCTK_GroupIndexFromVarI (n);
    assert (group>=0 and group<(int)Carpet::arrdata.size());
    const int n0 = CCTK_FirstVarIndexI(group);
    assert (n0>=0 and n0<CCTK_NumVars());
    const int var = n - n0;
    assert (var>=0 and var<CCTK_NumVarsInGroupI(group));
    const int num_tl = CCTK_NumTimeLevelsFromVarI(n);
    assert (num_tl>=1);
    
    // Check for storage
    if (not CCTK_QueryGroupStorageI(cctkGH, group)) {
      char * fullname = CCTK_FullName (n);
      CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                  "Cannot output variable \"%s\" because it has no storage",
                  fullname);
      free (fullname);
    }
    
    assert (do_global_mode);
    
    const int vartype = CCTK_VarTypeI(n);
    assert (vartype >= 0);
    
    // Get grid hierarchy extentsion from IOUtil
    const ioGH * const iogh = (const ioGH *)CCTK_GHExtension (cctkGH, "IO");
    assert (iogh);
    
    // Find the set of desired reductions
    vector<string> const reductionstrings
      = ParseReductions (outInfo_reductions);
    list<info> reductions;
    for (vector<string>::const_iterator ireduction = reductionstrings.begin();
         ireduction != reductionstrings.end();
         ++ireduction)
    {
      int const handle = CCTK_ReductionHandle ((*ireduction).c_str());
      if (handle < 0) {
        CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                    "Reduction operator \"%s\" does not exist (maybe there is no reduction thorn active?)",
                    (*ireduction).c_str());
      } else {
        info i;
        i.reduction = * ireduction;
        i.handle = handle;
        reductions.push_back (i);
      }
    }
    
    bool isint;
    int numcomps;
    bool isscalar;
    ExamineVariable (n, isint, numcomps, isscalar);
    
    // Output in global mode
    BEGIN_GLOBAL_MODE(cctkGH) {
      
      // Print vertical separator
      if (CCTK_MyProc(cctkGH) == 0) {
        cout << " |";
      }
      
      int const width = isint ? int_width : real_width;
      
      if (isscalar) {
        
        if (CCTK_MyProc(cctkGH) == 0) {
          
          cout << " " << setw(width);
          
          void const * const vardataptr = CCTK_VarDataPtrI (cctkGH, 0, n);
          assert (vardataptr);

          switch (vartype) {
#define TYPECASE(N,T)                                                   \
            case N:                                                     \
              {                                                         \
                T const val = * static_cast <T const *> (vardataptr);   \
                if (not isint) {                                        \
                  if (UseScientificNotation (val)) {                    \
                    cout << scientific << setprecision(real_prec_sci);  \
                  } else {                                              \
                    cout << fixed << setprecision(real_prec);           \
                  }                                                     \
                }                                                       \
                cout << val;                                            \
              }                                                         \
            break;
#include "carpet_typecase.hh"
#undef TYPECASE
          default:
            UnsupportedVarType (n);
          }

        } // if on root processor

      } else {

        for (list<info>::const_iterator ireduction = reductions.begin();
             ireduction != reductions.end();
             ++ireduction)
        {
          string const reduction = ireduction->reduction;
        
          int const handle = ireduction->handle;
        
          union {
#define TYPECASE(N,T) T var_##T;
#include "carpet_typecase.hh"
#undef TYPECASE
          } result;
        
          int const ierr
            = CCTK_Reduce (cctkGH, 0, handle, 1, vartype, &result, 1, n);
          assert (not ierr);
        
          if (CCTK_MyProc(cctkGH) == 0) {

            cout << " " << setw(width);
          
            switch (vartype) {
#define TYPECASE(N,T)                                                   \
              case N:                                                   \
                {                                                       \
                  T const val = result.var_##T;                         \
                  if (not isint) {                                      \
                    if (UseScientificNotation (val)) {                  \
                      cout << scientific << setprecision(real_prec_sci); \
                    } else {                                            \
                      cout << fixed << setprecision(real_prec);         \
                    }                                                   \
                  }                                                     \
                  cout << val;                                          \
                }                                                       \
              break;
#include "carpet_typecase.hh"
#undef TYPECASE
            default:
              UnsupportedVarType (n);
            }

          } // if on root processor
        
        } // for reductions
      
      } // not isscalar
      
    } END_GLOBAL_MODE;
  }



  bool
  TimeToOutput (const cGH * const cctkGH)
  {
    DECLARE_CCTK_ARGUMENTS;
    DECLARE_CCTK_PARAMETERS;

    if (not do_global_mode) return false;

    CheckSteerableParameters (cctkGH);

    // check if output for any variable was requested
    bool do_output = false;
    int const numvars = CCTK_NumVars();
    for (int vindex=0; vindex<numvars; ++vindex) {
      do_output = do_output or IOparameters.requests[vindex];
    }
    if (not do_output) return false;

    // check whether to output at this iteration
    bool output_this_iteration;

    const char* myoutcriterion = outInfo_criterion;
    if (CCTK_EQUALS(myoutcriterion, "default")) {
      myoutcriterion = out_criterion;
    }

    if (CCTK_EQUALS (myoutcriterion, "never")) {

      // Never output
      output_this_iteration = false;

    } else if (CCTK_EQUALS (myoutcriterion, "iteration")) {

      int myoutevery = outInfo_every;
      if (myoutevery == -2) {
        myoutevery = out_every;
      }
      if (myoutevery <= 0) {
        // output is disabled
        output_this_iteration = false;
      } else if (cctk_iteration == *this_iteration) {
        // we already decided to output this iteration
        output_this_iteration = true;
      } else if (cctk_iteration >= *last_output_iteration + myoutevery) {
        // it is time for the next output
        output_this_iteration = true;
        *last_output_iteration = cctk_iteration;
        *this_iteration = cctk_iteration;
      } else {
        // we want no output at this iteration
        output_this_iteration = false;
      }

    } else if (CCTK_EQUALS (myoutcriterion, "time")) {

      CCTK_REAL myoutdt = outInfo_dt;
      if (myoutdt == -2) {
        myoutdt = out_dt;
      }
      if (myoutdt < 0) {
        // output is disabled
        output_this_iteration = false;
      } else if (myoutdt == 0) {
        // output all iterations
        output_this_iteration = true;
      } else if (cctk_iteration == *this_iteration) {
        // we already decided to output this iteration
        output_this_iteration = true;
      } else if (cctk_time / cctk_delta_time
                 >= (*last_output_time + myoutdt) / cctk_delta_time - 1.0e-12) {
        // it is time for the next output
        output_this_iteration = true;
        *last_output_time = cctk_time;
        *this_iteration = cctk_iteration;
      } else {
        // we want no output at this iteration
        output_this_iteration = false;
      }

    } else {

      assert (0);

    } // select output criterion

    if (not output_this_iteration) return false;



    assert (last_output != cctk_iteration);

    assert (last_output < cctk_iteration);

    // Should be output during this iteration
    return true;
  }



  void
  CheckSteerableParameters (const cGH *const cctkGH)
  {
    DECLARE_CCTK_PARAMETERS;

    // re-parse the 'IOBasic::outInfo_vars' parameter if it has changed
    if (strcmp (outInfo_vars, IOparameters.out_vars)) {
      IOUtil_ParseVarsForOutput (cctkGH, CCTK_THORNSTRING,
                                 "IOBasic::outInfo_vars",
                                 IOparameters.stop_on_parse_errors,
                                 outInfo_vars, -1, IOparameters.requests);

      // save the last setting of 'IOBasic::outInfo_vars' parameter
      free (IOparameters.out_vars);
      IOparameters.out_vars = strdup (outInfo_vars);
    }
  }
  
  
  
  // Parse the set of reductions
  vector<string>
  ParseReductions (char const * const credlist)
  {
    string const redlist (credlist);
    vector<string> reductions;
    string::const_iterator p = redlist.begin();
    while (p!=redlist.end()) {
      // Skip white space
      while (p!=redlist.end() and isspace(*p)) ++p;
      // Exit if end of reductions is reached
      if (p==redlist.end()) break;
      // Mark beginning of reduction entry
      string::const_iterator const start = p;
      // Walk over reduction entry
      while (p!=redlist.end() and not isspace(*p)) ++p;
      // Mark end of reduction entry
      string::const_iterator const end = p;
      // Remember reduction
      string const reduction (start, end);
      reductions.push_back (reduction);
    }
    return reductions;
  }
  
  
  
  void
  ExamineVariable (int const vindex,
                   bool & isint, int & numcomps, bool & isscalar)
  {
    switch (CCTK_VarTypeI (vindex)) {
    case CCTK_VARIABLE_BYTE:
    case CCTK_VARIABLE_INT:
    case CCTK_VARIABLE_INT1:
    case CCTK_VARIABLE_INT2:
    case CCTK_VARIABLE_INT4:
    case CCTK_VARIABLE_INT8:
      isint = true;
      numcomps = 1;
      break;
    case CCTK_VARIABLE_REAL:
    case CCTK_VARIABLE_REAL4:
    case CCTK_VARIABLE_REAL8:
    case CCTK_VARIABLE_REAL16:
      isint = false;
      numcomps = 1;
      break;
    case CCTK_VARIABLE_COMPLEX:
    case CCTK_VARIABLE_COMPLEX8:
    case CCTK_VARIABLE_COMPLEX16:
    case CCTK_VARIABLE_COMPLEX32:
      isint = false;
      numcomps = 2;
      break;
    default:
      assert (0);
    }
    
    switch (CCTK_GroupTypeFromVarI (vindex)) {
    case CCTK_SCALAR:
      isscalar = true;
      break;
    case CCTK_ARRAY:
    case CCTK_GF:
      isscalar = false;
      break;
    default:
      assert (0);
    }
  }
  
  
  
  template <typename T>
  bool
  UseScientificNotation (T const & x)
  {
    double const xa = myabs (x);
    return xa != 0 and (xa < real_min or xa >= real_max);
  }
  
} // namespace CarpetIOBasic

#include <assert.h>

#include <map>
#include <sstream>

#include "util_Table.h"
#include "cctk.h"
#include "cctk_Arguments.h"
#include "cctk_Parameters.h"
#include "cctk_Version.h"

#include "CactusBase/IOUtil/src/ioGH.h"
#include "CactusBase/IOUtil/src/ioutil_CheckpointRecovery.h"

#include "CarpetIOHDF5.hh"


namespace CarpetIOHDF5
{

using namespace std;
using namespace Carpet;


// Variable definitions

// when was the last checkpoint written ?
static int last_checkpoint_iteration = -1;


// registered GH extension setup routine
static void* SetupGH (tFleshConfig* const fleshconfig,
                      const int convLevel, cGH* const cctkGH);

// callbacks for CarpetIOHDF5's I/O method
static int OutputGH (const cGH* const cctkGH);
static int OutputVarAs (const cGH* const cctkGH, const char* const varname,
                        const char* const alias);
static int TimeToOutput (const cGH* const cctkGH, const int vindex);
static int TriggerOutput (const cGH* const cctkGH, const int vindex);

// general checkpoint routine
static int Checkpoint (const cGH* const cctkGH, int called_from);

// callback for I/O parameter parsing routine
static void GetVarIndex (int vindex, const char* optstring, void* arg);

static void CheckSteerableParameters (const cGH *const cctkGH,
                                      CarpetIOHDF5GH *myGH);
static void WarnAboutDeprecatedParameters (void);

//////////////////////////////////////////////////////////////////////////////
// public routines
//////////////////////////////////////////////////////////////////////////////

int CarpetIOHDF5_Startup (void)
{
  CCTK_RegisterBanner ("AMR HDF5 I/O provided by CarpetIOHDF5");

  const int GHExtension = CCTK_RegisterGHExtension (CCTK_THORNSTRING);
  CCTK_RegisterGHExtensionSetupGH (GHExtension, SetupGH);

  // warn about deprecated parameters if they are still being used
  if (CCTK_ParameterQueryTimesSet ("out_unchunked", CCTK_THORNSTRING) > 0) {
    CCTK_WARN (CCTK_WARN_ALERT,
      "You set the parameter 'IOHDF5::out_unchunked'"
      " in your parfile. This parameter is deprecated and should not be used"
      " anymore. Use 'IO::out_unchunked' instead.");
  }
  if (CCTK_ParameterQueryTimesSet ("in_dir", CCTK_THORNSTRING) > 0) {
    CCTK_WARN (CCTK_WARN_ALERT,
      "You set the parameter 'IOHDF5::in_dir'"
      " in your parfile. This parameter is deprecated and should not be used"
      " anymore. Use 'IO::filereader_dir' instead.");
  }
  if (CCTK_ParameterQueryTimesSet ("in_vars", CCTK_THORNSTRING) > 0) {
    CCTK_WARN (CCTK_WARN_ALERT,
      "You set the parameter 'IOHDF5::in_vars'"
      " in your parfile. This parameter is deprecated and should not be used"
      " anymore. Use 'IO::filereader_vars' instead.");
  }

  return (0);
}


int CarpetIOHDF5_Init (const cGH* const cctkGH)
{
  DECLARE_CCTK_ARGUMENTS;

  *this_iteration = -1;
  *next_output_iteration = 0;
  *next_output_time = cctk_time;

  return (0);
}


int CarpetIOHDF5_InitialDataCheckpoint (const cGH* const cctkGH)
{
  DECLARE_CCTK_PARAMETERS;


  if (not CCTK_Equals (verbose, "none")) {
    CCTK_INFO ("---------------------------------------------------------");
    CCTK_INFO ("Dumping initial data checkpoint");
    CCTK_INFO ("---------------------------------------------------------");
  }
  int retval = Checkpoint (cctkGH, CP_INITIAL_DATA);

  return (retval);
}


int CarpetIOHDF5_EvolutionCheckpoint (const cGH* const cctkGH)
{
  int retval = 0;
  DECLARE_CCTK_PARAMETERS;


  if (checkpoint and
    ((checkpoint_every > 0 and cctkGH->cctk_iteration % checkpoint_every == 0) or
     checkpoint_next)) {
    if (not CCTK_Equals (verbose, "none")) {
      CCTK_INFO ("---------------------------------------------------------");
      CCTK_VInfo (CCTK_THORNSTRING, "Dumping periodic checkpoint at "
                  "iteration %d", cctkGH->cctk_iteration);
      CCTK_INFO ("---------------------------------------------------------");
    }

    retval = Checkpoint (cctkGH, CP_EVOLUTION_DATA);

    if (checkpoint_next) {
      CCTK_ParameterSet ("checkpoint_next", CCTK_THORNSTRING, "no");
    }
  }

  return (retval);
}


int CarpetIOHDF5_TerminationCheckpoint (const cGH *const GH)
{
  int retval = 0;
  DECLARE_CCTK_PARAMETERS;


  if (checkpoint and checkpoint_on_terminate) {
    if (last_checkpoint_iteration < GH->cctk_iteration) {
      if (not CCTK_Equals (verbose, "none")) {
        CCTK_INFO ("---------------------------------------------------------");
        CCTK_VInfo (CCTK_THORNSTRING, "Dumping termination checkpoint at "
                    "iteration %d", GH->cctk_iteration);
        CCTK_INFO ("---------------------------------------------------------");
      }

      retval = Checkpoint (GH, CP_EVOLUTION_DATA);
    } else if (not CCTK_Equals (verbose, "none")) {
      CCTK_INFO ("---------------------------------------------------------");
      CCTK_VInfo (CCTK_THORNSTRING, "Termination checkpoint already dumped "
                  "as last evolution checkpoint at iteration %d",
                  last_checkpoint_iteration);
      CCTK_INFO ("---------------------------------------------------------");
    }
  }

  return (retval);
}


hid_t CCTKtoHDF5_Datatype (const cGH* const cctkGH,
                           int cctk_type, bool single_precision)
{
  hid_t retval;

  const CarpetIOHDF5GH *myGH =
    (CarpetIOHDF5GH *) CCTK_GHExtension (cctkGH, CCTK_THORNSTRING);

  switch (cctk_type) {

    case CCTK_VARIABLE_CHAR:      retval = HDF5_CHAR; break;

    case CCTK_VARIABLE_INT:       retval = HDF5_INT;
#ifdef CCTK_INT2
                                  if (single_precision) {
                                    retval = H5T_NATIVE_SHORT;
                                  }
#endif
                                  break;
#ifdef CCTK_INT1
    case CCTK_VARIABLE_INT1:      retval = H5T_NATIVE_CHAR; break;
#endif
#ifdef CCTK_INT2
    case CCTK_VARIABLE_INT2:      retval = H5T_NATIVE_SHORT; break;
#endif
#ifdef CCTK_INT4
    case CCTK_VARIABLE_INT4:      retval = H5T_NATIVE_INT; break;
#endif
#ifdef CCTK_INT8
    case CCTK_VARIABLE_INT8:      retval = H5T_NATIVE_LLONG; break;
#endif

    case CCTK_VARIABLE_REAL:      retval = HDF5_REAL;
#ifdef CCTK_REAL4
                                  if (single_precision) {
                                    retval = H5T_NATIVE_FLOAT;
                                  }
#endif
                                  break;

    case CCTK_VARIABLE_COMPLEX:   retval = myGH->HDF5_COMPLEX;
#ifdef CCTK_REAL4
                                  if (single_precision) {
                                    retval = myGH->HDF5_COMPLEX8;
                                  }
#endif
                                  break;

#ifdef CCTK_REAL4
    case CCTK_VARIABLE_REAL4:     retval = H5T_NATIVE_FLOAT; break;
    case CCTK_VARIABLE_COMPLEX8:  retval = myGH->HDF5_COMPLEX8; break;
#endif
#ifdef CCTK_REAL8
    case CCTK_VARIABLE_REAL8:     retval = H5T_NATIVE_DOUBLE; break;
    case CCTK_VARIABLE_COMPLEX16: retval = myGH->HDF5_COMPLEX16; break;
#endif
#ifdef CCTK_REAL16
    case CCTK_VARIABLE_REAL16:    retval = H5T_NATIVE_LDOUBLE; break;
    case CCTK_VARIABLE_COMPLEX32: retval = myGH->HDF5_COMPLEX32; break;
#endif

    default: CCTK_VWarn (0, __LINE__, __FILE__, CCTK_THORNSTRING,
                         "Unsupported CCTK variable datatype %d", cctk_type);
             retval = -1;
  }

  return (retval);
}


//////////////////////////////////////////////////////////////////////////////
// private routines
//////////////////////////////////////////////////////////////////////////////
static void* SetupGH (tFleshConfig* const fleshconfig,
                      const int convLevel, cGH* const cctkGH)
{
  DECLARE_CCTK_PARAMETERS;


  // register CarpetIOHDF5's routines as a new I/O method
  const int IOMethod = CCTK_RegisterIOMethod ("IOHDF5");
  CCTK_RegisterIOMethodOutputGH (IOMethod, OutputGH);
  CCTK_RegisterIOMethodOutputVarAs (IOMethod, OutputVarAs);
  CCTK_RegisterIOMethodTimeToOutput (IOMethod, TimeToOutput);
  CCTK_RegisterIOMethodTriggerOutput (IOMethod, TriggerOutput);

  if (not CCTK_Equals (verbose, "none")) {
    CCTK_INFO ("I/O Method 'IOHDF5' registered: AMR output of grid variables "
               "to HDF5 files");
  }

  // register CarpetIOHDF5's recovery routine with IOUtil
  if (IOUtil_RegisterRecover ("CarpetIOHDF5 recovery", Recover) < 0) {
    CCTK_WARN (1, "Failed to register " CCTK_THORNSTRING " recovery routine");
  }

  const int numvars = CCTK_NumVars ();

  // allocate a new GH extension structure
  CarpetIOHDF5GH* myGH = new CarpetIOHDF5GH;

  myGH->requests.resize(numvars);
  myGH->cp_filename_index = 0;
  myGH->checkpoint_keep = abs (checkpoint_keep);
  myGH->cp_filename_list = (char **) calloc (myGH->checkpoint_keep, sizeof (char *));
  myGH->out_vars = strdup ("");
  myGH->out_every_default = out_every - 1;

  // initial I/O parameter check
  myGH->stop_on_parse_errors = strict_io_parameter_check;
  CheckSteerableParameters (cctkGH, myGH);
  myGH->stop_on_parse_errors = 0;

  // create the output directory (if it doesn't match ".")
  const char *my_out_dir = *out_dir ? out_dir : io_out_dir;
  if (strcmp (my_out_dir, ".")) {
    int i = strlen (my_out_dir);
    if (CCTK_Equals (out_mode, "onefile") or not strstr (my_out_dir, "%u")) {
      myGH->out_dir = (char*) malloc (i + 2);
      strcpy (myGH->out_dir, my_out_dir);
      myGH->out_dir[i] = '/';
      myGH->out_dir[i+1] = 0;
    } else {
      myGH->out_dir = (char*) malloc (i + 20);
      sprintf (myGH->out_dir, my_out_dir, dist::rank());
      strcat (myGH->out_dir, "/");
    }
  } else {
    myGH->out_dir = strdup ("");
  }

  /* create the output directory */
  const ioGH* const ioUtilGH = (const ioGH*) CCTK_GHExtension (cctkGH, "IO");
  int result = IOUtil_CreateDirectory (cctkGH, myGH->out_dir,
                                       not CCTK_Equals (out_mode, "onefile"),
                                       dist::rank());
  if (result < 0) {
    CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                "Problem creating HDF5 output directory '%s'", myGH->out_dir);
  } else if (result > 0 and CCTK_Equals (verbose, "full")) {
    CCTK_VInfo (CCTK_THORNSTRING,
                "HDF5 output directory '%s' already exists", myGH->out_dir);
  }

  // check parallel I/O parameters for chunked output
  if (not (CCTK_EQUALS(out_mode, "onefile") or CCTK_EQUALS(out_mode, "proc"))) {
    CCTK_VWarn (CCTK_WARN_COMPLAIN, __LINE__, __FILE__, CCTK_THORNSTRING,
                "IO::out_mode = '%s' is not implemented in %s. "
                "Defaulting to one output file per processor...",
                out_mode, CCTK_THORNSTRING);
  }
  if (CCTK_EQUALS(out_mode, "proc") and io_out_unchunked) {
    CCTK_WARN (CCTK_WARN_COMPLAIN,
               "IO::out_unchunked = 'yes' is incompatible with IO::out_mode = "
               "'proc'. Ignoring setting for IO::out_unchunked...");
  }

  // Now set hdf5 complex datatypes
  HDF5_ERROR (myGH->HDF5_COMPLEX =
              H5Tcreate (H5T_COMPOUND, sizeof (CCTK_COMPLEX)));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX, "real",
                         offsetof (CCTK_COMPLEX, Re), HDF5_REAL));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX, "imag",
                         offsetof (CCTK_COMPLEX, Im), HDF5_REAL));
#ifdef CCTK_REAL4
  HDF5_ERROR (myGH->HDF5_COMPLEX8 =
              H5Tcreate (H5T_COMPOUND, sizeof (CCTK_COMPLEX8)));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX8, "real",
                         offsetof (CCTK_COMPLEX8, Re), H5T_NATIVE_FLOAT));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX8, "imag",
                         offsetof (CCTK_COMPLEX8, Im), H5T_NATIVE_FLOAT));
#endif
#ifdef CCTK_REAL8
  HDF5_ERROR (myGH->HDF5_COMPLEX16 =
              H5Tcreate (H5T_COMPOUND, sizeof (CCTK_COMPLEX16)));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX16, "real",
                         offsetof (CCTK_COMPLEX16, Re), H5T_NATIVE_DOUBLE));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX16, "imag",
                         offsetof (CCTK_COMPLEX16, Im), H5T_NATIVE_DOUBLE));
#endif
#ifdef CCTK_REAL16
  HDF5_ERROR (myGH->HDF5_COMPLEX32 =
              H5Tcreate (H5T_COMPOUND, sizeof (CCTK_COMPLEX32)));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX32, "real",
                         offsetof (CCTK_COMPLEX32, Re), H5T_NATIVE_LDOUBLE));
  HDF5_ERROR (H5Tinsert (myGH->HDF5_COMPLEX32, "imag",
                         offsetof (CCTK_COMPLEX32, Im), H5T_NATIVE_LDOUBLE));
#endif

  return (myGH);
}


static void CheckSteerableParameters (const cGH *const cctkGH,
                                      CarpetIOHDF5GH *myGH)
{
  DECLARE_CCTK_PARAMETERS;

  // re-parse the 'IOHDF5::out_vars' parameter if it has changed
  if (strcmp (out_vars, myGH->out_vars)) {
    IOUtil_ParseVarsForOutput (cctkGH, CCTK_THORNSTRING, "IOHDF5::out_vars",
                               myGH->stop_on_parse_errors, out_vars,
                               -1, &myGH->requests[0]);

    // notify the user about the new setting
    if (not CCTK_Equals (verbose, "none")) {
      int count = 0;
      string msg ("Periodic HDF5 output requested for '");
      for (int i = CCTK_NumVars () - 1; i >= 0; i--) {
        if (myGH->requests[i]) {
          if (count++) {
            msg += "', '";
          }
          char *fullname = CCTK_FullName (i);
          msg += fullname;
          free (fullname);
        }
      }
      if (count) {
        msg += "'";
        CCTK_INFO (msg.c_str());
      }
    }

    // save the last setting of 'IOHDF5::out_vars' parameter
    free (myGH->out_vars);
    myGH->out_vars = strdup (out_vars);
  }
}


static int OutputGH (const cGH* const cctkGH)
{
  DECLARE_CCTK_PARAMETERS;
  static bool first_time = true;

  // check if any deprecated parameters have been set in the parameter file
  //  (don't check after recovery though)
  if (first_time) {

    if (CCTK_Equals (recover, "no") or not *recover_file) {
      WarnAboutDeprecatedParameters ();
    }
    first_time = false;
  }

  for (int vindex = CCTK_NumVars () - 1; vindex >= 0; vindex--) {
    if (TimeToOutput (cctkGH, vindex)) {
      TriggerOutput (cctkGH, vindex);
    }
  }

  return (0);
}


static int TimeToOutput (const cGH* const cctkGH, const int vindex)
{
  DECLARE_CCTK_ARGUMENTS;
  DECLARE_CCTK_PARAMETERS;

  const int numvars = CCTK_NumVars();
  assert (vindex>=0 and vindex<numvars);

  if (CCTK_GroupTypeFromVarI (vindex) != CCTK_GF and not do_global_mode) {
    return 0;
  }

  CarpetIOHDF5GH *myGH =
    (CarpetIOHDF5GH *) CCTK_GHExtension (cctkGH, CCTK_THORNSTRING);
  CheckSteerableParameters (cctkGH, myGH);

  // check if output for this variable was requested
  if (not myGH->requests[vindex]) {
    return (0);
  }

  // check whether this refinement level should be output
  if (not (myGH->requests[vindex]->refinement_levels & (1 << reflevel))) {
    return (0);
  }

  // check if output for this variable was requested individually
  // by a "<varname>{ out_every = <number> }" option string
  // this will overwrite the output criterion setting
  const char *myoutcriterion = CCTK_EQUALS (out_criterion, "default") ?
                               io_out_criterion : out_criterion;
  if (myGH->requests[vindex]->out_every >= 0) {
    myoutcriterion = "divisor";
  }

  if (CCTK_EQUALS (myoutcriterion, "never")) {
    return (0);
  }

  // check whether to output at this iteration
  bool output_this_iteration = false;

  if (CCTK_EQUALS (myoutcriterion, "iteration")) {
    int myoutevery = out_every == -2 ? io_out_every : out_every;
    if (myoutevery > 0) {
      if (*this_iteration == cctk_iteration) {
        // we already decided to output this iteration
        output_this_iteration = true;
      } else if (cctk_iteration >= *next_output_iteration) {
        // it is time for the next output
        output_this_iteration = true;
        *this_iteration = cctk_iteration;
        *next_output_iteration = cctk_iteration + myoutevery;
      }
    }
  } else if (CCTK_EQUALS (myoutcriterion, "divisor")) {
    int myoutevery = out_every == -2 ? io_out_every : out_every;
    if (myGH->requests[vindex]->out_every >= 0) {
      myoutevery = myGH->requests[vindex]->out_every;
    }
    if (myoutevery > 0 and (cctk_iteration % myoutevery) == 0) {
      // we already decided to output this iteration
      output_this_iteration = true;
    }
  } else if (CCTK_EQUALS (myoutcriterion, "time")) {
    CCTK_REAL myoutdt = out_dt == -2 ? io_out_dt : out_dt;
    if (myoutdt == 0 or *this_iteration == cctk_iteration) {
      output_this_iteration = true;
    } else if (myoutdt > 0 and (cctk_time / cctk_delta_time
                             >= *next_output_time / cctk_delta_time - 1.0e-12)) {
      // it is time for the next output
      output_this_iteration = true;
      *this_iteration = cctk_iteration;
      *next_output_time = cctk_time + myoutdt;
    }
  }

  return output_this_iteration ? 1 : 0;
}


static int TriggerOutput (const cGH* const cctkGH, const int vindex)
{
  char *fullname = CCTK_FullName (vindex);
  const char *varname = CCTK_VarName (vindex);
  const int retval = OutputVarAs (cctkGH, fullname, varname);
  free (fullname);

  return (retval);
}


static void GetVarIndex (int vindex, const char* optstring, void* arg)
{
  if (optstring) {
    char *fullname = CCTK_FullName (vindex);
    CCTK_VWarn (2, __LINE__, __FILE__, CCTK_THORNSTRING,
                "Option string '%s' will be ignored for HDF5 output of "
                "variable '%s'", optstring, fullname);
    free (fullname);
  }

  *((int *) arg) = vindex;
}


static int OutputVarAs (const cGH* const cctkGH, const char* const fullname,
                        const char* const alias)
{
  DECLARE_CCTK_ARGUMENTS;
  DECLARE_CCTK_PARAMETERS;

  int vindex = -1;

  if (CCTK_TraverseString (fullname, GetVarIndex, &vindex, CCTK_VAR) < 0) {
    CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                "error while parsing variable name '%s' (alias name '%s')",
                fullname, alias);
    return (-1);
  }

  if (vindex < 0) {
    return (-1);
  }

  const int group = CCTK_GroupIndexFromVarI (vindex);
  assert (group >= 0);
  cGroup groupdata;
  CCTK_GroupData (group, &groupdata);
  if (groupdata.grouptype == CCTK_SCALAR or groupdata.grouptype == CCTK_ARRAY) {
    assert (do_global_mode);
  }

  // Check for storage
  if (not CCTK_QueryGroupStorageI (cctkGH, group)) {
    CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                "Cannot output variable '%s' because it has no storage",
                fullname);
    return (0);
  }

  // get the default I/O request for this variable
  const CarpetIOHDF5GH *myGH =
    (CarpetIOHDF5GH *) CCTK_GHExtension (cctkGH, CCTK_THORNSTRING);
  ioRequest* request = myGH->requests[vindex];
  if (not request) {
    request = IOUtil_DefaultIORequest (cctkGH, vindex, 1);
  }

  // Get grid hierarchy extentsion from IOUtil
  const ioGH * const iogh = (const ioGH *) CCTK_GHExtension (cctkGH, "IO");
  assert (iogh);

  // Invent a file name
  int ioproc = 0, nioprocs = 1;
  string filename;
  filename.append (myGH->out_dir);
  filename.append (alias);
  if (not (CCTK_EQUALS (out_mode, "onefile") or
           request->out_unchunked or
           groupdata.disttype == CCTK_DISTRIB_CONSTANT or
           dist::size() == 1)) {
    char buffer[32];
    ioproc = dist::rank();
    nioprocs = dist::size();
    snprintf (buffer, sizeof (buffer), ".file_%d", ioproc);
    filename.append (buffer);
  }
  filename.append (out_extension);
  const char* const c_filename = filename.c_str();

  // check if the file has been created already
  typedef std::map<string, vector<vector<vector<int> > > > filelist;
  static filelist created_files;
  filelist::iterator thisfile = created_files.find (filename);
  bool is_new_file = thisfile == created_files.end();
  if (is_new_file) {
    int const numvars = CCTK_NumVars ();
    vector<vector<vector<int> > > last_outputs;   // [ml][rl][var]
    last_outputs.resize (mglevels);
    for (int ml = 0; ml < mglevels; ++ml) {
      last_outputs[ml].resize (maxreflevels);
      for (int rl = 0; rl < maxreflevels; ++rl) {
        last_outputs[ml][rl].resize (numvars, cctk_iteration - 1);
      }
    }
    thisfile = created_files.insert (thisfile,
                                     filelist::value_type (filename,
                                                           last_outputs));
    assert (thisfile != created_files.end());
  }

  // check if this variable has been output already during this iteration
  int& last_output = thisfile->second.at(mglevel).at(reflevel).at(vindex);
  if (last_output == cctk_iteration) {
    // Has already been output during this iteration
    char* varname = CCTK_FullName(vindex);
    CCTK_VWarn (5, __LINE__, __FILE__, CCTK_THORNSTRING,
                "Skipping output for variable \"%s\", because this variable "
                "has already been output during the current iteration -- "
                "probably via a trigger during the analysis stage",
                varname);
    free (varname);
    return (0);
  }
  assert (last_output < cctk_iteration);
  last_output = cctk_iteration;

  // Open the output file if this is a designated I/O processor
  hid_t file = -1;
  if (dist::rank() == ioproc) {

    if (is_new_file and not IO_TruncateOutputFiles (cctkGH)) {
      H5E_BEGIN_TRY {
        is_new_file = H5Fis_hdf5 (c_filename) <= 0;
      } H5E_END_TRY;
    }

    if (is_new_file) {
      HDF5_ERROR (file = H5Fcreate (c_filename, H5F_ACC_TRUNC, H5P_DEFAULT,
                                    H5P_DEFAULT));
      // write metadata information
      WriteMetadata (cctkGH, nioprocs, false, file);
    } else {
      HDF5_ERROR (file = H5Fopen (c_filename, H5F_ACC_RDWR, H5P_DEFAULT));
    }
  }

  if (CCTK_Equals (verbose, "full")) {
    CCTK_VInfo (CCTK_THORNSTRING,
                "Writing variable '%s' on mglevel %d reflevel %d",
                fullname, mglevel, reflevel);
  }
  if ((CCTK_EQUALS (out_mode, "onefile") and io_out_unchunked) or
      request->out_unchunked or
      groupdata.disttype == CCTK_DISTRIB_CONSTANT) {
    WriteVarUnchunked (cctkGH, file, request, false);
  } else if (CCTK_EQUALS (out_mode, "onefile")) {
    WriteVarChunkedSequential (cctkGH, file, request, false);
  } else {
    WriteVarChunkedParallel (cctkGH, file, request, false);
  }

  // free I/O request structure
  if (request != myGH->requests[vindex]) {
    IOUtil_FreeIORequest (&request);
  }

  // Close the file
  if (file >= 0) {
    HDF5_ERROR (H5Fclose (file));
  }

  return (0);
}


static int Checkpoint (const cGH* const cctkGH, int called_from)
{
  int retval = 0;
  DECLARE_CCTK_PARAMETERS;


  /* get the filenames for both the temporary and real checkpoint file */
  int ioproc = 0, nioprocs = 1;
  int parallel_io = 0;
  if (not (CCTK_EQUALS (out_mode, "onefile") or dist::size() == 1)) {
    ioproc = dist::rank();
    nioprocs = dist::size();
    parallel_io = 1;
  }
  char *filename =
    IOUtil_AssembleFilename (cctkGH, NULL, "", ".h5",
                             called_from, ioproc, not parallel_io);
  char *tempname =
    IOUtil_AssembleFilename (cctkGH, NULL, ".tmp", ".h5",
                             called_from, ioproc, not parallel_io);

  hid_t file = -1;
  if (dist::rank() == ioproc) {
    if (CCTK_Equals (verbose, "full")) {
      CCTK_VInfo (CCTK_THORNSTRING, "Creating temporary checkpoint file '%s'",
                  tempname);
    }

    HDF5_ERROR (file = H5Fcreate (tempname, H5F_ACC_TRUNC, H5P_DEFAULT,
                                  H5P_DEFAULT));

    // write metadata information
    WriteMetadata (cctkGH, nioprocs, true, file);
  }

  // now dump the grid variables on all mglevels, reflevels, maps and components
  BEGIN_MGLEVEL_LOOP (cctkGH) {
    BEGIN_REFLEVEL_LOOP (cctkGH) {

      if (CCTK_Equals (verbose, "full")) {
        CCTK_VInfo (CCTK_THORNSTRING,
                    "Dumping grid variables on mglevel %d reflevel %d ...",
                    mglevel, reflevel);
      }

      for (int group = CCTK_NumGroups () - 1; group >= 0; group--) {
        /* only dump groups which have storage assigned */
        if (CCTK_QueryGroupStorageI (cctkGH, group) <= 0 or
	    CCTK_NumVarsInGroupI(group) == 0) {
          continue;
        }

        cGroup gdata;
        CCTK_GroupData (group, &gdata);
        assert (gdata.grouptype == CCTK_ARRAY or gdata.grouptype == CCTK_GF or
                gdata.grouptype == CCTK_SCALAR);

        // scalars and grid arrays only have one reflevel
        if (gdata.grouptype != CCTK_GF and reflevel > 0) {
          continue;
        }

        int const len = Util_TableGetString (gdata.tagstable, 0, NULL,
                                             "checkpoint");
        if (len > 0) {
          char* value = new char[len + 1];
          Util_TableGetString (gdata.tagstable, len + 1, value, "checkpoint");
          if (len == sizeof ("no") - 1 and CCTK_Equals (value, "no")) {
            continue;
          } else if (not CCTK_Equals (value, "yes")) {
            char* groupname = CCTK_GroupName (group);
            CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                        "Ignoring unknown checkpoint tag '%s' for group '%s'",
                        value, groupname);
            free (groupname);
          }
          delete[] value;
        }

        /* get the number of active timelevels */
        gdata.numtimelevels = CCTK_ActiveTimeLevelsGI (cctkGH, group);

        int first_vindex = CCTK_FirstVarIndexI (group);

        /* get the default I/O request for this group */
        ioRequest *request = IOUtil_DefaultIORequest (cctkGH, first_vindex, 1);

        /* disable checking for old data objects, disable datatype conversion
           and downsampling */
        request->check_exist = 0;
        request->hdatatype = gdata.vartype;
        for (request->hdim = 0; request->hdim < request->vdim; request->hdim++){
          request->downsample[request->hdim] = 1;
        }

        /* loop over all variables in this group */
        for (request->vindex = first_vindex;
             request->vindex < first_vindex + gdata.numvars;
             request->vindex++) {
          char *fullname = CCTK_FullName (request->vindex);
          assert (fullname);

          /* loop over all timelevels of this variable */
          for (request->timelevel = 0;
               request->timelevel < gdata.numtimelevels;
               request->timelevel++) {
            if (CCTK_Equals (verbose, "full")) {
              CCTK_VInfo (CCTK_THORNSTRING, "  %s (timelevel %d)",
                          fullname, request->timelevel);
            }

            // write the var
            retval += parallel_io ?
                      WriteVarChunkedParallel (cctkGH, file, request, true) :
                      WriteVarChunkedSequential (cctkGH, file, request, true);
          }
          free (fullname);

        } /* end of loop over all variables */

        // free I/O request structure
        IOUtil_FreeIORequest (&request);

      } /* end of loop over all groups */
    } END_REFLEVEL_LOOP;

  } END_MGLEVEL_LOOP;


  // Close the file
  if (file >= 0) {
    HDF5_ERROR (H5Fclose(file));
  }

  // get global error count
  int temp = retval;
  MPI_Allreduce (&temp, &retval, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  if (retval == 0) {
    if (file >= 0) {
      if (rename (tempname, filename)) {
        CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                    "Could not rename temporary checkpoint file '%s' to '%s'",
                    tempname, filename);
        retval = -1;
      } else if (called_from == CP_EVOLUTION_DATA and checkpoint_keep > 0) {
        CarpetIOHDF5GH *myGH =
          (CarpetIOHDF5GH *) CCTK_GHExtension (cctkGH, CCTK_THORNSTRING);

        if (myGH->cp_filename_list[myGH->cp_filename_index]) {
          remove (myGH->cp_filename_list[myGH->cp_filename_index]);
          free (myGH->cp_filename_list[myGH->cp_filename_index]);
          myGH->cp_filename_list[myGH->cp_filename_index] = NULL;
        }
        myGH->cp_filename_list[myGH->cp_filename_index] = strdup (filename);
        myGH->cp_filename_index = (myGH->cp_filename_index+1) % checkpoint_keep;
        if (myGH->checkpoint_keep != checkpoint_keep) {
          char **cp_filename_list = (char **) calloc (checkpoint_keep,
                                                      sizeof (char *));
          int min = myGH->checkpoint_keep < checkpoint_keep ?
                    myGH->checkpoint_keep : checkpoint_keep;
          while (min-- > 0) {
            cp_filename_list[min] = myGH->cp_filename_list[min];
          }
          free (myGH->cp_filename_list);
          myGH->cp_filename_list = cp_filename_list;
          myGH->checkpoint_keep = checkpoint_keep;
        }
      }
    }
  } else {
    CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                "Failed to create checkpoint at iteration %d",
                cctkGH->cctk_iteration);
  }

  // save the iteration number of this checkpoint
  last_checkpoint_iteration = cctkGH->cctk_iteration;

  // free allocated resources
  free (tempname);
  free (filename);

  return retval;

} // Checkpoint


void WriteMetadata (const cGH *cctkGH, int nioprocs,
                    bool called_from_checkpoint, hid_t file)
{
  hid_t group, scalar_dataspace, array_dataspace, datatype, attr;
  DECLARE_CCTK_PARAMETERS;


  if (CCTK_Equals (verbose, "full")) {
    CCTK_INFO ("Writing simulation metadata...");
  }

  const ioGH *ioUtilGH = (const ioGH *) CCTK_GHExtension (cctkGH, "IO");
  HDF5_ERROR (group = H5Gcreate (file, METADATA_GROUP, 0));

  int ivalue = CCTK_MainLoopIndex ();
  HDF5_ERROR (scalar_dataspace = H5Screate (H5S_SCALAR));
  HDF5_ERROR (attr = H5Acreate (group, "main loop index", H5T_NATIVE_INT,
                                scalar_dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &ivalue));
  HDF5_ERROR (H5Aclose (attr));

  ivalue = cctkGH->cctk_iteration;
  HDF5_ERROR (attr = H5Acreate (group, "GH$iteration", H5T_NATIVE_INT,
                                scalar_dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &ivalue));
  HDF5_ERROR (H5Aclose (attr));

  ivalue = nioprocs;
  HDF5_ERROR (attr = H5Acreate (group, "nioprocs", H5T_NATIVE_INT,
                                scalar_dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &ivalue));
  HDF5_ERROR (H5Aclose (attr));

  ivalue = reflevels;
  HDF5_ERROR (attr = H5Acreate (group, "carpet_reflevels", H5T_NATIVE_INT,
                                scalar_dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &ivalue));
  HDF5_ERROR (H5Aclose (attr));

  double dvalue = global_time;
  HDF5_ERROR (attr = H5Acreate (group, "carpet_global_time",
                                H5T_NATIVE_DOUBLE, scalar_dataspace,
                                H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_DOUBLE, &dvalue));
  HDF5_ERROR (H5Aclose (attr));

  dvalue = delta_time;
  HDF5_ERROR (attr = H5Acreate (group, "carpet_delta_time",
                                H5T_NATIVE_DOUBLE, scalar_dataspace,
                                H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_DOUBLE, &dvalue));
  HDF5_ERROR (H5Aclose (attr));

  const char *version = CCTK_FullVersion();
  HDF5_ERROR (datatype = H5Tcopy (H5T_C_S1));
  HDF5_ERROR (H5Tset_size (datatype, strlen (version)));
  HDF5_ERROR (attr = H5Acreate (group, "Cactus version", datatype,
                                scalar_dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, datatype, version));
  HDF5_ERROR (H5Aclose (attr));

  // all times on all refinement levels
  ivalue = mglevels;
  HDF5_ERROR (attr = H5Acreate (group, "numberofmgtimes", H5T_NATIVE_INT,
                                scalar_dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &ivalue));
  HDF5_ERROR (H5Aclose (attr));
  hsize_t size = reflevels;
  HDF5_ERROR (array_dataspace = H5Screate_simple (1, &size, NULL));
  for (int i = 0; i < mglevels; i++) {
    char name[100];
    snprintf (name, sizeof (name), "mgleveltimes %d", i);
    HDF5_ERROR (attr = H5Acreate (group, name, H5T_NATIVE_DOUBLE,
                                  array_dataspace, H5P_DEFAULT));
    HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_DOUBLE, &leveltimes.at(i).at(0)));
    HDF5_ERROR (H5Aclose (attr));
  }

  // unique build identifier
  if (CCTK_IsFunctionAliased ("UniqueBuildID")) {
    char const * const build_id
      = static_cast<char const *> (UniqueBuildID (cctkGH));
    HDF5_ERROR (H5Tset_size (datatype, strlen (build_id)));
    HDF5_ERROR (attr = H5Acreate (group, "build id", datatype,
                                  scalar_dataspace, H5P_DEFAULT));
    HDF5_ERROR (H5Awrite (attr, datatype, build_id));
    HDF5_ERROR (H5Aclose (attr));
  }

  // unique simulation identifier
  if (CCTK_IsFunctionAliased ("UniqueSimulationID")) {
    char const * const job_id
      = static_cast<char const *> (UniqueSimulationID (cctkGH));
    HDF5_ERROR (H5Tset_size (datatype, strlen (job_id)));
    HDF5_ERROR (attr = H5Acreate (group, "simulation id", datatype,
                                  scalar_dataspace, H5P_DEFAULT));
    HDF5_ERROR (H5Awrite (attr, datatype, job_id));
    HDF5_ERROR (H5Aclose (attr));
  }

  // save parameters in a separate dataset (may be too big for an attribute)
  if (called_from_checkpoint or not CCTK_Equals (out_save_parameters, "no")) {
    hid_t dataset;
    const int get_all = called_from_checkpoint or
                        CCTK_Equals (out_save_parameters, "all");
    char* parameters = IOUtil_GetAllParameters (cctkGH, get_all);
    assert (parameters);
    size = strlen (parameters) + 1;
    HDF5_ERROR (H5Sset_extent_simple (array_dataspace, 1, &size, NULL));
    HDF5_ERROR (dataset = H5Dcreate (group, ALL_PARAMETERS, H5T_NATIVE_UCHAR,
                                     array_dataspace, H5P_DEFAULT));
    HDF5_ERROR (H5Dwrite (dataset, H5T_NATIVE_CHAR, H5S_ALL, H5S_ALL,
                          H5P_DEFAULT, parameters));
    HDF5_ERROR (H5Dclose (dataset));
    free (parameters);
  }

  HDF5_ERROR (H5Tclose (datatype));
  HDF5_ERROR (H5Sclose (scalar_dataspace));
  HDF5_ERROR (H5Sclose (array_dataspace));
  HDF5_ERROR (H5Gclose (group));
}


static void WarnAboutDeprecatedParameters (void)
{
  DECLARE_CCTK_PARAMETERS;
  char buffer[20];

  if (CCTK_ParameterQueryTimesSet ("out3D_dir", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("out_dir", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::out3D_dir' is deprecated, "
               "please use 'IOHDF5::out_dir' instead");
    CCTK_ParameterSet ("out_dir", CCTK_THORNSTRING, out3D_dir);
  }
  if (CCTK_ParameterQueryTimesSet ("out3D_vars", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("out_vars", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::out3D_vars' is deprecated, "
               "please use 'IOHDF5::out_vars' instead");
    CCTK_ParameterSet ("out_vars", CCTK_THORNSTRING, out3D_vars);
  }
  if (CCTK_ParameterQueryTimesSet ("out3D_extension", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("out_extension", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::out3D_extension' is "
               "deprecated, please use 'IOHDF5::out_extension' instead");
    CCTK_ParameterSet ("out_extension", CCTK_THORNSTRING, out3D_extension);
  }
  if (CCTK_ParameterQueryTimesSet ("out3D_criterion", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("out_criterion", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::out3D_criterion' is "
               "deprecated, please use 'IOHDF5::out_criterion' instead");
    CCTK_ParameterSet ("out_criterion", CCTK_THORNSTRING, out3D_criterion);
  }
  if (CCTK_ParameterQueryTimesSet ("out3D_every", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("out_every", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::out3D_every' is "
               "deprecated, please use 'IOHDF5::out_every' instead");
    snprintf (buffer, sizeof (buffer), "%d", out3D_every);
    CCTK_ParameterSet ("out_every", CCTK_THORNSTRING, buffer);
  }
  if (CCTK_ParameterQueryTimesSet ("out3D_dt", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("out_dt", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::out3D_dt' is deprecated, "
               "please use 'IOHDF5::out_dt' instead");
    snprintf (buffer, sizeof (buffer), "%f", (double)out3D_dt);
    CCTK_ParameterSet ("out_dt", CCTK_THORNSTRING, buffer);
  }
  if (CCTK_ParameterQueryTimesSet ("in3D_dir", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("in_dir", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::in3D_dir' is deprecated, "
               "please use 'IOHDF5::in_dir' instead");
    CCTK_ParameterSet ("in_dir", CCTK_THORNSTRING, in3D_dir);
  }
  if (CCTK_ParameterQueryTimesSet ("in3D_vars", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("in_vars", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::in3D_vars' is deprecated, "
               "please use 'IOHDF5::in_vars' instead");
    CCTK_ParameterSet ("in_vars", CCTK_THORNSTRING, in3D_vars);
  }
  if (CCTK_ParameterQueryTimesSet ("in3D_extension", CCTK_THORNSTRING) >
      CCTK_ParameterQueryTimesSet ("in_extension", CCTK_THORNSTRING)) {
    CCTK_WARN (CCTK_WARN_ALERT, "Parameter 'IOHDF5::in3D_extension' is "
               "deprecated, please use 'IOHDF5::in_extension' instead");
    CCTK_ParameterSet ("in_extension", CCTK_THORNSTRING, in3D_extension);
  }
}


} // namespace CarpetIOHDF5

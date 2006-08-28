 /*@@
   @file      hdf5toascii_slicer.cc
   @date      Sun 30 July 2007
   @author    Thomas Radke
   @desc
              Utility program to extract a 1D line or 2D slice from 3D datasets
              in datafiles generated by CarpetIOHDF5.
              The extracted slab is written to stdout in CarpetIOASCII format.
   @enddesc
 @@*/

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

#include <hdf5.h>

using namespace std;

/*****************************************************************************
 *************************     Macro Definitions   ***************************
 *****************************************************************************/

// uncomment the following line to get some debug output
// #define DEBUG 1

// value for an unset parameter
#define PARAMETER_UNSET	-424242.0

// fuzzy factor for comparing dataset timesteps with user-specified value
#define FUZZY_FACTOR	1e-6

// the datatype for the 'start' argument in H5Sselect_hyperslab()
#if (H5_VERS_MAJOR == 1 && \
     (H5_VERS_MINOR < 6 || (H5_VERS_MINOR == 6 && H5_VERS_RELEASE < 4)))
#define h5size_t hssize_t
#else
#define h5size_t hsize_t
#endif

// macro to check the return code of calls to the HDF5 library
#define CHECK_HDF5(fn_call) {                                                 \
          const int _retval = fn_call;                                        \
          if (_retval < 0) {                                                  \
            cerr << "HDF5 call " << #fn_call                                  \
                 << " in file " << __FILE__ << " line " << __LINE__           \
                 << " returned error code " << _retval << endl;               \
          }                                                                   \
        }


/*****************************************************************************
 **************************     Type Definitions   ***************************
 *****************************************************************************/

typedef struct {
  hid_t file;
  string name;
  hsize_t dims[3];

  int map, mglevel, component, iteration, timelevel, rflevel;
  double time;
  int iorigin[3];
  double origin[3], delta[3];
} patch_t;


/*****************************************************************************
 *************************       Global Data         *************************
 *****************************************************************************/

// the slab coordinate as selected by the user
static double slab_coord[3] = {PARAMETER_UNSET, PARAMETER_UNSET, PARAMETER_UNSET};

// the specific timestep selected by the user
static double timestep = PARAMETER_UNSET;

// the list of all patches
static vector<patch_t> patchlist;

// maximum refinement level across all patches
static int max_rflevel = -1;

// whether to print omitted patches
static bool verbose = false; 

/*****************************************************************************
 *************************     Function Prototypes   *************************
 *****************************************************************************/
static herr_t FindPatches (hid_t group, const char *name, void *_file);
static void ReadPatch (const patch_t& patch, int last_iteration);
static bool ComparePatches (const patch_t& a, const patch_t& b);


 /*@@
   @routine    main
   @date       Sun 30 July 2007
   @author     Thomas Radke
   @desc
               Evaluates command line options and opens the input files.
   @enddesc

   @var        argc
   @vdesc      number of command line parameters
   @vtype      int
   @vio        in
   @endvar
   @var        argv
   @vdesc      array of command line parameters
   @vtype      char* const []
   @vio        in
   @endvar
 @@*/
int main (int argc, char *const argv[])
{
  int i;
  bool help = false;
#if 0
  int iorigin[3] = {-1, -1, -1};
#endif
  int num_slab_options = 0;


  // evaluate command line parameters
  for (i = 1; i < argc; i++) {
    if (strcmp (argv[i], "--help") == 0) {
      help = true; break;
    } else if (strcmp (argv[i], "--verbose") == 0) {
      verbose = true;
    } else if (strcmp (argv[i], "--timestep") == 0 and i+1 < argc) {
      timestep = atof (argv[++i]);
    } else if (strcmp (argv[i], "--out-precision") == 0 and i+1 < argc) {
      cout << setprecision (atoi (argv[++i]));
    } else if (strcmp (argv[i], "--out1d-xline-yz") == 0 and i+2 < argc) {
      slab_coord[1] = atof (argv[++i]);
      slab_coord[2] = atof (argv[++i]); num_slab_options++;
    } else if (strcmp (argv[i], "--out1d-yline-xz") == 0 and i+2 < argc) {
      slab_coord[0] = atof (argv[++i]);
      slab_coord[2] = atof (argv[++i]); num_slab_options++;
    } else if (strcmp (argv[i], "--out1d-zline-xy") == 0 and i+2 < argc) {
      slab_coord[0] = atof (argv[++i]);
      slab_coord[1] = atof (argv[++i]); num_slab_options++;
    } else if (strcmp (argv[i], "--out2d-yzplane-x") == 0 and i+1 < argc) {
      slab_coord[0] = atof (argv[++i]); num_slab_options++;
    } else if (strcmp (argv[i], "--out2d-xzplane-y") == 0 and i+1 < argc) {
      slab_coord[1] = atof (argv[++i]); num_slab_options++;
    } else if (strcmp (argv[i], "--out2d-xyplane-z") == 0 and i+1 < argc) {
      slab_coord[2] = atof (argv[++i]); num_slab_options++;
#if 0
    } else if (strcmp (argv[i], "--out2d-yzplane-xi") == 0 and i+1 < argc) {
      iorigin[0] = atoi (argv[++i]); num_slab_options++;
    } else if (strcmp (argv[i], "--out2d-xzplane-yi") == 0 and i+1 < argc) {
      iorigin[1] = atoi (argv[++i]); num_slab_options++;
    } else if (strcmp (argv[i], "--out2d-xyplane-zi") == 0 and i+1 < argc) {
      iorigin[2] = atoi (argv[++i]); num_slab_options++;
#endif
    } else {
      break;
    }
  }

  /* give some help if called with incorrect number of parameters */
  if (help or i >= argc or num_slab_options != 1) {
    const string indent (strlen (argv[0]) + 1, ' ');
    cerr << endl << "   ---------------------------"
         << endl << "   Carpet HDF5 to ASCII Slicer"
         << endl << "   ---------------------------" << endl
         << endl
         << "Usage: " << endl
         << argv[0] << " [--help]" << endl
         << indent << "[--out-precision <digits>]" << endl
         << indent << "[--timestep <cctk_time value>]" << endl
         << indent << "[--verbose]" << endl
         << indent << "<--out1d-line value value> | <--out2d-plane value>" << endl
         << indent << "<hdf5_infiles>" << endl << endl
         << "  where" << endl
         << "    [--help]                         prints this help" << endl
         << "    [--out-precision <digits>]       sets the output precision" << endl
         << "                                     for floating-point numbers" << endl
         << "    [--timestep <cctk_time value>]   selects all HDF5 datasets which" << endl
         << "                                     (fuzzily) match the specified time" << endl
         << "    [--verbose]                      lists skipped HDF5 datasets on stderr" << endl
         << "  and either <--out1d-line value value> or <--out2d-plane value> triples" << endl
         << "  must be specified as in the following:" << endl
         << "    --out1d-xline-yz  <origin_y> <origin_z>" << endl
         << "    --out1d-yline-xz  <origin_x> <origin_z>" << endl
         << "    --out1d-zline-xy  <origin_x> <origin_y>" << endl
         << endl
         << "    --out2d-yzplane-x  <origin_x>" << endl
         << "    --out2d-xzplane-y  <origin_y>" << endl
         << "    --out2d-xyplane-z  <origin_z>" << endl
#if 0
         << "    --out2d-yzplane-xi <origin_xi>" << endl
         << "    --out2d-xzplane-yi <origin_yi>" << endl
         << "    --out2d-xyplane-zi <origin_zi>" << endl
#endif
         << endl
         << "  eg, " << argv[0] << " --out2d-xyplane-z 0.125 alp.file_*.h5" << endl << endl;
    return (0);
  }

  vector<hid_t> filelist;
  for (; i < argc; i++) {
    hid_t file;

    H5E_BEGIN_TRY {
      file = H5Fopen (argv[i], H5F_ACC_RDONLY, H5P_DEFAULT);
    } H5E_END_TRY;

    if (file < 0) {
      cerr << "Could not open Carpet HDF5 input file '" << argv[i] << "'"
           << endl << endl;
      continue;
    }

    CHECK_HDF5 (H5Giterate (file, "/", NULL, FindPatches, &file));

    filelist.push_back (file);
  }

  if (patchlist.size() > 0) {
    /* now sort the patches by their time attribute */
    sort (patchlist.begin(), patchlist.end(), ComparePatches);

    cout << "# 1D ASCII output created by";
    for (int j = 0; j < argc; j++) cout << " " << argv[j];
    cout << endl << "#" << endl;

    int last_iteration = patchlist[0].iteration - 1;
    for (size_t j = 0; j < patchlist.size(); j++) {
      ReadPatch (patchlist[j], last_iteration);
      last_iteration = patchlist[j].iteration;
    }
  } else {
    cerr << "No valid datasets found" << endl << endl;
  }

  // close all input files
  for (size_t j = 0; j < filelist.size(); j++) {
    if (filelist[j] >= 0) {
      CHECK_HDF5 (H5Fclose (filelist[j]));
    }
  }

  return (0);
}


 /*@@
   @routine    FindPatches
   @date       Tue 8 August 2006
   @author     Thomas Radke
   @desc
               The worker routine which is called by H5Giterate().
               It checks whether the current HDF5 object is a patch matching
               the user's slab criteria, and adds it to the patches list.
   @enddesc

   @var        group
   @vdesc      HDF5 object to start the iteration
   @vtype      hid_t
   @vio        in
   @endvar
   @var        name
   @vdesc      name of the object at the current iteration
   @vtype      const char *
   @vio        in
   @endvar
   @var        _file
   @vdesc      pointer to the descriptor of the currently opened file
   @vtype      void *
   @vio        in
   @endvar
@@*/
static herr_t FindPatches (hid_t group, const char *name, void *_file)
{
  int ndims;
  hid_t dataset, datatype, attr;
  H5T_class_t typeclass;
  H5G_stat_t object_info;
  patch_t patch;


  /* we are interested in datasets only - skip anything else */
  CHECK_HDF5 (H5Gget_objinfo (group, name, 0, &object_info));
  if (object_info.type != H5G_DATASET) {
    return (0);
  }

  /* check the dataset's datatype - make sure it is either integer or real */
  CHECK_HDF5 (dataset   = H5Dopen (group, name));
  patch.name = name;

  CHECK_HDF5 (datatype  = H5Dget_type (dataset));
  CHECK_HDF5 (typeclass = H5Tget_class (datatype));
  CHECK_HDF5 (H5Tclose (datatype));

  // read the timestep
  CHECK_HDF5 (attr = H5Aopen_name (dataset, "time"));
  CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_DOUBLE, &patch.time));
  CHECK_HDF5 (H5Aclose (attr));

  // read the dimensions
  hid_t dataspace = -1;
  CHECK_HDF5 (dataspace = H5Dget_space (dataset));
  ndims = H5Sget_simple_extent_ndims (dataspace);

  bool is_okay = false;
  if (typeclass != H5T_FLOAT) {
    if (verbose) {
      cerr << "skipping dataset '" << name << "':" << endl
           << "  is not of floating-point datatype" << endl;
    }
  } else if (ndims != 3) {
    if (verbose) {
      cerr << "skipping dataset '" << name << "':" << endl
           << "  dataset has " << ndims << " dimensions" << endl;
    }
  } else if (timestep != PARAMETER_UNSET and
             fabs (timestep - patch.time) > FUZZY_FACTOR) {
    if (verbose) {
      cerr << "skipping dataset '" << name << "':" << endl
           << "  timestep (" << patch.time << ") doesn't match" << endl;
    }
  } else {
    CHECK_HDF5 (attr = H5Aopen_name (dataset, "origin"));
    CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_DOUBLE, patch.origin));
    CHECK_HDF5 (H5Aclose (attr));
    CHECK_HDF5 (attr = H5Aopen_name (dataset, "delta"));
    CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_DOUBLE, patch.delta));
    CHECK_HDF5 (H5Aclose (attr));
    CHECK_HDF5 (H5Sget_simple_extent_dims (dataspace, patch.dims, NULL));

    int i;
    for (i = 0; i < 3; i++) {
      if (slab_coord[i] != PARAMETER_UNSET) {
        is_okay = slab_coord[i] >= patch.origin[i] and
                  slab_coord[i] <= patch.origin[i] +
                                    (patch.dims[2-i]-1)*patch.delta[i];
        if (not is_okay) break;
      }
    }
    if (not is_okay) {
      if (verbose) {
        cerr << "skipping dataset '" << name << "':" << endl
             << "  slab " << slab_coord[i] << " is out of dataset range ["
             << patch.origin[i] << ", "
             << patch.origin[i] + (patch.dims[2-i]-1)*patch.delta[i] << "]"
             << endl;
      }
    }
  }
  CHECK_HDF5 (H5Sclose (dataspace));

  if (not is_okay) {
    CHECK_HDF5 (H5Dclose (dataset));
    return (0);
  }

  patch.file = *(hid_t *) _file;

  /* read attributes */
  CHECK_HDF5 (attr = H5Aopen_name (dataset, "timestep"));
  CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_INT, &patch.iteration));
  CHECK_HDF5 (H5Aclose (attr));
  CHECK_HDF5 (attr = H5Aopen_name (dataset, "level"));
  CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_INT, &patch.rflevel));
  CHECK_HDF5 (H5Aclose (attr));
  CHECK_HDF5 (attr = H5Aopen_name (dataset, "group_timelevel"));
  CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_INT, &patch.timelevel));
  CHECK_HDF5 (H5Aclose (attr));
  CHECK_HDF5 (attr = H5Aopen_name (dataset, "carpet_mglevel"));
  CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_INT, &patch.mglevel));
  CHECK_HDF5 (H5Aclose (attr));
  CHECK_HDF5 (attr = H5Aopen_name (dataset, "iorigin"));
  CHECK_HDF5 (H5Aread (attr, H5T_NATIVE_INT, patch.iorigin));
  CHECK_HDF5 (H5Aclose (attr));

  CHECK_HDF5 (H5Dclose (dataset));

  // the component and map info are not contained in the HDF5 dataset
  // and thus have to be parsed from the dataset's name
  patch.map = patch.component = 0;
  string::size_type loc = patch.name.find (" m=", 0);
  if (loc != string::npos) patch.map = atoi (patch.name.c_str() + loc + 3);
  loc = patch.name.find (" c=", 0);
  if (loc != string::npos) patch.component = atoi(patch.name.c_str() + loc + 3);

  if (max_rflevel < patch.rflevel) max_rflevel = patch.rflevel;

  patchlist.push_back (patch);

  return (0);
}


 /*@@
   @routine    ReadPatch
   @date       Tue 8 August 2006
   @author     Thomas Radke
   @desc
               Reads a slab from the given patch
               and outputs it in CarpetIOASCII format.
   @enddesc

   @var        patch
   @vdesc      patch to output
   @vtype      const patch_t&
   @vio        in
   @endvar
   @var        last_iteration
   @vdesc      iteration number of the last patch that was output
   @vtype      int
   @vio        in
   @endvar
@@*/
static void ReadPatch (const patch_t& patch, int last_iteration)
{
  if (patch.iteration != last_iteration) cout << endl;

  cout << "# iteration " << patch.iteration << endl
       << "# refinement level " << patch.rflevel
       << "   multigrid level " << patch.mglevel
       << "   map " << patch.map
       << "   component " << patch.component
       << "   time level " << patch.timelevel
       << endl
       << "# column format: 1:it\t2:tl 3:rl 4:c 5:ml\t6:ix 7:iy 8:iz\t9:time\t10:x 11:y 12:z\t13:data" << endl;

  h5size_t slabstart[3] = {0, 0, 0};
  hsize_t slabcount[3] = {patch.dims[0], patch.dims[1], patch.dims[2]};
  for (int i = 0; i < 3; i++) {
    if (slab_coord[i] != PARAMETER_UNSET) {
      slabstart[2-i] = (h5size_t) ((slab_coord[i] - patch.origin[i]) /
                                   patch.delta[i] + 0.5);
      slabcount[2-i] = 1;
    }
  }

  hid_t dataset, filespace, slabspace;
  CHECK_HDF5 (dataset = H5Dopen (patch.file, patch.name.c_str()));
  CHECK_HDF5 (filespace = H5Dget_space (dataset));
  CHECK_HDF5 (slabspace = H5Screate_simple (3, slabcount, NULL));
  CHECK_HDF5 (H5Sselect_hyperslab (filespace, H5S_SELECT_SET,
                                   slabstart, NULL, slabcount, NULL));
  const hssize_t npoints = H5Sget_select_npoints (filespace);
  double* data = new double[npoints];
  CHECK_HDF5 (H5Dread (dataset, H5T_NATIVE_DOUBLE, slabspace, filespace,
                       H5P_DEFAULT, data));
  CHECK_HDF5 (H5Sclose (slabspace));
  CHECK_HDF5 (H5Sclose (filespace));
  CHECK_HDF5 (H5Dclose (dataset));

  for (h5size_t k = slabstart[0];
       k < slabstart[0] + (h5size_t) slabcount[0]; k++) {
    for (h5size_t j = slabstart[1];
         j < slabstart[1] + (h5size_t) slabcount[1]; j++) {
      for (h5size_t i = slabstart[2];
           i < slabstart[2] + (h5size_t) slabcount[2]; i++) {
        cout << patch.iteration << "\t"
             << patch.timelevel << " "
             << patch.rflevel << " "
             << patch.component << " "
             << patch.mglevel << "\t"
             << patch.iorigin[0] + i*(1 << (max_rflevel - patch.rflevel)) << " "
             << patch.iorigin[1] + j*(1 << (max_rflevel - patch.rflevel)) << " "
             << patch.iorigin[2] + k*(1 << (max_rflevel - patch.rflevel)) << "\t"
             << setiosflags (ios_base::fixed)
             << patch.time << "\t"
             << patch.origin[0] + i*patch.delta[0] << " "
             << patch.origin[1] + j*patch.delta[1] << " "
             << patch.origin[2] + k*patch.delta[2] << "\t"
             << resetiosflags (ios_base::fixed)
             << setiosflags (ios_base::scientific)
             << *data++
             << resetiosflags (ios_base::scientific)
             << endl;
      }
      if (slab_coord[0] == PARAMETER_UNSET) cout << endl;
    }
    if (slab_coord[1] == PARAMETER_UNSET) cout << endl;
  }
  if (slab_coord[2] == PARAMETER_UNSET) cout << endl;

  data -= npoints;
  delete[] data;
}


// comparison function to sort patches by iteration number, refinement level,
// multigrid level, map, and component
static bool ComparePatches (const patch_t& a, const patch_t& b)
{
  if (a.iteration != b.iteration) return (a.iteration < b.iteration);
  if (a.rflevel   != b.rflevel)   return (a.rflevel   < b.rflevel);
  if (a.mglevel   != b.mglevel)   return (a.mglevel   < b.mglevel);
  if (a.map       != b.map)       return (a.map       < b.map);
  if (a.component != b.component) return (a.component < b.component);
  return (a.timelevel < b.timelevel);
}

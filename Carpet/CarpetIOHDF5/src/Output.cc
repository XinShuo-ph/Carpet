#include <assert.h>
#include <stdlib.h>

#include <sstream>

#include "cctk.h"
#include "cctk_Arguments.h"
#include "cctk_Parameters.h"
#include "util_Table.h"

#include "CarpetIOHDF5.hh"
#include "CactusBase/IOUtil/src/ioGH.h"


namespace CarpetIOHDF5
{

using namespace std;
using namespace Carpet;


// add attributes to an HDF5 dataset
static void AddAttributes (const cGH *const cctkGH, const char *fullname,
                           int vdim, int refinementlevel,
                           const ioRequest* const request,
                           const ibbox& bbox, hid_t dataset);


int WriteVarUnchunked (const cGH* const cctkGH,
                       hid_t outfile,
                       const ioRequest* const request,
                       bool called_from_checkpoint)
{
  DECLARE_CCTK_PARAMETERS;

  char *fullname = CCTK_FullName(request->vindex);
  const int gindex = CCTK_GroupIndexFromVarI (request->vindex);
  assert (gindex >= 0 and gindex < (int) Carpet::arrdata.size ());
  const int var = request->vindex - CCTK_FirstVarIndexI (gindex);
  assert (var >= 0 and var < CCTK_NumVars ());
  cGroup group;
  CCTK_GroupData (gindex, &group);


  // Scalars and arrays have only one refinement level 0,
  // regardless of what the current refinement level is.
  // Output for them must be called in global mode.
  int refinementlevel = reflevel;
  if (group.grouptype == CCTK_SCALAR or group.grouptype == CCTK_ARRAY) {
    assert (do_global_mode);
    refinementlevel = 0;
  }

  // HDF5 doesn't like 0-dimensional arrays
  if (group.grouptype == CCTK_SCALAR) group.dim = 1;

  // If the user requested so, select single precision data output
  hid_t memdatatype, filedatatype;
  HDF5_ERROR (memdatatype = CCTKtoHDF5_Datatype (cctkGH, group.vartype, 0));
  HDF5_ERROR (filedatatype = CCTKtoHDF5_Datatype (cctkGH, group.vartype,
                          out_single_precision and not called_from_checkpoint));

  // create a file access property list to use the CORE virtual file driver
  hid_t plist;
  HDF5_ERROR (plist = H5Pcreate (H5P_FILE_ACCESS));
  HDF5_ERROR (H5Pset_fapl_core (plist, 0, 0));

  // Traverse all maps
  BEGIN_MAP_LOOP (cctkGH, group.grouptype) {
    // Collect the set of all components' bboxes
    ibset bboxes;
    BEGIN_COMPONENT_LOOP (cctkGH, group.grouptype) {
      // Using "interior" removes ghost zones and refinement boundaries.
      bboxes += arrdata.at(gindex).at(Carpet::map).dd->
                boxes.at(mglevel).at(refinementlevel).at(component).interior;
    } END_COMPONENT_LOOP;

    // Normalise the set, i.e., try to represent the set with fewer bboxes.
    //
    // According to Cactus conventions, DISTRIB=CONSTANT arrays
    // (including grid scalars) are assumed to be the same on all
    // processors and are therefore stored only by processor 0.
    if (group.disttype != CCTK_DISTRIB_CONSTANT) bboxes.normalize();

    // Loop over all components in the bbox set
    int bbox_id = 0;
    for (ibset::const_iterator bbox  = bboxes.begin();
                               bbox != bboxes.end();
                               bbox++, bbox_id++) {
      // Get the shape of the HDF5 dataset (in Fortran index order)
      hsize_t shape[dim];
      hsize_t num_elems = 1;
      for (int d = 0; d < group.dim; ++d) {
        shape[group.dim-1-d] = (bbox->shape() / bbox->stride())[d];
        num_elems *= shape[group.dim-1-d];
      }

      // Don't create zero-sized components
      if (num_elems == 0) continue;

      // create the dataset on the I/O processor
      // skip DISTRIB=CONSTANT components from processors other than 0
      hid_t memfile = -1, memdataset = -1;
      hid_t dataspace = -1, dataset = -1;
      if (dist::rank() == 0 and
          (bbox_id == 0 or group.disttype != CCTK_DISTRIB_CONSTANT)) {
        // Construct a file-wide unique HDF5 dataset name
        // (only add parts with varying metadata)
        ostringstream datasetname;
        datasetname << fullname
                    << " it=" << cctkGH->cctk_iteration
                    << " tl=" << request->timelevel;
        if (mglevels > 1) datasetname << " ml=" << mglevel;
        if (group.grouptype == CCTK_GF) {
          if (Carpet::maps > 1) datasetname << " m="  << Carpet::map;
          datasetname << " rl=" << refinementlevel;
        }
        if (bboxes.setsize () > 1 and group.disttype != CCTK_DISTRIB_CONSTANT) {
          datasetname << " c=" << bbox_id;
        }

        // We create a temporary HDF5 file in memory (using the core VFD)
        // in order to do the recombination of individual processor components
        // for a single dataset.
        // Although this requires some more memory, it should be much faster
        // than recombining an HDF5 dataset on a disk file.
        HDF5_ERROR (memfile = H5Fcreate ("tempfile", H5F_ACC_EXCL, H5P_DEFAULT,
                                         plist));
        HDF5_ERROR (dataspace = H5Screate_simple (group.dim, shape, NULL));
        HDF5_ERROR (memdataset = H5Dcreate (memfile, datasetname.str().c_str(),
                                         filedatatype, dataspace, H5P_DEFAULT));

        // remove an already existing dataset of the same name
        if (request->check_exist) {
          H5E_BEGIN_TRY {
            H5Gunlink (outfile, datasetname.str().c_str());
          } H5E_END_TRY;
        }
        HDF5_ERROR (dataset = H5Dcreate (outfile, datasetname.str().c_str(),
                                         filedatatype, dataspace, H5P_DEFAULT));
      }

      // Loop over all components
      bool first_time = true;
      BEGIN_COMPONENT_LOOP (cctkGH, group.grouptype) {
        // Get the intersection of the current component with this combination
        // (use either the interior or exterior here, as we did above)
        ibbox const overlap = *bbox &
          arrdata.at(gindex).at(Carpet::map).dd->
          boxes.at(mglevel).at(refinementlevel).at(component).interior;

        // Continue if this component is not part of this combination
        if (overlap.empty()) continue;

        // Copy the overlap to the local processor
        const ggf* ff = arrdata.at(gindex).at(Carpet::map).data.at(var);
        const gdata* const data = (*ff) (request->timelevel,
                                         refinementlevel,
                                         component, mglevel);
        gdata* const processor_component = data->make_typed (request->vindex);

        processor_component->allocate (overlap, 0);
        for (comm_state state(group.vartype); not state.done(); state.step()) {
          processor_component->copy_from (state, data, overlap);
        }

        // Write data
        if (dist::rank() == 0) {
          const void *data = (const void *) processor_component->storage();

          // As per Cactus convention, DISTRIB=CONSTANT arrays
          // (including grid scalars) are assumed to be the same on
          // all processors and are therefore stored only by processor
          // 0.
          //
          // Warn the user if this convention is violated.
          if (bbox_id > 0 and group.disttype == CCTK_DISTRIB_CONSTANT) {
            const void *proc0 = CCTK_VarDataPtrI (cctkGH, request->timelevel,
                                                  request->vindex);

            if (memcmp (proc0, data,
                        num_elems * CCTK_VarTypeSize(group.vartype))) {
              CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                          "values for DISTRIB=CONSTANT grid variable '%s' "
                          "(timelevel %d) differ between processors 0 and %d; "
                          "only the array from processor 0 will be stored",
                          fullname, request->timelevel, bbox_id);
            }
          } else {
            hsize_t overlapshape[dim];
#if (H5_VERS_MAJOR == 1 && H5_VERS_MINOR == 6 && H5_VERS_RELEASE >= 4)
            hsize_t overlaporigin[dim];
#else
            hssize_t overlaporigin[dim];
#endif
            for (int d = 0; d < group.dim; ++d) {
              overlaporigin[group.dim-1-d] =
                ((overlap.lower() - bbox->lower()) / overlap.stride())[d];
              overlapshape[group.dim-1-d]  =
                (overlap.shape() / overlap.stride())[d];
            }

            // Write the processor component as a hyperslab into the recombined
            // dataset.
            hid_t overlap_dataspace;
            HDF5_ERROR (overlap_dataspace =
                        H5Screate_simple (group.dim, overlapshape, NULL));
            HDF5_ERROR (H5Sselect_hyperslab (dataspace, H5S_SELECT_SET,
                                             overlaporigin, NULL,
                                             overlapshape, NULL));
            HDF5_ERROR (H5Dwrite (memdataset, memdatatype, overlap_dataspace,
                                  dataspace, H5P_DEFAULT, data));
            HDF5_ERROR (H5Sclose (overlap_dataspace));

            // Add metadata information on the first time through
            // (have to do it inside of the COMPONENT_LOOP so that we have
            //  access to the cGH elements)
            if (first_time) {
              AddAttributes (cctkGH, fullname, group.dim, refinementlevel,
                             request, *bbox, dataset);
              first_time = false;
            }
          }
        }

        // Delete temporary copy of this component
        delete processor_component;

      } END_COMPONENT_LOOP;

      // Finally create the recombined dataset in the real HDF5 file on disk
      // (skip DISTRIB=CONSTANT components from processors other than 0)
      if (dist::rank() == 0 and
          (bbox_id == 0 or group.disttype != CCTK_DISTRIB_CONSTANT)) {
        void *data = malloc (H5Dget_storage_size (memdataset));
        HDF5_ERROR (H5Dread (memdataset, filedatatype, H5S_ALL, H5S_ALL,
                             H5P_DEFAULT, data));
        HDF5_ERROR (H5Dclose (memdataset));
        HDF5_ERROR (H5Fclose (memfile));
        HDF5_ERROR (H5Dwrite (dataset, filedatatype, H5S_ALL, H5S_ALL,
                              H5P_DEFAULT, data));
        free (data);
        HDF5_ERROR (H5Sclose (dataspace));
        HDF5_ERROR (H5Dclose (dataset));
      }

    } // for bboxes
  } END_MAP_LOOP;

  HDF5_ERROR (H5Pclose (plist));

  free (fullname);

  return 0;
}


int WriteVarChunkedSequential (const cGH* const cctkGH,
                               hid_t outfile,
                               const ioRequest* const request,
                               bool called_from_checkpoint)
{
  DECLARE_CCTK_PARAMETERS;

  char *fullname = CCTK_FullName(request->vindex);
  const int gindex = CCTK_GroupIndexFromVarI (request->vindex);
  assert (gindex >= 0 and gindex < (int) Carpet::arrdata.size ());
  const int var = request->vindex - CCTK_FirstVarIndexI (gindex);
  assert (var >= 0 and var < CCTK_NumVars ());
  cGroup group;
  CCTK_GroupData (gindex, &group);


  // Scalars and arrays have only one refinement level 0,
  // regardless of what the current refinement level is.
  // Output for them must be called in global mode.
  int refinementlevel = reflevel;
  if (group.grouptype == CCTK_SCALAR or group.grouptype == CCTK_ARRAY) {
    assert (do_global_mode);
    refinementlevel = 0;
  }

  // HDF5 doesn't like 0-dimensional arrays
  if (group.grouptype == CCTK_SCALAR) group.dim = 1;

  // If the user requested so, select single precision data output
  hid_t memdatatype, filedatatype;
  HDF5_ERROR (memdatatype = CCTKtoHDF5_Datatype (cctkGH, group.vartype, 0));
  HDF5_ERROR (filedatatype = CCTKtoHDF5_Datatype (cctkGH, group.vartype,
                         out_single_precision and not  called_from_checkpoint));

  // Traverse all maps
  BEGIN_MAP_LOOP (cctkGH, group.grouptype) {
    BEGIN_COMPONENT_LOOP (cctkGH, group.grouptype) {
      // Using "exterior" includes ghost zones and refinement boundaries.
      ibbox& bbox = arrdata.at(gindex).at(Carpet::map).dd->
                    boxes.at(mglevel).at(refinementlevel).at(component).exterior;

      // Get the shape of the HDF5 dataset (in Fortran index order)
      hsize_t shape[dim];
      hsize_t num_elems = 1;
      for (int d = 0; d < group.dim; ++d) {
        shape[group.dim-1-d] = (bbox.shape() / bbox.stride())[d];
        num_elems *= shape[group.dim-1-d];
      }

      // Don't create zero-sized components
      if (num_elems == 0) continue;

      // Copy the overlap to the local processor
      const ggf* ff = arrdata.at(gindex).at(Carpet::map).data.at(var);
      const gdata* const data = (*ff) (request->timelevel, refinementlevel,
                                       component, mglevel);
      gdata* const processor_component = data->make_typed (request->vindex);

      processor_component->allocate (bbox, 0);
      for (comm_state state(group.vartype); not state.done(); state.step()) {
        processor_component->copy_from (state, data, bbox);
      }

      // Write data on I/O processor 0
      if (dist::rank() == 0) {
        const void *data = (const void *) processor_component->storage();

        // As per Cactus convention, DISTRIB=CONSTANT arrays
        // (including grid scalars) are assumed to be the same on
        // all processors and are therefore stored only by processor 0.
        //
        // Warn the user if this convention is violated.
        if (component > 0 and group.disttype == CCTK_DISTRIB_CONSTANT) {
          const void *proc0 = CCTK_VarDataPtrI (cctkGH, request->timelevel,
                                                request->vindex);

          if (memcmp (proc0, data, num_elems*CCTK_VarTypeSize(group.vartype))) {
            CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
                        "values for DISTRIB=CONSTANT grid variable '%s' "
                        "(timelevel %d) differ between processors 0 and %d; "
                        "only the array from processor 0 will be stored",
                        fullname, request->timelevel, component);
          }
        } else {
          // Construct a file-wide unique HDF5 dataset name
          // (only add parts with varying metadata)
          ostringstream datasetname;
          datasetname << fullname
                      << " it=" << cctkGH->cctk_iteration
                      << " tl=" << request->timelevel;
          if (mglevels > 1) datasetname << " ml=" << mglevel;
          if (group.grouptype == CCTK_GF) {
            if (Carpet::maps > 1) datasetname << " m="  << Carpet::map;
            datasetname << " rl=" << refinementlevel;
          }
          if (arrdata.at(gindex).at(Carpet::map).dd->
              boxes.at(mglevel).at(refinementlevel).size () > 1 and
              group.disttype != CCTK_DISTRIB_CONSTANT) {
            datasetname << " c=" << component;
          }

          // remove an already existing dataset of the same name
          if (request->check_exist) {
            H5E_BEGIN_TRY {
              H5Gunlink (outfile, datasetname.str().c_str());
            } H5E_END_TRY;
          }

          hsize_t shape[dim];
          hssize_t origin[dim];
          for (int d = 0; d < group.dim; ++d) {
            origin[group.dim-1-d] = (bbox.lower() / bbox.stride())[d];
            shape[group.dim-1-d]  = (bbox.shape() / bbox.stride())[d];
          }

          // Write the component as an individual dataset
          hid_t dataspace, dataset;
          HDF5_ERROR (dataspace = H5Screate_simple (group.dim, shape, NULL));
          HDF5_ERROR (dataset = H5Dcreate (outfile, datasetname.str().c_str(),
                                           filedatatype, dataspace, H5P_DEFAULT));
          HDF5_ERROR (H5Sclose (dataspace));
          HDF5_ERROR (H5Dwrite (dataset, memdatatype, H5S_ALL, H5S_ALL,
                                H5P_DEFAULT, data));
          AddAttributes (cctkGH, fullname, group.dim, refinementlevel,
                         request, bbox, dataset);
          HDF5_ERROR (H5Dclose (dataset));
        }

      } // if dist::rank() == 0

      // Delete temporary copy of this component
      delete processor_component;

    } END_COMPONENT_LOOP;
  } END_MAP_LOOP;

  free (fullname);

  return 0;
}


int WriteVarChunkedParallel (const cGH* const cctkGH,
                             hid_t outfile,
                             const ioRequest* const request,
                             bool called_from_checkpoint)
{
  DECLARE_CCTK_PARAMETERS;

  char *fullname = CCTK_FullName(request->vindex);
  const int gindex = CCTK_GroupIndexFromVarI (request->vindex);
  assert (gindex >= 0 and gindex < (int) Carpet::arrdata.size ());
  const int var = request->vindex - CCTK_FirstVarIndexI (gindex);
  assert (var >= 0 and var < CCTK_NumVars ());
  cGroup group;
  CCTK_GroupData (gindex, &group);


  // Scalars and arrays have only one refinement level 0,
  // regardless of what the current refinement level is.
  // Output for them must be called in global mode.
  int refinementlevel = reflevel;
  if (group.grouptype == CCTK_SCALAR or group.grouptype == CCTK_ARRAY) {
    assert (do_global_mode);
    refinementlevel = 0;
  }

  // HDF5 doesn't like 0-dimensional arrays
  if (group.grouptype == CCTK_SCALAR) group.dim = 1;

  // If the user requested so, select single precision data output
  hid_t memdatatype, filedatatype;
  HDF5_ERROR (memdatatype = CCTKtoHDF5_Datatype (cctkGH, group.vartype, 0));
  HDF5_ERROR (filedatatype = CCTKtoHDF5_Datatype (cctkGH, group.vartype,
                          out_single_precision and not called_from_checkpoint));

  // Traverse all maps
  BEGIN_MAP_LOOP (cctkGH, group.grouptype) {
    BEGIN_LOCAL_COMPONENT_LOOP (cctkGH, group.grouptype) {

      const ggf* ff = arrdata.at(gindex).at(Carpet::map).data.at(var);
      const ibbox& bbox = (*ff) (request->timelevel, refinementlevel,
                                 group.disttype == CCTK_DISTRIB_CONSTANT ?
                                 0 : component, mglevel)->extent();

      // Don't create zero-sized components
      if (bbox.empty()) continue;

      // As per Cactus convention, DISTRIB=CONSTANT arrays
      // (including grid scalars) are assumed to be the same on
      // all processors and are therefore stored only by processor 0.
      void* data = cctkGH->data[request->vindex][request->timelevel];
      const void* mydata = data;
      if (group.disttype == CCTK_DISTRIB_CONSTANT) {

        MPI_Datatype datatype;
        switch (group.vartype) {
#define TYPECASE(N,T)                                                     \
          case  N: { T dummy; datatype = dist::datatype(dummy); } break;
#include "carpet_typecase.hh"
#undef TYPECASE
          default: assert (0 and "invalid datatype");
        }

        const size_t size = bbox.size() * CCTK_VarTypeSize (group.vartype);
        if (dist::rank() > 0) {
          data = malloc (size);
        }
        MPI_Bcast (data, bbox.size(), datatype, 0, MPI_COMM_WORLD);

        if (memcmp (mydata, data, size)) {
          CCTK_VWarn (2, __LINE__, __FILE__, CCTK_THORNSTRING,
                      "values for DISTRIB=CONSTANT grid variable '%s' "
                      "(timelevel %d) differ between processors 0 and %d; "
                      "only the array from processor 0 will be stored",
                      fullname, request->timelevel, component);
        }
      }

      // Construct a file-wide unique HDF5 dataset name
      // (only add parts with varying metadata)
      ostringstream datasetname;
      datasetname << fullname
                  << " it=" << cctkGH->cctk_iteration
                  << " tl=" << request->timelevel;
      if (mglevels > 1) datasetname << " ml=" << mglevel;
      if (group.grouptype == CCTK_GF) {
        if (Carpet::maps > 1) datasetname << " m="  << Carpet::map;
        datasetname << " rl=" << refinementlevel;
      }
      if (arrdata.at(gindex).at(Carpet::map).dd->
          boxes.at(mglevel).at(refinementlevel).size () > 1 and
          group.disttype != CCTK_DISTRIB_CONSTANT) {
        datasetname << " c=" << component;
      }

      // remove an already existing dataset of the same name
      if (request->check_exist) {
        H5E_BEGIN_TRY {
          H5Gunlink (outfile, datasetname.str().c_str());
        } H5E_END_TRY;
      }

      // Get the shape of the HDF5 dataset (in Fortran index order)
      hsize_t shape[dim];
      hssize_t origin[dim];
      for (int d = 0; d < group.dim; ++d) {
        origin[group.dim-1-d] = (bbox.lower() / bbox.stride())[d];
        shape[group.dim-1-d]  = (bbox.shape() / bbox.stride())[d];
      }

      // Write the component as an individual dataset
      hid_t dataspace, dataset;
      HDF5_ERROR (dataspace = H5Screate_simple (group.dim, shape, NULL));
      HDF5_ERROR (dataset = H5Dcreate (outfile, datasetname.str().c_str(),
                                       filedatatype, dataspace, H5P_DEFAULT));
      HDF5_ERROR (H5Sclose (dataspace));
      HDF5_ERROR (H5Dwrite (dataset, memdatatype, H5S_ALL, H5S_ALL,
                            H5P_DEFAULT, data));
      AddAttributes (cctkGH, fullname, group.dim, refinementlevel,
                     request, bbox, dataset);
      HDF5_ERROR (H5Dclose (dataset));

      if (data != mydata) free (data);

    } END_LOCAL_COMPONENT_LOOP;
  } END_MAP_LOOP;

  free (fullname);

  return 0;
}


// add attributes to an HDF5 dataset
static void AddAttributes (const cGH *const cctkGH, const char *fullname,
                           int vdim, int refinementlevel,
                           const ioRequest* request,
                           const ibbox& bbox, hid_t dataset)
{
  DECLARE_CCTK_ARGUMENTS;


  // Legacy arguments
  hid_t attr, dataspace, datatype;
  HDF5_ERROR (dataspace = H5Screate (H5S_SCALAR));
  HDF5_ERROR (attr = H5Acreate (dataset, "level", H5T_NATIVE_INT,
                                dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &refinementlevel));
  HDF5_ERROR (H5Aclose (attr));

  HDF5_ERROR (attr = H5Acreate (dataset, "carpet_mglevel", H5T_NATIVE_INT,
                                dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &mglevel));
  HDF5_ERROR (H5Aclose (attr));

  HDF5_ERROR (attr = H5Acreate (dataset, "timestep", H5T_NATIVE_INT,
                                dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &cctk_iteration));
  HDF5_ERROR (H5Aclose (attr));

  HDF5_ERROR (attr = H5Acreate (dataset, "group_timelevel", H5T_NATIVE_INT,
                                dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &request->timelevel));
  HDF5_ERROR (H5Aclose (attr));

  HDF5_ERROR (attr = H5Acreate (dataset, "time", HDF5_REAL,
                                dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, HDF5_REAL, &cctk_time));
  HDF5_ERROR (H5Aclose (attr));

  HDF5_ERROR (datatype = H5Tcopy (H5T_C_S1));
  HDF5_ERROR (H5Tset_size (datatype, strlen (fullname)));
  HDF5_ERROR (attr = H5Acreate (dataset, "name", datatype,
                                dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, datatype, fullname));
  HDF5_ERROR (H5Aclose (attr));

#if 0
  // FIXME TR: output bbox and nghostzones again for chunked output
  // Cactus arguments
  WriteAttribute (dataset, "cctk_bbox", cctk_bbox, 2*vdim);
  WriteAttribute (dataset, "cctk_nghostzones", cctk_nghostzones, vdim);
#endif

  // write bbox attributes if we have coordinate system info
  CCTK_REAL origin[dim], delta[dim];
  char *groupname = CCTK_GroupNameFromVarI (request->vindex);
  int coord_system_handle = Coord_GroupSystem (cctkGH, groupname);
  free (groupname);

  HDF5_ERROR (H5Sclose (dataspace));
  hsize_t size = vdim;
  HDF5_ERROR (dataspace = H5Screate_simple (1, &size, NULL));

  CCTK_INT coord_handles[dim];
  if (coord_system_handle >= 0 and
      Util_TableGetIntArray (coord_system_handle, vdim,
                             coord_handles, "COORDINATES") >= 0) {
    const ibbox& baseext =
    vdd.at(Carpet::map)->bases.at(mglevel).at(reflevel).exterior;

    const ivect pos = (bbox.lower() - baseext.lower()) / bbox.stride();

    for (int d = 0; d < vdim; d++) {
      Util_TableGetReal (coord_handles[d], &origin[d], "COMPMIN");
      Util_TableGetReal (coord_handles[d], &delta[d], "DELTA");
      delta[d]  /= cctk_levfac[d];
      origin[d] += delta[d] * (cctk_levoff[d] / cctk_levoffdenom[d] + pos[d]);
    }

    HDF5_ERROR (attr = H5Acreate (dataset, "origin", HDF5_REAL,
                                  dataspace, H5P_DEFAULT));
    HDF5_ERROR (H5Awrite (attr, HDF5_REAL, origin));
    HDF5_ERROR (H5Aclose (attr));
    HDF5_ERROR (attr = H5Acreate (dataset, "delta", HDF5_REAL,
                                  dataspace, H5P_DEFAULT));
    HDF5_ERROR (H5Awrite (attr, HDF5_REAL, delta));
    HDF5_ERROR (H5Aclose (attr));
  }

  vect<int, dim> iorigin = bbox.lower() / bbox.stride();
  HDF5_ERROR (attr = H5Acreate (dataset, "iorigin", H5T_NATIVE_INT,
                                dataspace, H5P_DEFAULT));
  HDF5_ERROR (H5Awrite (attr, H5T_NATIVE_INT, &iorigin[0]));
  HDF5_ERROR (H5Aclose (attr));

  HDF5_ERROR (H5Tclose (datatype));
  HDF5_ERROR (H5Sclose (dataspace));
}


} // namespace CarpetIOHDF5

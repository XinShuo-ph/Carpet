#include <cassert>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stack>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "gh.hh"
#include "vect.hh"

#include "carpet.hh"
#include "CAR.hh"

extern "C" {
  static const char* rcsid = "$Header:$";
  CCTK_FILEVERSION(Carpet_CarpetAdaptiveregrid_regrid_cc);
}



namespace CarpetAdaptiveRegrid {
  
  using namespace std;
  using namespace Carpet;

  static gh::mexts local_bbsss;

  extern "C" {
    void CCTK_FCALL CCTK_FNAME(copy_mask)
      (const CCTK_INT& snx, const CCTK_INT& sny, const CCTK_INT& snz,
       const CCTK_INT* smask, const CCTK_INT sbbox[3][3],
       const CCTK_INT& dnx, const CCTK_INT& dny, const CCTK_INT& dnz,
       CCTK_INT* dmask, const CCTK_INT dbbox[3][3]);
    void CCTK_FCALL CCTK_FNAME(check_box)
      (const CCTK_INT& nx, const CCTK_INT& ny, const CCTK_INT& nz,
       const CCTK_INT* mask,
       CCTK_INT* sum_x, CCTK_INT* sum_y, CCTK_INT* sum_z,
       CCTK_INT* sig_x, CCTK_INT* sig_y, CCTK_INT* sig_z,
       const CCTK_INT bbox[3][3],
       CCTK_INT newbbox1[3][3], CCTK_INT newbbox2[3][3],
       const CCTK_INT& min_width, const CCTK_REAL& min_fraction,
       CCTK_INT& didit);
  }

  ivect pos2int (const cGH* const cctkGH, const gh& hh,
                 const rvect & rpos, const int rl);
  rvect int2pos (const cGH* const cctkGH, const gh& hh,
                 const ivect & ipos, const int rl);
  
  CCTK_INT CarpetAdaptiveRegrid_Regrid (CCTK_POINTER_TO_CONST const cctkGH_,
                                CCTK_POINTER const bbsss_,
                                CCTK_POINTER const obss_,
                                CCTK_POINTER const pss_,
				CCTK_INT force)
  {
    DECLARE_CCTK_PARAMETERS;
    
    const cGH * const cctkGH = (const cGH *) cctkGH_;
    
    gh::mexts  & bbsss = * (gh::mexts  *) bbsss_;
    gh::rbnds  & obss  = * (gh::rbnds  *) obss_;
    gh::rprocs & pss   = * (gh::rprocs *) pss_;
    
    gh const & hh = *vhh.at(Carpet::map);
    
    assert (is_singlemap_mode());

    if (local_bbsss.empty()) { // It's the first call
      // Is this really the right thing to do on 
      // multiprocessors?
      local_bbsss = bbsss;
    }

    // FIXME: We should check that the local reflevel "agrees"
    // with what is passed in.
    
    // In force mode (force == true) we do not check the
    // CarpetAdaptiveregrid parameters

    if (!force) {

      assert (regrid_every == -1 || regrid_every == 0
	      || regrid_every % maxmglevelfact == 0);
    
      // Return if no regridding is desired
      if (regrid_every == -1) return 0;
      
      // Return if we want to regrid during initial data only, and this
      // is not the time for initial data
      if (regrid_every == 0 && cctkGH->cctk_iteration != 0) return 0;

      // Return if we want to regrid regularly, but not at this time
      if (regrid_every > 0 && cctkGH->cctk_iteration != 0
	  && (cctkGH->cctk_iteration-1) % regrid_every != 0)
      {
	return 0;
      }

      // Return if it's initial data as we can't handle that yet.
      if (cctkGH->cctk_iteration ==0) {
        return 0;
      }

    }    

    if (reflevel == maxreflevels - 1) return 0;

//     cout << "bbsss at start" << endl << bbsss << endl;
//     cout << "obss at start" << endl << obss << endl;
//     cout << "pss at start" << endl << pss << endl;

    CCTK_INT do_recompose;
    do_recompose = 1;

    // We assume that we are called in fine->coarse order
    // So that on finer levels regridding has already been done

    // So the full algorithm should look something like:

    // Find how big the first bounding box should be on this level
    //   Do this by finding min lower and max upper bounds of all bboxes
    // Allocate box
    // Fill errors from local arrays
    // If grandchildren exist use their bboxes (expanded) to add to errors
    // Reduce errors (MPI sum)
    // Set errors to 0/1
    // Define vectors of bboxes final (empty) and todo (contains bbox)
    // Define vector of masks (contains error mask)
    // Loop over all entries in todo:
    //   Setup appropriate 1d array memory
    //   Call fortran routine
    //   If return is:
    //     zero: add bbox to final
    //     one:  add new bbox to todo and assoc mask to masklist
    //     two:  add both new bboxs to todo and assoc masks to masklist
    //   

    vector<ibbox> bbs = local_bbsss.at(mglevel).at(reflevel);
    
    stack<ibbox> final;

    vector<vector<ibbox> > bbss = local_bbsss.at(0);
      
    //    ivect low = 100000, upp = -1;

    bool did_regrid = false;

    rvect physical_min, physical_max;
    rvect interior_min, interior_max;
    rvect exterior_min, exterior_max;
    rvect base_spacing;
    int ierr = GetDomainSpecification
      (dim, &physical_min[0], &physical_max[0],
       &interior_min[0], &interior_max[0],
       &exterior_min[0], &exterior_max[0], &base_spacing[0]);
    assert (!ierr);

    for ( vector<ibbox>::const_iterator bbi = bbs.begin(); 
          bbi != bbs.end();
          ++bbi) 
    {
      //      low = min(low, bbi->lower());
      //      upp = max(upp, bbi->upper());
      //    }
    
      ivect low = bbi->lower();
      ivect upp = bbi->upper();
      
      // low and upp now define the starting bbox.

      ibbox bb(low, upp, bbs.at(0).stride());
    
      if (verbose) {
        ostringstream buf;
        buf << "Found the local size of the box: " << endl << bb;
        CCTK_INFO(buf.str().c_str());
      }
      
      vector<CCTK_INT> mask(prod(bb.shape()/bb.stride()), 0);
      
      if (veryverbose) {
        ostringstream buf;
        buf << "Allocated mask size: " << bb.shape()/bb.stride() 
            << " (points: " << prod(bb.shape()/bb.stride()) << ")";
        CCTK_INFO(buf.str().c_str());
      }
      

      // Setup the mask.
      
      const ibbox& baseext = 
        vdd.at(Carpet::map)->bases.at(mglevel).at(reflevel).exterior;
      ivect imin = (bb.lower() - baseext.lower())/bb.stride(), 
        imax = (bb.upper() - baseext.lower())/bb.stride();

      BEGIN_LOCAL_COMPONENT_LOOP(cctkGH, CCTK_GF) 
      {
        const CCTK_REAL *error_var_ptr = 
          static_cast<const CCTK_REAL*>(CCTK_VarDataPtr(cctkGH, 0, error_var));
        const CCTK_REAL *x_var_ptr = 
          static_cast<const CCTK_REAL*>(CCTK_VarDataPtr(cctkGH, 0, "Grid::x"));
        const CCTK_REAL *y_var_ptr = 
          static_cast<const CCTK_REAL*>(CCTK_VarDataPtr(cctkGH, 0, "Grid::y"));
        const CCTK_REAL *z_var_ptr = 
          static_cast<const CCTK_REAL*>(CCTK_VarDataPtr(cctkGH, 0, "Grid::z"));
     
        assert(all(imin >= 0));
        assert(all(imax >= 0));
        // FIXME: Why should the following assert be true?
        //      assert(all(imax < ivect::ref(cctkGH->cctk_lsh)));
        assert(all(imin <= imax));
      
        for (CCTK_INT k = 0; k < cctkGH->cctk_lsh[2]; ++k) {
          for (CCTK_INT j = 0; j < cctkGH->cctk_lsh[1]; ++j) {
            for (CCTK_INT i = 0; i < cctkGH->cctk_lsh[0]; ++i) {
              CCTK_INT index = CCTK_GFINDEX3D(cctkGH, i, j, k);
              if (abs(error_var_ptr[index]) > max_error) {
                CCTK_INT ii = i + cctkGH->cctk_lbnd[0] - imin[0];
                CCTK_INT jj = j + cctkGH->cctk_lbnd[1] - imin[1];
                CCTK_INT kk = k + cctkGH->cctk_lbnd[2] - imin[2];
                // Check that this point actually intersects with 
                // this box (if this component was actually a
                // different grid on the same processor, it need not)
                if ( (ii >= 0) and (jj >= 0) and (kk >= 0) and 
                     (ii <= imax[0] - imin[0]) and
                     (jj <= imax[1] - imin[1]) and
                     (kk <= imax[2] - imin[2]) )
                {
                  assert (ii >= 0);
                  assert (jj >= 0);
                  assert (kk >= 0);
                  assert (ii <= imax[0] - imin[0]);
                  assert (jj <= imax[1] - imin[1]);
                  assert (kk <= imax[2] - imin[2]);
                  CCTK_INT mindex = ii + 
                    (imax[0] - imin[0] + 1)*(jj + (imax[1] - imin[1] + 1) * kk);
                  mask[mindex] = 1;
                  if (veryverbose) {
                    CCTK_VInfo(CCTK_THORNSTRING, "In error at point"
                               "\n(%g,%g,%g) [%d,%d,%d] [[%d,%d,%d]]",
                               x_var_ptr[index],
                               y_var_ptr[index],
                               z_var_ptr[index],
                               ii, jj, kk, i,j,k);
                  }
                }
              }
            }
          }
        }
      } END_LOCAL_COMPONENT_LOOP;
      
      // FIXME    
      // If grandchildren exist use their bboxes (expanded) to add to errors
      // Reduce errors (MPI sum)
      // Set errors to 0/1

      // Pad the errors: stage 1 - buffer points marked as 2.

      for (CCTK_INT k = 0; k < imax[2] - imin[2]; k++) {
        for (CCTK_INT j = 0; j < imax[1] - imin[1]; j++) {
          for (CCTK_INT i = 0; i < imax[0] - imin[0]; i++) {
            CCTK_INT index =  i + 
              (imax[0] - imin[0] + 1)*(j + (imax[1] - imin[1] + 1) * k);
            if (mask[index] == 1) {
              for (CCTK_INT kk = max(k-pad, 0); 
                   kk < min(k+pad+1, imax[2] - imin[2]);
                   ++kk)
              {
                for (CCTK_INT jj = max(j-pad, 0); 
                     jj < min(j+pad+1, imax[1] - imin[1]);
                     ++jj)
                {
                  for (CCTK_INT ii = max(i-pad, 0); 
                       ii < min(i+pad+1, imax[0] - imin[0]);
                       ++ii)
                  {
                    CCTK_INT mindex = ii + 
                      (imax[0] - imin[0] + 1)*
                      (jj + (imax[1] - imin[1] + 1) * kk);
                    if (!mask[mindex]) mask[mindex] = 2;
                  }
                }
              }
            }
          }
        }
      }
      // stage 2: all buffer points marked truly in error.
      // Also mark if there are any errors.
      bool should_regrid = false;
      for (CCTK_INT k = 0; k < imax[2] - imin[2]; k++) {
        for (CCTK_INT j = 0; j < imax[1] - imin[1]; j++) {
          for (CCTK_INT i = 0; i < imax[0] - imin[0]; i++) {
            CCTK_INT index =  i + 
              (imax[0]-imin[0] + 1)*(j + (imax[1] - imin[1] + 1) * k);
            if (mask[index] > 1) mask[index] = 1;
            if ((veryverbose) and (mask[index])) {
              CCTK_VInfo(CCTK_THORNSTRING, "Mask set at point"
                         "\n[%d,%d,%d]",
                         i,j,k);
            } 
            should_regrid |= (mask[index]);
            did_regrid |= should_regrid;
          }
        }
      }    
      
      // Define vectors of bboxes final (empty) and todo (contains bbox)

      if (should_regrid) {
    
        stack<ibbox> todo;
        
        todo.push(bb);
        
        // Define vector of masks (contains error mask)
        
        stack<vector<CCTK_INT> > masklist;
        
        masklist.push(mask);
        
        // Loop over all entries in todo:
        //   Setup appropriate 1d array memory
        //   Call fortran routine
        //   If return is:
        //     zero: add bbox to final
        //     one:  add new bbox to todo and assoc mask to masklist
        //     two:  add both new bboxs to todo and assoc masks to masklist
        
        while (!todo.empty())
        {
          
          ibbox bb = todo.top(); todo.pop();
          vector<CCTK_INT> mask = masklist.top(); masklist.pop();
          
          CCTK_INT nx = bb.shape()[0]/bb.stride()[0];
          CCTK_INT ny = bb.shape()[1]/bb.stride()[1];
          CCTK_INT nz = bb.shape()[2]/bb.stride()[2];
          
          if (verbose) {
            ostringstream buf;
            buf << "todo loop. Box: " << endl << bb;
            CCTK_INFO(buf.str().c_str());
          }
          
          vector<CCTK_INT> sum_x(nx, 0);
          vector<CCTK_INT> sig_x(nx, 0);
          vector<CCTK_INT> sum_y(ny, 0);
          vector<CCTK_INT> sig_y(ny, 0);
          vector<CCTK_INT> sum_z(nz, 0);
          vector<CCTK_INT> sig_z(nz, 0);
          
          CCTK_INT fbbox[3][3], fbbox1[3][3], fbbox2[3][3];
          
          for (CCTK_INT d = 0; d < 3; ++d) {
            fbbox[0][d] = bb.lower()[d];
            fbbox[1][d] = bb.upper()[d];
            fbbox[2][d] = bb.stride()[d];
          }
          
          CCTK_INT didit;
          
          CCTK_FNAME(check_box)(nx, ny, nz,
                                &mask.front(),
                                &sum_x.front(), &sum_y.front(), &sum_z.front(),
                                &sig_x.front(), &sig_y.front(), &sig_z.front(),
                                fbbox,
                                fbbox1, fbbox2,
                                min_width, min_fraction,
                                didit);
          
          if (didit == 0) {
          
            final.push(bb);          

            if (verbose) {
              ostringstream buf;
              buf << "todo loop. Box pushed to final: " 
                  << endl << bb;
              CCTK_INFO(buf.str().c_str());
            }
          }
          else if (didit == 1) {
            
            ibbox newbbox1(ivect::ref(&fbbox1[0][0]),
                           ivect::ref(&fbbox1[1][0]),
                           ivect::ref(&fbbox1[2][0]));
            todo.push(newbbox1);
            
            CCTK_INT dnx = newbbox1.shape()[0]/newbbox1.stride()[0];
            CCTK_INT dny = newbbox1.shape()[1]/newbbox1.stride()[1];
            CCTK_INT dnz = newbbox1.shape()[2]/newbbox1.stride()[2];
            
            vector<CCTK_INT>  
              newmask1(prod(newbbox1.shape()/newbbox1.stride()), 0);
            
            CCTK_FNAME(copy_mask)(nx, ny, nz,
                                  &mask.front(), fbbox,
                                  dnx, dny, dnz,
                                  &newmask1.front(), fbbox1);
            masklist.push(newmask1);
            
            if (verbose) {
              ostringstream buf;
              buf << "todo loop. New (single) box created: " 
                  << endl << newbbox1;
              CCTK_INFO(buf.str().c_str());
            }
          }
          else if (didit == 2) {
            
            ibbox newbbox1(ivect::ref(&fbbox1[0][0]),
                           ivect::ref(&fbbox1[1][0]),
                           ivect::ref(&fbbox1[2][0]));
            todo.push(newbbox1);
            ibbox newbbox2(ivect::ref(&fbbox2[0][0]),
                           ivect::ref(&fbbox2[1][0]),
                           ivect::ref(&fbbox2[2][0]));
            todo.push(newbbox2);
            
            CCTK_INT dnx = newbbox1.shape()[0]/newbbox1.stride()[0];
            CCTK_INT dny = newbbox1.shape()[1]/newbbox1.stride()[1];
            CCTK_INT dnz = newbbox1.shape()[2]/newbbox1.stride()[2];
            
            vector<CCTK_INT>  
              newmask1(prod(newbbox1.shape()/newbbox1.stride()), 0);
            
            CCTK_FNAME(copy_mask)(nx, ny, nz,
                                  &mask.front(), fbbox,
                                  dnx, dny, dnz,
                                  &newmask1.front(), fbbox1);
            masklist.push(newmask1);
            
            dnx = newbbox2.shape()[0]/newbbox2.stride()[0];
            dny = newbbox2.shape()[1]/newbbox2.stride()[1];
            dnz = newbbox2.shape()[2]/newbbox2.stride()[2];
            
            vector<CCTK_INT>  
              newmask2(prod(newbbox2.shape()/newbbox2.stride()), 0);
            
            CCTK_FNAME(copy_mask)(nx, ny, nz,
                                  &mask.front(), fbbox,
                                  dnx, dny, dnz,
                                  &newmask2.front(), fbbox2);
            masklist.push(newmask2);
            
            if (verbose) {
              ostringstream buf;
              buf << "todo loop. New (double) box created. Box 1: " 
                  << endl << newbbox1
                  << "                                     Box 2: "
                  << endl << newbbox2;
              CCTK_INFO(buf.str().c_str());
            }
          }
          else {
            CCTK_WARN(0, "The fortran routine must be confused.");
          }
          
        } // loop over todo vector (boxes needing to be done).
      } // should regrid.
    } // Loop over boxes on the parent grid.

    if (did_regrid) {

      // Fixup the stride
      vector<ibbox> newbbs;
      vector<bbvect> obs;
      while (! final.empty()) {
        ibbox bb = final.top(); final.pop();
        ivect ilo = bb.lower();
        ivect ihi = bb.upper();
        rvect lo = int2pos(cctkGH, hh, ilo, reflevel);
        rvect hi = int2pos(cctkGH, hh, ihi, reflevel);
        rvect str = base_spacing * 
          ipow((CCTK_REAL)mgfact, basemglevel) / ipow(reffact, reflevel);
        rbbox newbbcoord(lo, hi, str);
        
        if (veryverbose) {
          ostringstream buf;
          buf << "Dealing with boundaries. Coord box is:"
              << endl << newbbcoord;
          CCTK_INFO(buf.str().c_str());
        }
        
        // FIXME: Set the correct ob here.
        
        bbvect ob(false);
        for (int d=0; d<dim; ++d) {
          assert (mglevel==0);
          
          // Find the size of the physical domain
          
          rvect const spacing = base_spacing * 
            ipow((CCTK_REAL)mgfact, basemglevel) / ipow(reffact, reflevel+1);
          ierr = ConvertFromPhysicalBoundary
            (dim, &physical_min[0], &physical_max[0],
             &interior_min[0], &interior_max[0],
             &exterior_min[0], &exterior_max[0], &spacing[0]);
          assert (!ierr);
          
          // If need be clip the domain
          
          rvect lo = newbbcoord.lower();
          if (newbbcoord.lower()[d] < physical_min[d]) {
            lo[d] = exterior_min[d];
          }
          rvect up = newbbcoord.upper();
          if (newbbcoord.upper()[d] > physical_max[d]) {
            up[d] = exterior_max[d];
          }
          rvect str = newbbcoord.stride();
          
          // Set the ob if outside the physical domain
          
          ob[d][0] = 
            abs(lo[d] - exterior_min[d]) < 1.0e-6 * spacing[d];
          ob[d][1] = 
            abs(up[d] - exterior_max[d]) < 1.0e-6 * spacing[d];
          
          newbbcoord = rbbox(lo, up, str);
        }
        if (verbose) {
          ostringstream buf;
          buf << "Done dealing with boundaries. Coord box is:"
              << endl << newbbcoord << endl
              << "obox is:" << endl << ob;
          CCTK_INFO(buf.str().c_str());
        }
        
        // Convert back to integer coordinates
        // We have to do this on the fine grid to ensure that
        // it is correct for an outer boundary with odd numbers
        // of ghost zones where the bbox does not align with the parent.
        
        ilo = pos2int(cctkGH, hh, newbbcoord.lower(), reflevel+1);
        ihi = pos2int(cctkGH, hh, newbbcoord.upper(), reflevel+1);
        ivect istr = bb.stride() / reffact;
        
        // Check that the width is sufficient
        // This can only be too small if the domain was clipped
        for (int d=0; d < dim; ++d) {
          if (ihi[d] - ilo[d] < min_width * istr[d]) {
            if (ob[d][0]) {
              if (ob[d][1]) {
                CCTK_WARN(0, "The domain is too small?!");
              }
              ihi[d] = ilo[d] + min_width * istr[d];
            }
            else if (ob[d][1]) {
              if (ob[d][0]) {
                CCTK_WARN(0, "The domain is too small?!");
              }
              ilo[d] = ihi[d] - min_width * istr[d];
            }
            else {
              CCTK_WARN(0, "The grid is unclipped and too small?!");
            }
          }
        }
        
        ibbox newbb(ilo, ihi, istr);          
        
        if (verbose) {
          ostringstream buf;
          buf << "After dealing with boundaries. Final box is:"
              << endl << newbb;
          CCTK_INFO(buf.str().c_str());
        }
        
        newbbs.push_back (newbb);
        obs.push_back(ob);
      }
      
      
      // FIXME: check if the newbbs is really different from the current bbs
      //        if not, set do_recompose = 0
      bbs = newbbs;
      
      // Set local bbss
      
      if (bbss.size() < reflevel+2) {
        if (verbose) {
          CCTK_INFO("Adding new refinement level");
        }
        bbss.resize(reflevel+2);
        obss.resize(reflevel+2);
        pss.resize(reflevel+2);
      }
      bbss.at(reflevel+1) = bbs;
      obss.at(reflevel+1) = obs;
      MakeMultigridBoxes (cctkGH, bbss, obss, local_bbsss);
      
      // make multiprocessor aware
      gh::cprocs ps;
      SplitRegions (cctkGH, bbs, obs, ps);    
      
      bbss.at(reflevel+1) = bbs;
      obss.at(reflevel+1) = obs;
      pss.at(reflevel+1) = ps;
    
    } // did_regrid?
    else
    {
      if (bbss.size() > reflevel+1) {
        if (verbose) {
          CCTK_INFO("Removing refinement level");
        }
      }
      bbss.resize(reflevel+1);
      obss.resize(reflevel+1);
      // Set local bbsss
      MakeMultigridBoxes (cctkGH, bbss, obss, local_bbsss);
      
      pss.resize(reflevel+1);
      
      do_recompose = 1;
    }
    
    // make multigrid aware
    MakeMultigridBoxes (cctkGH, bbss, obss, bbsss);

//     cout << "bbsss done:" << endl << bbsss << endl;
//     cout << "obss done:" << endl << obss << endl;
//     cout << "pss done:" << endl << pss << endl;
    
    return do_recompose;
  }
  
  ivect pos2int (const cGH* const cctkGH, const gh& hh,
                 const rvect & rpos, const int rl)
  {
    rvect global_lower, global_upper;
    for (int d=0; d<dim; ++d) {
      const int ierr = CCTK_CoordRange
	(cctkGH, &global_lower[d], &global_upper[d], d+1, 0, "cart3d");
      if (ierr<0) {
	global_lower[d] = 0;
	global_upper[d] = 1;
      }
    }
    const ivect global_extent (hh.baseextent.upper() - hh.baseextent.lower());
    
    const rvect scale  = rvect(global_extent) / (global_upper - global_lower);
    const int levfac = ipow(hh.reffact, rl);
    assert (all (hh.baseextent.stride() % levfac == 0));
    const ivect istride = hh.baseextent.stride() / levfac;
    
    const ivect ipos
      = (ivect(floor((rpos - global_lower) * scale / rvect(istride) + 0.5))
         * istride);
    
    return ipos;
  }
  
  rvect int2pos (const cGH* const cctkGH, const gh& hh,
                 const ivect & ipos, const int rl)
  {
    rvect global_lower, global_upper;
    for (int d=0; d<dim; ++d) {
      const int ierr = CCTK_CoordRange
	(cctkGH, &global_lower[d], &global_upper[d], d+1, 0, "cart3d");
      
      if (ierr<0) {
	global_lower[d] = 0;
	global_upper[d] = 1;
      }
    }
    const ivect global_extent (hh.baseextent.upper() - hh.baseextent.lower());
    
    const rvect scale  = rvect(global_extent) / (global_upper - global_lower);
    const int levfac = ipow(hh.reffact, rl);
    assert (all (hh.baseextent.stride() % levfac == 0));
    const ivect istride = hh.baseextent.stride() / levfac;    
    
    const rvect rpos
      = rvect(ipos) / scale + global_lower;
    
    return rpos;
  }
  
} // namespace CarpetAdaptiveRegrid

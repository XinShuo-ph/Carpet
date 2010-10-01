#ifndef DH_HH
#define DH_HH

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "bbox.hh"
#include "bboxset.hh"
#include "defs.hh"
#include "gh.hh"
#include "region.hh"
#include "vect.hh"

using namespace std;

#define CARPET_HAVE_BUFFER_WIDTHS

// Forward declaration
class ggf;
class dh;



// A data hierarchy (grid hierarchy plus ghost zones)
class dh {
  
  static list<dh*> alldh;
  list<dh*>::iterator alldhi;
  
  // Types
public:
  typedef list<ibbox>    iblist;
  typedef vector<iblist> iblistvect; // vector of lists
  
  typedef vector <pseudoregion_t> pvect;
  typedef vector <sendrecv_pseudoregion_t> srpvect;
  
  
  
  struct light_dboxes {
    
    // Region description:
    
    ibbox exterior;             // whole region (including boundaries)
    ibbox owned;                // evolved in time
    ibbox interior;             // interior (without ghost zones)
    
#if 0
    // TODO: Create a new datatype bboxarr for this?  Or get rid of
    // it?
    int numactive;
    static int const maxactive = 4;
    ibbox active[maxactive];    // owned minus buffers
#endif
    
    // Region statistics:
    typedef ibbox::size_type size_type;
    size_type exterior_size, owned_size, active_size;
    
#if 0
    static void ibset2ibboxs (ibset const& s, ibbox* bs, int& nbs);
    static void ibboxs2ibset (ibbox const* bs, int const& nbs, ibset& s);
#endif
    
    size_t memory () const CCTK_ATTRIBUTE_PURE;
    istream & input (istream & is);
    ostream & output (ostream & os) const;
  };
  
  struct local_dboxes {
    
    // Information about the processor-local region:
    
    ibset buffers;              // buffer zones
    vector<ibset> buffers_stepped; // buffer zones [substep]
    ibset active;               // owned minus buffers
    
    // Mask
    ibset restricted_region;                // filled by restriction
    ibset unused_region;                    // not used (overwritten later) region
    vect<vect<ibset,2>,dim> restriction_boundaries; // partly filled by restriction
    vect<vect<ibset,2>,dim> prolongation_boundaries; // partly used by prolongation
    
    // Refluxing
    vect<vect<ibset,2>,dim> coarse_boundary;
    vect<vect<ibset,2>,dim> fine_boundary;
    
    size_t memory () const CCTK_ATTRIBUTE_PURE;
    istream & input (istream & is);
    ostream & output (ostream & os) const;
  };
  
  struct full_dboxes {
    
    // Complete region description:
    
    ibbox exterior;             // whole region (including boundaries)
    
    b2vect is_outer_boundary;
    ibset outer_boundaries;     // outer boundary
    ibbox communicated;         // exterior without outer boundary
    
    ibset boundaries;           // ghost zones
    ibbox owned;                // evolved in time
    
    ibset buffers;              // buffer zones
    ibset active;               // owned minus buffers
    
    ibset sync;                 // filled by synchronisation
    ibset bndref;               // filled by boundary prolongation
    
    // For Cactus: (these are like boundary or owned, but include the
    // outer boundary)
    ibset ghosts;               // ghost zones, as seen from Cactus
    ibbox interior;             // interior (without ghost zones)
    
    bool operator== (full_dboxes const & b) const;
    bool operator!= (full_dboxes const & b) const
    {
      return not operator==(b);
    }
    
    size_t memory () const CCTK_ATTRIBUTE_PURE;
    istream & input (istream& is);
    ostream & output (ostream & os) const;
  };
  
  struct fast_dboxes {
    
    // Communication schedule:
    
    srpvect fast_mg_rest_sendrecv;
    srpvect fast_mg_prol_sendrecv;
    srpvect fast_ref_prol_sendrecv;
    srpvect fast_ref_rest_sendrecv;
    srpvect fast_sync_sendrecv;
    srpvect fast_ref_bnd_prol_sendrecv;
    
    // refluxing
    srpvect fast_ref_refl_sendrecv_0_0;
    srpvect fast_ref_refl_sendrecv_0_1;
    srpvect fast_ref_refl_sendrecv_1_0;
    srpvect fast_ref_refl_sendrecv_1_1;
    srpvect fast_ref_refl_sendrecv_2_0;
    srpvect fast_ref_refl_sendrecv_2_1;
    // Note: Unfortunately we can't use an array of srpvects since
    // this doesn't work with C++ member pointers.  We instead define
    // them explicitly above (bah!), and maintain a static array with
    // member pointers for easier access.
    static
    vect<vect<srpvect fast_dboxes::*,2>,dim> fast_ref_refl_sendrecv;
    static
    void init_fast_ref_refl_sendrecv ();
    
    // Regridding schedule:
    
    bool do_init;               // the srpvects below are only defined
                                // if this is true
    srpvect fast_old2new_sync_sendrecv;
    srpvect fast_old2new_ref_prol_sendrecv;
    
    bool operator== (fast_dboxes const & b) const CCTK_ATTRIBUTE_PURE;
    bool operator!= (fast_dboxes const & b) const
    {
      return not operator==(b);
    }
    
    size_t memory () const CCTK_ATTRIBUTE_PURE;
    istream & input (istream & is);
    ostream & output (ostream & os) const;
  };
  
  typedef vector<light_dboxes> light_cboxes; // ... for each component
  typedef vector<light_cboxes> light_rboxes; // ... for each refinement level
  typedef vector<light_rboxes> light_mboxes; // ... for each multigrid level
  
  typedef vector<local_dboxes> local_cboxes; // ... for each component
  typedef vector<local_cboxes> local_rboxes; // ... for each refinement level
  typedef vector<local_rboxes> local_mboxes; // ... for each multigrid level
  
  typedef vector<full_dboxes> full_cboxes; // ... for each component
  typedef vector<full_cboxes> full_rboxes; // ... for each refinement level
  typedef vector<full_rboxes> full_mboxes; // ... for each multigrid level
  
  typedef vector<fast_dboxes> fast_rboxes; // ... for each refinement level
  typedef vector<fast_rboxes> fast_mboxes; // ... for each multigrid level
  
private:
  
  void
  setup_bboxes ();
  
public:                         // should be readonly
  
  // Fields
  gh & h;                       // hierarchy
  gh::dh_handle gh_handle;
  
#if 0
  i2vect ghost_width;           // number of ghost zones
  i2vect buffer_width;          // number of buffer zones
  int prolongation_order_space; // order of spatial prolongation operator
#endif
  vector<i2vect> ghost_widths;  // number of ghost zones [rl]
  vector<i2vect> buffer_widths; // number of buffer zones [rl]
  vector<int> prolongation_orders_space; // order of spatial
                                         // prolongation operator [rl]
  
  light_mboxes light_boxes;     // grid hierarchy [ml][rl][c]
  local_mboxes local_boxes;     // grid hierarchy [ml][rl][lc]
  fast_mboxes fast_boxes;       // grid hierarchy [ml][rl][p]
  
  typedef list<ggf*>::iterator ggf_handle;
  list<ggf*> gfs;               // list of all grid functions
  
public:
  
  // Constructors
  dh (gh & h,
      vector<i2vect> const & ghost_widths, vector<i2vect> const & buffer_widths,
      vector<int> const & prolongation_orders_space);
  
  // Destructors
  ~dh ();
  
  // Helpers
  int prolongation_stencil_size (int rl) const CCTK_ATTRIBUTE_CONST;
  
  // Modifiers
  void regrid (bool do_init);
  void regrid_free (bool do_init);
  void recompose (int rl, bool do_prolongate);
  
private:
  int this_proc (int rl, int c) const CCTK_ATTRIBUTE_PURE;
  bool on_this_proc (int rl, int c) const CCTK_ATTRIBUTE_PURE;
  bool on_this_proc (int rl, int c, int cc) const CCTK_ATTRIBUTE_PURE;
  int this_oldproc (int rl, int c) const CCTK_ATTRIBUTE_PURE;
  bool on_this_oldproc (int rl, int c) const CCTK_ATTRIBUTE_PURE;
  
  static
  void
  broadcast_schedule (vector<fast_dboxes> & fast_level_otherprocs,
                      fast_dboxes & fast_level,
                      srpvect fast_dboxes::* const schedule_item);
  
public:
  // Grid function management
  ggf_handle add (ggf * f);
  void erase (ggf_handle fi);
  
  // Output
  size_t memory () const CCTK_ATTRIBUTE_PURE;
  static size_t allmemory () CCTK_ATTRIBUTE_PURE;
  ostream & output (ostream & os) const;
};



MPI_Datatype mpi_datatype (dh::light_dboxes const &) CCTK_ATTRIBUTE_CONST;
MPI_Datatype mpi_datatype (dh::fast_dboxes const &);
namespace dist {
  template<> inline MPI_Datatype mpi_datatype<dh::light_dboxes> ()
  CCTK_ATTRIBUTE_CONST;
  template<> inline MPI_Datatype mpi_datatype<dh::light_dboxes> ()
  { dh::light_dboxes dummy; return mpi_datatype(dummy); }
  template<> inline MPI_Datatype mpi_datatype<dh::fast_dboxes> ()
  CCTK_ATTRIBUTE_CONST;
  template<> inline MPI_Datatype mpi_datatype<dh::fast_dboxes> ()
  { dh::fast_dboxes dummy; return mpi_datatype(dummy); }
}

inline size_t memoryof (dh::light_dboxes const & b) CCTK_ATTRIBUTE_PURE;
inline size_t memoryof (dh::light_dboxes const & b)
{
  return b.memory ();
}

inline size_t memoryof (dh::local_dboxes const & b) CCTK_ATTRIBUTE_PURE;
inline size_t memoryof (dh::local_dboxes const & b)
{
  return b.memory ();
}

inline size_t memoryof (dh::full_dboxes const & b) CCTK_ATTRIBUTE_PURE;
inline size_t memoryof (dh::full_dboxes const & b)
{
  return b.memory ();
}

inline size_t memoryof (dh::fast_dboxes const & b) CCTK_ATTRIBUTE_PURE;
inline size_t memoryof (dh::fast_dboxes const & b)
{
  return b.memory ();
}

inline size_t memoryof (dh const & d) CCTK_ATTRIBUTE_PURE;
inline size_t memoryof (dh const & d)
{
  return d.memory ();
}

inline istream & operator>> (istream & is, dh::light_dboxes & b)
{
  return b.input (is);
}

inline istream & operator>> (istream & is, dh::local_dboxes & b)
{
  return b.input (is);
}

inline istream & operator>> (istream & is, dh::full_dboxes & b)
{
  return b.input (is);
}

inline istream & operator>> (istream & is, dh::fast_dboxes & b)
{
  return b.input (is);
}

inline ostream & operator<< (ostream & os, dh::light_dboxes const & b)
{
  return b.output (os);
}

inline ostream & operator<< (ostream & os, dh::local_dboxes const & b)
{
  return b.output (os);
}

inline ostream & operator<< (ostream & os, dh::full_dboxes const & b)
{
  return b.output (os);
}

inline ostream & operator<< (ostream & os, dh::fast_dboxes const & b)
{
  return b.output (os);
}

inline ostream & operator<< (ostream & os, dh const & d)
{
  return d.output (os);
}



#endif // DH_HH

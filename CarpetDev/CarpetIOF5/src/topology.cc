#include <cassert>
#include <sstream>

#include <hdf5.h>

#include "cctk.h"

#include "topology.hh"
#include "utils.hh"



namespace CarpetIOF5 {
  
  namespace F5 {
    
    topology_t::
    topology_t (simulation_t & simulation)
      : m_simulation (simulation)
    {
    }
    
    
    
    topology_t::
    ~ topology_t ()
    {
    }
    
    
    
    hid_t topology_t::
    get_hdf5_topology()
      const
    {
      return m_hdf5_topology;
    }
    
    
    
    bool topology_t::
    invariant()
      const
    {
      return m_hdf5_topology >= 0;
    }
    
    
    
    unigrid_topology_t::
    unigrid_topology_t (simulation_t & simulation)
      : topology_t (simulation)
    {
      char const * const name = "Vertices";
      m_hdf5_topology
        = open_or_create_group (m_simulation.get_hdf5_simulation(), name);
      assert (m_hdf5_topology >= 0);
      
      assert (invariant());
    }
    
    
    
    unigrid_topology_t::
    ~ unigrid_topology_t ()
    {
      herr_t const herr = H5Gclose (m_hdf5_topology);
      assert (! herr);
    }
    
    
    
    bool unigrid_topology_t::
    invariant()
      const
    {
      return topology_t::invariant();
    }
    
    
    
    mesh_refinement_topology_t::
    mesh_refinement_topology_t (simulation_t & simulation,
                                int const refinement_level,
                                int const max_refinement_levels,
                                int const level_refinement_factor,
                                int const max_refinement_factor)
      : topology_t (simulation),
        m_refinement_level (refinement_level),
        m_max_refinement_levels (max_refinement_levels),
        m_level_refinement_factor (level_refinement_factor),
        m_max_refinement_factor (max_refinement_factor)
    {
      assert (refinement_level >= 0);
      assert (refinement_level < max_refinement_levels);
      assert (level_refinement_factor > 0);
      assert (level_refinement_factor <= max_refinement_factor);
      
      ostringstream buf;
      buf << "Vertices level " << refinement_level;
      char const * const name = buf.str().c_str();
      
      m_hdf5_topology
        = open_or_create_group (m_simulation.get_hdf5_simulation(), name);
      assert (m_hdf5_topology >= 0);
      
      assert (invariant());
    }
    
    
    
    void mesh_refinement_topology_t::
    calculate_level_origin_delta (vect<CCTK_REAL, dim> const & coarse_origin,
                                  vect<CCTK_REAL, dim> const & coarse_delta,
                                  vect<int, dim> const & level_offset,
                                  vect<int, dim> const & level_offset_denominator,
                                  vect<CCTK_REAL, dim> & level_origin,
                                  vect<CCTK_REAL, dim> & level_delta)
      const
    {
      assert (all (coarse_delta > 0));
      assert (all (level_offset_denominator > 0));
      
      CCTK_REAL const one = 1;
      level_delta = coarse_delta / m_level_refinement_factor;
      level_origin
        = (coarse_origin
           + (level_offset * one / level_offset_denominator
              / m_max_refinement_factor));
    }
    
    
    
    mesh_refinement_topology_t::
    ~ mesh_refinement_topology_t ()
    {
      herr_t const herr = H5Gclose (m_hdf5_topology);
      assert (! herr);
    }
    
    
    
    bool mesh_refinement_topology_t::
    invariant()
      const
    {
      return (topology_t::invariant()
              and m_refinement_level >= 0
              and m_refinement_level < m_max_refinement_levels
              and m_level_refinement_factor > 0
              and m_level_refinement_factor <= m_max_refinement_factor);
    }
    
  } // namespace F5
  
} // namespace CarpetIOF5

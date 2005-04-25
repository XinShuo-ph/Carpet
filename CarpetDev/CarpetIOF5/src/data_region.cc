#include <cassert>
#include <cstdlib>
#include <sstream>

#include "cctk.h"

#include "data_region.hh"
#include "utils.hh"



namespace CarpetIOF5 {
  
  namespace F5 {
    
    using std::ostringstream;
    
    
    
    data_region_t::
    data_region_t (tensor_component_t & tensor_component,
                   bbox<int, dim> const & region)
      : m_tensor_component (tensor_component),
        m_region (region)
    {
      assert (! region.empty());
      
      ostringstream buf;
      buf << "Region " << m_region;
      char const * const name = buf.str().c_str();
      assert (name != 0);
      
      int const vartype = CCTK_VarTypeI (m_tensor_component.get_variable());
      assert (vartype >= 0);
      hid_t const datatype = hdf5_datatype_from_cactus (vartype);
      assert (datatype >= 0);
      
      m_properties = H5Pcreate (H5P_DATASET_CREATE);
      assert (m_properties >= 0);
      
      vect<hsize_t, dim> const dims
        = (region.shape() / region.stride()).reverse();
      m_dataspace = H5Screate_simple (dim, & dims [0], & dims [0]);
      assert (m_dataspace >= 0);

      m_dataset
        = H5Dcreate (m_dataspace, name, datatype, m_dataspace, m_properties);
      assert (m_dataset >= 0);
      
      write_or_check_attribute
        (m_dataset, "iorigin", region.lower() / region.stride());
      
      assert (invariant());
    }
    
    
    
    data_region_t::
    ~ data_region_t ()
    {
      herr_t herr;
      
      herr = H5Dclose (m_dataset);
      assert (! herr);
      
      herr = H5Sclose (m_dataspace);
      assert (! herr);
      
      herr = H5Pclose (m_properties);
      assert (! herr);
    }
    
    
    
    template<typename T>
    void data_region_t::
    write (T const * const data)
      const
    {
      T dummy;
      hid_t const memory_datatype = hdf5_datatype (dummy);
      assert (memory_datatype >= 0);
      
      vect<hsize_t, dim> const dims
        = (m_region.shape() / m_region.stride()).reverse();
      hid_t const memory_dataspace
        = H5Screate_simple (dim, & dims [0], & dims [0]);
      assert (memory_dataspace >= 0);
      
      hid_t const transfer_properties = H5Pcreate (H5P_DATASET_XFER);
      assert (transfer_properties >= 0);
      
      herr_t herr;
      herr
        = H5Dwrite (m_dataset, memory_datatype, memory_dataspace,
                    m_dataspace, transfer_properties, data);
      assert (! herr);
      
      herr = H5Pclose (transfer_properties);
      assert (! herr);
      
      herr = H5Sclose (memory_dataspace);
      assert (! herr);
    }
    
    template void data_region_t::write (CCTK_INT     const * const data) const;
    template void data_region_t::write (CCTK_REAL    const * const data) const;
    template void data_region_t::write (CCTK_COMPLEX const * const data) const;
    
    
    
    bool data_region_t::
    invariant ()
      const
    {
      return (! m_region.empty()
              and m_properties >= 0
              and m_dataset >= 0
              and m_dataspace >= 0);
    }
    
  } // namespace F5

} // namespace CarpetIOF5

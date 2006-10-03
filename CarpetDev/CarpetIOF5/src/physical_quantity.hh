#ifndef PHYSICAL_QUANTITY_HH
#define PHYSICAL_QUANTITY_HH

#include <hdf5.h>

#include "coordinate_system.hh"



namespace CarpetIOF5 {
  
  namespace F5 {
    
    class physical_quantity_t {
      
      coordinate_system_t & m_coordinate_system;
      
      int const m_group;
      
      hid_t m_hdf5_physical_quantity;
      
      physical_quantity_t ();
      physical_quantity_t (physical_quantity_t const &);
      physical_quantity_t operator= (physical_quantity_t const &);
      
    public:
      
      physical_quantity_t (coordinate_system_t & coordinate_system,
                           int group);
      
      virtual
      ~ physical_quantity_t ();
      
      coordinate_system_t &
      get_coordinate_system ()
        const;
      
      int
      get_group ()
        const;
      
      hid_t
      get_hdf5_physical_quantity ()
        const;
      
      virtual bool
      invariant ()
        const;
      
    };
    
  } // namespace F5

} // namespace CarpetIOF5

#endif  // #ifndef PHYSICAL_QUANTITY_HH
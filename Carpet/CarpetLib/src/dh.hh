/***************************************************************************
                          dh.hh  -  Data Hierarchy
													A grid hierarchy plus ghost zones
                             -------------------
    begin                : Sun Jun 11 2000
    copyright            : (C) 2000 by Erik Schnetter
    email                : schnetter@astro.psu.edu

    $Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetLib/src/dh.hh,v 1.1 2001/03/01 13:40:10 eschnett Exp $

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef DH_HH
#define DH_HH

#include <cassert>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "bbox.hh"
#include "bboxset.hh"
#include "defs.hh"
#include "gh.hh"
#include "vect.hh"



// Forward declaration
template<int D> class dh;
template<int D> class generic_gf;

// Output
template<int D>
ostream& operator<< (ostream& os, const dh<D>& d);



// A data hierarchy (grid hierarchy plus ghost zones)
template<int D>
class dh {
  
  // Types
  typedef vect<int,D>    ivect;
  typedef bbox<int,D>    ibbox;
  typedef bboxset<int,D> ibset;
  typedef list<ibbox>    iblist;
  typedef vector<iblist> iblistvect; // vector of lists
  
  struct dboxes {
    ibbox exterior;		// whole region (including boundaries)
    
    ibbox interior;		// interior (without boundaries)
    iblist send_mg_fine;
    iblist send_mg_coarse;
    iblist recv_mg_fine;
    iblist recv_mg_coarse;
    iblistvect send_ref_fine;
    iblistvect send_ref_coarse;
    iblistvect recv_ref_fine;
    iblistvect recv_ref_coarse;
    iblistvect send_sync;	// send while syncing
    iblistvect send_ref_bnd_fine;
    
    ibset boundaries;		// boundaries
    iblistvect recv_sync;	// receive while syncing
    iblistvect recv_ref_bnd_coarse;
  };
  
  typedef vector<dboxes> mboxes; // ... for each multigrid level
  typedef vector<mboxes> cboxes; // ... for each component
  typedef vector<cboxes> rboxes; // ... for each refinement level
  
public:				// should be readonly
  
  // Fields
  gh<D> &h;			// hierarchy
  ivect lghosts, ughosts;	// ghost zones
  
  rboxes boxes;
  
  list<generic_gf<D>*> gfs;
  
public:
  
  // Constructors
  dh (gh<D>& h, const ivect& lghosts, const ivect& ughosts);
  
  // Destructors
  ~dh ();
  
  // Modifiers
  void recompose ();
  
  // Grid function management
  void add (generic_gf<D>* f);
  void remove (generic_gf<D>* f);
  
  // Output
  friend ostream& operator<< <> (ostream& os, const dh& d);
};



#if defined(TMPL_IMPLICIT)
#  include "dh.cc"
#endif

#endif // DH_HH

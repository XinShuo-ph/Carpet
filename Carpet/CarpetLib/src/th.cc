/***************************************************************************
                          th.cc  -  Time Hierarchy
                          information about time levels
                             -------------------
    begin                : Sun Jun 11 2000
    copyright            : (C) 2000 by Erik Schnetter
    email                : schnetter@astro.psu.edu

    $Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetLib/src/th.cc,v 1.4 2001/03/27 22:26:31 eschnett Exp $

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <assert.h>

#include <iostream>

#include "defs.hh"
#include "gh.hh"

#if !defined(TMPL_IMPLICIT) || !defined(TH_HH)
#  include "th.hh"
#endif

using namespace std;



// Constructors
template<int D>
th<D>::th (gh<D>& h, const int basedelta) : h(h), delta(basedelta) {
  h.add(this);
}

// Destructors
template<int D>
th<D>::~th () {
  h.remove(this);
}

// Modifiers
template<int D>
void th<D>::recompose () {
  times.resize(h.reflevels());
  deltas.resize(h.reflevels());
  for (int rl=0; rl<h.reflevels(); ++rl) {
    const int old_mglevels = times[rl].size();
    int mgtime;
    // Select default time
    if (old_mglevels==0 && rl==0) {
      mgtime = 0;
    } else if (old_mglevels==0) {
      mgtime = times[rl-1][0];
    } else {
      mgtime = times[rl][old_mglevels-1];
    }
    times[rl].resize(h.mglevels(rl,0), mgtime);
    deltas[rl].resize(h.mglevels(rl,0));
    for (int ml=0; ml<h.mglevels(rl,0); ++ml) {
      if (rl==0 && ml==0) {
	deltas[rl][ml] = delta;
      } else if (ml==0) {
	assert (deltas[rl-1][ml] % h.reffact == 0);
	deltas[rl][ml] = deltas[rl-1][ml] / h.reffact;
      } else {
	deltas[rl][ml] = deltas[rl][ml-1] * h.mgfact;
      }
    }
  }
}



// Output
template<int D>
void th<D>::output (ostream& os) const {
  os << "th<" << D << ">:"
     << "times={";
  for (int rl=0; rl<h.reflevels(); ++rl) {
    for (int ml=0; ml<h.mglevels(rl,0); ++ml) {
      if (!(rl==0 && ml==0)) os << ",";
      os << rl << ":" << ml << ":"
	 << times[rl][ml] << "(" << deltas[rl][ml] << ")";
    }
  }
  os << "}";
}



#if defined(TMPL_EXPLICIT)
template class th<3>;
#endif

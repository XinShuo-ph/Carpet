/***************************************************************************
                          bboxset.cc  -  Sets of bounding boxes
                             -------------------
    begin                : Sun Jun 11 2000
    copyright            : (C) 2000 by Erik Schnetter
    email                : schnetter@astro.psu.edu

    $Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetLib/src/bboxset.cc,v 1.1 2001/03/01 13:40:10 eschnett Exp $

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <cassert>
#include <iostream>
#include <set>

#include "defs.hh"

#if !defined(TMPL_IMPLICIT) && !defined(BBOXSET_HH)
#  include "bboxset.hh"
#endif



// Constructors
template<class T, int D>
bboxset<T,D>::bboxset () {
  assert (invariant());
}

template<class T, int D>
bboxset<T,D>::bboxset (const box& b) {
  bs.insert(b);
  assert (invariant());
}

template<class T, int D>
bboxset<T,D>::bboxset (const bboxset& s): bs(s.bs) {
  assert (invariant());
}

template<class T, int D>
bboxset<T,D>::bboxset (const bset& bs): bs(bs) {
  assert (invariant());
}



// Invariant
template<class T, int D>
bool bboxset<T,D>::invariant () const {
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    if ((*bi).empty()) return false;
    if (! (*bi).aligned_with(*bs.begin())) return false;
    // check for overlap (quadratic -- expensive)
    int cnt=0;
    for (const_iterator bi2=bi; bi2!=end(); ++bi2) {
      if (!cnt++) continue;
      if (! ((*bi2) & (*bi)).empty()) return false;
    }
  }
  return true;
}



// Normalisation
template<class T, int D>
void bboxset<T,D>::normalize () {
  // TODO
  assert (invariant());
}



// Accessors
template<class T, int D>
T bboxset<T,D>::size () const {
  T s=0;
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    s += (*bi).size();
  }
  return s;
}



// Add (bboxes that don't overlap)
template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator+= (const box& b) {
  if (b.empty()) return *this;
  // check for overlap
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    assert ((*bi & b).empty());
  }
  bs.insert(b);
  assert (invariant());
  return *this;
}

template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator+= (const bboxset& s) {
  for (const_iterator bi=s.begin(); bi!=s.end(); ++bi) {
    *this += *bi;
  }
  assert (invariant());
  return *this;
}

template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator+ (const box& b) const {
  bboxset r(*this);
  r += b;
  assert (r.invariant());
  return r;
}

template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator+ (const bboxset& s) const {
  bboxset r(*this);
  r += s;
  assert (r.invariant());
  return r;
}



// Union
template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator|= (const box& b) {
  *this += b - *this;
  assert (invariant());
  return *this;
}

template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator|= (const bboxset& s) {
  *this += s - *this;
  assert (invariant());
  return *this;
}

template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator| (const box& b) const {
  bboxset r(*this);
  r |= b;
  assert (r.invariant());
  return r;
}

template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator| (const bboxset& s) const {
  bboxset r(*this);
  r |= s;
  assert (r.invariant());
  return r;
}



// Intersection
template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator& (const box& b) const {
  // start with an empty set
  bboxset r;
  // walk all my elements
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    // insert the intersection with the bbox
    r += *bi & b;
  }
  assert (r.invariant());
  return r;
}

template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator& (const bboxset& s) const {
  // start with an empty set
  bboxset r;
  // walk all the bboxes
  for (const_iterator bi=s.begin(); bi!=s.end(); ++bi) {
    // insert the intersection with this bbox
    r += *this & *bi;
  }
  assert (r.invariant());
  return r;
}

template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator&= (const box& b) {
  *this = *this & b;
  assert (invariant());
  return *this;
}

template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator&= (const bboxset& s) {
  *this = *this & s;
  assert (invariant());
  return *this;
}



// Difference
template<class T, int D>
bboxset<T,D> operator- (const bbox<T,D>& b1, const bbox<T,D>& b2) {
  assert (b1.aligned_with(b2));
  if (b1.empty()) return bboxset<T,D>();
  if (b2.empty()) return bboxset<T,D>(b1);
  const vect<T,D> str = b1.stride();
  bboxset<T,D> r;
  for (int d=0; d<D; ++d) {
    // make resulting bboxes as large as possible in x-direction (for
    // better consumption by Fortranly ordered arrays)
    vect<T,D> lb, ub;
    bbox<T,D> b;
    for (int dd=0; dd<D; ++dd) {
      if (dd<d) {
	lb[dd] = b2.lower()[dd];
	ub[dd] = b2.upper()[dd];
      } else if (dd>d) {
	lb[dd] = b1.lower()[dd];
	ub[dd] = b1.upper()[dd];
      }
    }
    lb[d] = b1.lower()[d];
    ub[d] = b2.lower()[d] - str[d];
    b = bbox<T,D>(lb,ub,str) & b1;
    r += b;
    lb[d] = b2.upper()[d] + str[d];
    ub[d] = b1.upper()[d];
    b = bbox<T,D>(lb,ub,str) & b1;
    r += b;
  }
  assert (r.invariant());
  return r;
}

template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator- (const box& b) const {
  // start with an empty set
  bboxset r;
  // walk all my elements
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    // insert the difference with the bbox
    r += *bi - b;
  }
  assert (r.invariant());
  return r;
}

template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator-= (const box& b) {
  *this = *this - b;
  assert (invariant());
  return *this;
}
  
template<class T, int D>
bboxset<T,D>& bboxset<T,D>::operator-= (const bboxset& s) {
  for (const_iterator bi=s.begin(); bi!=s.end(); ++bi) {
    *this -= *bi;
  }
  assert (invariant());
  return *this;
}

template<class T, int D>
bboxset<T,D> bboxset<T,D>::operator- (const bboxset& s) const {
  bboxset r(*this);
  r -= s;
  assert (r.invariant());
  return r;
}

template<class T, int D>
bboxset<T,D> operator- (const bbox<T,D>& b, const bboxset<T,D>& s) {
  bboxset<T,D> r = bboxset<T,D>(b) - s;
  assert (r.invariant());
  return r;
}



// Output
template<class T,int D>
ostream& operator<< (ostream& os, const bboxset<T,D>& s) {
//   os << "bboxset<" STR(T) "," << D << ">:size=" << s.size() << ","
//      << "set=" << s.bs;
  os << s.bs;
  return os;
}



#if defined(TMPL_EXPLICIT)
template class bboxset<int,1>;
template bboxset<int,1>	operator- (const bbox<int,1>& b1, const bbox<int,1>& b2);
template bboxset<int,1>	operator- (const bbox<int,1>& b, const bboxset<int,1>& s);
template ostream& operator<< (ostream& os, const bboxset<int,1>& b);

template class bboxset<int,2>;
template bboxset<int,2>	operator- (const bbox<int,2>& b1, const bbox<int,2>& b2);
template bboxset<int,2>	operator- (const bbox<int,2>& b, const bboxset<int,2>& s);
template ostream& operator<< (ostream& os, const bboxset<int,2>& b);

template class bboxset<int,3>;
template bboxset<int,3>	operator- (const bbox<int,3>& b1, const bbox<int,3>& b3);
template bboxset<int,3>	operator- (const bbox<int,3>& b, const bboxset<int,3>& s);
template ostream& operator<< (ostream& os, const bboxset<int,3>& b);
#endif

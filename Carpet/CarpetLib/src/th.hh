#ifndef TH_HH
#define TH_HH

#include <cassert>
#include <iostream>
#include <vector>

#include "cctk.h"

#include "defs.hh"
#include "gh.hh"

using namespace std;



// Forward declaration
class th;

// Output
ostream& operator<< (ostream& os, const th& t);



// The time hierarchy (information about the current time)
class th {
  
public:				// should be readonly
  
  // Fields
  gh& h;                        // hierarchy
  
private:
  
  CCTK_REAL delta;		// time step
  vector<vector<CCTK_REAL> > times; // current times
  vector<vector<CCTK_REAL> > deltas; // time steps
  
public:
  
  // Constructors
  th (gh& h, const CCTK_REAL basedelta);
  
  // Destructors
  ~th ();
  
  // Modifiers
  void recompose ();
  
  // Time management
  CCTK_REAL get_time (const int rl, const int ml) const {
    assert (rl>=0 && rl<h.reflevels());
    assert (ml>=0 && ml<h.mglevels(rl,0));
    return times.at(rl).at(ml);
  }
  
  void set_time (const int rl, const int ml, const CCTK_REAL t) {
    assert (rl>=0 && rl<h.reflevels());
    assert (ml>=0 && ml<h.mglevels(rl,0));
    times.at(rl).at(ml) = t;
  }
  
  void advance_time (const int rl, const int ml) {
    set_time(rl,ml, get_time(rl,ml) + get_delta(rl,ml));
  }
  
  CCTK_REAL get_delta (const int rl, const int ml) const {
    assert (rl>=0 && rl<h.reflevels());
    assert (ml>=0 && ml<h.mglevels(rl,0));
    return deltas.at(rl).at(ml);
  }
  
  void set_delta (const int rl, const int ml, const CCTK_REAL dt) {
    assert (rl>=0 && rl<h.reflevels());
    assert (ml>=0 && ml<h.mglevels(rl,0));
    deltas.at(rl).at(ml) = dt;
  }
  
  CCTK_REAL time (const int tl, const int rl, const int ml) const {
    assert (rl>=0 && rl<h.reflevels());
    assert (ml>=0 && ml<h.mglevels(rl,0));
    return get_time(rl, ml) + tl * get_delta(rl, ml);
  }
  
  // Output
  void output (ostream& os) const;
};



inline ostream& operator<< (ostream& os, const th& t) {
  t.output(os);
  return os;
}



#endif // TH_HH

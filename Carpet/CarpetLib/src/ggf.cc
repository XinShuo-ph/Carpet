#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "cctk.h"

#include "defs.hh"
#include "dh.hh"
#include "th.hh"

#include "ggf.hh"

using namespace std;



// Constructors
ggf::ggf (const int varindex_, const operator_type transport_operator_,
          th& t_, dh& d_,
          const int prolongation_order_time_,
          const int vectorlength_, const int vectorindex_,
          ggf* const vectorleader_)
  : varindex(varindex_), transport_operator(transport_operator_), t(t_),
    prolongation_order_time(prolongation_order_time_),
    h(d_.h), d(d_),
    timelevels_(0),
    storage(h.mglevels()),
    vectorlength(vectorlength_), vectorindex(vectorindex_),
    vectorleader(vectorleader_)
{
  assert (&t.h == &d.h);
  
  assert (vectorlength >= 1);
  assert (vectorindex >= 0 and vectorindex < vectorlength);
  assert ((vectorindex==0 and !vectorleader)
          or (vectorindex!=0 and vectorleader));
  
  d.add(this);
}

// Destructors
ggf::~ggf () {
  d.remove(this);
}

// Comparison
bool ggf::operator== (const ggf& f) const {
  return this == &f;
}



// Modifiers
void ggf::set_timelevels (const int ml, const int rl, const int new_timelevels)
{
  assert (ml>=0 and ml<(int)storage.size());
  assert (rl>=0 and rl<(int)storage.at(ml).size());
  
  assert (new_timelevels >= 0);
  
  if (new_timelevels < timelevels()) {
    
    for (int c=0; c<(int)storage.at(ml).at(rl).size(); ++c) {
      for (int tl=new_timelevels; tl<timelevels(); ++tl) {
        delete storage.at(ml).at(rl).at(c).at(tl);
      }
      storage.at(ml).at(rl).at(c).resize (new_timelevels);
    } // for c
    
  } else if (new_timelevels > timelevels()) {
    
    for (int c=0; c<(int)storage.at(ml).at(rl).size(); ++c) {
      storage.at(ml).at(rl).at(c).resize (new_timelevels);
      for (int tl=timelevels(); tl<new_timelevels; ++tl) {
        storage.at(ml).at(rl).at(c).at(tl) = typed_data(tl,rl,c,ml);
        storage.at(ml).at(rl).at(c).at(tl)->allocate
          (d.boxes.at(ml).at(rl).at(c).exterior, h.proc(rl,c));
      } // for tl
    } // for c
    
  }
  
  timelevels_ = new_timelevels;
}



void ggf::recompose_crop ()
{
  // Free storage that will not be needed
  for (int ml=0; ml<h.mglevels(); ++ml) {
    for (int rl=h.reflevels(); rl<(int)storage.at(ml).size(); ++rl) {
      for (int c=0; c<(int)storage.at(ml).at(rl).size(); ++c) {
        for (int tl=0; tl<(int)storage.at(ml).at(rl).at(c).size(); ++tl) {
          delete storage.at(ml).at(rl).at(c).at(tl);
        } // for tl
      } // for c
    } // for rl
    storage.at(ml).resize(h.reflevels());
  } // for ml
}

void ggf::recompose_allocate (const int rl)
{
  // TODO: restructure storage only when needed
  
  // Retain storage that might be needed
  oldstorage.resize(storage.size());
  for (int ml=0; ml<(int)storage.size(); ++ml) {
    oldstorage.at(ml).resize(storage.at(ml).size());
    oldstorage.at(ml).at(rl) = storage.at(ml).at(rl);
    storage.at(ml).at(rl).resize(0);
  }
  
  // Resize structure and allocate storage
  storage.resize(h.mglevels());
  for (int ml=0; ml<h.mglevels(); ++ml) {
    storage.at(ml).resize(h.reflevels());
    storage.at(ml).at(rl).resize(h.components(rl));
    for (int c=0; c<h.components(rl); ++c) {
      storage.at(ml).at(rl).at(c).resize(timelevels());
      for (int tl=0; tl<timelevels(); ++tl) {
        storage.at(ml).at(rl).at(c).at(tl) = typed_data(tl,rl,c,ml);
        storage.at(ml).at(rl).at(c).at(tl)->allocate
          (d.boxes.at(ml).at(rl).at(c).exterior, h.proc(rl,c));
      } // for tl
    } // for c
  } // for ml
}

void ggf::recompose_fill (comm_state& state, const int rl,
                          const bool do_prolongate)
{
  // Initialise the new storage
  for (int ml=0; ml<h.mglevels(); ++ml) {
    for (int c=0; c<h.components(rl); ++c) {
      for (int tl=0; tl<timelevels(); ++tl) {
        
        // Find out which regions need to be prolongated
        // (Copy the exterior because some variables are not prolongated)
        // TODO: do this once in the dh instead of for each variable here
        ibset work (d.boxes.at(ml).at(rl).at(c).exterior);
        
        // Copy from old storage, if possible
        // TODO: copy only from interior regions?
        if (rl<(int)oldstorage.at(ml).size()) {
          for (int cc=0; cc<(int)oldstorage.at(ml).at(rl).size(); ++cc) {
            // TODO: prefer same processor, etc., see dh.cc
            ibset ovlp
              = work & oldstorage.at(ml).at(rl).at(cc).at(tl)->extent();
            ovlp.normalize();
            work -= ovlp;
            for (ibset::const_iterator r=ovlp.begin(); r!=ovlp.end(); ++r) {
              storage.at(ml).at(rl).at(c).at(tl)->copy_from
                (state, oldstorage.at(ml).at(rl).at(cc).at(tl), *r);
            }
          } // for cc
        } // if rl
        
        if (do_prolongate) {
          // Initialise from coarser level, if possible
          if (rl>0) {
            if (transport_operator != op_none) {
              const int numtl = prolongation_order_time+1;
              assert (timelevels() >= numtl);
              vector<int> tls(numtl);
              vector<CCTK_REAL> times(numtl);
              for (int i=0; i<numtl; ++i) {
                tls.at(i) = i;
                times.at(i) = t.time(tls.at(i),rl-1,ml);
              }
              for (int cc=0; cc<(int)storage.at(ml).at(rl-1).size(); ++cc) {
                vector<const gdata*> gsrcs(numtl);
                for (int i=0; i<numtl; ++i) {
                  gsrcs.at(i) = storage.at(ml).at(rl-1).at(cc).at(tls.at(i));
                  assert (gsrcs.at(i)->extent() == gsrcs.at(0)->extent());
                }
                const CCTK_REAL time = t.time(tl,rl,ml);
                
                // TODO: choose larger regions first
                // TODO: prefer regions from the same processor
                const iblist& list
                  = d.boxes.at(ml).at(rl).at(c).recv_ref_coarse.at(cc);
                for (iblist::const_iterator iter=list.begin();
                     iter!=list.end(); ++iter)
                {
                  ibset ovlp = work & *iter;
                  ovlp.normalize();
                  work -= ovlp;
                  for (ibset::const_iterator r=ovlp.begin();
                       r!=ovlp.end(); ++r)
                  {
                    storage.at(ml).at(rl).at(c).at(tl)->interpolate_from
                      (state, gsrcs, times, *r, time,
                       d.prolongation_order_space, prolongation_order_time);
                  } // for r
                } // for iter
              } // for cc
            } // if transport_operator
          } // if rl
        } // if do_prolongate
        
        // Note that work need not be empty here; in this case, not
        // everything could be initialised.  This is okay on outer
        // boundaries.
        // TODO: check this.
        
      } // for tl
    } // for c
  } // for ml
}

void ggf::recompose_free (const int rl)
{
  // Delete old storage
  for (int ml=0; ml<(int)oldstorage.size(); ++ml) {
    for (int c=0; c<(int)oldstorage.at(ml).at(rl).size(); ++c) {
      for (int tl=0; tl<timelevels(); ++tl) {
        delete oldstorage.at(ml).at(rl).at(c).at(tl);
      } // for tl
    } // for c
    oldstorage.at(ml).at(rl).resize(0);
  } // for ml
}

void ggf::recompose_bnd_prolongate (comm_state& state, const int rl,
                                    const bool do_prolongate)
{
  if (do_prolongate) {
    // Set boundaries
    if (rl>0) {
      for (int ml=0; ml<h.mglevels(); ++ml) {
        for (int c=0; c<h.components(rl); ++c) {
          for (int tl=0; tl<timelevels(); ++tl) {
            
            // TODO: assert that reflevel 0 boundaries are copied
            const CCTK_REAL time = t.time(tl,rl,ml);
            ref_bnd_prolongate (state,tl,rl,c,ml,time);
            
          } // for tl
        } // for c
      } // for ml
    } // if rl
  } // if do_prolongate
}

void ggf::recompose_sync (comm_state& state, const int rl,
                          const bool do_prolongate)
{
  if (do_prolongate) {
    // Set boundaries
    for (int ml=0; ml<h.mglevels(); ++ml) {
      for (int c=0; c<h.components(rl); ++c) {
        for (int tl=0; tl<timelevels(); ++tl) {
          
          sync (state,tl,rl,c,ml);
          
        } // for tl
      } // for c
    } // for ml
  } // if do_prolongate
}



// Cycle the time levels by rotating the data sets
void ggf::cycle (int rl, int c, int ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (c>=0 and c<h.components(rl));
  assert (ml>=0 and ml<h.mglevels());
  gdata* tmpdata = storage.at(ml).at(rl).at(c).at(timelevels()-1);
  for (int tl=timelevels()-1; tl>0; --tl) {
    storage.at(ml).at(rl).at(c).at(tl) = storage.at(ml).at(rl).at(c).at(tl-1);
  }
  storage.at(ml).at(rl).at(c).at(0) = tmpdata;
}

// Flip the time levels by exchanging the data sets
void ggf::flip (int rl, int c, int ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (c>=0 and c<h.components(rl));
  assert (ml>=0 and ml<h.mglevels());
  for (int tl=0; tl<(timelevels()-1)/2; ++tl) {
    const int tl1 =                  tl;
    const int tl2 = timelevels()-1 - tl;
    assert (tl1 < tl2);
    gdata* tmpdata = storage.at(ml).at(rl).at(c).at(tl1);
    storage.at(ml).at(rl).at(c).at(tl1) = storage.at(ml).at(rl).at(c).at(tl2);
    storage.at(ml).at(rl).at(c).at(tl2) = tmpdata;
  }
}



// Operations

// Copy a region
void ggf::copycat (comm_state& state,
                   int tl1, int rl1, int c1, int ml1,
                   const ibbox dh::dboxes::* recv_box,
                   int tl2, int rl2, int ml2,
                   const ibbox dh::dboxes::* send_box)
{
  assert (tl1>=0 and tl1<timelevels());
  assert (rl1>=0 and rl1<h.reflevels());
  assert (c1>=0 and c1<h.components(rl1));
  assert (ml1>=0 and ml1<h.mglevels());
  assert (tl2>=0 and tl2<timelevels());
  assert (rl2>=0 and rl2<h.reflevels());
  const int c2=c1;
  assert (ml2<h.mglevels());
  const ibbox recv = d.boxes.at(ml1).at(rl1).at(c1).*recv_box;
  const ibbox send = d.boxes.at(ml2).at(rl2).at(c2).*send_box;
  assert (all(recv.shape()==send.shape()));
  // copy the content
  assert (recv==send);
  storage.at(ml1).at(rl1).at(c1).at(tl1)->copy_from
    (state, storage.at(ml2).at(rl2).at(c2).at(tl2), recv);
}

// Copy regions
void ggf::copycat (comm_state& state,
                   int tl1, int rl1, int c1, int ml1,
                   const iblist dh::dboxes::* recv_list,
                   int tl2, int rl2, int ml2,
                   const iblist dh::dboxes::* send_list)
{
  assert (tl1>=0 and tl1<timelevels());
  assert (rl1>=0 and rl1<h.reflevels());
  assert (c1>=0 and c1<h.components(rl1));
  assert (ml1>=0 and ml1<h.mglevels());
  assert (tl2>=0 and tl2<timelevels());
  assert (rl2>=0 and rl2<h.reflevels());
  const int c2=c1;
  assert (ml2<h.mglevels());
  const iblist recv = d.boxes.at(ml1).at(rl1).at(c1).*recv_list;
  const iblist send = d.boxes.at(ml2).at(rl2).at(c2).*send_list;
  assert (recv.size()==send.size());
  // walk all boxes
  for (iblist::const_iterator r=recv.begin(), s=send.begin();
       r!=recv.end(); ++r, ++s)
  {
    // (use the send boxes for communication)
    // copy the content
    storage.at(ml1).at(rl1).at(c1).at(tl1)->copy_from
      (state, storage.at(ml2).at(rl2).at(c2).at(tl2), *r);
  }
}

// Copy regions
void ggf::copycat (comm_state& state,
                   int tl1, int rl1, int c1, int ml1,
                   const iblistvect dh::dboxes::* recv_listvect,
                   int tl2, int rl2, int ml2,
                   const iblistvect dh::dboxes::* send_listvect)
{
  assert (tl1>=0 and tl1<timelevels());
  assert (rl1>=0 and rl1<h.reflevels());
  assert (c1>=0 and c1<h.components(rl1));
  assert (ml1>=0 and ml1<h.mglevels());
  assert (tl2>=0 and tl2<timelevels());
  assert (rl2>=0 and rl2<h.reflevels());
  // walk all components
  for (int c2=0; c2<h.components(rl2); ++c2) {
    assert (ml2<h.mglevels());
    const iblist recv = (d.boxes.at(ml1).at(rl1).at(c1).*recv_listvect).at(c2);
    const iblist send = (d.boxes.at(ml2).at(rl2).at(c2).*send_listvect).at(c1);
    assert (recv.size()==send.size());
    // walk all boxes
    for (iblist::const_iterator r=recv.begin(), s=send.begin();
      	 r!=recv.end(); ++r, ++s) {
      // (use the send boxes for communication)
      // copy the content
      storage.at(ml1).at(rl1).at(c1).at(tl1)->copy_from
      	(state, storage.at(ml2).at(rl2).at(c2).at(tl2), *r);
    }
  }
}

// Interpolate a region
void ggf::intercat (comm_state& state,
                    int tl1, int rl1, int c1, int ml1,
                    const ibbox dh::dboxes::* recv_list,
                    const vector<int> tl2s, int rl2, int ml2,
                    const ibbox dh::dboxes::* send_list,
                    CCTK_REAL time)
{
  assert (tl1>=0 and tl1<timelevels());
  assert (rl1>=0 and rl1<h.reflevels());
  assert (c1>=0 and c1<h.components(rl1));
  assert (ml1>=0 and ml1<h.mglevels());
  for (int i=0; i<(int)tl2s.size(); ++i) {
    assert (tl2s.at(i)>=0 and tl2s.at(i)<timelevels());
  }
  assert (rl2>=0 and rl2<h.reflevels());
  const int c2=c1;
  assert (ml2>=0 and ml2<h.mglevels());
  
  vector<const gdata*> gsrcs(tl2s.size());
  vector<CCTK_REAL> times(tl2s.size());
  for (int i=0; i<(int)gsrcs.size(); ++i) {
    gsrcs.at(i) = storage.at(ml2).at(rl2).at(c2).at(tl2s.at(i));
    times.at(i) = t.time(tl2s.at(i),rl2,ml2);
  }
  
  const ibbox recv = d.boxes.at(ml1).at(rl1).at(c1).*recv_list;
  const ibbox send = d.boxes.at(ml2).at(rl2).at(c2).*send_list;
  assert (all(recv.shape()==send.shape()));
  // interpolate the content
  assert (recv==send);
  storage.at(ml1).at(rl1).at(c1).at(tl1)->interpolate_from
    (state, gsrcs, times, recv, time,
     d.prolongation_order_space, prolongation_order_time);
}

// Interpolate regions
void ggf::intercat (comm_state& state,
                    int tl1, int rl1, int c1, int ml1,
                    const iblist dh::dboxes::* recv_list,
                    const vector<int> tl2s, int rl2, int ml2,
                    const iblist dh::dboxes::* send_list,
                    const CCTK_REAL time)
{
  assert (tl1>=0 and tl1<timelevels());
  assert (rl1>=0 and rl1<h.reflevels());
  assert (c1>=0 and c1<h.components(rl1));
  assert (ml1>=0 and ml1<h.mglevels());
  for (int i=0; i<(int)tl2s.size(); ++i) {
    assert (tl2s.at(i)>=0 and tl2s.at(i)<timelevels());
  }
  assert (rl2>=0 and rl2<h.reflevels());
  const int c2=c1;
  assert (ml2>=0 and ml2<h.mglevels());
  
  vector<const gdata*> gsrcs(tl2s.size());
  vector<CCTK_REAL> times(tl2s.size());
  for (int i=0; i<(int)gsrcs.size(); ++i) {
    gsrcs.at(i) = storage.at(ml2).at(rl2).at(c2).at(tl2s.at(i));
    times.at(i) = t.time(tl2s.at(i),rl2,ml2);
  }
  
  const iblist recv = d.boxes.at(ml1).at(rl1).at(c1).*recv_list;
  const iblist send = d.boxes.at(ml2).at(rl2).at(c2).*send_list;
  assert (recv.size()==send.size());
  // walk all boxes
  for (iblist::const_iterator r=recv.begin(), s=send.begin();
       r!=recv.end(); ++r, ++s)
  {
    // (use the send boxes for communication)
    // interpolate the content
    storage.at(ml1).at(rl1).at(c1).at(tl1)->interpolate_from
      (state, gsrcs, times, *r, time,
       d.prolongation_order_space, prolongation_order_time);
  }
}

// Interpolate regions
void ggf::intercat (comm_state& state,
                    int tl1, int rl1, int c1, int ml1,
                    const iblistvect dh::dboxes::* recv_listvect,
                    const vector<int> tl2s, int rl2, int ml2,
                    const iblistvect dh::dboxes::* send_listvect,
                    const CCTK_REAL time)
{
  assert (tl1>=0 and tl1<timelevels());
  assert (rl1>=0 and rl1<h.reflevels());
  assert (c1>=0 and c1<h.components(rl1));
  assert (ml1>=0 and ml1<h.mglevels());
  for (int i=0; i<(int)tl2s.size(); ++i) {
    assert (tl2s.at(i)>=0 and tl2s.at(i)<timelevels());
  }
  assert (rl2>=0 and rl2<h.reflevels());
  // walk all components
  for (int c2=0; c2<h.components(rl2); ++c2) {
    assert (ml2>=0 and ml2<h.mglevels());
    
    vector<const gdata*> gsrcs(tl2s.size());
    vector<CCTK_REAL> times(tl2s.size());
    for (int i=0; i<(int)gsrcs.size(); ++i) {
      gsrcs.at(i) = storage.at(ml2).at(rl2).at(c2).at(tl2s.at(i));
      times.at(i) = t.time(tl2s.at(i),rl2,ml2);
    }
    
    const iblist recv = (d.boxes.at(ml1).at(rl1).at(c1).*recv_listvect).at(c2);
    const iblist send = (d.boxes.at(ml2).at(rl2).at(c2).*send_listvect).at(c1);
    assert (recv.size()==send.size());
    // walk all boxes
    for (iblist::const_iterator r=recv.begin(), s=send.begin();
      	 r!=recv.end(); ++r, ++s)
    {
      // (use the send boxes for communication)
      // interpolate the content
      storage.at(ml1).at(rl1).at(c1).at(tl1)->interpolate_from
      	(state, gsrcs, times, *r, time,
	 d.prolongation_order_space, prolongation_order_time);
    }
  }
}



// Copy a component from the next time level
void ggf::copy (comm_state& state, int tl, int rl, int c, int ml)
{
  // Copy
  copycat (state,
           tl  ,rl,c,ml, &dh::dboxes::exterior,
      	   tl+1,rl,  ml, &dh::dboxes::exterior);
}

// Synchronise the boundaries a component
void ggf::sync (comm_state& state, int tl, int rl, int c, int ml)
{
  // Copy
  copycat (state,
           tl,rl,c,ml, &dh::dboxes::recv_sync,
      	   tl,rl,  ml, &dh::dboxes::send_sync);
}

// Prolongate the boundaries of a component
void ggf::ref_bnd_prolongate (comm_state& state, 
                              int tl, int rl, int c, int ml,
                              CCTK_REAL time)
{
  // Interpolate
  assert (rl>=1);
  if (transport_operator == op_none) return;
  vector<int> tl2s;
  // Interpolation in time
  assert (timelevels() >= prolongation_order_time+1);
  tl2s.resize(prolongation_order_time+1);
  for (int i=0; i<=prolongation_order_time; ++i) tl2s.at(i) = i;
  intercat (state,
            tl  ,rl  ,c,ml, &dh::dboxes::recv_ref_bnd_coarse,
	    tl2s,rl-1,  ml, &dh::dboxes::send_ref_bnd_fine,
	    time);
}

// Restrict a multigrid level
void ggf::mg_restrict (comm_state& state,
                       int tl, int rl, int c, int ml,
                       CCTK_REAL time)
{
  // Require same times
  assert (abs(t.get_time(rl,ml) - t.get_time(rl,ml-1))
	  <= 1.0e-8 * abs(t.get_time(rl,ml)));
  const vector<int> tl2s(1,tl);
  intercat (state,
            tl  ,rl,c,ml,   &dh::dboxes::recv_mg_coarse,
	    tl2s,rl,  ml-1, &dh::dboxes::send_mg_fine,
	    time);
}

// Prolongate a multigrid level
void ggf::mg_prolongate (comm_state& state,
                         int tl, int rl, int c, int ml,
                         CCTK_REAL time)
{
  // Require same times
  assert (abs(t.get_time(rl,ml) - t.get_time(rl,ml+1))
	  <= 1.0e-8 * abs(t.get_time(rl,ml)));
  const vector<int> tl2s(1,tl);
  intercat (state,
            tl  ,rl,c,ml,   &dh::dboxes::recv_mg_coarse,
	    tl2s,rl,  ml+1, &dh::dboxes::send_mg_fine,
	    time);
}

// Restrict a refinement level
void ggf::ref_restrict (comm_state& state,
                        int tl, int rl, int c, int ml,
                        CCTK_REAL time)
{
  // Require same times
  assert (abs(t.get_time(rl,ml) - t.get_time(rl+1,ml))
	  <= 1.0e-8 * abs(t.get_time(rl,ml)));
  if (transport_operator == op_none) return;
  const vector<int> tl2s(1,tl);
  intercat (state,
            tl  ,rl  ,c,ml, &dh::dboxes::recv_ref_fine,
	    tl2s,rl+1,  ml, &dh::dboxes::send_ref_coarse,
	    time);
}

// Prolongate a refinement level
void ggf::ref_prolongate (comm_state& state,
                          int tl, int rl, int c, int ml,
                          CCTK_REAL time)
{
  assert (rl>=1);
  if (transport_operator == op_none) return;
  vector<int> tl2s;
  // Interpolation in time
  assert (timelevels() >= prolongation_order_time+1);
  tl2s.resize(prolongation_order_time+1);
  for (int i=0; i<=prolongation_order_time; ++i) tl2s.at(i) = i;
  intercat (state,
            tl  ,rl  ,c,ml, &dh::dboxes::recv_ref_coarse,
	    tl2s,rl-1,  ml, &dh::dboxes::send_ref_fine,
	    time);
}

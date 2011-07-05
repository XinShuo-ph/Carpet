#include <cctk.h>
#include <cctk_Arguments.h>
#include <cctk_Parameters.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

/* #ifdef HAVE_TGMATH_H */
/* #  include <tgmath.h> */
/* #endif */

#include "loopcontrol.h"

#include "lc_auto.h"
#include "lc_hill.h"



#ifndef _OPENMP
/* Replacements for some OpenMP routines if OpenMP is not available */

static inline
int
omp_get_thread_num (void)
{
  return 0;
}

static inline
int
omp_get_num_threads (void)
{
  return 1;
}

static inline
int
omp_get_max_threads (void)
{
  return 1;
}

static inline
double
omp_get_wtime (void)
{
  struct timeval tv;
  gettimeofday (& tv, NULL);
  return tv.tv_sec + 1.0e-6 * tv.tv_usec;
}

#endif



/* Linked list of all loop statistics structures */
lc_statmap_t * lc_statmap_list = NULL;



/* Find all possible thread topologies */
/* This finds all possible thread topologies which can be expressed as
   NIxNJxNK x NIIxNJJxNKK. More complex topologies, e.g. based on a
   recursive subdivision, are not considered (and cannot be expressed
   with the data structures currently used in LoopControl). I expect
   that more complex topologies are not necessary, since the number of
   threads is usually quite small and contains many small factors in
   its prime decomposition. */
static
void
find_thread_topologies (lc_topology_t * restrict const topologies,
                        const int maxntopologies,
                        int * restrict const ntopologies,
                        int const nthreads)
{
  * ntopologies = 0;
  
  for (int nk=1; nk<=nthreads; ++nk) {
    if (nthreads % nk == 0) {
      for (int nj=1; nj<=nthreads/nk; ++nj) {
        if (nthreads % (nj*nk) == 0) {
          for (int ni=1; ni<=nthreads/(nj*nk); ++ni) {
            if (nthreads % (ni*nj*nk) == 0) {
              
              int const nithreads = nthreads/(ni*nj*nk);
              for (int nkk=1; nkk<=nithreads; ++nkk) {
                if (nithreads % nkk == 0) {
                  for (int njj=1; njj<=nithreads/nkk; ++njj) {
                    if (nithreads % (njj*nkk) == 0) {
                      int const nii = nithreads/(njj*nkk);
                      
                      assert (* ntopologies < maxntopologies);
                      topologies[* ntopologies].nthreads[0][0] = ni;
                      topologies[* ntopologies].nthreads[0][1] = nj;
                      topologies[* ntopologies].nthreads[0][2] = nk;
                      topologies[* ntopologies].nthreads[1][0] = nii;
                      topologies[* ntopologies].nthreads[1][1] = njj;
                      topologies[* ntopologies].nthreads[1][2] = nkk;
                      ++ * ntopologies;
                      
                    }
                  }
                }
              }
              
            }
          }
        }
      }
    }
  }
}


#if 1

/* Find "good" tiling specifications */
/* This calculates a subset of all possible thread specifications. One
   aim is to reduce the search space by disregarding some
   specifications. The other aim is to distribute the specifications
   "equally", so that one does not have to spend much effort
   investigating tiling specifications with very similar properties.
   For example, if there are 200 grid points, then half of the
   possible tiling specifications consist of splitting the domain into
   two subdomains with [100+N, 100-N] points. This is avoided by
   covering all possible tiling specifications in exponentially
   growing step sizes. */
static
int tiling_compare (const void * const a, const void * const b)
{
  lc_tiling_t const * const aa = a;
  lc_tiling_t const * const bb = b;
  return aa->npoints - bb->npoints;
}

static
void
find_tiling_specifications (lc_tiling_t * restrict const tilings,
                            const int maxntilings,
                            int * restrict const ntilings,
                            int const npoints)
{
  /* In order to reduce the number of possible tilings, require that
     the step sizes differ by more than 10%. */
  double const distance_factor = 1.1;
  /* Determine the "good" step sizes in two passes: first small step
     sizes from 1 up to snpoints, then large step sizes from npoints
     down to snpoints+1. */
  int const snpoints = floor (sqrt (npoints));
  /* For N grid points and a minimum spacing factor F, there are at
     most log(N) / log(F) possible tilings. There will be fewer, since
     the actual spacings will be rounded up to integers. */
  
  * ntilings = 0;
  
  /* Small step sizes */
  int minnpoints = 0;
  for (int n=1; n<=snpoints; ++n) {
    if ((double) n > minnpoints * distance_factor) {
      assert (* ntilings < maxntilings);
      tilings[* ntilings].npoints = n;
      minnpoints = n;
      ++ * ntilings;
    }
  }
  
  /* Large step sizes */
  int maxnpoints = 1000000000;
  for (int n=npoints; n>snpoints; --n) {
    if (n * distance_factor < (double) maxnpoints) {
      assert (* ntilings < maxntilings);
      tilings[* ntilings].npoints = n;
      maxnpoints = n;
      ++ * ntilings;
    }
  }
  
  /* Sort */
  qsort (tilings, * ntilings, sizeof * tilings, tiling_compare);
  
  /* step size should be at least 1, even if there are only 0
     points */
  assert (* ntilings < maxntilings);
  tilings[* ntilings].npoints = lc_max (npoints, 1);
  ++ * ntilings;
  
  assert (* ntilings >= 1);
}

#else

static
void
find_tiling_specifications (lc_tiling_t * restrict const tilings,
                            const int maxntilings,
                            int * restrict const ntilings,
                            int const npoints)
{
  /* In order to reduce the number of possible tilings, require that
     the step sizes differ by more than 10%. */
  double const distance_factor = 1.1;
  /* For N grid points and a minimum spacing factor F, there are at
     most log(N) / log(F) possible tilings. There will be fewer, since
     the actual spacings will be rounded up to integers. */
  
  * ntilings = 0;
  
  int minnpoints = 0;
  for (int n=1; n<npoints; ++n) {
    if ((double) n > minnpoints * distance_factor) {
      assert (* ntilings < maxntilings);
      tilings[* ntilings].npoints = n;
      minnpoints = n;
      ++ * ntilings;
    }
  }
  
  assert (* ntilings < maxntilings);
  /* step size should be at least 1, even if there are only 0
     points */
  tilings[* ntilings].npoints = lc_max (npoints, 1);
  ++ * ntilings;
}

#endif



/* Initialise control parameter set statistics */
void
lc_stattime_init (lc_stattime_t * restrict const lt,
                  lc_statset_t * restrict const ls,
                  lc_state_t const * restrict const state)
{
  DECLARE_CCTK_PARAMETERS;
  
  /* Check arguments */
  assert (lt);
  assert (ls);
  assert (state);
  
  /*** Topology ***************************************************************/
  
  lt->state.topology = state->topology;
  
  if (state->topology == -1) {
    
    /* User-specified topology */
    lt->inthreads = -1;
    lt->jnthreads = -1;
    lt->knthreads = -1;
    lt->inithreads = -1;
    lt->jnithreads = -1;
    lt->knithreads = -1;
    
  } else {
    
    assert (state->topology >= 0 && state->topology < ls->ntopologies);
    
    lt->inthreads = ls->topologies[lt->state.topology].nthreads[0][0];
    lt->jnthreads = ls->topologies[lt->state.topology].nthreads[0][1];
    lt->knthreads = ls->topologies[lt->state.topology].nthreads[0][2];
    lt->inithreads = ls->topologies[lt->state.topology].nthreads[1][0];
    lt->jnithreads = ls->topologies[lt->state.topology].nthreads[1][1];
    lt->knithreads = ls->topologies[lt->state.topology].nthreads[1][2];
    
  }
  
  if (debug) {
    printf ("Thread topology #%d [%d,%d,%d]x[%d,%d,%d]\n",
            lt->state.topology,
            lt->inthreads, lt->jnthreads, lt->knthreads,
            lt->inithreads, lt->jnithreads, lt->knithreads);
  }
  
  /* Assert thread topology consistency */
  if (lt->state.topology != -1) {
    assert (lt->inthreads >= 1);
    assert (lt->jnthreads >= 1);
    assert (lt->knthreads >= 1);
    assert (lt->inithreads >= 1);
    assert (lt->jnithreads >= 1);
    assert (lt->knithreads >= 1);
    assert (lt->inthreads * lt->jnthreads * lt->knthreads *
            lt->inithreads * lt->jnithreads * lt->knithreads ==
            ls->num_threads);
  }
  
  /*** Tilings ****************************************************************/
  
  for (int d=0; d<3; ++d) {
    lt->state.tiling[d] = state->tiling[d];
    if (state->tiling[d] != -1) {
      assert (state->tiling[d] >= 0 &&
              state->tiling[d] < ls->topology_ntilings[d][lt->state.topology]);
    }
  }
  
  if (state->tiling[0] != -1) {
    lt->inpoints = ls->tilings[0][lt->state.tiling[0]].npoints;
  }
  if (state->tiling[1] != -1) {
    lt->jnpoints = ls->tilings[1][lt->state.tiling[1]].npoints;
  }
  if (state->tiling[2] != -1) {
    lt->knpoints = ls->tilings[2][lt->state.tiling[2]].npoints;
  }
  
  if (debug) {
    printf ("Tiling stride [%d,%d,%d]\n",
            lt->inpoints, lt->jnpoints, lt->knpoints);
  }
  
  /* Assert tiling specification consistency */
  if (state->tiling[0] != -1) {
    assert (lt->inpoints > 0);
  }
  if (state->tiling[1] != -1) {
    assert (lt->jnpoints > 0);
  }
  if (state->tiling[2] != -1) {
    assert (lt->knpoints > 0);
  }
  
  
  
  /* Initialise statistics */
  lt->time_count      = 0.0;
  lt->time_count_init = 0.0;
  lt->time_setup_sum  = 0.0;
  lt->time_setup_sum2 = 0.0;
  lt->time_calc_sum   = 0.0;
  lt->time_calc_sum2  = 0.0;
  lt->time_calc_init  = 0.0;
  
  lt->last_updated = 0.0;       /* never updated */
  
  
  
  /* Append to loop statistics list */
  lt->next = ls->stattime_list;
  ls->stattime_list = lt;
}

lc_stattime_t *
lc_stattime_find (lc_statset_t const * restrict const ls,
                  lc_state_t const * restrict const state)
{
  assert (ls);
  
  lc_stattime_t * lt;
  
  for (lt = ls->stattime_list; lt; lt = lt->next) {
    if (lc_state_equal (& lt->state, state)) {
      break;
    }
  }
  
  return lt;
}

lc_stattime_t *
lc_stattime_find_create (lc_statset_t * restrict const ls,
                         lc_state_t const * restrict const state)
{
  assert (ls);
  
  lc_stattime_t * lt;
  
  for (lt = ls->stattime_list; lt; lt = lt->next) {
    if (lc_state_equal (& lt->state, state)) {
      break;
    }
  }
  
  if (! lt) {
    lt = malloc (sizeof * lt);
    assert (lt);
    lc_stattime_init (lt, ls, state);
  }
  
  assert (lt);
  return lt;
}



/* Initialise user parameter set statistics */
void
lc_statset_init (lc_statset_t * restrict const ls,
                 lc_statmap_t * restrict const lm,
                 int const num_threads,
                 int const npoints[3])
{
  DECLARE_CCTK_PARAMETERS;
  
  /* Check arguments */
  assert (ls);
  assert (lm);
  assert (num_threads >= 1);
  int total_npoints = 1;
  for (int d=0; d<3; ++d) {
    assert (npoints[d] >= 0);
    assert (npoints[d] < 1000000000);
    assert (total_npoints == 0 || npoints[d] < 1000000000 / total_npoints);
    total_npoints *= npoints[d];
  }
  
  /*** Threads ****************************************************************/
  
  static int saved_maxthreads = -1;
  static lc_topology_t * * saved_topologies = NULL;
  static int * restrict saved_ntopologies = NULL;

  // Allocate memory the first time this function is called
  if (saved_maxthreads < 0) {
    saved_maxthreads = omp_get_max_threads();
    saved_topologies  = malloc (saved_maxthreads * sizeof * saved_topologies );
    saved_ntopologies = malloc (saved_maxthreads * sizeof * saved_ntopologies);
    assert (saved_topologies );
    assert (saved_ntopologies);
    for (int n=0; n<saved_maxthreads; ++n) {
      saved_topologies [n] = NULL;
      saved_ntopologies[n] = -1;
    }
  }
  // Reallocate memory in case we need more
  if (num_threads > saved_maxthreads) {
    int old_saved_maxthreads = saved_maxthreads;
    saved_maxthreads = num_threads;
    saved_topologies  = realloc (saved_topologies,  saved_maxthreads * sizeof * saved_topologies );
    saved_ntopologies = realloc (saved_ntopologies, saved_maxthreads * sizeof * saved_ntopologies);
    assert (saved_topologies );
    assert (saved_ntopologies);
    for (int n=old_saved_maxthreads; n<saved_maxthreads; ++n) {
      saved_topologies [n] = NULL;
      saved_ntopologies[n] = -1;
    }
  }
  
  if (! saved_topologies[num_threads-1]) {
    
    /* For up to 1024 threads, there are at most 611556 possible
       topologies */
    int const maxntopologies = 1000000;
    if (debug) {
      printf ("Running on %d threads\n", num_threads);
    }
    
    saved_topologies[num_threads-1] =
      malloc (maxntopologies * sizeof * saved_topologies[num_threads-1]);
    assert (saved_topologies[num_threads-1]);
    find_thread_topologies
      (saved_topologies[num_threads-1],
       maxntopologies, & saved_ntopologies[num_threads-1],
       num_threads);
    saved_topologies[num_threads-1] =
      realloc (saved_topologies[num_threads-1],
               (saved_ntopologies[num_threads-1] *
                sizeof * saved_topologies[num_threads-1]));
    assert (saved_topologies[num_threads-1]);
    
    if (debug) {
      printf ("Found %d possible thread topologies\n",
              saved_ntopologies[num_threads-1]);
      for (int n = 0; n < saved_ntopologies[num_threads-1]; ++n) {
        printf ("   %2d: %2d %2d %2d   %2d %2d %2d\n",
                n,
                saved_topologies[num_threads-1][n].nthreads[0][0],
                saved_topologies[num_threads-1][n].nthreads[0][1],
                saved_topologies[num_threads-1][n].nthreads[0][2],
                saved_topologies[num_threads-1][n].nthreads[1][0],
                saved_topologies[num_threads-1][n].nthreads[1][1],
                saved_topologies[num_threads-1][n].nthreads[1][2]);
      }
    }
  }
  
  ls->num_threads = num_threads;
  ls->topologies  = saved_topologies [num_threads-1];
  ls->ntopologies = saved_ntopologies[num_threads-1];
  assert (ls->ntopologies > 0);
  
  /*** Tilings ****************************************************************/
  
  for (int d=0; d<3; ++d) {
    ls->npoints[d] = npoints[d];
  }
  
  /* For up to 1000000 grid points, there are at most 126 possible
     tilings (assuming a minimum distance of 10%) */
  int const maxntilings = 1000;
  for (int d=0; d<3; ++d) {
    if (debug) {
      printf ("Dimension %d: %d points\n", d, ls->npoints[d]);
    }
    ls->tilings[d] = malloc (maxntilings * sizeof * ls->tilings[d]);
    assert (ls->tilings[d]);
    find_tiling_specifications
      (ls->tilings[d], maxntilings, & ls->ntilings[d], ls->npoints[d]);
    ls->topology_ntilings[d] =
      malloc (ls->ntopologies * sizeof * ls->topology_ntilings[d]);
    assert (ls->topology_ntilings[d]);
    for (int n = 0; n < ls->ntopologies; ++n) {
      int tiling;
      for (tiling = 1; tiling < ls->ntilings[d]; ++tiling) {
        if (ls->tilings[d][tiling].npoints *
            ls->topologies[n].nthreads[0][d] *
            ls->topologies[n].nthreads[1][d] >
            ls->npoints[d])
        {
          break;
        }
      }
      for (int t=tiling; t < ls->ntilings[d]; ++t) {
        assert (ls->tilings[d][t].npoints *
                ls->topologies[n].nthreads[0][d] *
                ls->topologies[n].nthreads[1][d] >
                ls->npoints[d]);
      }
      assert (tiling != 0);     /* this can't be? */
      if (tiling == 0) {
        /* Always allow at least one tiling */
        tiling = 1;
      }
      ls->topology_ntilings[d][n] = tiling;
    }
    if (debug) {
      printf ("   Found %d possible tilings\n", ls->ntilings[d]);
      printf ("     ");
      for (int n = 0; n < ls->ntilings[d]; ++n) {
        printf (" %d", ls->tilings[d][n].npoints);
      }
      printf ("\n");
    }
  }
  
  
  
  /* Simulated annealing state */
  ls->auto_state = NULL;
  
  /* Hill climbing state */
  ls->hill_state = NULL;
  
  
  
  /* Initialise list */
  ls->stattime_list = NULL;
  
  /* Initialise statistics */
  ls->time_count      = 0.0;
  ls->time_count_init = 0.0;
  ls->time_setup_sum  = 0.0;
  ls->time_setup_sum2 = 0.0;
  ls->time_calc_sum   = 0.0;
  ls->time_calc_sum2  = 0.0;
  ls->time_calc_init  = 0.0;
  
  /* Append to loop statistics list */
  ls->next = lm->statset_list;
  lm->statset_list = ls;
}

lc_statset_t *
lc_statset_find (lc_statmap_t const * restrict const lm,
                 int const num_threads,
                 int const npoints[3])
{
  assert (lm);
  
  lc_statset_t * ls;
  
  for (ls = lm->statset_list; ls; ls = ls->next) {
    if (ls->num_threads == num_threads &&
        ls->npoints[0] == npoints[0] &&
        ls->npoints[1] == npoints[1] &&
        ls->npoints[2] == npoints[2])
    {
      break;
    }
  }
  
  return ls;
}

lc_statset_t *
lc_statset_find_create (lc_statmap_t * restrict const lm,
                        int const num_threads,
                        int const npoints[3])
{
  assert (lm);
  
  lc_statset_t * ls;
  
  for (ls = lm->statset_list; ls; ls = ls->next) {
    if (ls->num_threads == num_threads &&
        ls->npoints[0] == npoints[0] &&
        ls->npoints[1] == npoints[1] &&
        ls->npoints[2] == npoints[2])
    {
      break;
    }
  }
  
  if (! ls) {
    ls = malloc (sizeof * ls);
    assert (ls);
    lc_statset_init (ls, lm, num_threads, npoints);
  }
  
  assert (ls);
  return ls;
}
                         


/* Initialise loop statistics */
void
lc_statmap_init (int * restrict const initialised,
                 lc_statmap_t * restrict const lm,
                 char const * restrict const name)
{
  /* Check arguments */
  assert (initialised);
  assert (lm);
  
#pragma omp single
  {
    
    /* Ensure this thorn is active */
    if (! CCTK_IsThornActive (CCTK_THORNSTRING)) {
      CCTK_WARN (CCTK_WARN_ABORT, "Thorn LoopControl is used, but has not been activated. (Note: If a thorn has an optional depencency on LoopControl, and if LoopControl is then in your thorn list, then you are using it and need to activate it.)");
    }
    
    /* Set name */
    lm->name = strdup (name);
    
    /* Initialise list */
    lm->statset_list = NULL;
    
    /* Append to loop statistics list */
    lm->next = lc_statmap_list;
    lc_statmap_list = lm;
    
  }
  
#pragma omp single
  {
    /* Set this flag only after initialising */
    * initialised = 1;
  }
}



void
lc_control_init (lc_control_t * restrict const lc,
                 lc_statmap_t * restrict const lm,
                 int const imin, int const jmin, int const kmin,
                 int const imax, int const jmax, int const kmax,
                 int const ilsh, int const jlsh, int const klsh,
                 int const di)
{
  DECLARE_CCTK_PARAMETERS;
  
  /* Check arguments */
  assert (lc);
  
  /* Timer */
  lc->time_setup_begin = omp_get_wtime();
  
  /* Check arguments */
  if (! (imin >= 0 && imax <= ilsh && ilsh >= 0) ||
      ! (jmin >= 0 && jmax <= jlsh && jlsh >= 0) ||
      ! (kmin >= 0 && kmax <= klsh && klsh >= 0))
  {
    CCTK_VWarn (CCTK_WARN_ABORT, __LINE__, __FILE__, CCTK_THORNSTRING,
                "Illegal loop control arguments:\n"
                "name=\"%s\"\n"
                "imin=%d imax=%d ilsh=%d\n"
                "jmin=%d jmax=%d jlsh=%d\n"
                "kmin=%d kmax=%d klsh=%d\n",
                lm->name,
                imin, imax, ilsh,
                jmin, jmax, jlsh,
                kmin, kmax, klsh);
  }
  assert (imin >= 0 && imax <= ilsh && ilsh >= 0);
  assert (jmin >= 0 && jmax <= jlsh && jlsh >= 0);
  assert (kmin >= 0 && kmax <= klsh && klsh >= 0);
  assert (di > 0);
  
  /* Copy arguments */
  lc->imin = imin;
  lc->jmin = jmin;
  lc->kmin = kmin;
  lc->imax = imax;
  lc->jmax = jmax;
  lc->kmax = kmax;
  lc->ilsh = ilsh;
  lc->jlsh = jlsh;
  lc->klsh = klsh;
  lc->di   = di;                /* vector size */
  
  
  
  int const num_threads = omp_get_num_threads();
  lc_statset_t * restrict ls;
#pragma omp single copyprivate (ls)
  {
    /* Calculate number of points */
    /* TODO: Take vector size into account */
    int npoints[3];
    npoints[0] = lc_max (imax - imin, 0);
    npoints[1] = lc_max (jmax - jmin, 0);
    npoints[2] = lc_max (kmax - kmin, 0);
    
    ls = lc_statset_find_create (lm, num_threads, npoints);
  }
  
  
  
  lc_stattime_t * restrict lt;
#pragma omp single copyprivate (lt)
  {
    
    lc_state_t state;
    
    /* Select topology */
    
    if (lc_inthreads != -1 || lc_jnthreads != -1 || lc_knthreads != -1)
    {
      /* User-specified thread topology */
      
      if (lc_inthreads == -1 || lc_jnthreads == -1 || lc_knthreads == -1) {
        CCTK_VWarn (CCTK_WARN_ABORT, __LINE__, __FILE__, CCTK_THORNSTRING,
                    "Illegal thread topology [%d,%d,%d]x[1,1,1] specified",
                    (int)lc_inthreads, (int)lc_jnthreads, (int)lc_knthreads);
      }
      if (lc_inthreads * lc_jnthreads * lc_knthreads != ls->num_threads) {
        CCTK_VWarn (CCTK_WARN_ABORT, __LINE__, __FILE__, CCTK_THORNSTRING,
                    "Specified thread topology [%d,%d,%d]x[1,1,1] is not compatible with the number of threads %d",
                    (int)lc_inthreads, (int)lc_jnthreads, (int)lc_knthreads,
                    ls->num_threads);
      }
      
      state.topology = -1;
      
    } else {
      
      /* Split in the k direction */
      
      for (state.topology = ls->ntopologies - 1;
           state.topology >= 0;
           -- state.topology)
      {
        int have_tilings = 1;
        for (int d=0; d<3; ++d) {
          have_tilings = have_tilings &&
            ls->topology_ntilings[d][state.topology] > 0;
        }
        if (have_tilings) break;
      }
      if (state.topology < 0) {
        assert (0);
        CCTK_WARN (CCTK_WARN_ABORT, "grid too small");
      }
    }
    
    /* Select tiling */
    
    if (lc_inpoints != -1) {
      /* User-specified tiling */
      state.tiling[0] = -1;
    } else {
      /* as many points as possible */
      assert (state.topology >= 0);
      state.tiling[0] = ls->topology_ntilings[0][state.topology] - 1;
    }
    
    if (lc_jnpoints != -1) {
      /* User-specified tiling */
      state.tiling[1] = -1;
    } else {
      if (cycle_j_tilings) {
        /* cycle through all tilings */
        static int count = 0;
        assert (state.topology >= 0);
        state.tiling[1] = (count ++) % ls->topology_ntilings[1][state.topology];
      } else if (legacy_init) {
        /* as many points as possible */
        assert (state.topology >= 0);
        state.tiling[1] = ls->topology_ntilings[1][state.topology] - 1;
      } else {
        /* as few points as possible */
        state.tiling[1] = 0;
      }
    }
    
    if (lc_knpoints != -1) {
      /* User-specified tiling */
      state.tiling[2] = -1;
    } else {
      /* as many points as possible */
      assert (state.topology >= 0);
      state.tiling[2] = ls->topology_ntilings[2][state.topology] - 1;
    }
    
    /* Use simulated annealing to find the best loop configuration */
    if (use_simulated_annealing) {
      lc_auto_init (ls, & state);
    }
    /* Use hill climbing to find the best loop configuration */
    if (use_random_restart_hill_climbing) {
      lc_hill_init (ls, & state);
    }
    
    /* Find or create database entry */
    
    lt = lc_stattime_find_create (ls, & state);
    
    /* Topology */
    
    if (state.topology == -1) {
      /* User-specified topology */
      lt->inthreads = lc_inthreads;
      lt->jnthreads = lc_jnthreads;
      lt->knthreads = lc_knthreads;
      lt->inithreads = 1;
      lt->jnithreads = 1;
      lt->knithreads = 1;
    }
    
    /* Assert thread topology consistency */
    assert (lt->inthreads >= 1);
    assert (lt->jnthreads >= 1);
    assert (lt->knthreads >= 1);
    assert (lt->inithreads >= 1);
    assert (lt->jnithreads >= 1);
    assert (lt->knithreads >= 1);
    assert (lt->inthreads * lt->jnthreads * lt->knthreads *
            lt->inithreads * lt->jnithreads * lt->knithreads ==
            ls->num_threads);
    
    /* Tilings */
    
    if (state.tiling[0] == -1) {
      /* User-specified tiling */
      lt->inpoints = lc_inpoints;
    }
    if (state.tiling[1] == -1) {
      /* User-specified tiling */
      lt->jnpoints = lc_jnpoints;
    }
    if (state.tiling[2] == -1) {
      /* User-specified tiling */
      lt->knpoints = lc_knpoints;
    }
    
    /* Assert tiling specification consistency */
    assert (lt->inpoints > 0);
    assert (lt->jnpoints > 0);
    assert (lt->knpoints > 0);
    
  } /* omp single */
  
  
  
  lc->statmap  = lm;
  lc->statset  = ls;
  lc->stattime = lt;
  
  

  /*** Threads ****************************************************************/
  
  /* Thread loop settings */
  lc->iiimin = imin;
  lc->jjjmin = jmin;
  lc->kkkmin = kmin;
  lc->iiimax = imax;
  lc->jjjmax = jmax;
  lc->kkkmax = kmax;
  lc->iiistep = (lc->iiimax - lc->iiimin + lt->inthreads-1) / lt->inthreads;
  lc->jjjstep = (lc->jjjmax - lc->jjjmin + lt->jnthreads-1) / lt->jnthreads;
  lc->kkkstep = (lc->kkkmax - lc->kkkmin + lt->knthreads-1) / lt->knthreads;
  
  /* Find location of current thread */
  lc->thread_num = omp_get_thread_num();
  int c_outer =
    lc->thread_num / (lt->inithreads * lt->jnithreads * lt->knithreads);
  int const ci = c_outer % lt->inthreads; c_outer /= lt->inthreads;
  int const cj = c_outer % lt->jnthreads; c_outer /= lt->jnthreads;
  int const ck = c_outer % lt->knthreads; c_outer /= lt->knthreads;
  assert (c_outer == 0);
  lc->iii = lc->iiimin + ci * lc->iiistep;
  lc->jjj = lc->jjjmin + cj * lc->jjjstep;
  lc->kkk = lc->kkkmin + ck * lc->kkkstep;
  
  
  
  /*** Tilings ****************************************************************/
  
  /* Tiling loop settings */
  lc->iimin = ci == 0 ? lc->iii : lc_align (lc->iii, lc->di);
  lc->jjmin = lc->jjj;
  lc->kkmin = lc->kkk;
  lc->iimax = lc_min (lc_align (lc->iii + lc->iiistep, lc->di), lc->iiimax);
  lc->jjmax = lc_min (lc->jjj + lc->jjjstep, lc->jjjmax);
  lc->kkmax = lc_min (lc->kkk + lc->kkkstep, lc->kkkmax);
  lc->iistep = lt->inpoints;
  lc->jjstep = lt->jnpoints;
  lc->kkstep = lt->knpoints;
  
  
  
  /*** Inner threads **********************************************************/
  
  /* Inner loop thread parallelism */
  int c_inner =
    lc->thread_num % (lt->inithreads * lt->jnithreads * lt->knithreads);
  int const cii = c_inner % lt->inithreads; c_inner /= lt->inithreads;
  int const cjj = c_inner % lt->jnithreads; c_inner /= lt->jnithreads;
  int const ckk = c_inner % lt->knithreads; c_inner /= lt->knithreads;
  assert (c_inner == 0);
  lc->iiii = cii;
  lc->jjjj = cjj;
  lc->kkkk = ckk;
  lc->iiiistep =
    lc_align ((lc->iistep + lt->inithreads - 1) / lt->inithreads, lc->di);
  lc->jjjjstep = (lc->jjstep + lt->jnithreads - 1) / lt->jnithreads;
  lc->kkkkstep = (lc->kkstep + lt->knithreads - 1) / lt->knithreads;
  lc->iiiimin = lc->iiii * lc->iiiistep;
  lc->jjjjmin = lc->jjjj * lc->jjjjstep;
  lc->kkkkmin = lc->kkkk * lc->kkkkstep;
  lc->iiiimax = lc->iiiimin + lc->iiiistep;
  lc->jjjjmax = lc->jjjjmin + lc->jjjjstep;
  lc->kkkkmax = lc->kkkkmin + lc->kkkkstep;
  
  
  
  /****************************************************************************/
  
  /* Self test */
  if (do_selftest) {
    char * restrict mem;
#pragma omp single copyprivate (mem)
    {
      mem =
        calloc (lc->ilsh * lc->jlsh * lc->klsh, sizeof * lc->selftest_count);
    }
    lc->selftest_count = mem;
  } else {
    lc->selftest_count = NULL;
  }
  
  
  
  /****************************************************************************/
  
  /* Timer */
  lc->time_calc_begin = omp_get_wtime();
}



void
lc_control_selftest (lc_control_t * restrict const lc,
                     int const imin, int const imax, int const j, int const k)
{
  assert (imin >= 0 && imax <= lc->ilsh);
  assert (j >= 0 && j < lc->jlsh);
  assert (k >= 0 && k < lc->klsh);
  for (int i = imin; i < imax; ++i) {
    int const ind3d = i + lc->ilsh * (j + lc->jlsh * k);
#pragma omp atomic
    ++ lc->selftest_count[ind3d];
  }
}



void
lc_control_finish (lc_control_t * restrict const lc)
{
  DECLARE_CCTK_PARAMETERS;
  
  lc_stattime_t * restrict const lt = lc->stattime;
  lc_statset_t * restrict const ls = lc->statset;
  
  int ignore_iteration;
  int first_iteration;
#pragma omp single copyprivate (ignore_iteration)
  {
    ignore_iteration = ignore_initial_overhead && lt->time_count == 0.0;
    first_iteration = lt->time_count_init == 0.0;
  }
  
  /* Add a barrier to catch load imbalances */
#pragma omp barrier
  ;
  
  /* Timer */
  double const time_calc_end   = omp_get_wtime();
  double const time_calc_begin = lc->time_calc_begin;
  
  double const time_setup_end   = time_calc_begin;
  double const time_setup_begin = lc->time_setup_begin;
  
  double const time_setup_sum  = time_setup_end - time_setup_begin;
  double const time_setup_sum2 = pow (time_setup_sum, 2);
  
  double const time_calc_sum  = time_calc_end - time_calc_begin;
  double const time_calc_sum2 = pow (time_calc_sum, 2);

  /* Update statistics */
#pragma omp critical
  {
    lt->time_count += 1.0;
    if (first_iteration) lt->time_count_init += 1.0;
    
    if (! ignore_iteration) lt->time_setup_sum  += time_setup_sum;
    if (! ignore_iteration) lt->time_setup_sum2 += time_setup_sum2;
    
    lt->time_calc_sum  += time_calc_sum;
    lt->time_calc_sum2 += time_calc_sum2;
    if (first_iteration) lt->time_calc_init += time_calc_sum;
    
    ls->time_count += 1.0;
    if (first_iteration) ls->time_count_init += 1.0;
    
    if (! ignore_iteration) ls->time_setup_sum  += time_setup_sum;
    if (! ignore_iteration) ls->time_setup_sum2 += time_setup_sum2;
    
    ls->time_calc_sum  += time_calc_sum;
    ls->time_calc_sum2 += time_calc_sum2;
    if (first_iteration) ls->time_calc_init += time_calc_sum;
  }
  
#pragma omp master
  {
    lt->last_updated = time_calc_end;
  }
  
/* #pragma omp barrier */
  
  {
    if (use_simulated_annealing) {
#pragma omp single
      {
        lc_auto_finish (ls, lt);
      }
    }
    if (use_random_restart_hill_climbing) {
#pragma omp single
      {
        lc_hill_finish (ls, lt);
      }
    }
  }
  
  /* Perform self-check */
  
  if (do_selftest) {
    /* Ensure all threads have finished the loop */
#pragma omp barrier
    ;
    /* Assert that exactly the specified points have been set */
    static int failure = 0;
#pragma omp for reduction(+: failure)
    for (int k=0; k<lc->klsh; ++k) {
      for (int j=0; j<lc->jlsh; ++j) {
        for (int i=0; i<lc->ilsh; ++i) {
          int const ind3d = i + lc->ilsh * (j + lc->jlsh * k);
          char const inside =
            (i >= lc->imin && i < lc->imax) &&
            (j >= lc->jmin && j < lc->jmax) &&
            (k >= lc->kmin && k < lc->kmax);
          if (lc->selftest_count[ind3d] != inside) {
            ++ failure;
#pragma omp critical
            {
              fprintf (stderr, "   i=[%d,%d,%d] count=%d expected=%d\n",
                       i, j, k,
                       (int) lc->selftest_count[ind3d], (int) inside);
            }
          }
        }
      }
    }
    if (failure) {
      for (int n=0; n<omp_get_num_threads(); ++n) {
        if (n == omp_get_thread_num()) {
          fprintf (stderr, "Thread: %d\n", n);
          fprintf (stderr, "   Arguments:\n");
          fprintf (stderr, "      imin=[%d,%d,%d]\n", lc->imin, lc->jmin, lc->kmin);
          fprintf (stderr, "      imax=[%d,%d,%d]\n", lc->imax, lc->jmax, lc->kmax);
          fprintf (stderr, "      ilsh=[%d,%d,%d]\n", lc->ilsh, lc->jlsh, lc->klsh);
          fprintf (stderr, "      di=%d\n", lc->di);
          fprintf (stderr, "   Thread parallellism:\n");
          fprintf (stderr, "      iiimin =[%d,%d,%d]\n", lc->iiimin, lc->jjjmin, lc->kkkmin);
          fprintf (stderr, "      iiimax =[%d,%d,%d]\n", lc->iiimax, lc->jjjmax, lc->kkkmax);
          fprintf (stderr, "      iiistep=[%d,%d,%d]\n", lc->iiistep, lc->jjjstep, lc->kkkstep);
          fprintf (stderr, "   Current thread:\n");
          fprintf (stderr, "      thread_num=%d\n", lc->thread_num);
          fprintf (stderr, "      iii       =[%d,%d,%d]\n", lc->iii, lc->jjj, lc->kkk);
          fprintf (stderr, "      iiii      =[%d,%d,%d]\n", lc->iiii, lc->jjjj, lc->kkkk);
          fprintf (stderr, "   Tiling loop:\n");
          fprintf (stderr, "      iimin =[%d,%d,%d]\n", lc->iimin, lc->jjmin, lc->kkmin);
          fprintf (stderr, "      iimax =[%d,%d,%d]\n", lc->iimax, lc->jjmax, lc->kkmax);
          fprintf (stderr, "      iistep=[%d,%d,%d]\n", lc->iistep, lc->jjstep, lc->kkstep);
          fprintf (stderr, "   Inner thread parallelism:\n");
          fprintf (stderr, "      iiiimin =[%d,%d,%d]\n", lc->iiiimin, lc->jjjjmin, lc->kkkkmin);
          fprintf (stderr, "      iiiimax =[%d,%d,%d]\n", lc->iiiimax, lc->jjjjmax, lc->kkkkmax);
          fprintf (stderr, "      iiiistep=[%d,%d,%d]\n", lc->iiiistep, lc->jjjjstep, lc->kkkkstep);
        }
#pragma omp barrier
        ;
      }
#pragma omp critical
      {
        CCTK_VWarn (CCTK_WARN_ABORT, __LINE__, __FILE__, CCTK_THORNSTRING,
                    "LoopControl loop \"%s\" self-test failed",
                    lc->statmap->name);
      }
    }
#pragma omp single nowait
    {
      free (lc->selftest_count);
    }
  }
}


/* appears to be unused
static
double
avg (double const c, double const s)
{
  if (c == 0.0) return 0.0;
  return s / c;
}

static
double
stddev (double const c, double const s, double const s2)
{
  if (c == 0.0) return 0.0;
  return sqrt (s2 / c - pow (s / c, 2));
}
*/



/* Output statistics */
void
lc_printstats (void);
void
lc_printstats (void)
{
  DECLARE_CCTK_PARAMETERS;
  
  printf ("LoopControl timing statistics:\n");
  
  double total_calc_time = 0.0;
  for (lc_statmap_t * lm = lc_statmap_list; lm; lm = lm->next) {
    for (lc_statset_t * ls = lm->statset_list; ls; ls = ls->next) {
      for (lc_stattime_t * lt = ls->stattime_list; lt; lt = lt->next) {
        total_calc_time += lt->time_calc_sum;
      }
    }
  }
  
  double total_saved = 0.0;
  int nmaps = 0;
  for (lc_statmap_t * lm = lc_statmap_list; lm; lm = lm->next) {
    
    double calc_time = 0.0;
    for (lc_statset_t * ls = lm->statset_list; ls; ls = ls->next) {
      for (lc_stattime_t * lt = ls->stattime_list; lt; lt = lt->next) {
        calc_time += lt->time_calc_sum;
      }
    }
    if (calc_time < printstats_threshold / 100.0 * total_calc_time) continue;
    
    if (printstats_verbosity >= 1) {
      printf ("Loop #%d \"%s\":\n",
              nmaps,
              lm->name);
    }
    double lm_sum_count      = 0.0;
    double lm_sum_setup      = 0.0;
    double lm_sum_calc       = 0.0;
    double lm_sum_count_init = 0.0;
    double lm_sum_init       = 0.0;
    double lm_sum_improv     = 0.0;
    int nsets = 0;
    for (lc_statset_t * ls = lm->statset_list; ls; ls = ls->next) {
      if (printstats_verbosity >= 2) {
        printf ("   Parameter set #%d nthreads=%d npoints=[%d,%d,%d]\n",
                nsets,
                ls->num_threads, ls->npoints[0], ls->npoints[1], ls->npoints[2]);
      }
      double sum_count      = 0.0;
      double sum_setup      = 0.0;
      double sum_calc       = 0.0;
      double sum_count_init = 0.0;
      double sum_init       = 0.0;
      double min_calc       = DBL_MAX;
      int    imin_calc      = -1;
      double max_calc       = 0.0;
      int    imax_calc      = -1;
      int ntimes = 0;
      for (lc_stattime_t * lt = ls->stattime_list; lt; lt = lt->next) {
        if (printstats_verbosity >= 3) {
          printf ("      Configuration #%d topology=%d [%d,%d,%d]x[%d,%d,%d] tiling=[%d,%d,%d]\n",
                  ntimes,
                  lt->state.topology,
                  lt->inthreads, lt->jnthreads, lt->knthreads,
                  lt->inithreads, lt->jnithreads, lt->knithreads,
                  lt->inpoints, lt->jnpoints, lt->knpoints);
        }
        double const count = lt->time_count;
        double const setup = lt->time_setup_sum / count;
        double const calc  = lt->time_calc_sum / count;
        double const init  = lt->time_calc_init / lt->time_count_init;
        if (printstats_verbosity >= 3) {
          printf ("         count: %g   setup: %g   first: %g   calc: %g\n",
                  count, setup, init, calc);
        }
        sum_count      += lt->time_count;
        sum_setup      += lt->time_setup_sum;
        sum_calc       += lt->time_calc_sum;
        sum_count_init += lt->time_count_init;
        sum_init       += lt->time_calc_init;
        if (calc < min_calc) {
          min_calc  = calc;
          imin_calc = ntimes;
        }
        if (calc > max_calc) {
          max_calc  = calc;
          imax_calc = ntimes;
        }
        ++ ntimes;
      }
      double const init_calc = sum_init / sum_count_init;
      double const avg_calc  = sum_calc / sum_count;
      double const saved     = (init_calc - avg_calc) * sum_count;
      double const improv    = (init_calc - min_calc) / init_calc;
      if (printstats_verbosity >= 2) {
        printf ("      total count: %g   total setup: %g   total calc: %g\n",
                sum_count, sum_setup, sum_calc);
        printf ("      avg calc: %g   min calc: %g (#%d)   max calc: %g (#%d)\n",
                avg_calc, min_calc, imin_calc, max_calc, imax_calc);
        if (printstats_verbosity < 3) {
          int ntimes = 0;
          for (lc_stattime_t * lt = ls->stattime_list; lt; lt = lt->next) {
            if (ntimes == imin_calc || ntimes == imax_calc) {
              printf ("         #%d: topology=%d [%d,%d,%d]x[%d,%d,%d] tiling=[%d,%d,%d]\n",
                      ntimes,
                      lt->state.topology,
                      lt->inthreads, lt->jnthreads, lt->knthreads,
                      lt->inithreads, lt->jnithreads, lt->knithreads,
                      lt->inpoints, lt->jnpoints, lt->knpoints);
            }
            ++ ntimes;
          }
        }
        printf ("      first calc: %g   improvement: %.0f%%   saved: %g\n",
                init_calc, 100.0*improv, saved);
      }
      lm_sum_count      += sum_count;
      lm_sum_setup      += sum_setup;
      lm_sum_calc       += sum_calc;
      lm_sum_count_init += sum_count_init;
      lm_sum_init       += sum_init;
      lm_sum_improv     += improv * sum_count;
      ++ nsets;
    }
    double const init_calc  = lm_sum_init / lm_sum_count_init;
    double const avg_calc   = lm_sum_calc / lm_sum_count;
    double const saved      = (init_calc - avg_calc) * lm_sum_count;
    double const avg_improv = lm_sum_improv / lm_sum_count;
    if (printstats_verbosity >= 1) {
      printf ("   total count: %g   total setup: %g   total calc: %g\n",
              lm_sum_count, lm_sum_setup, lm_sum_calc);
      printf ("   avg calc: %g   avg first calc: %g\n",
              avg_calc, init_calc);
      printf ("   avg improvement: %.0f%%   saved: %g seconds\n",
              100.0*avg_improv, saved);
    }
    total_saved += saved;
    ++ nmaps;
  }
  
  printf ("Total calculation time: %g seconds; total saved time: %g seconds\n",
          total_calc_time, total_saved);
}



void
lc_printstats_analysis (CCTK_ARGUMENTS);
void
lc_printstats_analysis (CCTK_ARGUMENTS)
{
  DECLARE_CCTK_ARGUMENTS;
  DECLARE_CCTK_PARAMETERS;
  
  static int last_output = 0;
  
  int const current_time = CCTK_RunTime();
  if (current_time >= last_output + 60.0 * printstats_every_minutes) {
    last_output = current_time;
    lc_printstats ();
  }
}

void
lc_printstats_terminate (CCTK_ARGUMENTS);
void
lc_printstats_terminate (CCTK_ARGUMENTS)
{
  DECLARE_CCTK_ARGUMENTS;
  
  lc_printstats ();
}

CCTK_FCALL 
void
CCTK_FNAME (lc_get_fortran_type_sizes) (int * lc_control_size,
                                        int * lc_statmap_size);

int
lc_check_type_sizes (void)
{
  /* check that the sizes of LoopControls control structures are the
   * same in loopcontrol.h and loopcontrol_fortran.h */

  int lc_control_size, lc_statmap_size;

  CCTK_FNAME (lc_get_fortran_type_sizes) ( & lc_control_size,
                                           & lc_statmap_size );

  if ( lc_control_size != sizeof(lc_control_t) ||
       lc_statmap_size != sizeof(lc_statmap_t) ) {
    CCTK_VWarn (CCTK_WARN_ABORT, __LINE__, __FILE__, CCTK_THORNSTRING,
                "Fortran and C control structures (lc_statmap_t and lc_control_t) "
                "differ in size. If you are not using Fortran or believe "
                "your Fortran compiler to be strange, then this check can be "
                "disabled by setting LoopControl::check_type_sizes=no.");
  }

  return 0;
}


CCTK_FCALL
void
CCTK_FNAME (lc_statmap_init) (int * restrict const initialised,
                              lc_statmap_t * restrict const lm,
                              ONE_FORTSTRING_ARG);
CCTK_FCALL
void
CCTK_FNAME (lc_statmap_init) (int * restrict const initialised,
                              lc_statmap_t * restrict const lm,
                              ONE_FORTSTRING_ARG)
{
  ONE_FORTSTRING_CREATE (name);
  lc_statmap_init (initialised, lm, name);
  free (name);
}

CCTK_FCALL
void
CCTK_FNAME (lc_control_init) (lc_control_t * restrict const lc,
                              lc_statmap_t * restrict const lm,
                              int const * restrict const imin,
                              int const * restrict const jmin,
                              int const * restrict const kmin,
                              int const * restrict const imax,
                              int const * restrict const jmax,
                              int const * restrict const kmax,
                              int const * restrict const ilsh,
                              int const * restrict const jlsh,
                              int const * restrict const klsh);
CCTK_FCALL
void
CCTK_FNAME (lc_control_init) (lc_control_t * restrict const lc,
                              lc_statmap_t * restrict const lm,
                              int const * restrict const imin,
                              int const * restrict const jmin,
                              int const * restrict const kmin,
                              int const * restrict const imax,
                              int const * restrict const jmax,
                              int const * restrict const kmax,
                              int const * restrict const ilsh,
                              int const * restrict const jlsh,
                              int const * restrict const klsh)
{
  lc_control_init (lc, lm,
                   * imin - 1, * jmin - 1, * kmin - 1,
                   * imax, * jmax, * kmax,
                   * ilsh, * jlsh, * klsh,
                   1);
}

CCTK_FCALL
void
CCTK_FNAME (lc_control_finish) (lc_control_t * restrict const lc);
CCTK_FCALL
void
CCTK_FNAME (lc_control_finish) (lc_control_t * restrict const lc)
{
  lc_control_finish (lc);
}

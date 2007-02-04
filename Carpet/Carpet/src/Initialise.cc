#include <cassert>
#include <cstdlib>
#include <iostream>

#include <cctk.h>
#include <cctk_Parameters.h>
#include <cctki_GHExtensions.h>
#include <cctki_ScheduleBindings.h>
#include <cctki_WarnLevel.h>

#include "carpet.hh"
#include "Timers.hh"



namespace Carpet {
  
  using namespace std;
  
  
  
  static void CallSetup (cGH * cctkGH);
  static void CallRecoverVariables (cGH * cctkGH);
  static void CallRegrid (cGH * cctkGH);
  static void CallPostRecoverVariables (cGH * cctkGH);
  static void CallInitial (cGH * cctkGH);
  static void CallRestrict (cGH * cctkGH);
  static void CallPostInitial (cGH * cctkGH);
  static void CallAnalysis (cGH * cctkGH);
  
  static void Initialise3tl (cGH * cctkGH);
  
  static void print_internal_data ();
  
  
  
  int
  Initialise (tFleshConfig * const fc)
  {
    DECLARE_CCTK_PARAMETERS;
    
    int const convlev = 0;
    cGH * const cctkGH = CCTK_SetupGH (fc, convlev);
    CCTKi_AddGH (fc, convlev, cctkGH);
    
    do_global_mode = true;
    do_meta_mode = true;
    global_time = cctk_initial_time;
    delta_time = 1.0;
    
    cctkGH->cctk_iteration = 0;
    cctkGH->cctk_time = global_time;
    cctkGH->cctk_delta_time = delta_time;
    
    // Timing statistics
    InitTiming (cctkGH);
    
    static Timer timer ("Initialise");
    timer.start();
#warning "TODO: add more timers"
    
    // Delay checkpoint until MPI has been initialised
    Waypoint ("Starting initialisation");
    
    CCTKi_ScheduleGHInit (cctkGH); // Enable storage and communication
    GroupsStorageCheck (cctkGH);
    do_warn_about_storage = true;
    
    CCTKi_InitGHExtensions (cctkGH);
    
    // Write grid structure to file
    for (int m=0; m<maps; ++m) {
      OutputGridStructure (cctkGH, m, vhh.at(m)->regions());
    } // for m
    
    CallSetup (cctkGH);
    
    if (fc->recovered) {
      // Read data from a checkpoint file
      
      CallRecoverVariables (cctkGH);
      CallPostRecoverVariables (cctkGH);
      print_internal_data ();
      
    } else {
      // Calculate initial data
      
      CallInitial (cctkGH);
      CallRestrict (cctkGH);
      CallPostInitial (cctkGH);
      print_internal_data ();
      
      if (init_3_timelevels) {
        Initialise3tl (cctkGH);
      }
    }
    
    // Analyse initial data
    CallAnalysis (cctkGH);
    print_internal_data ();
    
    timer.stop();
    TimerSet::writeData (cctkGH, timer_file);
    
    Waypoint ("Done with initialisation");
    
    return 0;
  }
  
  
  
  //////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  
  
  
  void
  CallSetup (cGH * const cctkGH)
  {
    BEGIN_MGLEVEL_LOOP(cctkGH) {
      do_global_mode = true;
      do_meta_mode = mglevel==mglevels-1; // on first iteration, coarsest grid
      
      // Checking
      {
        int const rl=0;
        ENTER_LEVEL_MODE (cctkGH, rl) {
          Poison (cctkGH, alltimes, CCTK_ARRAY);
        } LEAVE_LEVEL_MODE;
      }
      
      // Register coordinates
      Checkpoint ("Scheduling CCTK_WRAGH");
      CCTK_ScheduleTraverse ("CCTK_WRAGH", cctkGH, CallFunction);
      
      // Check parameters
      Checkpoint ("Scheduling PARAMCHECK");
      CCTK_ScheduleTraverse ("CCTK_PARAMCHECK", cctkGH, CallFunction);
    } END_MGLEVEL_LOOP;
    
    CCTKi_FinaliseParamWarn();
  }
  
  
  
  void
  CallRecoverVariables (cGH * const cctkGH)
  {
    DECLARE_CCTK_PARAMETERS;
    
    for (int rl=0; rl<reflevels; ++rl) {
      BEGIN_MGLEVEL_LOOP(cctkGH) {
        ENTER_LEVEL_MODE (cctkGH, rl) {
          do_global_mode = reflevel==0; // on first iteration, coarsest grid
          do_meta_mode = do_global_mode and mglevel==mglevels-1; // on first iteration, coarsest grid
          
          cctkGH->cctk_time = global_time;
          
          Waypoint ("Recover I at iteration %d time %g%s%s",
                    cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
                    (do_global_mode ? " (global)" : ""),
                    (do_meta_mode ? " (meta)" : ""));
          
          // Checking
          Poison (cctkGH, alltimes, CCTK_GF);
          
          // Set up the grids
          Checkpoint ("Scheduling BASEGRID");
          CCTK_ScheduleTraverse ("CCTK_BASEGRID", cctkGH, CallFunction);
          
          // Timing statistics
          if (do_global_mode) {
            InitTimingVariables (cctkGH);
          }
          
          // Recover
          Checkpoint ("Scheduling RECOVER_VARIABLES");
          CCTK_ScheduleTraverse ("CCTK_RECOVER_VARIABLES", cctkGH, CallFunction);
          
          if (regrid_during_recovery) {
            CallRegrid (cctkGH);
          } // if regrid_during_recovery
          
        } LEAVE_LEVEL_MODE;
      } END_MGLEVEL_LOOP;
    } // for rl
  }
  
  
  
  void
  CallPostRecoverVariables (cGH * const cctkGH)
  {
    for (int rl=0; rl<reflevels; ++rl) {
      BEGIN_MGLEVEL_LOOP(cctkGH) {
        ENTER_LEVEL_MODE (cctkGH, rl) {
          do_global_mode = reflevel==reflevels-1; // on last iteration, finest grid
          do_meta_mode = do_global_mode and mglevel==mglevels-1; // on first iteration, coarsest grid
          
          Waypoint ("Recovering II at iteration %d time %g%s%s",
                    cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
                    (do_global_mode ? " (global)" : ""),
                    (do_meta_mode ? " (meta)" : ""));
          
          // Post recover variables
          Checkpoint ("Scheduling POST_RECOVER_VARIABLES");
          CCTK_ScheduleTraverse
            ("CCTK_POST_RECOVER_VARIABLES", cctkGH, CallFunction);
          
          // Checking
          PoisonCheck (cctkGH, alltimes);
          CheckChecksums (cctkGH, allbutcurrenttime);
          
        } LEAVE_LEVEL_MODE;
      } END_MGLEVEL_LOOP;
    } // for rl
  }
  
  
  
  void
  CallInitial (cGH * const cctkGH)
  {
    DECLARE_CCTK_PARAMETERS;
    
    if (not regrid_during_initialisation) {
      // Regrid once in the beginning
      ENTER_GLOBAL_MODE (cctkGH, 0) {
        ENTER_LEVEL_MODE (cctkGH, 0) {
          CallRegrid (cctkGH);
        } LEAVE_LEVEL_MODE;
      } LEAVE_GLOBAL_MODE;
    }
    
    for (int rl=0; rl<reflevels; ++rl) {
      BEGIN_MGLEVEL_LOOP(cctkGH) {
        ENTER_LEVEL_MODE (cctkGH, rl) {
          do_global_mode = reflevel==0; // on first iteration, coarsest grid
          do_meta_mode = do_global_mode and mglevel==mglevels-1; // on first iteration, coarsest grid
          
          cctkGH->cctk_time = global_time;
          
          Waypoint ("Initialisation I at iteration %d time %g%s%s",
                    cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
                    (do_global_mode ? " (global)" : ""),
                    (do_meta_mode ? " (meta)" : ""));
          
          // Checking
          Poison (cctkGH, alltimes, CCTK_GF);
          
          // Set up the grids
          Checkpoint ("Scheduling BASEGRID");
          CCTK_ScheduleTraverse ("CCTK_BASEGRID", cctkGH, CallFunction);
          
          // Timing statistics
          if (do_global_mode) {
            InitTimingVariables (cctkGH);
          }
          
          int const num_tl =
            init_each_timelevel ? prolongation_order_time+1 : 1;
          
          for (int m=0; m<maps; ++m) {
            vtt.at(m)->set_delta
              (reflevel, mglevel, - vtt.at(m)->get_delta (reflevel, mglevel));
            FlipTimeLevels (cctkGH);
            for (int tl=0; tl<num_tl; ++tl) {
              vtt.at(m)->advance_time (reflevel, mglevel);
              CycleTimeLevels (cctkGH);
            }
            vtt.at(m)->set_delta
              (reflevel, mglevel, - vtt.at(m)->get_delta (reflevel, mglevel));
            FlipTimeLevels (cctkGH);
          } // for m
          
          for (int tl=num_tl-1; tl>=0; --tl) {
            
            // Advance times
            for (int m=0; m<maps; ++m) {
              vtt.at(m)->advance_time (reflevel, mglevel);
            }
            cctkGH->cctk_time =
              (+ global_time
               - tl * delta_time * mglevelfact / timereflevelfact);
            CycleTimeLevels (cctkGH);
            
            // Set up the initial data
            Checkpoint ("Scheduling INITIAL");
            CCTK_ScheduleTraverse ("CCTK_INITIAL", cctkGH, CallFunction);
            
          } // for tl
          
          // Checking
          PoisonCheck (cctkGH, currenttime);
          
          if (regrid_during_initialisation and mglevel==0) {
            // Regrid after initialising each level
            CallRegrid (cctkGH);
          }
          
        } LEAVE_LEVEL_MODE;
      } END_MGLEVEL_LOOP;
      
    } // for rl
  }
  
  
  
  void
  CallRestrict (cGH * const cctkGH)
  {
    for (int rl=reflevels-1; rl>=0; --rl) {
      BEGIN_MGLEVEL_LOOP(cctkGH) {
        ENTER_LEVEL_MODE (cctkGH, rl) {
          
          Waypoint ("Initialisation/Restrict at iteration %d time %g",
                    cctkGH->cctk_iteration, (double)cctkGH->cctk_time);
          
          Restrict (cctkGH);
          
        } LEAVE_LEVEL_MODE;
      } END_MGLEVEL_LOOP;
    } // for rl
  }
  
  
  
  void
  CallPostInitial (cGH * const cctkGH)
  {
    for (int rl=0; rl<reflevels; ++rl) {
      BEGIN_MGLEVEL_LOOP(cctkGH) {
        ENTER_LEVEL_MODE (cctkGH, rl) {
          do_global_mode = reflevel==reflevels-1; // on last iteration, finest grid
          do_meta_mode = do_global_mode and mglevel==mglevels-1; // on first iteration, coarsest grid
          
          Waypoint ("Initialisation II at iteration %d time %g%s%s",
                    cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
                    (do_global_mode ? " (global)" : ""),
                    (do_meta_mode ? " (meta)" : ""));
          
          if (reflevel < reflevels-1) {
            Checkpoint ("Scheduling POSTRESTRICTINITIAL");
            CCTK_ScheduleTraverse
              ("CCTK_POSTRESTRICTINITIAL", cctkGH, CallFunction);
          }
          
          Checkpoint ("Scheduling POSTINITIAL");
          CCTK_ScheduleTraverse ("CCTK_POSTINITIAL", cctkGH, CallFunction);
          
          Checkpoint ("Scheduling POSTSTEP");
          CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cctkGH, CallFunction);
          
          PoisonCheck (cctkGH, alltimes);
          CheckChecksums (cctkGH, allbutcurrenttime);
          
        } LEAVE_LEVEL_MODE;
      } END_MGLEVEL_LOOP;
    } // for rl
  }
  
  
  
  void
  CallAnalysis (cGH * const cctkGH)
  {
    for (int rl=0; rl<reflevels; ++rl) {
      BEGIN_MGLEVEL_LOOP(cctkGH) {
        ENTER_LEVEL_MODE (cctkGH, rl) {
          do_global_mode = reflevel==reflevels-1; // last iteration, finest grid
          do_meta_mode = do_global_mode and mglevel==mglevels-1; // first iteration, coarsest grid
          
          Waypoint ("Initialisation III at iteration %d time %g%s%s",
                    cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
                    (do_global_mode ? " (global)" : ""),
                    (do_meta_mode ? " (meta)" : ""));
          
          int const do_every =
            ipow(mgfact, mglevel) * (maxtimereflevelfact / timereffacts.at(rl));
          if (cctkGH->cctk_iteration % do_every == 0)
          {
            // Checkpoint
            Checkpoint ("Scheduling CPINITIAL");
            CCTK_ScheduleTraverse ("CCTK_CPINITIAL", cctkGH, CallFunction);
            
            // Analysis
            Checkpoint ("Scheduling ANALYSIS");
            CCTK_ScheduleTraverse ("CCTK_ANALYSIS", cctkGH, CallFunction);
            
            // Output
            Checkpoint ("OutputGH");
            CCTK_OutputGH (cctkGH);
            
            // Checking
            PoisonCheck (cctkGH, alltimes);
            CheckChecksums (cctkGH, allbutcurrenttime);
          } // if do_every
          
        } LEAVE_LEVEL_MODE;
      } END_MGLEVEL_LOOP;
    } // for rl
  }
  
  
  
  void
  CallRegrid (cGH * const cctkGH)
  {
    DECLARE_CCTK_PARAMETERS;
    
    assert (is_level_mode());
    
    bool const old_do_global_mode = do_global_mode;
    bool const old_do_meta_mode = do_meta_mode;
    do_global_mode = true;
    do_meta_mode = true;
    
    Waypoint ("Preregridinitial at iteration %d time %g%s%s",
              cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
              (do_global_mode ? " (global)" : ""),
              (do_meta_mode ? " (meta)" : ""));
    
    // Preregrid
    Checkpoint ("Scheduling PREREGRIDINITIAL");
    CCTK_ScheduleTraverse ("CCTK_PREREGRIDINITIAL", cctkGH, CallFunction);
    
    // Regrid
    Checkpoint ("Regrid");
    bool const did_regrid = Regrid (cctkGH, true);
    
    if (did_regrid) {
      BEGIN_META_MODE (cctkGH) {
        for (int rl=0; rl<reflevels; ++rl) {
          
          bool const did_recompose
            = Recompose (cctkGH, rl, prolongate_initial_data);
          
          if (regrid_during_initialisation and did_recompose) {
            BEGIN_MGLEVEL_LOOP (cctkGH) {
              ENTER_LEVEL_MODE (cctkGH, rl) {
                do_global_mode = reflevel == reflevels - 1;
                do_meta_mode = do_global_mode and mglevel==mglevels-1;
                
                Waypoint ("Postregridinitial at iteration %d time %g%s%s",
                          cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
                          (do_global_mode ? " (global)" : ""),
                          (do_meta_mode ? " (meta)" : ""));
                
                int const num_tl =
                  init_each_timelevel ? prolongation_order_time+1 : 1;
                
                // Rewind times
                for (int m=0; m<maps; ++m) {
                  vtt.at(m)->set_delta
                    (reflevel, mglevel,
                     - vtt.at(m)->get_delta (reflevel, mglevel));
                  FlipTimeLevels (cctkGH);
                  for (int tl=0; tl<num_tl; ++tl) {
                    vtt.at(m)->advance_time (reflevel, mglevel);
                    CycleTimeLevels (cctkGH);
                  }
                  vtt.at(m)->set_delta
                    (reflevel, mglevel,
                     - vtt.at(m)->get_delta (reflevel, mglevel));
                  FlipTimeLevels (cctkGH);
                } // for m
                CCTK_REAL const old_cctk_time = cctkGH->cctk_time;
                cctkGH->cctk_time -=
                  num_tl * (cctkGH->cctk_delta_time / cctkGH->cctk_timefac);
                
                for (int tl=num_tl-1; tl>=0; --tl) {
                  
                  // Advance times
                  for (int m=0; m<maps; ++m) {
                    vtt.at(m)->advance_time (reflevel, mglevel);
                  }
                  CycleTimeLevels (cctkGH);
                  cctkGH->cctk_time +=
                    cctkGH->cctk_delta_time / cctkGH->cctk_timefac;
                
                  // Postregrid
                  Checkpoint ("Scheduling POSTREGRIDINITIAL");
                  CCTK_ScheduleTraverse
                    ("CCTK_POSTREGRIDINITIAL", cctkGH, CallFunction);
                  
                } // for tl
                cctkGH->cctk_time = old_cctk_time;
                
              } LEAVE_LEVEL_MODE;
            } END_MGLEVEL_LOOP;
          } // if did_recompose
        } // for rl
      } END_META_MODE;
    } // if did_regrid
    
    do_global_mode = old_do_global_mode;
    do_meta_mode = old_do_meta_mode;
  }
  
  
  
  //////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  
  
  
  // Use Scott Hawley's algorithm to get two extra timelevels of data
  
  static void initialise_3tl_advance_time (cGH * const cctkGH);
  static void initialise_3tl_evolve_Ia (cGH * const cctkGH);
  static void initialise_3tl_flip_timelevels (cGH * const cctkGH);
  static void initialise_3tl_evolve_Ib (cGH * const cctkGH);
  static void initialise_3tl_evolve_IIb (cGH * const cctkGH);
  static void initialise_3tl_advance_time_2 (cGH * const cctkGH);
  static void initialise_3tl_evolve_Ic (cGH * const cctkGH);
  static void initialise_3tl_reset_time (cGH * const cctkGH);
  
  void
  Initialise3tl (cGH * const cctkGH)
  {
    DECLARE_CCTK_PARAMETERS;
    
    Waypoint ("Initialising three timelevels");
    
    // TODO: ensure that there are 3 timelevels
    assert (prolongation_order_time == 2);
    
    BEGIN_MGLEVEL_LOOP(cctkGH) {
      BEGIN_REFLEVEL_LOOP(cctkGH) {
        do_global_mode = reflevel==0;
        do_meta_mode = do_global_mode and mglevel==mglevels-1;
        
        initialise_3tl_advance_time (cctkGH);
        initialise_3tl_evolve_Ia (cctkGH);
        initialise_3tl_flip_timelevels (cctkGH);
        initialise_3tl_evolve_Ib (cctkGH);
        initialise_3tl_flip_timelevels (cctkGH);
        
      } END_REFLEVEL_LOOP;
    } END_MGLEVEL_LOOP;
    
    Waypoint ("Hourglass structure in place");
    
    initialise_3tl_flip_timelevels (cctkGH);
    
    BEGIN_MGLEVEL_LOOP(cctkGH) {
      BEGIN_REVERSE_REFLEVEL_LOOP(cctkGH) {
        do_global_mode = reflevel==0;
        do_meta_mode = do_global_mode and mglevel==mglevels-1;
        
        initialise_3tl_evolve_IIb (cctkGH);
        initialise_3tl_advance_time_2 (cctkGH);
        initialise_3tl_evolve_Ic (cctkGH);
        
      } END_REVERSE_REFLEVEL_LOOP;
    } END_MGLEVEL_LOOP;
    
    initialise_3tl_flip_timelevels (cctkGH);
    
    BEGIN_MGLEVEL_LOOP(cctkGH) {
      BEGIN_REVERSE_REFLEVEL_LOOP(cctkGH) {
        do_global_mode = reflevel==0;
        do_meta_mode = do_global_mode and mglevel==mglevels-1;
        
        initialise_3tl_reset_time (cctkGH);
        
      } END_REVERSE_REFLEVEL_LOOP;
    } END_MGLEVEL_LOOP;
    
    Waypoint ("Finished initialising three timelevels");
  }
  
  
  
  void
  initialise_3tl_advance_time (cGH * const cctkGH)
  {
    Waypoint ("Advancing time");
    
    cctkGH->cctk_time
      = global_time + delta_time * mglevelfact / timereflevelfact;
    for (int m=0; m<maps; ++m) {
      vtt.at(m)->advance_time (reflevel, mglevel);
    }
    
    CycleTimeLevels (cctkGH);
  }
  
  void
  initialise_3tl_evolve_Ia (cGH * const cctkGH)
  {
    Waypoint ("Initialisation 3TL evolution I (a) (forwards) at iteration"
              " %d time %g%s%s",
              cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
              (do_global_mode ? " (global)" : ""),
              (do_meta_mode ? " (meta)" : ""));
    
    CalculateChecksums (cctkGH, allbutcurrenttime);
    Poison (cctkGH, currenttimebutnotifonly);
    
    // Evolve forward
    Checkpoint ("Scheduling PRESTEP");
    CCTK_ScheduleTraverse ("CCTK_PRESTEP", cctkGH, CallFunction);
    Checkpoint ("Scheduling EVOL");
    CCTK_ScheduleTraverse ("CCTK_EVOL", cctkGH, CallFunction);
    
    PoisonCheck (cctkGH, currenttime);
  }
  
  void
  initialise_3tl_flip_timelevels (cGH * const cctkGH)
  {
    Waypoint ("Flipping timelevels");
    
    BEGIN_META_MODE(cctkGH) {
      
      delta_time *= -1;
      
      BEGIN_MGLEVEL_LOOP(cctkGH) {
        BEGIN_REFLEVEL_LOOP (cctkGH) {
          
          cctkGH->cctk_time
            = global_time + delta_time * mglevelfact / timereflevelfact;
          
          FlipTimeLevels (cctkGH);
          
        } END_REFLEVEL_LOOP;
      } END_MGLEVEL_LOOP;
    } END_META_MODE;
  }
  
  void
  initialise_3tl_evolve_Ib (cGH * const cctkGH)
  {
    Waypoint ("Initialisation 3TL evolution I (b) (backwards) at iteration"
              " %d time %g%s%s",
              cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
              (do_global_mode ? " (global)" : ""),
              (do_meta_mode ? " (meta)" : ""));
    
    // Checking
    CalculateChecksums (cctkGH, allbutcurrenttime);
    Poison (cctkGH, currenttimebutnotifonly);
    
    // Evolve backward
    Checkpoint ("Scheduling PRESTEP");
    CCTK_ScheduleTraverse ("CCTK_PRESTEP", cctkGH, CallFunction);
    Checkpoint ("Scheduling EVOL");
    CCTK_ScheduleTraverse ("CCTK_EVOL", cctkGH, CallFunction);
    
    // Checking
    PoisonCheck (cctkGH, alltimes);
  }
  
  // Evolve backwards one more timestep
  // Starting with the finest level and proceeding to the coarsest
  void
  initialise_3tl_evolve_IIb (cGH * const cctkGH)
  {
    Waypoint ("Initialisation 3TL evolution II (b) (backwards) at iteration"
              " %d time %g%s%s",
              cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
              (do_global_mode ? " (global)" : ""),
              (do_meta_mode ? " (meta)" : ""));
    
    Restrict (cctkGH);
    
    if (reflevel < reflevels-1) {
      Checkpoint ("Scheduling POSTRESTRICT");
      CCTK_ScheduleTraverse ("CCTK_POSTRESTRICT", cctkGH, CallFunction);
    }
    
    Checkpoint ("Scheduling POSTSTEP");
    CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cctkGH, CallFunction);
    
    PoisonCheck (cctkGH, alltimes);
  }
  
  void
  initialise_3tl_advance_time_2 (cGH * const cctkGH)
  {
    Waypoint ("Advancing time");
    
    cctkGH->cctk_time
      = global_time + 2 * delta_time * mglevelfact / timereflevelfact;
    for (int m=0; m<maps; ++m) {
      vtt.at(m)->advance_time (reflevel, mglevel);
    }
    
    CycleTimeLevels (cctkGH);
  }
  
  void
  initialise_3tl_evolve_Ic (cGH * const cctkGH)
  {
    Waypoint ("Initialisation 3TL evolution I (c) (backwards) at iteration"
              " %d time %g%s%s",
              cctkGH->cctk_iteration, (double)cctkGH->cctk_time,
              (do_global_mode ? " (global)" : ""),
              (do_meta_mode ? " (meta)" : ""));
    
    CalculateChecksums (cctkGH, allbutcurrenttime);
    Poison (cctkGH, currenttimebutnotifonly);
    
    // Evolve backward
    Checkpoint ("Scheduling PRESTEP");
    CCTK_ScheduleTraverse ("CCTK_PRESTEP", cctkGH, CallFunction);
    Checkpoint ("Scheduling EVOL");
    CCTK_ScheduleTraverse ("CCTK_EVOL", cctkGH, CallFunction);
    Checkpoint ("Scheduling POSTSTEP");
    CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cctkGH, CallFunction);
    
    PoisonCheck (cctkGH, alltimes);
  }
  
  void
  initialise_3tl_reset_time (cGH * const cctkGH)
  {
    Waypoint ("Resetting time");
    
    cctkGH->cctk_time = global_time;
    for (int m=0; m<maps; ++m) {
      vtt.at(m)->set_time (reflevel, mglevel, 0);
    }
  }
  
  
  
  //////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
  
  
  
  void
  print_internal_data ()
  {
    DECLARE_CCTK_PARAMETERS;
    
    if (output_internal_data) {
      CCTK_INFO ("Internal data dump:");
      int const oldprecision = cout.precision();
      cout.precision (17);
      cout << "   global_time: " << global_time << endl
           << "   leveltimes: " << leveltimes << endl
           << "   delta_time: " << delta_time << endl;
      cout.precision (oldprecision);
    }
  }
  
  
  
} // namespace Carpet

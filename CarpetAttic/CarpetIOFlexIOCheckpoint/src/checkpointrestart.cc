#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>



#include <AMRwriter.hh>
#include <AmrGridReader.hh>
#include <H5IO.hh>
#include <HDFIO.hh>
#include <IEEEIO.hh>
#include <IO.hh>

// Hack to stop FlexIO type clash

#undef BYTE
#undef CHAR


#include "cctk.h"
#include "cctk_Parameters.h"
#include "cctk_Version.h"


#include "CactusBase/IOUtil/src/ioGH.h"

#include "Carpet/CarpetLib/src/bbox.hh"
#include "Carpet/CarpetLib/src/data.hh"
#include "Carpet/CarpetLib/src/gdata.hh"
#include "Carpet/CarpetLib/src/ggf.hh"
#include "Carpet/CarpetLib/src/vect.hh"
#include "CactusBase/IOUtil/src/ioGH.h"
#include "CactusBase/IOUtil/src/ioutil_CheckpointRecovery.h"
#include "CactusBase/IOUtil/src/ioutil_Utils.h"

//#include "StoreNamedData.h"
#include "carpet.hh"

#include "ioflexio.hh"

extern "C" {
  static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/CarpetAttic/CarpetIOFlexIOCheckpoint/src/checkpointrestart.cc,v 1.2 2003/05/16 15:17:13 hawke Exp $";
  CCTK_FILEVERSION(Carpet_CarpetIOFlexIO_checkpointrestart_cc);
}

namespace CarpetCheckpointRestart {

  using namespace std;
  using namespace Carpet;
  using namespace CarpetIOFlexIO;

  static int Checkpoint (const cGH* const cgh, int called_from);



  void CarpetChReEvolutionCheckpoint( const cGH* const cgh){
    
    DECLARE_CCTK_PARAMETERS

      if (checkpoint &&
      ((checkpoint_every > 0 && cgh->cctk_iteration % checkpoint_every == 0) ||
       checkpoint_next))
      {
	if (! CCTK_Equals ("verbose", "none"))
	{
	  CCTK_INFO ("---------------------------------------------------------");
	  CCTK_VInfo (CCTK_THORNSTRING, "Dumping periodic checkpoint at "
		      "iteration %d", cgh->cctk_iteration);
	  CCTK_INFO ("---------------------------------------------------------");
	}
	Checkpoint (cgh, CP_EVOLUTION_DATA);

       	if (checkpoint_next)
	{
	  CCTK_ParameterSet ("checkpoint_next", CCTK_THORNSTRING, "no");
	}
      }
    

  }

/********************************************************************
 ********************    Internal Routines   ************************
 ********************************************************************/


  static int DumpParams (const cGH* const cgh, int all, IObase* writer){

    char *parameters;
    
    parameters = IOUtil_GetAllParameters(cgh,all);

    if(parameters)
    {
      writer->writeAttribute("global_parameters",IObase::Char,
			     strlen(parameters)+1,parameters);
      free(parameters);
    }
    return 0;
  }

  static int DumpGHExtensions (const cGH* const cgh, IObase* writer){

    CCTK_INT4 itmp;
    CCTK_REAL dtmp;
    const char *version;
    ioGH *ioUtilGH;


    /* get the handle for IOUtil extensions */
    ioUtilGH = (ioGH *) CCTK_GHExtension (cgh, "IO");

    itmp = CCTK_MainLoopIndex ();
    writer->writeAttribute("main loop index",FLEXIO_INT4,1,&itmp);

    itmp = cgh->cctk_iteration;
    writer->writeAttribute("GH$iteration",FLEXIO_INT4, 1, &itmp);

    itmp = ioUtilGH->ioproc_every;
    writer->writeAttribute("GH$ioproc_every",FLEXIO_INT4,1,&itmp);

    itmp = CCTK_nProcs (cgh);
    writer->writeAttribute("GH$nprocs",FLEXIO_INT4, 1, &itmp);

    dtmp = cgh->cctk_time;
    writer->writeAttribute("GH$time", FLEXIO_REAL, 1, &dtmp);

    version = CCTK_FullVersion ();
    writer->writeAttribute("Cactus version", FLEXIO_CHAR,
                                  strlen (version) + 1, version);



    return 0;
  }


  static int Checkpoint (const cGH* const cgh, int called_from)
  {
    char filename[1024];
    int myproc, first_vindex, gindex, retval;
    char *fullname;
    const char *timer_descriptions[3] = {"Time to dump parameters: ",
					 "Time to dump datasets:   ",
					 "Total time to checkpoint:"};
    const ioGH *ioUtilGH;

    //    const int varindex = CCTK_VarIndex("ADMBASE:gxx");
    int varindex = 0;
    int group = 0;

    cGroup  gdata;
    IObase* writer = 0;
    AMRwriter* amrwriter = 0;
    ioRequest *request;

    DECLARE_CCTK_PARAMETERS
     
    myproc = CCTK_MyProc (cgh);
    ioUtilGH = (const ioGH *) CCTK_GHExtension (cgh, "IO");
    IOUtil_PrepareFilename (cgh, NULL, filename, called_from,
            myproc / ioUtilGH->ioproc_every, ioUtilGH->unchunked);
    
    sprintf(filename, "%s.ieee",filename);

    fprintf(stderr,"%s\n",filename);

    if (CCTK_MyProc(cgh)==0)
      {
	if (CCTK_Equals ("verbose", "full"))
	  {
	    CCTK_VInfo (CCTK_THORNSTRING, "Creating file '%s'", filename);
	  }

	writer = new IEEEIO(filename, IObase::Create);

	if (! (writer->isValid()) )
	  {
	    CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
			"Can't open checkpoint file '%s'. Checkpointing is skipped",
			filename);
	    return (-1);
	  }
	amrwriter = new AMRwriter(*writer);


	// dump parameters 
	DumpParams (cgh, 1, writer);
	// dump GH extentions
	DumpGHExtensions(cgh,writer);

      }

    
    // now dump the grid varibles, sorted by groups

    if (CCTK_Equals ("verbose", "full"))
      {
	CCTK_INFO ("Dumping Grid Variables ...");
      }
    for (group = CCTK_NumGroups () - 1; group >= 0; group--)
      {
	/* only dump groups which have storage assigned */
	if (CCTK_QueryGroupStorageI (cgh, group) <= 0)
	  {
	    continue;
	  }

	/* dump all timelevels except the oldest (for multi-level groups) */
	CCTK_GroupData (group, &gdata);
	if (gdata.numtimelevels > 1)
	  {
	    gdata.numtimelevels--;
	  }

	int first_vindex = CCTK_FirstVarIndexI (group);

	/* get the default I/O request for this group */
	request = IOUtil_DefaultIORequest (cgh, first_vindex, 1);

	/* disable checking for old data objects, disable datatype conversion
	   and downsampling */
	request->check_exist = 0;
	request->hdatatype = gdata.vartype;
	for (request->hdim = 0; request->hdim < request->vdim; request->hdim++)
	  {
	    request->downsample[request->hdim] = 1;
	  }

	/* loop over all variables in this group */
	for (request->vindex = first_vindex;
	     request->vindex < first_vindex + gdata.numvars;
	     request->vindex++)
	  {
	    /* loop over all timelevels of this variable */
	    for (request->timelevel = 0;
		 request->timelevel < gdata.numtimelevels;
		 request->timelevel++)
	      {
	
		if (CCTK_Equals (verbose, "full"))
		  {
		    fullname = CCTK_FullName (request->vindex);
		    CCTK_VInfo (CCTK_THORNSTRING, "  %s (timelevel %d)",
				fullname, request->timelevel);
		    free (fullname);
		  }
		// write the var
		retval += WriteVarAs(cgh,writer,amrwriter,request->vindex,group);
	      }
	  } /* end of loop over all variables */

      } /* end of loop over all groups */


    // Close the file
    if (CCTK_MyProc(cgh)==0) {
      delete amrwriter;
      amrwriter = 0;
      delete writer;
      writer = 0;
    }


    return 0;
      }


} // namespace CarpetCheckpointRestart















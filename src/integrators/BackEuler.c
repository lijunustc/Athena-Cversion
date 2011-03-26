#include "../copyright.h"
/*==============================================================================
 * FILE: BackEuler.c
 *
 * PURPOSE: Contains public functions to set BackEuler.
 *
 * CONTAINS PUBLIC FUNCTIONS: 
 *   BackEuler_init()        - set pointer to integrate function based on dim
 *   BackEuler_destruct()    - call destruct integrate function based on dim
 *============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include "../defs.h"
#include "../athena.h"
#include "prototypes.h"
#include "../prototypes.h"

#if defined(RADIATION_HYDRO) || defined(RADIATION_MHD)

/* dimension of calculation (determined at runtime) */
static int dim=0;
static int ie, is, je, js, ke, ks;

/*----------------------------------------------------------------------------*/
/* BackEuler_init: initialize BackEuler; VMFun_t defined in athena.h   */

VMFun_t BackEuler_init(MeshS *pM)
{
  int i;
 
/* Calculate the dimensions (using root Domain)  */
  dim = 0;
  for (i=0; i<3; i++) if(pM->Nx[i] > 1) dim++;

/* Get number of grids in each direction */
  int NGridx, NGridy, NGridz;
  NGridx = pM->Domain[0][0].NGrid[0];
  NGridy = pM->Domain[0][0].NGrid[1];
  NGridz = pM->Domain[0][0].NGrid[2];

/* set function pointer to appropriate integrator based on dimensions */
  switch(dim){

  case 1:
    if(pM->Nx[0] <= 1) break;
    ie = pM->Domain[0][0].Grid->ie;
    is = pM->Domain[0][0].Grid->is;

    BackEuler_init_1d(ie-is+1,pM);
    return BackEuler_1d;

  case 2:
    if(pM->Nx[2] > 1) break;
    ie = pM->Domain[0][0].Grid->ie;
    is = pM->Domain[0][0].Grid->is;
    je = pM->Domain[0][0].Grid->je;
    js = pM->Domain[0][0].Grid->js;

    BackEuler_init_2d(ie-is+1,je-js+1,NGridx,NGridy);

    return BackEuler_2d;	

  case 3:
    ie = pM->Domain[0][0].Grid->ie;
    is = pM->Domain[0][0].Grid->is;
    je = pM->Domain[0][0].Grid->je;
    js = pM->Domain[0][0].Grid->js;
    ke = pM->Domain[0][0].Grid->ke;
    ks = pM->Domain[0][0].Grid->ks;	

    BackEuler_init_3d(ie-is+1,je-js+1,ke-ks+1,NGridx,NGridy,NGridz);

    return BackEuler_3d;

  }

 
/* This is never executed, but generates a warning on some compilers. */
  return NULL;
}

/*----------------------------------------------------------------------------*/
/* BackEuler_destruct:  */

void BackEuler_destruct()
{
  switch(dim){
  case 1:
    BackEuler_destruct_1d(ie-is+1);
    return;
  case 2:
    BackEuler_destruct_2d();
    return;
  case 3:
    BackEuler_destruct_3d();
    return;
  }

  ath_error("[integrate_destruct]: Grid dimension = %d\n",dim);
}


#endif


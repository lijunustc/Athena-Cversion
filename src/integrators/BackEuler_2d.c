#include "../copyright.h"
/*==============================================================================
 * FILE: BackEuler.c
 *
 * PURPOSE: Use backward Euler method to update the radiation quantities
 * First set up the matrix
 * Then solve the matrix equations.
 * We need the flag for boundary condition.
 *
 * Backward Euler should be used for the whole mesh
 *
 * CONTAINS PUBLIC FUNCTIONS: 
 *   BackEuler_2d()
 *
 *============================================================================*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../defs.h"
#include "../athena.h"
#include "../globals.h"
#include "prototypes.h"
#include "../prototypes.h"
#ifdef PARTICLES
#include "../particles/particle.h"
#endif


#if defined(RADIATION_HYDRO) || defined(RADIATION_MHD)
/*================================*/
/* For the matrix solver */
/* we use lis library now */
#include <lis.h>

/*===============================*/

#endif

#if defined(RADIATIONMHD_INTEGRATOR)
#ifdef SPECIAL_RELATIVITY
#error : The radiation MHD integrator cannot be used for special relativity.
#endif /* SPECIAL_RELATIVITY */



/*===========================================================
 * Private functions used for different boundary conditions *
 * and MPI case *
 */

void i_j(int i, int j);

void i_je_phy(int i);
void i_je_MPI(int i);
void i_js_phy(int i);
void i_js_MPI(int i);

void ie_j_phy(int j);
void ie_j_MPI(int j);

void ie_je_phy_phy();
void ie_je_MPI_phy();
void ie_je_phy_MPI();
void ie_je_MPI_MPI();

void ie_js_phy_phy();
void ie_js_MPI_phy();
void ie_js_phy_MPI();
void ie_js_MPI_MPI();

void is_j_phy(int j);
void is_j_MPI(int j);

void is_je_phy_phy();
void is_je_MPI_phy();
void is_je_phy_MPI();
void is_je_MPI_MPI();

void is_js_phy_phy();
void is_js_MPI_phy();
void is_js_phy_MPI();
void is_js_MPI_MPI();

/*===========================================================*/



/*Store the index of all non-zero elements */
/* The matrix coefficient, this is oneD in GMRES solver */
static LIS_MATRIX Euler;

static LIS_SOLVER solver;
/* Right hand side of the Matrix equation */
static LIS_VECTOR RHSEuler;
/* RHSEuler is 0-------3*Nx*Ny-1; only for GMRES method */ 
static LIS_VECTOR INIguess;
/* Used for initial guess solution, also return the correct solution */

static LIS_SCALAR *Value;
static int *indexValue;
static int *ptr;


/* parameters used to setup the matrix elements */
static int NoEr;
static int NoFr1;
static int NoFr2;
static int is;
static int ie;
static int js;
static int je;
static Real *theta;
static Real *phi;
static Real *psi;

/* subtract background state, which is initial condition */
static Real ***Er_t0;
static Real ***dErdx_t0;
static Real ***dErdy_t0;
static Real ***Fr1_t0;
static Real ***Fr2_t0;

/* boundary flag */
static int ix1;
static int ox1;
static int ix2;
static int ox2;
static int ix3;
static int ox3;

static int NGx; /* Number of Grid in x direction */
static int NGy; /* Number of Grid in y direction */
static int Nx; /* Number of zones in x direction of each Grid */
static int Ny; /* Number of Zones in y direction of each Grid */
static int count_Grids;
static int ID;
static int lx1;
static int rx1;
static int lx2;
static int rx2;


static int MPIcount1;
static int MPIcount2; 
/* Used for MPI periodic boundary condition */



/********Public function****************/
/*-------BackEuler_2d(): Use back euler method to update E_r and Fluxr-----------*/
/* Only work with SMR now. So there is only one Domain in the Mesh */


void BackEuler_2d(MeshS *pM)
{
/* Cell number in root domain is Nx[0,1,2] */
 


	DomainS *pD;
	pD= &(pM->Domain[0][0]);
	
	GridS *pG=pD->Grid;
	Real hdtodx1 = 0.5*pG->dt/pG->dx1;
	Real hdtodx2 = 0.5 * pG->dt/pG->dx2;
	Real dt = pG->dt;
	
	int i, j;
	is = pG->is;
	ie = pG->ie;
	js = pG->js;
	je = pG->je;
	int ks = pG->ks;
	int Nmatrix, NZ_NUM, lines, count;
	
	/* NZ_NUM is the number of non-zero elements in Matrix. It may change if periodic boundary condition is applied */
	/* lines is the size of the partial matrix */
	/* count is the number of total non-zeros before that row */
	/* count_Grids is the total number of lines before this Grids, which depends on the relative position of this grid */
	Real temperature, velocity_x, velocity_y, pressure, T4, Fr0x, Fr0y;
	Real Ci0, Ci1, Cj0, Cj1;
	/* This is equivilent to Cspeeds[] in 1D */


  	Real Sigma_s, Sigma_t, Sigma_a;

/* variables used to subtract background state */
/* NOTICE Here we assume background state grad E_r = -Sigma_t Fr, a uniform flux and background velocity is zero */
/* We assume the background state, Eddington tensor is 1/3 */

	int bgflag;		/* used to subtract whether subtract background or not */
	static int t0flag = 1; /* used to judge if this is the first time call this function or not */  

	bgflag = 0;	/* 1 means subtract background, 0 means not. Default is not */
	
	if(bgflag){
		if(t0flag){
		/* If this the first time, save the background state, including boundary condition */
			for(j=js-nghost; j<=je+nghost; j++){
				for(i=is-nghost; i<=ie+nghost; i++){
					Er_t0[ks][j][i] = pG->U[ks][j][i].Er;
					Fr1_t0[ks][j][i] = pG->U[ks][j][i].Fr1;
					Fr2_t0[ks][j][i] = pG->U[ks][j][i].Fr2;
					
					dErdx_t0[ks][j][i] = -pG->U[ks][j][i].Sigma_t * pG->U[ks][j][i].Fr1;
					dErdy_t0[ks][j][i] = -pG->U[ks][j][i].Sigma_t * pG->U[ks][j][i].Fr2;
				}
			}	
			t0flag = 0;
		}
	}


	/* Boundary condition flag */
	
	ix1 = pM->BCFlag_ix1;
	ox1 = pM->BCFlag_ox1;
	ix2 = pM->BCFlag_ix2;
	ox2 = pM->BCFlag_ox2;
	ix3 = pM->BCFlag_ix3;
	ox3 = pM->BCFlag_ox3;
	


/* Allocate memory space for the Matrix calculation, just used for this grids */
/* Nmatrix is the number of active cells just in this grids */
/* Matrix size should be 3 * Nmatrix, ghost zones are not included*/
	Nx = ie - is + 1;
	Ny = je - js + 1;
   	Nmatrix = Ny * Nx;
	lines  = 3 * Nmatrix; 
	/* total number of lines in the grid. This is also the same for every grid */

	/* For the matrix solver */
	/* The three vectors will be destroyed when destroy LIS matrix Euler */
	Value = (LIS_SCALAR *)malloc(31*Nmatrix*sizeof(LIS_SCALAR));

	if ((indexValue = (int*)malloc(31*Nmatrix*sizeof(int))) == NULL) 
	ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");

	if ((ptr = (int*)malloc((lines+1)*sizeof(int))) == NULL) 
	ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");




	/* ID of this grid */
#ifdef MPI_PARALLEL
	ID = myID_Comm_world;
	lx1 = pG->lx1_id;
	rx1 = pG->rx1_id;
	lx2 = pG->lx2_id;
	rx2 = pG->rx2_id;
#else
	ID = 0;
	lx1 = 0;
	rx1 = 0;
	lx2 = 0;
	rx2 = 0;
#endif	

	/* total number of lines before this grids. This is also the starting line for this grid */ 
	count_Grids = ID * lines;

	/* NGx and NGy are initialized in initialization function */
/*	NGx = pD->NGrid[0];
	NGy = pD->NGrid[1];
*/
	NZ_NUM = 31 * Nmatrix; 
	
	/* Number of non-zero elements is the same for both periodic 
	 * boundary condition and MPI boundary. 
	 * It is just that index will be different *
	 */
	
	NoEr = 0;	/* Position of first non-zero element in row Er */
	NoFr1 = 0;	/* Position of first non-zero element in row Fr1 */
	NoFr2 = 0;	/* POsition of first non-zero element in row Fr2 */
	count = 0;
	/* For non-periodic boundary condition, this number will change */
	

	/* For temporary use only */
	int index,Matrixiter;
	Real tempvalue;

	if(bgflag){
	/* subtract the background state */
		for(j=js-nghost; j<=je+nghost; j++){
			for(i=is-nghost; i<=ie+nghost; i++){
				pG->U[ks][j][i].Er -= Er_t0[ks][j][i];
				pG->U[ks][j][i].Fr1 -= Fr1_t0[ks][j][i];
				pG->U[ks][j][i].Fr2 -= Fr2_t0[ks][j][i];				

			}
		}

	}	
	
	/* *****************************************************/
/* Step 1 : Use Backward Euler to update the radiation energy density and flux */


/* Step 1a: Calculate the Matrix elements  */
/* ie-is+1 =size1, otherwise it is wrong */

/*
	for(i=is; i<=ie+1; i++){
	 	Cspeeds[i-is] = (U1d[i].Edd_11 - U1d[i-1].Edd_11) 
				/ (U1d[i].Edd_11 + U1d[i-1].Edd_11); 		
	}
*/
	for(j=js; j<=je; j++) {
		for(i=is; i<=ie; i++){
/* E is the total energy. should subtract the kinetic energy and magnetic energy density */
/*    		pressure = (pG->U[ks][j][i].E - 0.5 * (pG->U[ks][j][i].M1 * pG->U[ks][j][i].M1 
			+ pG->U[ks][j][i].M2 * pG->U[ks][j][i].M2)/pG->U[ks][j][i].d) * (Gamma - 1.0);
*/
/* if MHD - 0.5 * Bx * Bx   */
/*
#ifdef RADIATION_MHD

		pressure -= 0.5 * (pG->U[ks][j][i].B1c * pG->U[ks][j][i].B1c + pG->U[ks][j][i].B2c * pG->U[ks][j][i].B2c + pG->U[ks][j][i].B3c * pG->U[ks][j][i].B3c) * (Gamma - 1.0);
#endif

    		temperature = pressure / (pG->U[ks][j][i].d * R_ideal);
*/
		/* Guess temperature is updated in the main loop */
		temperature = pG->Tguess[ks][j][i];
		T4 = pow(temperature, 4.0);

		if(bgflag){
			T4 -= Er_t0[ks][j][i];
		}

		/* RHSEuler[0...N-1]  */
		Sigma_a = pG->U[ks][j][i].Sigma_a;
		Sigma_t = pG->U[ks][j][i].Sigma_t;
		Sigma_s = Sigma_t - Sigma_a;

		velocity_x = pG->U[ks][j][i].M1 /pG->U[ks][j][i].d;
		velocity_y = pG->U[ks][j][i].M2 / pG->U[ks][j][i].d;

		/*-----------------------------*/	
		/* index of the vector should be the global vector, not the partial vector */	
    		tempvalue = pG->U[ks][j][i].Er + Crat * dt * Sigma_a * T4;
		
		if(bgflag){
			Fr0x = Fr1_t0[ks][j][i] - ((1.0 + pG->U[ks][j][i].Edd_11) * velocity_x + pG->U[ks][j][i].Edd_21 * velocity_y) * Er_t0[ks][j][i]/Crat;
			Fr0y = Fr2_t0[ks][j][i] - ((1.0 + pG->U[ks][j][i].Edd_22) * velocity_y + pG->U[ks][j][i].Edd_21 * velocity_x) * Er_t0[ks][j][i]/Crat;

			tempvalue += dt * (Sigma_a - Sigma_s) * (velocity_x * Fr0x + velocity_y * Fr0y);
		}	

		index = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
		lis_vector_set_value(LIS_INS_VALUE,index,tempvalue,RHSEuler);

		/*----------------------------*/
    		tempvalue = pG->U[ks][j][i].Fr1 + dt * Sigma_a * T4 * velocity_x;
		
		if(bgflag){
			Fr0x =Fr1_t0[ks][j][i] -  ((1.0 + pG->U[ks][j][i].Edd_11) * velocity_x + pG->U[ks][j][i].Edd_21 * velocity_y) * Er_t0[ks][j][i]/Crat;		
			tempvalue += (-dt * Crat * (dErdx_t0[ks][j][i] * 3.0 * pG->U[ks][j][i].Edd_11 + Sigma_t * Fr0x));
			/* background Edd tensor is 1/3. Here we include perturbation of Eddington tensor to first order and opacity */
		}

		++index;
		lis_vector_set_value(LIS_INS_VALUE,index,tempvalue,RHSEuler);
		
		/*-------------------------*/
		tempvalue = pG->U[ks][j][i].Fr2 + dt * Sigma_a * T4 * velocity_y;
		
		if(bgflag){
			Fr0y = Fr2_t0[ks][j][i] -  ((1.0 + pG->U[ks][j][i].Edd_22) * velocity_y + pG->U[ks][j][i].Edd_21 * velocity_x) * Er_t0[ks][j][i]/Crat;
			tempvalue += (-dt * Crat * (dErdy_t0[ks][j][i] * 3.0 * pG->U[ks][j][i].Edd_22 + Sigma_t * Fr0y));
			/* background Edd tensor is 1/3. Here we include perturbation of Eddington tensor to first order and opacity */
		}

		++index;
		lis_vector_set_value(LIS_INS_VALUE,index,tempvalue,RHSEuler);

		/* For inflow boundary condition along x direction*/
		if((i == is) && (ix1 == 3) && (lx1 < 0)) {
			Ci0 = (sqrt(pG->U[ks][j][i].Edd_11) - sqrt(pG->U[ks][j][i-1].Edd_11)) 
				/ (sqrt(pG->U[ks][j][i].Edd_11) + sqrt(pG->U[ks][j][i-1].Edd_11));
			
			theta[2] = -Crat * hdtodx1 * (1.0 + Ci0) * sqrt(pG->U[ks][j][i-1].Edd_11);
			theta[3] = -Crat * hdtodx1 * (1.0 + Ci0);
			phi[2]	= theta[2] * sqrt(pG->U[ks][j][i-1].Edd_11);
			phi[3]	= theta[3] * sqrt(pG->U[ks][j][i-1].Edd_11);
			psi[2] = -Crat * hdtodx1 * (1.0 + Ci0) * pG->U[ks][j][i-1].Edd_21;
			psi[3] = phi[3];

			/* Subtract some value */
			tempvalue = -(theta[2] * pG->U[ks][j][i-1].Er + theta[3] * pG->U[ks][j][i-1].Fr1);
			index = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);


			tempvalue = -(phi[2] * pG->U[ks][j][i-1].Er + phi[3] * pG->U[ks][j][i-1].Fr1);
			++index;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);

			tempvalue = -(psi[2] * pG->U[ks][j][i-1].Er + psi[3] * pG->U[ks][j][i-1].Fr2);
			++index;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);
			
		}

		if((i == ie) && (ox1 == 3) && (rx1 < 0)) {
			Ci1 =  (sqrt(pG->U[ks][j][i+1].Edd_11) - sqrt(pG->U[ks][j][i].Edd_11)) 
				/ (sqrt(pG->U[ks][j][i+1].Edd_11) + sqrt(pG->U[ks][j][i].Edd_11));

			theta[7] = -Crat * hdtodx1 * (1.0 - Ci1) * sqrt(pG->U[ks][j][i+1].Edd_11);
			theta[8] = Crat * hdtodx1 * (1.0 - Ci1);
			phi[6]	= -theta[7] * sqrt(pG->U[ks][j][i+1].Edd_11);
			phi[7]	= -theta[8] * sqrt(pG->U[ks][j][i+1].Edd_11);
			psi[6]	= Crat * hdtodx1 * (1.0 - Ci1) * pG->U[ks][j][i+1].Edd_21;
			psi[7]	= phi[7];

			tempvalue = -(theta[7] * pG->U[ks][j][i+1].Er + theta[8] * pG->U[ks][j][i+1].Fr1);
			index = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);

			tempvalue = -(phi[6] * pG->U[ks][j][i+1].Er + phi[7] * pG->U[ks][j][i+1].Fr1);
			++index; 
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);

			tempvalue = -(psi[6] * pG->U[ks][j][i+1].Er + psi[7] * pG->U[ks][j][i+1].Fr2);
			++index;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);
					
		}
		

		/* For inflow boundary condition along y direction*/
		if((j == js) && (ix2 == 3) && (lx2 < 0)) {
			Cj0 = (sqrt(pG->U[ks][j][i].Edd_22) - sqrt(pG->U[ks][j-1][i].Edd_22)) 
				/ (sqrt(pG->U[ks][j][i].Edd_22) + sqrt(pG->U[ks][j-1][i].Edd_22));
			
			theta[0] = -Crat * hdtodx2 * (1.0 + Cj0) * sqrt(pG->U[ks][j-1][i].Edd_22);
			theta[1] = -Crat * hdtodx2 * (1.0 + Cj0);
			phi[0]	= -Crat * hdtodx2 * (1.0 + Cj0) * pG->U[ks][j-1][i].Edd_21;
			phi[1]	= theta[1] * sqrt(pG->U[ks][j-1][i].Edd_22);
			psi[0] = theta[0] * sqrt(pG->U[ks][j-1][i].Edd_22);
			psi[1] = phi[1];

			tempvalue = -(theta[0] * pG->U[ks][j-1][i].Er + theta[1] * pG->U[ks][j-1][i].Fr2);
			index = 3*(j-js)*Nx + 3 * (i - is) + count_Grids;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);


			tempvalue = -(phi[0] * pG->U[ks][j-1][i].Er + phi[1] * pG->U[ks][j-1][i].Fr1);
			++index;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);


			tempvalue = -(psi[0] * pG->U[ks][j-1][i].Er + psi[1] * pG->U[ks][j-1][i].Fr2);
			++index;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);
				
		}

		if((j == je) && (ox2 == 3) && (rx2 < 0)) {
			Cj1 =  (sqrt(pG->U[ks][j+1][i].Edd_22) - sqrt(pG->U[ks][j][i].Edd_22)) 
				/ (sqrt(pG->U[ks][j+1][i].Edd_22) + sqrt(pG->U[ks][j][i].Edd_22));

			theta[9] = -Crat * hdtodx2 * (1.0 - Cj1) * sqrt(pG->U[ks][j+1][i].Edd_22);
			theta[10] = Crat * hdtodx2 * (1.0 - Cj1);
			phi[8]	= theta[10] * pG->U[ks][j+1][i].Edd_21;
			phi[9]	= -theta[10] * sqrt(pG->U[ks][j+1][i].Edd_22);
			psi[8]	= -theta[9] * sqrt(pG->U[ks][j+1][i].Edd_22);
			psi[9]	= phi[9];

			tempvalue = -(theta[9] * pG->U[ks][j+1][i].Er + theta[10] * pG->U[ks][j+1][i].Fr2);
			index = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);

			tempvalue = -(phi[8] * pG->U[ks][j+1][i].Er + phi[9] * pG->U[ks][j+1][i].Fr1);
			++index;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);


			tempvalue = -(psi[8] * pG->U[ks][j+1][i].Er + psi[9] * pG->U[ks][j+1][i].Fr2);
			++index;
			lis_vector_set_value(LIS_ADD_VALUE,index,tempvalue,RHSEuler);
			
		}
				
		}
	}

	
/*--------------------Note--------------------*/


/* Step 1b: Setup the Matrix */
		
 	/* First, setup the guess solution. Guess solution is the solution from last time step */
	for(j=js; j<=je; j++){
		for(i=is; i<=ie; i++){
			lis_vector_set_value(LIS_INS_VALUE,3*(j-js)*Nx + 3*(i-is) + count_Grids, pG->U[ks][j][i].Er,INIguess);
			lis_vector_set_value(LIS_INS_VALUE,3*(j-js)*Nx + 3*(i-is) + 1 + count_Grids, pG->U[ks][j][i].Fr1,INIguess);
			lis_vector_set_value(LIS_INS_VALUE,3*(j-js)*Nx + 3*(i-is) + 2 + count_Grids, pG->U[ks][j][i].Fr2,INIguess);
			
		}
	}

	
	/*--------Now set the Euler matrix-----------*/
	for(j=js; j<=je; j++){
		for(i=is; i<=ie; i++){
			
			velocity_x = pG->U[ks][j][i].M1 / pG->U[ks][j][i].d;
			velocity_y = pG->U[ks][j][i].M2 / pG->U[ks][j][i].d;
			Sigma_a = pG->U[ks][j][i].Sigma_a;
			Sigma_t = pG->U[ks][j][i].Sigma_t;
			Sigma_s = Sigma_t - Sigma_a;
			Ci0 = (sqrt(pG->U[ks][j][i].Edd_11) - sqrt(pG->U[ks][j][i-1].Edd_11)) 
				/ (sqrt(pG->U[ks][j][i].Edd_11) + sqrt(pG->U[ks][j][i-1].Edd_11));
			Ci1 =  (sqrt(pG->U[ks][j][i+1].Edd_11) - sqrt(pG->U[ks][j][i].Edd_11)) 
				/ (sqrt(pG->U[ks][j][i+1].Edd_11) + sqrt(pG->U[ks][j][i].Edd_11));
			Cj0 = (sqrt(pG->U[ks][j][i].Edd_22) - sqrt(pG->U[ks][j-1][i].Edd_22)) 
				/ (sqrt(pG->U[ks][j][i].Edd_22) + sqrt(pG->U[ks][j-1][i].Edd_22));
			Cj1 =  (sqrt(pG->U[ks][j+1][i].Edd_22) - sqrt(pG->U[ks][j][i].Edd_22)) 
				/ (sqrt(pG->U[ks][j+1][i].Edd_22) + sqrt(pG->U[ks][j][i].Edd_22));
			theta[0] = -Crat * hdtodx2 * (1.0 + Cj0) * sqrt(pG->U[ks][j-1][i].Edd_22);
			theta[1] = -Crat * hdtodx2 * (1.0 + Cj0);
			theta[2] = -Crat * hdtodx1 * (1.0 + Ci0) * sqrt(pG->U[ks][j][i-1].Edd_11);
			theta[3] = -Crat * hdtodx1 * (1.0 + Ci0);
			theta[4] = 1.0 + Crat * hdtodx1 * (2.0 + Ci1 - Ci0) * sqrt(pG->U[ks][j][i].Edd_11) 
				+ Crat * hdtodx2 * (2.0 + Cj1 - Cj0) * sqrt(pG->U[ks][j][i].Edd_22)
				+ Crat * pG->dt * Sigma_a 
				+ pG->dt * (Sigma_a - Sigma_s) * ((1.0 + pG->U[ks][j][i].Edd_11) * velocity_x 
				+ velocity_y * pG->U[ks][j][i].Edd_21) * velocity_x / Crat
				+ pG->dt * (Sigma_a - Sigma_s) * ((1.0 + pG->U[ks][j][i].Edd_22) * velocity_y 
				+ velocity_x * pG->U[ks][j][i].Edd_21) * velocity_y / Crat;
			theta[5] = Crat * hdtodx1 * (Ci0 + Ci1)	- pG->dt * (Sigma_a - Sigma_s) * velocity_x;
			theta[6] = Crat * hdtodx2 * (Cj0 + Cj1)	- pG->dt * (Sigma_a - Sigma_s) * velocity_y;
			theta[7] = -Crat * hdtodx1 * (1.0 - Ci1) * sqrt(pG->U[ks][j][i+1].Edd_11);
			theta[8] = Crat * hdtodx1 * (1.0 - Ci1);
			theta[9] = -Crat * hdtodx2 * (1.0 - Cj1) * sqrt(pG->U[ks][j+1][i].Edd_22);
			theta[10] = Crat * hdtodx2 * (1.0 - Cj1);
			

			phi[0] = theta[1] * pG->U[ks][j-1][i].Edd_21;
			phi[1] = theta[0];
			phi[2] = theta[3] * pG->U[ks][j][i-1].Edd_11;
			phi[3] = theta[2]; 
			phi[4] = Crat * hdtodx1 * (Ci0 + Ci1) * pG->U[ks][j][i].Edd_11
			       + Crat * hdtodx2 * (Cj0 + Cj1) * pG->U[ks][j][i].Edd_21   
			       - pG->dt * Sigma_t * ((1.0 + pG->U[ks][j][i].Edd_11) * velocity_x + pG->U[ks][j][i].Edd_21 * velocity_y) 
			       + pG->dt * Sigma_a * velocity_x;
			phi[5] = 1.0 + Crat * hdtodx1 * (2.0 + Ci1 - Ci0) * sqrt(pG->U[ks][j][i].Edd_11) 
				     + Crat * hdtodx2 * (2.0 + Cj1 - Cj0) * sqrt(pG->U[ks][j][i].Edd_22) 
				     + Crat * pG->dt * Sigma_t;
			phi[6] = theta[8] * pG->U[ks][j][i+1].Edd_11;
			phi[7] = theta[7];
			phi[8] = theta[10] * pG->U[ks][j+1][i].Edd_21;
			phi[9] = theta[9];


			psi[0] = theta[1] * pG->U[ks][j-1][i].Edd_22;
			psi[1] = theta[0];
			psi[2] = theta[3] * pG->U[ks][j][i-1].Edd_21;
			psi[3] = theta[2];			
			psi[4] = Crat * hdtodx1 * (Ci0 + Ci1) * pG->U[ks][j][i].Edd_21
			       + Crat * hdtodx2 * (Cj0 + Cj1) * pG->U[ks][j][i].Edd_22   
			       - pG->dt * Sigma_t * ((1.0 + pG->U[ks][j][i].Edd_22) * velocity_y + pG->U[ks][j][i].Edd_21 * velocity_x) 
			       + pG->dt * Sigma_a * velocity_y;
			psi[5] = phi[5];
			psi[6] = theta[8] * pG->U[ks][j][i+1].Edd_21;
			psi[7] = theta[7];
			psi[8] = theta[10] * pG->U[ks][j+1][i].Edd_22;
			psi[9] = theta[9];


		if(i == is){
			if(j == js){
				if((ix1 != 4) && (pG->lx1_id < 0)){
				/* physical boundary on the left */						
					if((ix2 !=4) && (pG->lx2_id < 0)){
						NZ_NUM -= 12;
						NoEr = count;
						NoFr1 = count + 7;
						NoFr2 = count + 13;
						count += 19;
					}
					else{
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					} /* either periodic or MPI boundary condition */
				}/* Non periodic for x1 */
				else{
					if((ix2 !=4) && (pG->lx2_id < 0)){
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					}
					else{
						NoEr = count;
						NoFr1 = count + 11;
						NoFr2 = count + 21;
						count += 31;
					}
				}/* either periodic for x1 or MPI boundary condition */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;

				/* judge MPI or physical boundary condition */
				if((pG->lx1_id<0) && (pG->lx2_id<0))
					is_js_phy_phy();
				else if((pG->lx1_id<0) && (pG->lx2_id>=0))
					is_js_phy_MPI();
				else if((pG->lx1_id>=0) && (pG->lx2_id<0))
					is_js_MPI_phy();
				else
					is_js_MPI_MPI();
	
			}/* End j == js */
			else if(j == je){
				if((ix1 != 4) && (pG->lx1_id < 0)){						
					if((ox2 !=4) && (pG->rx2_id < 0)){
						NZ_NUM -= 12;
						NoEr = count;
						NoFr1 = count + 7;
						NoFr2 = count + 13;
						count += 19;
					}
					else{
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					}
				}/* Non periodic for x1 */
				else{
					if((ox2 !=4) && (pG->rx2_id < 0)){
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					}
					else{
						NoEr = count;
						NoFr1 = count + 11;
						NoFr2 = count + 21;
						count += 31;
					}
				}/* periodic for x1 */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;
				
				/* judge MPI or physical boundary condition */
				if((pG->lx1_id<0) && (pG->rx2_id<0))
					is_je_phy_phy();
				else if((pG->lx1_id<0) && (pG->rx2_id>=0))
					is_je_phy_MPI();
				else if((pG->lx1_id>=0) && (pG->rx2_id<0))
					is_je_MPI_phy();
				else
					is_je_MPI_MPI();				
			
			} /* End j == je */
			else {
				if((ix1 != 4) && (pG->lx1_id < 0)){						
					NZ_NUM -= 6;
					NoEr = count;
					NoFr1 = count + 9;
					NoFr2 = count + 17;
					count += 25;
					
				}/* Non periodic for x1 */
				else{
				
					NoEr = count;
					NoFr1 = count + 11;
					NoFr2 = count + 21;
					count += 31;
				
				}/* periodic for x1 */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;

				/* judge MPI or physical boundary condition */
				if(pG->lx1_id<0)
					is_j_phy(j);				
				else
					is_j_MPI(j);						

			} /* End j!= js & j != je */
		}/* End i==is */
		else if (i == ie){
			if(j == js){
				if((ox1 != 4) && (pG->rx1_id <0)){						
					if((ix2 !=4) && (pG->lx2_id < 0)){
						NZ_NUM -= 12;
						NoEr = count;
						NoFr1 = count + 7;
						NoFr2 = count + 13;
						count += 19;
					}
					else{
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					}
				}/* Non periodic for x1 */
				else{
					if((ix2 !=4) && (pG->lx2_id < 0)){
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					}
					else{
						NoEr = count;
						NoFr1 = count + 11;
						NoFr2 = count + 21;
						count += 31;
					}
				}/* periodic for x1 */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;

				/* judge MPI or physical boundary condition */
				if((pG->rx1_id<0) && (pG->lx2_id<0))
					ie_js_phy_phy();
				else if((pG->rx1_id<0) && (pG->lx2_id>=0))
					ie_js_phy_MPI();
				else if((pG->rx1_id>=0) && (pG->lx2_id<0))
					ie_js_MPI_phy();
				else
					ie_js_MPI_MPI();	
				
			} /* End j==js */
			else if(j == je){
				if((ox1 != 4) && (pG->rx1_id < 0)){						
					if((ox2 !=4) && (pG->rx2_id < 0)){
						NZ_NUM -= 12;
						NoEr = count;
						NoFr1 = count + 7;
						NoFr2 = count + 13;
						count += 19;
					}
					else{
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					}
				}/* Non periodic for x1 */
				else{
					if((ox2 !=4) && (pG->rx2_id < 0)){
						NZ_NUM -= 6;
						NoEr = count;
						NoFr1 = count + 9;
						NoFr2 = count + 17;
						count += 25;
					}
					else{
						NoEr = count;
						NoFr1 = count + 11;
						NoFr2 = count + 21;
						count += 31;
					}
				}/* periodic for x1 */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;
				
				
				/* judge MPI or physical boundary condition */
				if((pG->rx1_id<0) && (pG->rx2_id<0))
					ie_je_phy_phy();
				else if((pG->rx1_id<0) && (pG->rx2_id>=0))
					ie_je_phy_MPI();
				else if((pG->rx1_id>=0) && (pG->rx2_id<0))
					ie_je_MPI_phy();
				else
					ie_je_MPI_MPI();	
			
				
			} /* End j==je */
			else {
				if((ox1 != 4) && (pG->rx1_id < 0)){						
					NZ_NUM -= 6;
					NoEr = count;
					NoFr1 = count + 9;
					NoFr2 = count + 17;
					count += 25;
					
				}/* Non periodic for x1 */
				else{
				
					NoEr = count;
					NoFr1 = count + 11;
					NoFr2 = count + 21;
					count += 31;
				
				}/* periodic for x1 */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;

				/* judge MPI or physical boundary condition */
				if(pG->rx1_id<0)
					ie_j_phy(j);				
				else
					ie_j_MPI(j);

				
			} /* End j!=js & j!= je*/
		}/* End i==ie */
		else {
			if(j == js){
				if((ix2 != 4) && (pG->lx2_id<0)){						
					NZ_NUM -= 6;
					NoEr = count;
					NoFr1 = count + 9;
					NoFr2 = count + 17;
					count += 25;
					
				}/* Non periodic for x2 */
				else{
				
					NoEr = count;
					NoFr1 = count + 11;
					NoFr2 = count + 21;
					count += 31;
				
				}/* periodic for x2 */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;

				/* judge MPI or physical boundary condition */
				if(pG->lx2_id<0)
					i_js_phy(i);				
				else
					i_js_MPI(i);
			
				
			}
			else if(j == je){

				if((ox2 != 4) && (pG->rx2_id < 0)){						
					NZ_NUM -= 6;
					NoEr = count;
					NoFr1 = count + 9;
					NoFr2 = count + 17;
					count += 25;
					
				}/* Non periodic for x2 */
				else{
				
					NoEr = count;
					NoFr1 = count + 11;
					NoFr2 = count + 21;
					count += 31;
				
				}/* periodic for x2 */

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;

				/* judge MPI or physical boundary condition */
				if(pG->rx2_id<0)
					i_je_phy(i);				
				else
					i_je_MPI(i);

				
			}
			else {
				/* no boundary cells on either direction */
				/*NZ_NUM;*/
				NoEr = count;
				NoFr1 = count + 11;
				NoFr2 = count + 21;
				count += 31;

				ptr[3*(j-js)*Nx+3*(i-is)] = NoEr;
				ptr[3*(j-js)*Nx+3*(i-is)+1] = NoFr1;
				ptr[3*(j-js)*Nx+3*(i-is)+2] = NoFr2;

				/* no boundary */
				i_j(i,j);

				
			}
		}/* End i!=is & i!=ie */
		}/* End loop i */
	}/* End loop j */

		/* The last element of ptr */
		ptr[lines] = NZ_NUM;

		/* We have to create Euler every time. Otherwise the matrix solver will be wrong */
#ifdef MPI_PARALLEL
		lis_matrix_create(MPI_COMM_WORLD,&Euler);
#else
		lis_matrix_create(0,&Euler);
#endif	
		lis_matrix_set_size(Euler,lines,0);


		

		/* Assemble the matrix and solve the matrix */
		lis_matrix_set_crs(NZ_NUM,ptr,indexValue,Value,Euler);
		lis_matrix_assemble(Euler);

		
		lis_solver_set_option("-i gmres -p ilu",solver);
		lis_solver_set_option("-tol 1.0e-12",solver);
		lis_solver_set_option("-maxiter 2000",solver);
		lis_solve(Euler,RHSEuler,INIguess,solver);
		
		/* check the iteration step to make sure 1.0e-12 is reached */
		lis_solver_get_iters(solver,&Matrixiter);

		ath_pout(0,"Matrix Iteration steps: %d\n",Matrixiter);

		
	/* update the radiation quantities in the mesh */	
	for(j=js;j<=je;j++){
		for(i=is; i<=ie; i++){

		lis_vector_get_value(INIguess,3*(j-js)*Nx + 3*(i-is) + count_Grids,&(pG->U[ks][j][i].Er));
		lis_vector_get_value(INIguess,3*(j-js)*Nx + 3*(i-is)+1+count_Grids,&(pG->U[ks][j][i].Fr1));
		lis_vector_get_value(INIguess,3*(j-js)*Nx + 3*(i-is)+2+count_Grids,&(pG->U[ks][j][i].Fr2));
/*
		if(pG->U[ks][j][i].Er < 0.0)
			fprintf(stderr,"[BackEuler_2d]: Negative Radiation energy: %e\n",pG->U[ks][j][i].Er);
*/
		}
	
		
	}
	/* Eddington factor is updated in the integrator  */

	/* Add back the background state */
	if(bgflag){
		for(j=js-nghost; j<=je+nghost; j++){
			for(i=is-nghost; i<=ie+nghost; i++){
				
				pG->U[ks][j][i].Er += Er_t0[ks][j][i];
				pG->U[ks][j][i].Fr1 += Fr1_t0[ks][j][i];
				pG->U[ks][j][i].Fr2 += Fr2_t0[ks][j][i];

			}
		}
	}


/* Update the ghost zones for different boundary condition to be used later */
	for (i=0; i<pM->NLevels; i++){ 
            for (j=0; j<pM->DomainsPerLevel[i]; j++){  
        	if (pM->Domain[i][j].Grid != NULL){
  			bvals_radMHD(&(pM->Domain[i][j]));

        	}
      	     }
    	}

		/* Destroy the matrix, Value, indexValue  and ptr are also destroyed here */
		lis_matrix_destroy(Euler);

	
  return;	
	

}



/*-------------------------------------------------------------------------*/
/* BackEuler_init_2d: function to allocate memory used just for radiation variables */
/* BackEuler_destruct_2d(): function to free memory */
void BackEuler_init_2d(const int Nx, const int Ny, const int NGridx, const int NGridy)
{

/* Nelements = (je-js+1)*(ie-is+1) */
/* Nx = ie-is+1 */
/* Ny = je-js+1 */

	int line;
	line = 3*Nx*Ny;
	

/* Number of Grids in each direction */
	NGx = NGridx;
	NGy = NGridy;

/* The matrix Euler is stored as a compact form.  */
	/*FOR LIS LIBRARY */
#ifdef MPI_PARALLEL
	lis_vector_create(MPI_COMM_WORLD,&RHSEuler);
	lis_vector_create(MPI_COMM_WORLD,&INIguess);
#else
	lis_matrix_create(0,&RHSEuler);
	lis_matrix_create(0,&INIguess);
#endif	
	
	lis_vector_set_size(RHSEuler,line,0);
	lis_vector_set_size(INIguess,line,0);	


	lis_solver_create(&solver);

	/* Allocate value and index array */
/*
	if ((Value = (Real*)malloc(Nelements*sizeof(Real))) == NULL) 
	ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");
*/
	/* to save Er and Fr at time t0, which are used to subtract the background state */
	if((Er_t0 = (Real***)calloc_3d_array(1, Ny+2*nghost, Nx+2*nghost, sizeof(Real))) == NULL)
		ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");
	if((dErdx_t0 = (Real***)calloc_3d_array(1, Ny+2*nghost, Nx+2*nghost, sizeof(Real))) == NULL)
		ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");
	if((dErdy_t0 = (Real***)calloc_3d_array(1, Ny+2*nghost, Nx+2*nghost, sizeof(Real))) == NULL)
		ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");
	if((Fr1_t0 = (Real***)calloc_3d_array(1, Ny+2*nghost, Nx+2*nghost, sizeof(Real))) == NULL)
		ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");
	if((Fr2_t0 = (Real***)calloc_3d_array(1, Ny+2*nghost, Nx+2*nghost, sizeof(Real))) == NULL)
		ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");



/* For temporary vector theta, phi, psi */
	if ((theta = (Real*)malloc(11*sizeof(Real))) == NULL) 
	ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");

	if ((phi = (Real*)malloc(11*sizeof(Real))) == NULL) 
	ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");

	if ((psi = (Real*)malloc(11*sizeof(Real))) == NULL) 
	ath_error("[BackEuler_init_2d]: malloc returned a NULL pointer\n");

	
	return;

}


void BackEuler_destruct_2d()
{


	lis_solver_destroy(solver);	
	lis_vector_destroy(RHSEuler);
	lis_vector_destroy(INIguess);

/* For temporary vector theta, phi, psi */
	if(theta != NULL) free(theta);
	if(phi != NULL) free(phi);
	if(psi != NULL) free(psi);

/* variables used to subtract background state */
	free_3d_array(Er_t0);
	free_3d_array(dErdx_t0);
	free_3d_array(dErdy_t0);	
	free_3d_array(Fr1_t0);
	free_3d_array(Fr2_t0);



/* Memory for Value, indexValue, ptr are already freed when destroy Euler matrix */
/*	
	if(Value != NULL) free(Value);
	if(indexValue != NULL) free(indexValue);
	if(ptr != NULL) free(ptr);
*/
}

/*********************************************************************************/
/* ========= Private Function =====================*/
/* ===============================================*/
/* Function to assign values of the vectors and matrix for different 
 * boundary condition. Judge whether physical boundary or MPI boundary */

/*--------------------------------*/
/*------ is and js --------------*/
/*--------------------------------------*/
void is_js_MPI_MPI()
{
	int m;
	int shiftx, shifty, index1, index2, index;
	int MPIcount1x, MPIcount1y;
	int MPIcount2x, MPIcount2y;
	int MPIcount2xF, MPIcount2yF;

	
	if(lx1 > ID){ 
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount2x = 7;
		MPIcount2xF = 6;
		MPIcount1x = 0;
	}
	else {  
		shiftx = 3 * Nx * Ny;
		MPIcount2x = 0;
		MPIcount2xF = 0;
		MPIcount1x = 2;
	}


	if(lx2 > ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount2y = 9;
		MPIcount2yF = 8;
		MPIcount1y = 0; 
	}
	else{ 
		shifty = 3 * Nx * Ny * NGx;
		MPIcount2y = 0;
		MPIcount2yF = 0;
		MPIcount1y = 2;

	}

	/* For MPI part */
	/* For Er */
	index2 = NoEr + MPIcount2y;
	index1 = NoEr + MPIcount2x + MPIcount1y;

	Value[index2] = theta[0];
	Value[index2+1] = theta[1];

	indexValue[index2] = 3*(je-js)*Nx + count_Grids - shifty;
	indexValue[index2+1] = indexValue[index2] + 2;	


	Value[index1] = theta[2];
	Value[index1+1] = theta[3];
	
	indexValue[index1] = 3*(ie-is) + count_Grids - shiftx;
	indexValue[index1+1] = indexValue[index1] + 1; 

	
	/* For Fr1 */
	index2 = NoFr1 + MPIcount2yF;
	index1 = NoFr1 + MPIcount2xF + MPIcount1y;

	Value[index2] = phi[0];
	Value[index2+1] = phi[1];

	indexValue[index2] = 3*(je-js)*Nx + count_Grids - shifty;
	indexValue[index2+1] = indexValue[index2] + 1;		

	Value[index1] = phi[2];
	Value[index1+1] = phi[3];	

	indexValue[index1] = 3*(ie-is) + count_Grids - shiftx;
	indexValue[index1+1] = indexValue[index1] + 1; 

	


	/* For Fr2 */
	index2 = NoFr2 + MPIcount2yF;
	index1 = NoFr2 + MPIcount2xF + MPIcount1y;

	Value[index2] = psi[0];
	Value[index2+1] = psi[1];

	indexValue[index2] = 3*(je-js)*Nx + count_Grids - shifty;
	indexValue[index2+1] = indexValue[index2] + 2;		


	Value[index1] = psi[2];
	Value[index1+1] = psi[3];	

	indexValue[index1] = 3*(ie-is) + count_Grids - shiftx;
	indexValue[index1+1] = indexValue[index1] + 2; 

	

	/* Er */
	index = NoEr + MPIcount1x + MPIcount1y;
	for(m=0; m<7; m++)
		Value[index+m] = theta[m+4];

	for(m=0;m<5;m++)
		indexValue[index+m] = m + count_Grids;
		
	indexValue[index+5] = 3 * Nx + count_Grids;
	indexValue[index+6] = 3 * Nx + 2 + count_Grids;
				
					
						
	/* For Fr1 */
	index = NoFr1 + MPIcount1x + MPIcount1y;
	for(m=0; m<6; m++)
		Value[index+m] = phi[m+4];

		indexValue[index] = 0 + count_Grids;
		indexValue[index+1] = 1 + count_Grids;
		indexValue[index+2] = 3 + count_Grids;
		indexValue[index+3] = 4 + count_Grids;

		indexValue[index+4] = 3 * Nx + count_Grids;
		indexValue[index+5] = 3 * Nx + 1 + count_Grids;
				
					

	/* For Fr2 */
	index = NoFr2 + MPIcount1x + MPIcount1y;			
	for(m=0; m<6; m++)
		Value[index+m] = psi[m+4];

		indexValue[index] = 0 + count_Grids;
		indexValue[index+1] = 2 + count_Grids;
		indexValue[index+2] = 3 + count_Grids;
		indexValue[index+3] = 5 + count_Grids;

		indexValue[index+4] = 3 * Nx + count_Grids;
		indexValue[index+5] = 3 * Nx + 2 + count_Grids;
				

	return;
} /* MPI boundary for both x and y direction */


void is_js_phy_MPI()
{
	int m;
	int shifty;
	int index;
	int MPIcount2F;
	

	if(lx2 > ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount1 = 0;
		if(ix1 == 4){
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else{ 
			MPIcount2 = 7;
			MPIcount2F = 6;
		}
		
	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;

	}
	/* For Er */
	index = NoEr+MPIcount2;
	Value[index] = theta[0];
	Value[index+1] = theta[1];

	indexValue[index] = 3*(je-js)*Nx + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 2;	

	index = NoEr + MPIcount1;
	for(m=0; m<5; m++)
		Value[index+m] = theta[4+m];
	for(m=0;m<5;m++)
		indexValue[index+m] = m + count_Grids;
				
	index += 5;
		if(ix1 != 4){
						
			Value[index]	= theta[9];
			Value[index+1]	= theta[10];
			indexValue[index] = 3 * Nx + count_Grids;
			indexValue[index+1] = 3 * Nx + 2 + count_Grids;
			
		}
		else {
			Value[index]	= theta[2];
			Value[index+1]	= theta[3];			
			indexValue[index] = 3 * (ie - is) + count_Grids;
			indexValue[index+1] = 3 * (ie - is) + 1 + count_Grids;

			
			Value[index+2]	= theta[9];
			Value[index+3]	= theta[10];
			indexValue[index+2] = 3 * Nx + count_Grids;
			indexValue[index+3] = 3 * Nx + 2 + count_Grids;
		}

	
		
						
	/* For Fr1 */	
	index = NoFr1 + MPIcount2F;


	Value[index] = phi[0];
	Value[index+1] = phi[1];

	indexValue[index] = 3*(je-js)*Nx + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 1;	
	
	index = NoFr1 + MPIcount1;
	for(m=0; m<4; m++)
		Value[index+m] = phi[4+m];

		indexValue[index] = 0 + count_Grids;
		indexValue[index+1] = 1 + count_Grids;
		indexValue[index+2] = 3 + count_Grids;
		indexValue[index+3] = 4 + count_Grids;
				
	index += 4;
		if(ix1 != 4){
			Value[index]	= phi[8];
			Value[index+1]	= phi[9];
			indexValue[index] = 3 * Nx + count_Grids;
			indexValue[index+1] = 3 * Nx + 1 + count_Grids;
			
		}
		else{
			Value[index]	= phi[2];
			Value[index+1]	= phi[3];			
			indexValue[index] = 3 * (ie - is) + count_Grids;
			indexValue[index+1] = 3 * (ie - is) + 1 + count_Grids;

			
			Value[index+2]	= phi[8];
			Value[index+3]	= phi[9];
			indexValue[index+2] = 3 * Nx + count_Grids;
			indexValue[index+3] = 3 * Nx + 1 + count_Grids;
		}				

					

	/* For Fr2 */
	index = NoFr2 + MPIcount2F;
		
	Value[index] = psi[0];
	Value[index+1] = psi[1];

	indexValue[index] = 3*(je-js)*Nx + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 2;	

	index = NoFr2 + MPIcount1;
	for(m=0; m<4; m++)
		Value[m+index] = psi[4+m];

		indexValue[index] = 0 + count_Grids;
		indexValue[index+1] = 2 + count_Grids;
		indexValue[index+2] = 3 + count_Grids;
		indexValue[index+3] = 5 + count_Grids;

	index += 4;
				
		if(ix1 != 4){
			Value[index]	= psi[8];
			Value[index+1]	= psi[9];
			indexValue[index] = 3 * Nx + count_Grids;
			indexValue[index+1] = 3 * Nx + 2 + count_Grids;
			
		}
		else{
			Value[index]	= psi[2];
			Value[index+1]	= psi[3];			
			indexValue[index] = 3 * (ie - is) + count_Grids;
			indexValue[index+1] = 3 * (ie - is) + 2 + count_Grids;

			

			Value[index+2]	= psi[8];
			Value[index+3]	= psi[9];
			indexValue[index+2] = 3 * Nx + count_Grids;
			indexValue[index+3] = 3 * Nx + 2 + count_Grids;
		}				


		

		/* other ix1 boundary condition */
		if(ix1 == 1 || ix1 == 5){
				
			Value[NoEr+MPIcount1] += theta[2];
			Value[NoEr+MPIcount1+1] -= theta[3];
		
			Value[NoFr1+MPIcount1]+= phi[2];
			Value[NoFr1+MPIcount1+1]-= phi[3];
				
			Value[NoFr2+MPIcount1]+= psi[2];
			Value[NoFr2+MPIcount1+1]+= psi[3];
		}
		else if(ix1 == 2){
			Value[NoEr+MPIcount1] += theta[2];
			Value[NoEr+MPIcount1+1] += theta[3];
	
			Value[NoFr1+MPIcount1]+= phi[2];
			Value[NoFr1+MPIcount1+1]+= phi[3];
				
			Value[NoFr2+MPIcount1]+= psi[2];
			Value[NoFr2+MPIcount1+1]+= psi[3];
		}
		else if(ix1 == 3){

			/* Do nothing */
		}

	return;
} /* physical for x direction and MPI for y direction */


/* MPIcount2 is for MPI part */
/* MPIcount1 is for nonMPI part */


void is_js_MPI_phy()
{
	int m;
	int shiftx, index;
	int MPIcount2F;

	if(lx1 > ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount1 = 0;
		if(ix2 == 4){
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else {
			MPIcount2 = 7;
			MPIcount2F = 6;
		}
	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;
	}

	/* for MPI part */
	/* NoEr */
	index = NoEr+MPIcount2;
	Value[index] = theta[2];
	Value[index+1] = theta[3];

	indexValue[index] = 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* NoFr1 */
	index = NoFr1 + MPIcount2F;	

	Value[index] = phi[2];
	Value[index+1] = phi[3];

	indexValue[index  ] = 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index ] + 1;

	/* NoFr2 */
	index = NoFr2 + MPIcount2F;		

	Value[index] = psi[2];
	Value[index+1] = psi[3];

	indexValue[index  ] = 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index ] + 2;



	/* For Er */
	
	index = NoEr + MPIcount1;
	for(m=0; m<7; m++)
		Value[index+m] = theta[4+m];	

	
	for(m=0;m<5;m++)
		indexValue[index+m] = m + count_Grids;

	indexValue[index+5] = 3 * Nx + count_Grids;
	indexValue[index+6] = 3 * Nx + 2 + count_Grids;
	
				
						
	/* For Fr1 */
	index = NoFr1 + MPIcount1;
	for(m=0; m<6; m++)
		Value[index+m] = phi[4+m];


	indexValue[index] = 0 + count_Grids;
	indexValue[index+1] = 1 + count_Grids;
	
	indexValue[index+2] = 3 + count_Grids;
	indexValue[index+3] = 4 + count_Grids;

	indexValue[index+4] = 3 * Nx + count_Grids;
	indexValue[index+5] = 3 * Nx + 1 + count_Grids;

	/* For Fr2 */
			
	index = NoFr2 + MPIcount1;
	for(m=0; m<6; m++)
		Value[index+m] = psi[4+m];

	
	indexValue[index] = 0 + count_Grids;
	indexValue[index+1] = 2 + count_Grids;
	
	indexValue[index+2] = 3 + count_Grids;
	indexValue[index+3] = 5 + count_Grids;

	indexValue[index+4] = 3 * Nx + count_Grids;
	indexValue[index+5] = 3 * Nx + 2 + count_Grids;



	/* ix2 boundary condition */
	if(ix2 == 1 || ix2 == 5){
					
		Value[NoEr+MPIcount1] += theta[0];
		Value[NoEr+MPIcount1+2] -= theta[1];
			
		Value[NoFr1+MPIcount1]+= phi[0];
		Value[NoFr1+MPIcount1+1]+= phi[1];
				
		Value[NoFr2+MPIcount1]+= psi[0];
		Value[NoFr2+MPIcount1+1]-= psi[1];
	}
	else if(ix2 == 2){
		Value[NoEr+MPIcount1] += theta[0];
		Value[NoEr+MPIcount1+2] += theta[1];
			
		Value[NoFr1+MPIcount1]+= phi[0];
		Value[NoFr1+MPIcount1+1]+= phi[1];
				
		Value[NoFr2+MPIcount1]+= psi[0];
		Value[NoFr2+MPIcount1+1]+= psi[1];
	}
	else if(ix2 == 3){

		/* Do nothing */
	}
	else if(ix2 == 4){
		index = NoEr+MPIcount1+7;
		Value[index] = theta[0];
		Value[index+1] = theta[1];

		indexValue[index] = 3*(je-js)*Nx + count_Grids;
		indexValue[index+1] = 3*(je-js)*Nx + 2 + count_Grids;

		/* NoFr1 */
		index = NoFr1 + MPIcount1 + 6;

		Value[index] = phi[0];
		Value[index+1] = phi[1];
	
		indexValue[index] = 3*(je-js)*Nx + count_Grids;
		indexValue[index+1] = 3*(je-js)*Nx + 1 + count_Grids;

		/* NoFr2 */
		index = NoFr2 + MPIcount1 + 6;

		Value[index] = psi[0];
		Value[index+1] = psi[1];

		indexValue[index] = 3*(je-js)*Nx + count_Grids;
		indexValue[index+1] = 3*(je-js)*Nx + 2 + count_Grids;
		
	}
		
		
	return;
} /* MPI for x direction and MPI for y direction */


void is_js_phy_phy()
{
	int m;

/* For Er */
	for(m=0; m<5; m++)
		Value[NoEr+m] = theta[4+m];
		for(m=0;m<5;m++)
			indexValue[NoEr+m] = m + count_Grids;
				
		if(ix1 != 4){
						
			Value[NoEr+5]	= theta[9];
			Value[NoEr+6]	= theta[10];
			indexValue[NoEr+5] = 3 * Nx + count_Grids;
			indexValue[NoEr+6] = 3 * Nx + 2 + count_Grids;
					
			if(ix2 == 4){
				Value[NoEr+7] = theta[0];
				Value[NoEr+8] = theta[1];
				indexValue[NoEr+7] = 3*(je-js)*Nx + count_Grids;
				indexValue[NoEr+8] = 3*(je-js)*Nx + 2 + count_Grids;
			}
		}
		else {
			Value[NoEr+5]	= theta[2];
			Value[NoEr+6]	= theta[3];
			Value[NoEr+7]	= theta[9];
			Value[NoEr+8]	= theta[10];
			indexValue[NoEr+5] = 3 * (ie - is) + count_Grids;
			indexValue[NoEr+6] = 3 * (ie - is) + 1 + count_Grids;
			indexValue[NoEr+7] = 3 * Nx + count_Grids;
			indexValue[NoEr+8] = 3 * Nx + 2 + count_Grids;
				
			if(ix2 == 4){
				Value[NoEr+9] = theta[0];
				Value[NoEr+10] = theta[1];
				indexValue[NoEr+9] = 3*(je-js)*Nx + count_Grids;
				indexValue[NoEr+10] = 3*(je-js)*Nx + 2 + count_Grids;
			}
		}

				
						
		/* For Fr1 */
	for(m=0; m<4; m++)
		Value[NoFr1+m] = phi[4+m];

		indexValue[NoFr1+0] = 0 + count_Grids;
		indexValue[NoFr1+1] = 1 + count_Grids;
		indexValue[NoFr1+2] = 3 + count_Grids;
		indexValue[NoFr1+3] = 4 + count_Grids;
				
		if(ix1 != 4){
			Value[NoFr1+4]	= phi[8];
			Value[NoFr1+5]	= phi[9];
			indexValue[NoFr1+4] = 3 * Nx + count_Grids;
			indexValue[NoFr1+5] = 3 * Nx + 1 + count_Grids;
			if(ix2 == 4){
				Value[NoFr1+6] = phi[0];
				Value[NoFr1+7] = phi[1];
				indexValue[NoFr1+6] = 3*(je-js)*Nx + count_Grids;
				indexValue[NoFr1+7] = 3*(je-js)*Nx + 1 + count_Grids;
			}
		}
		else{
			Value[NoFr1+4]	= phi[2];
			Value[NoFr1+5]	= phi[3];
			Value[NoFr1+6]	= phi[8];
			Value[NoFr1+7]	= phi[9];
			indexValue[NoFr1+4] = 3 * (ie - is) + count_Grids;
			indexValue[NoFr1+5] = 3 * (ie - is) + 1 + count_Grids;
			indexValue[NoFr1+6] = 3 * Nx + count_Grids;
			indexValue[NoFr1+7] = 3 * Nx + 1 + count_Grids;
			if(ix2 == 4){
				Value[NoFr1+8] = phi[0];
				Value[NoFr1+9] = phi[1];
				indexValue[NoFr1+8] = 3*(je-js)*Nx + count_Grids;
				indexValue[NoFr1+9] = 3*(je-js)*Nx + 1 + count_Grids;
			}
		}				

					

	/* For Fr2 */
			
	for(m=0; m<4; m++)
		Value[NoFr2+m] = psi[4+m];

	indexValue[NoFr2+0] = 0 + count_Grids;
	indexValue[NoFr2+1] = 2 + count_Grids;
	indexValue[NoFr2+2] = 3 + count_Grids;
	indexValue[NoFr2+3] = 5 + count_Grids;
				
	if(ix1 != 4){
		Value[NoFr2+4]	= psi[8];
		Value[NoFr2+5]	= psi[9];
		indexValue[NoFr2+4] = 3 * Nx + count_Grids;
		indexValue[NoFr2+5] = 3 * Nx + 2 + count_Grids;
	
		if(ix2 == 4){
			Value[NoFr2+6] = psi[0];
			Value[NoFr2+7] = psi[1];
			indexValue[NoFr2+6] = 3*(je-js)*Nx + count_Grids;
			indexValue[NoFr2+7] = 3*(je-js)*Nx + 2 + count_Grids;
			}
		}
		else{
			Value[NoFr2+4]	= psi[2];
			Value[NoFr2+5]	= psi[3];
			Value[NoFr2+6]	= psi[8];
			Value[NoFr2+7]	= psi[9];
			indexValue[NoFr2+4] = 3 * (ie - is) + count_Grids;
			indexValue[NoFr2+5] = 3 * (ie - is) + 2 + count_Grids;
			indexValue[NoFr2+6] = 3 * Nx + count_Grids;
			indexValue[NoFr2+7] = 3 * Nx + 2 + count_Grids;
		
			if(ix2 == 4){
				Value[NoFr2+8] = psi[0];
				Value[NoFr2+9] = psi[1];
				indexValue[NoFr2+8] = 3*(je-js)*Nx + count_Grids;
				indexValue[NoFr2+9] = 3*(je-js)*Nx + 2 + count_Grids;
			}
		}


		/* other ix1 boundary condition */
		if(ix1 == 1 || ix1 == 5){
				
			Value[NoEr+0] += theta[2];
			Value[NoEr+1] -= theta[3];
		
			Value[NoFr1+0]+= phi[2];
			Value[NoFr1+1]-= phi[3];
				
			Value[NoFr2+0]+= psi[2];
			Value[NoFr2+1]+= psi[3];
		}
		else if(ix1 == 2){
			Value[NoEr+0] += theta[2];
			Value[NoEr+1] += theta[3];
	
			Value[NoFr1+0]+= phi[2];
			Value[NoFr1+1]+= phi[3];
				
			Value[NoFr2+0]+= psi[2];
			Value[NoFr2+1]+= psi[3];
		}
		else if(ix1 == 3){

			/* Do nothing */
		}
		
				
		/* other ix2 boundary condition */	

		if(ix2 == 1 || ix2 == 5){
					
			Value[NoEr+0] += theta[0];
			Value[NoEr+2] -= theta[1];
			
			Value[NoFr1+0]+= phi[0];
			Value[NoFr1+1]+= phi[1];
				
			Value[NoFr2+0]+= psi[0];
			Value[NoFr2+1]-= psi[1];
		}
		else if(ix2 == 2){
			Value[NoEr+0] += theta[0];
			Value[NoEr+2] += theta[1];
			
			Value[NoFr1+0]+= phi[0];
			Value[NoFr1+1]+= phi[1];
				
			Value[NoFr2+0]+= psi[0];
			Value[NoFr2+1]+= psi[1];
		}
		else if(ix2 == 3){

			/* Do nothing */
		}
		

	return;
} /* physical for x and y direction */




/*-----------------------------------*/
/*------is and je -------------------*/ 
/*-----------------------------------*/

void is_je_MPI_MPI()
{
	int m;
	int shiftx, shifty, index1, index2, index;
	int MPIcount1x, MPIcount1y;
	int MPIcount2x, MPIcount2y;
	int MPIcount2xF, MPIcount2yF;

	if(lx1 > ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount2x = 7;
		MPIcount2xF = 6;
		MPIcount1x = 0;
	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount2x = 0;
		MPIcount1x = 2;
		MPIcount2xF = 0;
	}


	if(rx2 < ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount2y = 0;
		MPIcount1y = 2;
		MPIcount2yF = 0;

	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount2y = 9;
		MPIcount1y = 0;
		MPIcount2yF = 8;
	}


	/* First , the MPI part */
	/* For Er */
	index1 = NoEr + MPIcount2x + MPIcount1y;
	index2 = NoEr + MPIcount2y;

	Value[index1] = theta[2];
	Value[index1+1] = theta[3];

	indexValue[index1] = 3*(je-js) * Nx + 3 * (ie-is) + count_Grids - shiftx;
	indexValue[index1+1] = indexValue[index1] + 1;
	
	Value[index2] = theta[9];
	Value[index2+1] = theta[10];	

	indexValue[index2] = count_Grids + shifty;
	indexValue[index2+1] = indexValue[index2] + 2;

	/* For Fr1 */
	index1 = NoFr1 + MPIcount2xF + MPIcount1y;
	index2 = NoFr1 + MPIcount2yF;

	Value[index1] = phi[2];
	Value[index1+1] = phi[3];

	indexValue[index1] = 3*(je-js) * Nx + 3 * (ie-is) + count_Grids - shiftx;
	indexValue[index1+1] = indexValue[index1] + 1;

	Value[index2] = phi[8];
	Value[index2+1] = phi[9];

	indexValue[index2] = count_Grids + shifty; 
	indexValue[index2+1] = indexValue[index2] + 1;
	
	/* For Fr2 */
	index1 = NoFr2 + MPIcount2xF + MPIcount1y;
	index2 = NoFr2 + MPIcount2yF;

	Value[index1] = psi[2];
	Value[index1+1] = psi[3];

	indexValue[index1] = 3*(je-js) * Nx + 3 * (ie-is) + count_Grids - shiftx;
	indexValue[index1+1] = indexValue[index1] + 2;

	Value[index2] = psi[8];
	Value[index2+1] = psi[9];

	indexValue[index2] = count_Grids + shifty; 
	indexValue[index2+1] = indexValue[index2] + 2;

/* For Er */
	
	index = NoEr + MPIcount1x + MPIcount1y;
 
	Value[index] = theta[0];
	Value[index+1] = theta[1];

	for(m=0; m<5; m++)
		Value[index+2+m] = theta[m+4];

	indexValue[index] = 3*(je-js-1)* Nx + count_Grids;
	indexValue[index+1] = indexValue[index] + 2;

	for(m=0;m<5;m++)
		indexValue[index+m+2] = 3*(je-js)*Nx + m + count_Grids;

	
	/* For Fr1 */
	index = NoFr1 + MPIcount1x + MPIcount1y;

	Value[index] = phi[0];
	Value[index+1] = phi[1];	

	for(m=0; m<4; m++)
		Value[index+2+m] = phi[m+4];


	indexValue[index] = 3*(je-js-1)* Nx + count_Grids;
	indexValue[index+1] = indexValue[index] + 1;

	indexValue[index+2] = 3*(je-js)*Nx + count_Grids;
	indexValue[index+3] = 3*(je-js)*Nx + 1 + count_Grids;
	indexValue[index+4] = 3*(je-js)*Nx + 3 + count_Grids;
	indexValue[index+5] = 3*(je-js)*Nx + 4 + count_Grids;

	
	/* For Fr2 */
	index = NoFr2 + MPIcount1x + MPIcount1y;
	
	Value[index] = psi[0];
	Value[index+1] = psi[1];

	for(m=0; m<4; m++)
		Value[index+2+m] = psi[m+4];

	

	indexValue[index] = 3*(je-js-1)* Nx + count_Grids;
	indexValue[index+1] = indexValue[index] + 2;

	indexValue[index+2] = 3*(je-js)*Nx + count_Grids;
	indexValue[index+3] = 3*(je-js)*Nx + 2 + count_Grids;
	indexValue[index+4] = 3*(je-js)*Nx + 3 + count_Grids;
	indexValue[index+5] = 3*(je-js)*Nx + 5 + count_Grids;

	
		

	return;
} /* MPI boundary for both x and y direction */


void is_je_phy_MPI()
{
	int m, i, j;
	i = is;
	j = je;
	
	int shifty, index;
	int MPIcount2F;


	if(rx2 < ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;
	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount1 = 0;
		if(ix1 == 4){
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else{
			MPIcount2 = 7;
			MPIcount2F = 6;
		}
	}





	/* The following is true no matter ix1 == 4 or not */

	/* For Er */
	index = NoEr + MPIcount1;
	Value[index] = theta[0];
	Value[index+1] = theta[1];
							
	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

	/* For Fr1 */
	index = NoFr1 + MPIcount1;
	Value[index] = phi[0];
	Value[index+1] = phi[1];
							
	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 1 + count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1;				
	Value[index] = psi[0];
	Value[index+1] = psi[1];
							
	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

					
	/* for Er */
	index = NoEr + MPIcount1 +2;
	for(m=0; m<5; m++){
		Value[index+m] = theta[4+m];
		indexValue[index+m] = 3*(j-js)*Nx + 3*(i-is) + m + count_Grids;
	}

	/* For Fr1 */
	index = NoFr1 + MPIcount1 + 2;
	Value[index] = phi[4];
	Value[index+1] = phi[5];
	Value[index+2] = phi[6];
	Value[index+3] = phi[7];

	indexValue[index] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(i-is) + 1 + count_Grids;
	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is) + 4 + count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1 + 2;
	Value[index] = psi[4];
	Value[index+1] = psi[5];
	Value[index+2] = psi[6];
	Value[index+3] = psi[7];

	indexValue[index] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(i-is) + 2 + count_Grids;
	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is) + 5 + count_Grids;	


	/* for MPI part */
	/* The following is true no matter ix1 == 4 or not becuase MPIcount2 will change accordingly */
	/* Er */
	index = NoEr + MPIcount2;
	Value[index] = theta[9];
	Value[index+1] = theta[10];

	indexValue[index] = count_Grids + shifty;
	indexValue[index+1] = indexValue[index] + 2;	

	/* Fr1 */
	index = NoFr1 + MPIcount2F;

	Value[index] = phi[8];
	Value[index+1] = phi[9];		

	indexValue[index] = count_Grids + shifty;
	indexValue[index+1] = indexValue[index] + 1;

	/* Fr2 */
	index = NoFr2 + MPIcount2F;

	Value[index] = psi[8];
	Value[index+1] = psi[9];

	indexValue[index] = count_Grids + shifty;
	indexValue[index+1] = indexValue[index] + 2;

					
	if (ix1 == 4) {

						
	/* For Er */
	index = NoEr + MPIcount1 + 7;
	Value[index] = theta[2];
	Value[index+1] = theta[3];
							
	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(ie-is) + 1 + count_Grids;

	/* For Fr1 */

	index = NoFr1 + MPIcount1 + 6;
	Value[index] = phi[2];
	Value[index+1] = phi[3];
							
	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(ie-is) + 1 + count_Grids;
	

	/* For Fr2 */
	index = NoFr2 + MPIcount1 + 6;				
	Value[index] = psi[2];
	Value[index+1] = psi[3];
							
	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(ie-is) + 2 + count_Grids;

	
	} /* for periodic boundary condition */
	else if(ix1 == 1 || ix1 == 5){
					
		Value[NoEr+2+MPIcount1] += theta[2];
		Value[NoEr+3+MPIcount1] -= theta[3];
			
		Value[NoFr1+2+MPIcount1]+= phi[2];
		Value[NoFr1+3+MPIcount1]-= phi[3];
				
		Value[NoFr2+2+MPIcount1]+= psi[2];
		Value[NoFr2+3+MPIcount1]+= psi[3];
	}
	else if(ix1 == 2){
		Value[NoEr+2+MPIcount1] += theta[2];
		Value[NoEr+3+MPIcount1] += theta[3];
			
		Value[NoFr1+2+MPIcount1]+= phi[2];
		Value[NoFr1+3+MPIcount1]+= phi[3];
				
		Value[NoFr2+2+MPIcount1]+= psi[2];
		Value[NoFr2+3+MPIcount1]+= psi[3];
	}
	else if(ix1 == 3){

		/* Do nothing */
	}
	

	return;
} /* physical for x direction and MPI for y direction */



void is_je_MPI_phy()
{	
	int m, i, j;
	int shiftx, index;
	int MPIcount2F;

	if(lx1 > ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount1 = 0;
		if(ox2 == 4) {
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else {
			MPIcount2 = 7;
			MPIcount2F = 6;
		}
	}
	else{	
		shiftx = 3 * Nx * Ny;
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;

	}


	i = is;
	j = je;

	/* The following is true no matter ox2==4 or not */
	/* For Er */
	index = NoEr + MPIcount2;
	Value[index] = theta[2];
	Value[index+1] = theta[3];

	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* Fr1*/
	index = NoFr1 + MPIcount2F;
	
 
	Value[index] = phi[2];
	Value[index+1] = phi[3];

	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* Fr2 */
	index = NoFr2 + MPIcount2F;
	

	Value[index] = psi[2];
	Value[index+1] = psi[3];

	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index] + 2;
	
	if(ox2 != 4){
	index = NoEr + MPIcount1;	

	Value[index] = theta[0];
	Value[index+1] = theta[1];

	
							
	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

	/* For Fr1 */
	index = NoFr1 + MPIcount1;
	Value[index] = phi[0];
	Value[index+1] = phi[1];
							
	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 1 + count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1;				
	Value[index] = psi[0];
	Value[index+1] = psi[1];
							
	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

					
	/* for Er */
	index = NoEr+MPIcount1 +2;
	for(m=0; m<5; m++){
		Value[index+m] = theta[4+m];
		indexValue[index+m] = 3*(j-js)*Nx + 3*(i-is) + m + count_Grids;
	}

	/* For Fr1 */
	index = NoFr1 + MPIcount1 + 2;
	Value[index] = phi[4];
	Value[index+1] = phi[5];
	Value[index+2] = phi[6];
	Value[index+3] = phi[7];

	indexValue[index] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(i-is) + 1 + count_Grids;
	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is) + 4 + count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1 + 2;
	Value[index] = psi[4];
	Value[index+1] = psi[5];
	Value[index+2] = psi[6];
	Value[index+3] = psi[7];

	indexValue[index] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(i-is) + 2 + count_Grids;
	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is) + 5 + count_Grids;				


	/* other ox2 boundary condition */
	if(ox2 == 1 || ox2 == 5){
					
		Value[NoEr+MPIcount1+2] += theta[9];
		Value[NoEr+MPIcount1+4] -= theta[10];
			
		Value[NoFr1+MPIcount1+2]+= phi[8];
		Value[NoFr1+MPIcount1+3]+= phi[9];
				
		Value[NoFr2+MPIcount1+2]+= psi[8];
		Value[NoFr2+MPIcount1+3]-= psi[9];
	}
	else if(ox2 == 2){
		Value[NoEr+MPIcount1+2] += theta[9];
		Value[NoEr+MPIcount1+4] += theta[10];
			
		Value[NoFr1+MPIcount1+2]+= phi[8];
		Value[NoFr1+MPIcount1+3]+= phi[9];
				
		Value[NoFr2+MPIcount1+2]+= psi[8];
		Value[NoFr2+MPIcount1+3]+= psi[9];
	}
	else if(ox2 == 3){

		/* Do nothing */
	}
	
	}/* Non-periodic for x2 */
	else{

	/* For Er */
	index = NoEr + MPIcount1;
	Value[index] = theta[9];
	Value[index+1] = theta[10];
	Value[index+2] = theta[0];
	Value[index+3] = theta[1];
					
	indexValue[index] = 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(i-is) + 2 + count_Grids;		
	indexValue[index+2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+3] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

	/* For Fr1 */
	index = NoFr1 + MPIcount1;				
	Value[index] = phi[8];
	Value[index+1] = phi[9];
	Value[index+2] = phi[0];
	Value[index+3] = phi[1];
					
	indexValue[index] = 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(i-is) + 1 + count_Grids;		
	indexValue[index+2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+3] = 3*(j-js-1)*Nx + 3*(i-is) + 1 + count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1;				
	Value[index] = psi[8];
	Value[index+1] = psi[9];
	Value[index+2] = psi[0];
	Value[index+3] = psi[1];
					
	indexValue[index] = 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(i-is) + 2 + count_Grids;		
	indexValue[index+2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+3] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

					
	/* for Er */
	index = NoEr + MPIcount1 + 4;
	for(m=0; m<5; m++){
		Value[index+m] = theta[4+m];
		indexValue[index+m] = 3*(j-js)*Nx + 3*(i-is) + m + count_Grids;
	}

	/* For Fr1 */
	index = NoFr1 + MPIcount1 + 4;
	Value[index] = phi[4];
	Value[index+1] = phi[5];
	Value[index+2] = phi[6];
	Value[index+3] = phi[7];

	indexValue[index] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(i-is) + 1 + count_Grids;
	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is) + 4 + count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1 + 4;
	Value[index] = psi[4];
	Value[index+1] = psi[5];
	Value[index+2] = psi[6];
	Value[index+3] = psi[7];

	indexValue[index] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx + 3*(i-is) + 2 + count_Grids;
	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is) + 5 + count_Grids;				
	

	}/* periodic for x2 */


	return;
} /* MPI for x direction and MPI for y direction */


void is_je_phy_phy()
{
	int m, i, j;
	i = is;
	j = je;

	if(ox2 != 4){
	/* The following is true no matter ix1 == 4 or not */

	/* For Er */
	Value[NoEr] = theta[0];
	Value[NoEr+1] = theta[1];
							
	indexValue[NoEr] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoEr+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

	/* For Fr1 */

	Value[NoFr1] = phi[0];
	Value[NoFr1+1] = phi[1];
							
	indexValue[NoFr1] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr1+1] = 3*(j-js-1)*Nx + 3*(i-is) + 1 + count_Grids;

	/* For Fr2 */
					
	Value[NoFr2] = psi[0];
	Value[NoFr2+1] = psi[1];
							
	indexValue[NoFr2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr2+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

					
	/* for Er */
	for(m=0; m<5; m++){
		Value[NoEr+2+m] = theta[4+m];
		indexValue[NoEr+2+m] = 3*(j-js)*Nx + 3*(i-is) + m + count_Grids;
	}

	/* For Fr1 */
	Value[NoFr1+2] = phi[4];
	Value[NoFr1+3] = phi[5];
	Value[NoFr1+4] = phi[6];
	Value[NoFr1+5] = phi[7];

	indexValue[NoFr1+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr1+3] = 3*(j-js)*Nx + 3*(i-is) + 1 + count_Grids;
	indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is) + 4 + count_Grids;

	/* For Fr2 */
	Value[NoFr2+2] = psi[4];
	Value[NoFr2+3] = psi[5];
	Value[NoFr2+4] = psi[6];
	Value[NoFr2+5] = psi[7];

	indexValue[NoFr2+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr2+3] = 3*(j-js)*Nx + 3*(i-is) + 2 + count_Grids;
	indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is) + 5 + count_Grids;				

					
	if (ix1 == 4) {

						
	/* For Er */
	Value[NoEr+7] = theta[2];
	Value[NoEr+8] = theta[3];
							
	indexValue[NoEr+7] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[NoEr+8] = 3*(j-js)*Nx + 3*(ie-is) + 1 + count_Grids;

	/* For Fr1 */

	Value[NoFr1+6] = phi[2];
	Value[NoFr1+7] = phi[3];
							
	indexValue[NoFr1+6] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[NoFr1+7] = 3*(j-js)*Nx + 3*(ie-is) + 1 + count_Grids;

	/* For Fr2 */
					
	Value[NoFr2+6] = psi[2];
	Value[NoFr2+7] = psi[3];
							
	indexValue[NoFr2+6] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[NoFr2+7] = 3*(j-js)*Nx + 3*(ie-is) + 2 + count_Grids;

	} /* for periodic boundary condition */
	else if(ix1 == 1 || ix1 == 5){
					
		Value[NoEr+2] += theta[2];
		Value[NoEr+3] -= theta[3];
			
		Value[NoFr1+2]+= phi[2];
		Value[NoFr1+3]-= phi[3];
				
		Value[NoFr2+2]+= psi[2];
		Value[NoFr2+3]+= psi[3];
	}
	else if(ix1 == 2){
		Value[NoEr+2] += theta[2];
		Value[NoEr+3] += theta[3];
			
		Value[NoFr1+2]+= phi[2];
		Value[NoFr1+3]+= phi[3];
				
		Value[NoFr2+2]+= psi[2];
		Value[NoFr2+3]+= psi[3];
	}
	else if(ix1 == 3){

		/* Do nothing */
	}
	


	/* other ox2 boundary condition */
	if(ox2 == 1 || ox2 == 5){
					
		Value[NoEr+2] += theta[9];
		Value[NoEr+4] -= theta[10];
			
		Value[NoFr1+2]+= phi[8];
		Value[NoFr1+3]+= phi[9];
				
		Value[NoFr2+2]+= psi[8];
		Value[NoFr2+3]-= psi[9];
	}
	else if(ox2 == 2){
		Value[NoEr+2] += theta[9];
		Value[NoEr+4] += theta[10];
			
		Value[NoFr1+2]+= phi[8];
		Value[NoFr1+3]+= phi[9];
				
		Value[NoFr2+2]+= psi[8];
		Value[NoFr2+3]+= psi[9];
	}
	else if(ox2 == 3){

		/* Do nothing */
	}
	
	}/* Non-periodic for x2 */
	else{

	/* The following is true no matter ix1 == 4 or not */
	/* For Er */
	Value[NoEr] = theta[9];
	Value[NoEr+1] = theta[10];
	Value[NoEr+2] = theta[0];
	Value[NoEr+3] = theta[1];
					
	indexValue[NoEr] = 3*(i-is) + count_Grids;
	indexValue[NoEr+1] =3*(i-is) + 2 + count_Grids;		
	indexValue[NoEr+2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoEr+3] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

	/* For Fr1 */
					
	Value[NoFr1] = phi[8];
	Value[NoFr1+1] = phi[9];
	Value[NoFr1+2] = phi[0];
	Value[NoFr1+3] = phi[1];
					
	indexValue[NoFr1] = 3*(i-is) + count_Grids;
	indexValue[NoFr1+1] = 3*(i-is) + 1 + count_Grids;		
	indexValue[NoFr1+2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr1+3] = 3*(j-js-1)*Nx + 3*(i-is) + 1 + count_Grids;

	/* For Fr2 */
					
	Value[NoFr2] = psi[8];
	Value[NoFr2+1] = psi[9];
	Value[NoFr2+2] = psi[0];
	Value[NoFr2+3] = psi[1];
					
	indexValue[NoFr2] = 3*(i-is) + count_Grids;
	indexValue[NoFr2+1] = 3*(i-is) + 2 + count_Grids;		
	indexValue[NoFr2+2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr2+3] = 3*(j-js-1)*Nx + 3*(i-is) + 2 + count_Grids;

					
	/* for Er */
	for(m=0; m<5; m++){
		Value[NoEr+4+m] = theta[4+m];
		indexValue[NoEr+4+m] = 3*(j-js)*Nx + 3*(i-is) + m + count_Grids;
	}

	/* For Fr1 */
	Value[NoFr1+4] = phi[4];
	Value[NoFr1+5] = phi[5];
	Value[NoFr1+6] = phi[6];
	Value[NoFr1+7] = phi[7];

	indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is) + 1 + count_Grids;
	indexValue[NoFr1+6] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[NoFr1+7] = 3*(j-js)*Nx + 3*(i-is) + 4 + count_Grids;

	/* For Fr2 */
	Value[NoFr2+4] = psi[4];
	Value[NoFr2+5] = psi[5];
	Value[NoFr2+6] = psi[6];
	Value[NoFr2+7] = psi[7];

	indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is) + 2 + count_Grids;
	indexValue[NoFr2+6] = 3*(j-js)*Nx + 3*(i-is) + 3 + count_Grids;
	indexValue[NoFr2+7] = 3*(j-js)*Nx + 3*(i-is) + 5 + count_Grids;				

					
	if (ix1 == 4) {

						
	/* For Er */
	Value[NoEr+9] = theta[2];
	Value[NoEr+10] = theta[3];
							
	indexValue[NoEr+9] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[NoEr+10] = 3*(j-js)*Nx + 3*(ie-is) + 1 + count_Grids;

	/* For Fr1 */

	Value[NoFr1+8] = phi[2];
	Value[NoFr1+9] = phi[3];
							
	indexValue[NoFr1+8] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[NoFr1+9] = 3*(j-js)*Nx + 3*(ie-is) + 1 + count_Grids;

	/* For Fr2 */
					
	Value[NoFr2+8] = psi[2];
	Value[NoFr2+9] = psi[3];
							
	indexValue[NoFr2+8] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids;
	indexValue[NoFr2+9] = 3*(j-js)*Nx + 3*(ie-is) + 2 + count_Grids;

	} /* for periodic boundary condition */
	else if(ix1 == 1 || ix1 == 5){
					
		Value[NoEr+4] += theta[2];
		Value[NoEr+5] -= theta[3];
			
		Value[NoFr1+4]+= phi[2];
		Value[NoFr1+5]-= phi[3];
				
		Value[NoFr2+4]+= psi[2];
		Value[NoFr2+5]+= psi[3];
	}
	else if(ix1 == 2){
		Value[NoEr+4] += theta[2];
		Value[NoEr+5] += theta[3];
			
		Value[NoFr1+4]+= phi[2];
		Value[NoFr1+5]+= phi[3];
				
		Value[NoFr2+4]+= psi[2];
		Value[NoFr2+5]+= psi[3];
	}
	else if(ix1 == 3){

		/* Do nothing */
	}
	

	}/* periodic for x2 */

	return;
} /* physical for x and y direction */



/*-----------------------------------*/
/*------is and j -------------------*/ 
/*-----------------------------------*/

void is_j_MPI(int j)
{

	int i;
	i = is;
	int shiftx;
	int MPIcount2F, index;

	if(lx1 > ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount1 = 0;
		MPIcount2 = 9;
		MPIcount2F = 8;
	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;

	}

	/* For MPI part */
	/* Er */
	index = NoEr + MPIcount2;
	
	Value[index] = theta[2];
	Value[index+1] = theta[3];

	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* Fr1 */
	index = NoFr1 + MPIcount2F;
	Value[index] = phi[2];
	Value[index+1] = phi[3];

	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index] + 1;


	/* Fr2 */
	index = NoFr2 + MPIcount2F;
	Value[index] = psi[2];
	Value[index+1] = psi[3];

	indexValue[index] = 3*(j-js)*Nx + 3*(ie-is) + count_Grids - shiftx;
	indexValue[index+1] = indexValue[index] + 2;

	
	/* Er */

	index = NoEr + MPIcount1;
	Value[index] = theta[0];
	Value[index+1] = theta[1];

	Value[index+2] = theta[4];
	Value[index+3] = theta[5];
	Value[index+4] = theta[6];
	Value[index+5] = theta[7];
	Value[index+6] = theta[8];
	
	Value[index+7] = theta[9];
	Value[index+8] = theta[10];

	

	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is)+2 + count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is)+1 + count_Grids;
	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+2 + count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+3 + count_Grids;
	indexValue[index+6] = 3*(j-js)*Nx + 3*(i-is)+4 + count_Grids;

	indexValue[index+7] = 3*(j-js+1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+8] = indexValue[index+7] + 2;


	/* For Fr1 */

	index = NoFr1 + MPIcount1;


	Value[index] = phi[0];
	Value[index+1] = phi[1];

	Value[index+2] = phi[4];
	Value[index+3] = phi[5];
	Value[index+4] = phi[6];
	Value[index+5] = phi[7];

	Value[index+6] = phi[8];
	Value[index+7] = phi[9];

	

	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is)+1 + count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is)+1 + count_Grids;
	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+3 + count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+4 + count_Grids;

	indexValue[index+6] =3*(j-js+1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+7] = indexValue[index+6] + 1;

	/* For Fr2 */
	index = NoFr2 + MPIcount1;
	
	Value[index] = psi[0];
	Value[index+1] = psi[1];

	Value[index+2] = psi[4];
	Value[index+3] = psi[5];
	Value[index+4] = psi[6];
	Value[index+5] = psi[7];

	Value[index+6] = psi[8];
	Value[index+7] = psi[9];


	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is)+2 + count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is)+2 + count_Grids;
	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+3 + count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+5 + count_Grids;

	indexValue[index+6] =3*(j-js+1)*Nx + 3*(i-is) + count_Grids;
	indexValue[index+7] = indexValue[index+6] + 2;


	return;
} /* MPI boundary condition for x */


void is_j_phy(int j)
{
	int i;
	i = is;
	/* For Er */

	Value[NoEr] = theta[0];
	Value[NoEr+1] = theta[1];

	Value[NoEr+2] = theta[4];
	Value[NoEr+3] = theta[5];
	Value[NoEr+4] = theta[6];
	Value[NoEr+5] = theta[7];
	Value[NoEr+6] = theta[8];

	indexValue[NoEr] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoEr+1] = 3*(j-js-1)*Nx + 3*(i-is)+2 + count_Grids;

	indexValue[NoEr+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoEr+3] = 3*(j-js)*Nx + 3*(i-is)+1 + count_Grids;
	indexValue[NoEr+4] = 3*(j-js)*Nx + 3*(i-is)+2 + count_Grids;
	indexValue[NoEr+5] = 3*(j-js)*Nx + 3*(i-is)+3 + count_Grids;
	indexValue[NoEr+6] = 3*(j-js)*Nx + 3*(i-is)+4 + count_Grids;

	/* For Fr1 */			
				
	Value[NoFr1] = phi[0];
	Value[NoFr1+1] = phi[1];

	Value[NoFr1+2] = phi[4];
	Value[NoFr1+3] = phi[5];
	Value[NoFr1+4] = phi[6];
	Value[NoFr1+5] = phi[7];

	indexValue[NoFr1] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr1+1] = 3*(j-js-1)*Nx + 3*(i-is)+1 + count_Grids;

	indexValue[NoFr1+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr1+3] = 3*(j-js)*Nx + 3*(i-is)+1 + count_Grids;
	indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is)+3 + count_Grids;
	indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is)+4 + count_Grids;

	/* For Fr2 */

	Value[NoFr2] = psi[0];
	Value[NoFr2+1] = psi[1];

	Value[NoFr2+2] = psi[4];
	Value[NoFr2+3] = psi[5];
	Value[NoFr2+4] = psi[6];
	Value[NoFr2+5] = psi[7];

	indexValue[NoFr2] = 3*(j-js-1)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr2+1] = 3*(j-js-1)*Nx + 3*(i-is)+2 + count_Grids;

	indexValue[NoFr2+2] = 3*(j-js)*Nx + 3*(i-is) + count_Grids;
	indexValue[NoFr2+3] = 3*(j-js)*Nx + 3*(i-is)+2 + count_Grids;
	indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is)+3 + count_Grids;
	indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is)+5 + count_Grids;


	if(ix1 != 4){
					
	/* For Er */
					
	Value[NoEr+7] = theta[9];
	Value[NoEr+8] = theta[10];

					
					
	indexValue[NoEr+7] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoEr+8] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;


	/* For Fr1 */
					
	Value[NoFr1+6] = phi[8];
	Value[NoFr1+7] = phi[9];					
										
	indexValue[NoFr1+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr1+7] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

	/* For Fr2 */
		
	Value[NoFr2+6] = psi[8];
	Value[NoFr2+7] = psi[9];					
										
	indexValue[NoFr2+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr2+7] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;			


	}/* no periodic boundary condition */
	else {
	/* For Er */
					
	Value[NoEr+7] = theta[2];
	Value[NoEr+8] = theta[3];
	Value[NoEr+9] = theta[9];
	Value[NoEr+10] = theta[10];
					
					
	indexValue[NoEr+7] = 3*(j-js)*Nx + 3*(ie-is)+ count_Grids;
	indexValue[NoEr+8] = 3*(j-js)*Nx + 3*(ie-is)+1+ count_Grids;
	indexValue[NoEr+9] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoEr+10] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;



	/* For Fr1 */
					
	Value[NoFr1+6] = phi[2];
	Value[NoFr1+7] = phi[3];	
	Value[NoFr1+8] = phi[8];
	Value[NoFr1+9] = phi[9];						
										
	indexValue[NoFr1+6] = 3*(j-js)*Nx + 3*(ie-is)+ count_Grids;
	indexValue[NoFr1+7] = 3*(j-js)*Nx + 3*(ie-is)+1+ count_Grids;
	indexValue[NoFr1+8] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr1+9] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

	/* For Fr2 */
		
	Value[NoFr2+6] = psi[2];
	Value[NoFr2+7] = psi[3];
	Value[NoFr2+8] = psi[8];
	Value[NoFr2+9] = psi[9];						
										
	indexValue[NoFr2+6] = 3*(j-js)*Nx + 3*(ie-is)+ count_Grids;
	indexValue[NoFr2+7] = 3*(j-js)*Nx + 3*(ie-is)+2+ count_Grids;
	indexValue[NoFr2+8] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr2+9] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;


	}/* For periodic boundary condition */



				
	/* other ix1 boundary condition */

	if(ix1 == 1 || ix1 == 5){
					
		Value[NoEr+2] += theta[2];
		Value[NoEr+3] -= theta[3];
	
		Value[NoFr1+2]+= phi[2];
		Value[NoFr1+3]-= phi[3];
				
		Value[NoFr2+2]+= psi[2];
		Value[NoFr2+3]+= psi[3];
	}
	else if(ix1 == 2){
		Value[NoEr+2] += theta[2];
		Value[NoEr+3] += theta[3];
			
		Value[NoFr1+2]+= phi[2];
		Value[NoFr1+3]+= phi[3];
				
		Value[NoFr2+2]+= psi[2];
		Value[NoFr2+3]+= psi[3];
	}
	else if(ix1 == 3){
	/* Do nothing */
	}
	
	return;
} /* physical boundary for x */



/*-----------------------------------*/
/*------ie and js -------------------*/ 
/*-----------------------------------*/

void ie_js_MPI_MPI()
{
	int m, i, j;
	i = ie;
	j = js;
	int shiftx, shifty;

	int index1, index2, index;
	int MPIcount1x, MPIcount1y;
	int MPIcount2x, MPIcount2y;
	int MPIcount2xF, MPIcount2yF;

	if(rx1 < ID){
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount2x = 0;
		MPIcount2xF = 0;
		MPIcount1x = 2;
	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount2x = 7;
		MPIcount2xF = 6;
		MPIcount1x = 0;
	}


	if(lx2 > ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount2y = 9;
		MPIcount2yF = 8;
		MPIcount1y = 0;
	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount2y = 0;
		MPIcount1y = 2;
		MPIcount2yF = 0;
	}



	/* For Er */
	index = NoEr + MPIcount1x + MPIcount1y;
	for(m=0; m<5; m++)
		Value[index+m] = theta[2+m];

	Value[index+5] = theta[9];
	Value[index+6] = theta[10];
				
	indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

	for(m=0; m<3; m++)
		indexValue[index+2+m] = 3*(j-js)*Nx+3*(i-is)+m+ count_Grids;

	indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+6] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;

	/* For Fr1 */
	index = NoFr1 + MPIcount1x + MPIcount1y;
	for(m=0; m<4; m++)
		Value[index+m] = phi[2+m];

	Value[index+4] = phi[8];
	Value[index+5] = phi[9];
				
	indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

	for(m=0; m<2; m++)
		indexValue[index+2+m] = 3*(j-js) * Nx + 3*(i-is) + m + count_Grids;

	indexValue[index+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+1+ count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1x + MPIcount1y;
	for(m=0; m<4; m++)
		Value[index+m] = psi[2+m];

	Value[index+4] = psi[8];
	Value[index+5] = psi[9];
				
	indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;
	indexValue[index+2] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;				

	indexValue[index+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;	


	/* MPI part */

	/* Er */
	index1 = NoEr + MPIcount2x  + MPIcount1y;
	index2 = NoEr + MPIcount2y;

	Value[index1] = theta[7];
	Value[index1+1] = theta[8];

	indexValue[index1] =  count_Grids + shiftx;
	indexValue[index1+1] = indexValue[index1] + 1;

	Value[index2] = theta[0];
	Value[index2+1] = theta[1];

	indexValue[index2] = 3*(je-js)*Nx + 3*(ie-is) + count_Grids - shifty;
	indexValue[index2+1] = indexValue[index2] + 2;

	/* Fr1 */
	index1 = NoFr1 + MPIcount2xF  + MPIcount1y;
	index2 = NoFr1 + MPIcount2yF;

	Value[index1] = phi[6];
	Value[index1+1] = phi[7];

	indexValue[index1] = count_Grids + shiftx;
	indexValue[index1+1] = indexValue[index1] + 1;

	Value[index2] = phi[0];
	Value[index2+1] = phi[1];

	indexValue[index2] = 3*(je-js)*Nx + 3*(ie-is) + count_Grids - shifty;
	indexValue[index2+1] = indexValue[index2] + 1;


	/* Fr2 */
	index1 = NoFr2 + MPIcount2xF  + MPIcount1y;
	index2 = NoFr2 + MPIcount2yF;
	
	Value[index1] = psi[6];
	Value[index1+1] = psi[7];

	indexValue[index1] = count_Grids + shiftx;
	indexValue[index1+1] = indexValue[index1] + 2;

	Value[index2] = psi[0];
	Value[index2+1] = psi[1];

	indexValue[index2] = 3*(je-js)*Nx + 3*(ie-is) + count_Grids - shifty;
	indexValue[index2+1] = indexValue[index2] + 1;


	
	return;
} /* MPI boundary for both x and y direction */


void ie_js_phy_MPI()
{
	int m, i, j;
	i = ie;
	j = js;
	int shifty, index;
	int MPIcount2F;


	if(lx2 > ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount1 = 0;
		if(ox1 == 4){
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else{
			MPIcount2 = 7;
			MPIcount2F = 6;
		} 
	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;	
	}

	/* First do the MPI part */
	index = NoEr + MPIcount2;

	Value[index] = theta[0];
	Value[index+1] = theta[1];

	indexValue[index] = 3*(je-js)*Nx + 3*(ie-is) + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 2;

	
	index = NoFr1 + MPIcount2F;
	
	Value[index] = phi[0];
	Value[index+1] = phi[1];

	indexValue[index] = 3*(je-js)*Nx + 3*(ie-is) + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 1;

	index = NoFr2 + MPIcount2F;
	
	Value[index] = psi[0];
	Value[index+1] = psi[1];

	indexValue[index] = 3*(je-js)*Nx + 3*(ie-is) + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 2;

	if(ox1 !=4 ){
	
	/* For Er */
		index = NoEr + MPIcount1;
		for(m=0; m<5; m++)
			Value[index+m] = theta[2+m];

		Value[index+5] = theta[9];
		Value[index+6] = theta[10];
				
		indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		for(m=0; m<3; m++)
			indexValue[index+2+m] = 3*(j-js)*Nx+3*(i-is)+m+ count_Grids;

		indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+6] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;

		/* For Fr1 */
		index = NoFr1 + MPIcount1;
		for(m=0; m<4; m++)
			Value[index+m] = phi[2+m];

		Value[index+4] = phi[8];
		Value[index+5] = phi[9];
				
		indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		for(m=0; m<2; m++)
			indexValue[index+2+m] = 3*(j-js)*Nx+3*(i-is)+m + count_Grids;

		indexValue[index+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+1+ count_Grids;

	/* For Fr2 */
		index = NoFr2 + MPIcount1;
		for(m=0; m<4; m++)
			Value[index+m] = psi[2+m];

		Value[index+4] = psi[8];
		Value[index+5] = psi[9];
				
		indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;
		indexValue[index+2] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+3] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;				

		indexValue[index+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;		


	}/* Non periodic boundary condition */
	else {
	/* For Er */
		index =  NoEr + MPIcount1;
		Value[index] = theta[7];
		Value[index+1] = theta[8];

		for(m=0; m<5; m++)
			Value[index+2+m] = theta[2+m];

		Value[index+7] = theta[9];
		Value[index+8] = theta[10];
				
		indexValue[index] = 3*(j-js)*Nx+ count_Grids;
		indexValue[index+1] = 3*(j-js)*Nx+1+ count_Grids;
		indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;
		
		for(m=0; m<3; m++)
			indexValue[index+4+m] = 3*(j-js)*Nx+3*(i-is)+m + count_Grids;

		indexValue[index+7] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+8] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;

		/* For Fr1 */
		index = NoFr1 + MPIcount1;
		Value[index] = phi[6];
		Value[index+1] = phi[7];

		for(m=0; m<4; m++)
			Value[index+2+m] = phi[2+m];

		Value[index+6] = phi[8];
		Value[index+7] = phi[9];
				
		indexValue[index] = 3*(j-js)*Nx+ count_Grids;
		indexValue[index+1] = 3*(j-js)*Nx+1+ count_Grids;
		indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		for(m=0; m<2; m++)
			indexValue[index+4+m] = 3*(j-js)*Nx+3*(i-is)+m+ count_Grids;

		indexValue[index+6] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+7] = 3*(j-js+1)*Nx+3*(i-is)+1+ count_Grids;

		/* For Fr2 */
		index = NoFr2 + MPIcount1;
		Value[index] = psi[6];
		Value[index+1] = psi[7];

		for(m=0; m<4; m++)
			Value[index+m+2] = psi[2+m];

		Value[index+6] = psi[8];
		Value[index+7] = psi[9];
				
		indexValue[index] = 3*(j-js)*Nx+ count_Grids;
		indexValue[index+1] = 3*(j-js)*Nx+2+ count_Grids;
		indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;
		indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;			

		indexValue[index+6] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+7] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;

				
	}/* Periodic boundary condition */


	/* other ox1 boundary condition */
	if(ox1 == 1 || ox1 == 5){
					
		Value[NoEr+2+MPIcount1] += theta[7];
		Value[NoEr+3+MPIcount1] -= theta[8];
			
		Value[NoFr1+2+MPIcount1]+= phi[6];
		Value[NoFr1+3+MPIcount1]-= phi[7];
			
		Value[NoFr2+2+MPIcount1]+= psi[6];
		Value[NoFr2+3+MPIcount1]+= psi[7];
	}
	else if(ox1 == 2){
		Value[NoEr+2+MPIcount1] += theta[7];
		Value[NoEr+3+MPIcount1] += theta[8];
			
		Value[NoFr1+2+MPIcount1]+= phi[6];
		Value[NoFr1+3+MPIcount1]+= phi[7];
				
		Value[NoFr2+2+MPIcount1]+= psi[6];
		Value[NoFr2+3+MPIcount1]+= psi[7];
	}
	else if(ox1 == 3){

		/* Do nothing */
	}


	return;
} /* physical for x direction and MPI for y direction */



void ie_js_MPI_phy()
{
	int m, i, j;
	i = ie;
	j = js;
	int shiftx, index;
	int MPIcount2F;


	if(rx1 < ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;		

	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount1 = 0;
		if(ix2 == 4){
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else{
			MPIcount2 = 7;
			MPIcount2F = 6;
		}
	}

	/* For MPI part */
	index = NoEr + MPIcount2;
	
			
	Value[index] = theta[7];
	Value[index+1] = theta[8];

	indexValue[index] = count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* For Fr1 */

	index = NoFr1 + MPIcount2F;	
	Value[index] = phi[6];
	Value[index+1] = phi[7];

	indexValue[index] = count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* For Fr2 */
		
	index = NoFr2 + MPIcount2F;		
	Value[index] = psi[6];
	Value[index+1] = psi[7];

	indexValue[index] = count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 2;




	/* For Er */
	index = NoEr + MPIcount1;

	for(m=0; m<5; m++)
		Value[index+m] = theta[2+m];

	indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

	for(m=0; m<3; m++)
		indexValue[index+2+m] = 3*(j-js)*Nx+3*(i-is)+m + count_Grids;


	/* For Fr1 */
	index = NoFr1 + MPIcount1;
	
	for(m=0; m<4; m++)
		Value[index+m] = phi[2+m];

	indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;
		
	for(m=0; m<2; m++)
		indexValue[index+2+m] = 3*(j-js)*Nx+3*(i-is)+m + count_Grids;


	/* For Fr2 */
	index = NoFr2 + MPIcount1;
	for(m=0; m<4; m++)
		Value[index+m] = psi[2+m];

	indexValue[index] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;
		
	
	indexValue[index+2] = 3*(j-js)*Nx+3*(i-is) + count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx+3*(i-is) + 2 + count_Grids;




	/* Er */
	index = NoEr + MPIcount1;

	Value[index+5] = theta[9];
	Value[index+6] = theta[10];
		
					
	indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+6] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;

		
				
	/* Fr1 */
	index = NoFr1 + MPIcount1;

	Value[index+4] = phi[8];
	Value[index+5] = phi[9];

	indexValue[index+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+1+ count_Grids;

	/* Fr2 */
	index = NoFr2 + MPIcount1;

	Value[index+4] = psi[8];
	Value[index+5] = psi[9];

	indexValue[index+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;	
	
	



	/* ix2 boundary condition */
	if(ix2 == 4){
	
		/* Er */
		index = NoEr + MPIcount1;

	
		Value[index+7] = theta[0];
		Value[index+8] = theta[1];
					
		indexValue[index+7] = 3*(je-js) * Nx + 3*(i-is) + count_Grids;
		indexValue[index+8] = indexValue[index+7] + 2;				
				
		/* Fr1 */
		index = NoFr1 + MPIcount1;

		Value[index+6] = phi[0];
		Value[index+7] = phi[1];

		indexValue[index+6] = 3*(je-js) * Nx + 3*(i-is) + count_Grids;
		indexValue[index+7] = indexValue[index+6] + 1;
				
		/* Fr2 */
		index = NoFr2 + MPIcount1;


		Value[index+6] = psi[0];
		Value[index+7] = psi[1];

		indexValue[index+6] = 3*(je-js) * Nx + 3*(i-is) + count_Grids;
		indexValue[index+7] = indexValue[index+6] + 2;		


	}
	else if(ix2 == 1 || ix2 == 5){
					
		Value[NoEr+MPIcount1+2] += theta[0];
		Value[NoEr+MPIcount1+4] -= theta[1];
			
		Value[NoFr1+MPIcount1+2]+= phi[0];
		Value[NoFr1+MPIcount1+3]+= phi[1];
				
		Value[NoFr2+MPIcount1+2]+= psi[0];
		Value[NoFr2+MPIcount1+3]-= psi[1];
	}
	else if(ix2 == 2){
		Value[NoEr+MPIcount1+2] += theta[0];
		Value[NoEr+MPIcount1+4] += theta[1];
		
		Value[NoFr1+MPIcount1+2]+= phi[0];
		Value[NoFr1+MPIcount1+3]+= phi[1];
				
		Value[NoFr2+MPIcount1+2]+= psi[0];
		Value[NoFr2+MPIcount1+3]+= psi[1];
	}
	else if(ix2 == 3){
		/* Do nothing */
	}


	return;
} /* MPI for x direction and MPI for y direction */


void ie_js_phy_phy()
{
	int m, i, j;
	i = ie;
	j = js;

	if(ox1 !=4 ){
	
	/* For Er */
		for(m=0; m<5; m++)
			Value[NoEr+m] = theta[2+m];

		Value[NoEr+5] = theta[9];
		Value[NoEr+6] = theta[10];
				
		indexValue[NoEr] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[NoEr+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		for(m=0; m<3; m++)
			indexValue[NoEr+2+m] = 3*(j-js)*Nx+3*(i-is)+m+ count_Grids;

		indexValue[NoEr+5] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoEr+6] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;

		/* For Fr1 */

		for(m=0; m<4; m++)
			Value[NoFr1+m] = phi[2+m];

		Value[NoFr1+4] = phi[8];
		Value[NoFr1+5] = phi[9];
				
		indexValue[NoFr1] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[NoFr1+1] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		for(m=0; m<2; m++)
			indexValue[NoFr1+2+m] = 3*(j-js)*Nx+3*(i-is)+m;

		indexValue[NoFr1+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr1+5] = 3*(j-js+1)*Nx+3*(i-is)+1+ count_Grids;

	/* For Fr2 */

		for(m=0; m<4; m++)
			Value[NoFr2+m] = psi[2+m];

		Value[NoFr2+4] = psi[8];
		Value[NoFr2+5] = psi[9];
				
		indexValue[NoFr2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[NoFr2+1] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;
		indexValue[NoFr2+2] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr2+3] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;				

		indexValue[NoFr2+4] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr2+5] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;		

	/* ix2 boundary condition */
		if(ix2 == 4){
	
			Value[NoEr+7] = theta[0];
			Value[NoEr+8] = theta[1];
			
			indexValue[NoEr+7] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+8] = 3*(je-js)*Nx+3*(i-is)+2+ count_Grids;
		
			Value[NoFr1+6] = phi[0];
			Value[NoFr1+7] = phi[1];

			indexValue[NoFr1+6] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+7] = 3*(je-js)*Nx+3*(i-is)+1+ count_Grids;
				
			Value[NoFr2+6] = psi[0];
			Value[NoFr2+7] = psi[1];

			indexValue[NoFr2+6] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+7] = 3*(je-js)*Nx+3*(i-is)+2+ count_Grids;			


		}
		else if(ix2 == 1 || ix2 == 5){
			
			Value[NoEr+2] += theta[0];
			Value[NoEr+4] -= theta[1];
			
			Value[NoFr1+2]+= phi[0];
			Value[NoFr1+3]+= phi[1];
				
			Value[NoFr2+2]+= psi[0];
			Value[NoFr2+3]-= psi[1];
		}
		else if(ix2 == 2){
			Value[NoEr+2] += theta[0];
			Value[NoEr+4] += theta[1];
			
			Value[NoFr1+2]+= phi[0];
			Value[NoFr1+3]+= phi[1];
				
			Value[NoFr2+2]+= psi[0];
			Value[NoFr2+3]+= psi[1];
		}
		else if(ix2 == 3){

			/* Do nothing */
		}
					

	}/* Non periodic boundary condition */
	else {
	/* For Er */
		Value[NoEr] = theta[7];
		Value[NoEr+1] = theta[8];

		for(m=0; m<5; m++)
			Value[NoEr+m+2] = theta[2+m];

		Value[NoEr+7] = theta[9];
		Value[NoEr+8] = theta[10];
				
		indexValue[NoEr] = 3*(j-js)*Nx+ count_Grids;
		indexValue[NoEr+1] = 3*(j-js)*Nx+1+ count_Grids;
		indexValue[NoEr+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[NoEr+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;
		
		for(m=0; m<3; m++)
			indexValue[NoEr+4+m] = 3*(j-js)*Nx+3*(i-is)+m;

		indexValue[NoEr+7] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoEr+8] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;

		/* For Fr1 */
		Value[NoFr1] = phi[6];
		Value[NoFr1+1] = phi[7];

		for(m=0; m<4; m++)
			Value[NoFr1+m+2] = phi[2+m];

		Value[NoFr1+6] = phi[8];
		Value[NoFr1+7] = phi[9];
				
		indexValue[NoFr1] = 3*(j-js)*Nx+ count_Grids;
		indexValue[NoFr1+1] = 3*(j-js)*Nx+1+ count_Grids;
		indexValue[NoFr1+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[NoFr1+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		for(m=0; m<2; m++)
			indexValue[NoFr1+4+m] = 3*(j-js)*Nx+3*(i-is)+m+ count_Grids;

		indexValue[NoFr1+6] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr1+7] = 3*(j-js+1)*Nx+3*(i-is)+1+ count_Grids;

		/* For Fr2 */

		Value[NoFr2] = psi[6];
		Value[NoFr2+1] = psi[7];

		for(m=0; m<4; m++)
			Value[NoFr2+m+2] = psi[2+m];

		Value[NoFr2+6] = psi[8];
		Value[NoFr2+7] = psi[9];
				
		indexValue[NoFr2] = 3*(j-js)*Nx+ count_Grids;
		indexValue[NoFr2+1] = 3*(j-js)*Nx+2+ count_Grids;
		indexValue[NoFr2+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[NoFr2+3] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;
		indexValue[NoFr2+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr2+5] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;			

		indexValue[NoFr2+6] = 3*(j-js+1)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr2+7] = 3*(j-js+1)*Nx+3*(i-is)+2+ count_Grids;


		/* ix2 boundary condition */
		if(ix2 == 4){
	
			Value[NoEr+9] = theta[0];
			Value[NoEr+10] = theta[1];
					
			indexValue[NoEr+9] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+10] = 3*(je-js)*Nx+3*(i-is)+2+ count_Grids;
			
			Value[NoFr1+8] = phi[0];
			Value[NoFr1+9] = phi[1];

			indexValue[NoFr1+8] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+9] = 3*(je-js)*Nx+3*(i-is)+1+ count_Grids;
				
			Value[NoFr2+8] = psi[0];
			Value[NoFr2+9] = psi[1];

			indexValue[NoFr2+8] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+9] = 3*(je-js)*Nx+3*(i-is)+2+ count_Grids;				


		}
		else if(ix2 == 1 || ix2 == 5){
					
			Value[NoEr+4] += theta[0];
			Value[NoEr+6] -= theta[1];
			
			Value[NoFr1+4]+= phi[0];
			Value[NoFr1+5]+= phi[1];
				
			Value[NoFr2+4]+= psi[0];
			Value[NoFr2+5]-= psi[1];
		}
		else if(ix2 == 2){
			Value[NoEr+4] += theta[0];
			Value[NoEr+6] += theta[1];
		
			Value[NoFr1+4]+= phi[0];
			Value[NoFr1+5]+= phi[1];
				
			Value[NoFr2+4]+= psi[0];
			Value[NoFr2+5]+= psi[1];
		}
		else if(ix2 == 3){
			/* Do nothing */
		}
					
	}/* Periodic boundary condition */


	/* other ox1 boundary condition */
	if(ox1 == 1 || ox1 == 5){
					
		Value[NoEr+2] += theta[7];
		Value[NoEr+3] -= theta[8];
			
		Value[NoFr1+2]+= phi[6];
		Value[NoFr1+3]-= phi[7];
			
		Value[NoFr2+2]+= psi[6];
		Value[NoFr2+3]+= psi[7];
	}
	else if(ox1 == 2){
		Value[NoEr+2] += theta[7];
		Value[NoEr+3] += theta[8];
			
		Value[NoFr1+2]+= phi[6];
		Value[NoFr1+3]+= phi[7];
				
		Value[NoFr2+2]+= psi[6];
		Value[NoFr2+3]+= psi[7];
	}
	else if(ox1 == 3){

		/* Do nothing */
	}
		
	return;
} /* physical for x and y direction */



/*-----------------------------------*/
/*------ie and je -------------------*/ 
/*-----------------------------------*/

void ie_je_MPI_MPI()
{
	int m, i, j;
	i = ie;
	j = je;
	int shiftx, shifty;
	int index1, index2, index;
	int MPIcount1x, MPIcount1y;
	int MPIcount2x, MPIcount2y;
	int MPIcount2xF, MPIcount2yF;


	if(rx1 < ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount2x = 0;
		MPIcount2xF = 0;
		MPIcount1x = 2;
	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount2x = 7;
		MPIcount2xF = 6;
		MPIcount1x = 0;
	}


	if(rx2 < ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount2y = 0;
		MPIcount2yF = 0;
		MPIcount1y = 2;
	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount2y = 9;
		MPIcount2yF = 8;
		MPIcount1y = 0;
	}

	/* For MPI part */
	/* Er */
	index1 = NoEr + MPIcount2x + MPIcount1y;
	index2 = NoEr + MPIcount2y;

	Value[index1] = theta[7];
	Value[index1+1] = theta[8];

	indexValue[index1] = 3*(je-js)*Nx + count_Grids + shiftx;
	indexValue[index1+1] = indexValue[index1] + 1;

	Value[index2] = theta[9];
	Value[index2+1] = theta[10];		

	indexValue[index2] = 3*(ie-is) + count_Grids + shifty;
	indexValue[index2+1] = indexValue[index2] + 2;

	/* Fr1 */
	index1 = NoFr1 + MPIcount2xF + MPIcount1y;
	index2 = NoFr1 + MPIcount2yF;	

	Value[index1] = phi[6];
	Value[index1+1] = phi[7];

	indexValue[index1] = 3*(je-js)*Nx + count_Grids + shiftx;
	indexValue[index1+1] = indexValue[index1] + 1;		

	Value[index2] = phi[8];
	Value[index2+1] = phi[9];
	
	indexValue[index2] = 3*(ie-is) + count_Grids + shifty;
	indexValue[index2+1] = indexValue[index2] + 1;

	/* Fr2 */
	index1 = NoFr2 + MPIcount2xF + MPIcount1y;
	index2 = NoFr2 + MPIcount2yF;	
			
	Value[index1] = psi[6];
	Value[index1+1] = psi[7];

	indexValue[index1] = 3*(je-js)*Nx + count_Grids + shiftx;
	indexValue[index1+1] = indexValue[index1] + 2;		


	Value[index2] = psi[8];
	Value[index2+1] = psi[9];
	
	indexValue[index2] = 3*(ie-is) + count_Grids + shifty;
	indexValue[index2+1] = indexValue[index2] + 2;							
		
		
	/* For Er */
	index = NoEr + MPIcount1x + MPIcount1y;
	for(m=0; m<7; m++)
		Value[index+m] = theta[m];
				
	indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
	indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

	

	/* For Fr1 */
	index = NoFr1 + MPIcount1x + MPIcount1y;
	for(m=0; m<6; m++)
		Value[index+m] = phi[m];
				
	indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;

				
	/* For Fr2 */
	index = NoFr2 + MPIcount1x + MPIcount1y;

	for(m=0; m<6; m++)
		Value[index+m] = psi[m];
				
	indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;



	return;

} /* MPI boundary for both x and y direction */


void ie_je_phy_MPI()
{
	int m, i, j;
	i = ie;
	j = je;
	int shifty, index;
	int MPIcount2F;

	if(rx2 < ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;
		
	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount1 = 0;
		if(ox1 == 4){
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else{ 
			MPIcount2 = 7;
			MPIcount2F = 6;
		} 

	}

	/* First do the MPI part, which is true no matter ox1 == 4 or not */

		index = NoEr + MPIcount2;
		Value[index] = theta[9];
		Value[index+1] = theta[10];

		indexValue[index] = 3*(ie-is) + count_Grids + shifty;
		indexValue[index+1] = indexValue[index] + 2;

	
		index = NoFr1 + MPIcount2F;
		
		Value[index] = phi[8];
		Value[index+1] = phi[9];

		indexValue[index] = 3*(ie-is) + count_Grids + shifty;
		indexValue[index+1] = indexValue[index] + 1;	

	
		index = NoFr2 + MPIcount2F;
		
		Value[index] = psi[8];
		Value[index+1] = psi[9];

		indexValue[index] = 3*(ie-is) + count_Grids + shifty;
		indexValue[index+1] = indexValue[index] + 2;			
						

		if(ox1 != 4){					
			/* For Er */
			index = NoEr + MPIcount1;
			for(m=0; m<7; m++)
				Value[index+m] = theta[m];
				
			indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
			indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

				

			/* For Fr1 */
			index = NoFr1 + MPIcount1;
			for(m=0; m<6; m++)
				Value[index+m] = phi[m];
				
			indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

			indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;

			
						
			/* For Fr2 */
			index = NoFr2 + MPIcount1;
			for(m=0; m<6; m++)
				Value[index+m] = psi[m];
				
			indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

			indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

									


			/* other ox1 boundary condition */
			if(ox1 == 1 || ox1 == 5){
					
				Value[NoEr+4+MPIcount1] += theta[7];
				Value[NoEr+5+MPIcount1] -= theta[8];
			
				Value[NoFr1+4+MPIcount1]+= phi[6];
				Value[NoFr1+5+MPIcount1]-= phi[7];
		
				Value[NoFr2+4+MPIcount1]+= psi[6];
				Value[NoFr2+5+MPIcount1]+= psi[7];
			}
			else if(ox1 == 2){
				Value[NoEr+4+MPIcount1] += theta[7];
				Value[NoEr+5+MPIcount1] += theta[8];
			
				Value[NoFr1+4+MPIcount1]+= phi[6];
				Value[NoFr1+5+MPIcount1]+= phi[7];
				
				Value[NoFr2+4+MPIcount1]+= psi[6];
				Value[NoFr2+5+MPIcount1]+= psi[7];
			}
			else if(ox1 == 3){

				/* Do nothing */
			}
			
		
		}/* non periodic for x1 */
		else{
			/* for Er */
			index = NoEr + MPIcount1;
			Value[index] = theta[0];
			Value[index+1] = theta[1];

			Value[index+2] = theta[7];
			Value[index+3] = theta[8];

			for(m=0; m<5; m++)
				Value[index+4+m] = theta[2+m];
			
			indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[index+2] = 3*(j-js)*Nx+ count_Grids;
			indexValue[index+3] = 3*(j-js)*Nx+1+ count_Grids;

			indexValue[index+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[index+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
			indexValue[index+8] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

						
				

			/* For Fr1 */
			index = NoFr1 + MPIcount1;
			Value[index] = phi[0];
			Value[index+1] = phi[1];

			Value[index+2] = phi[6];
			Value[index+3] = phi[7];

			for(m=0; m<4; m++)
				Value[index+4+m] = phi[2+m];
				
			indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

			indexValue[index+2] = 3*(j-js)*Nx+ count_Grids;
			indexValue[index+3] = 3*(j-js)*Nx+1+ count_Grids;

			indexValue[index+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[index+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;

					
						
						
			/* For Fr2 */
			index = NoFr2 + MPIcount1;
			Value[index] = psi[0];
			Value[index+1] = psi[1];

			Value[index+2] = psi[6];
			Value[index+3] = psi[7];

			for(m=0; m<4; m++)
				Value[index+4+m] = psi[2+m];
				
			indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[index+2] = 3*(j-js)*Nx+ count_Grids;
			indexValue[index+3] = 3*(j-js)*Nx+2+ count_Grids;

			indexValue[index+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[index+5] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

			indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[index+7] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

			
	

		}/* periodic for x1 */				


	return;
} /* physical for x direction and MPI for y direction */



void ie_je_MPI_phy()
{
	int m, i, j;
	i = ie;
	j = je;
	int shiftx, index;
	int MPIcount2F;
	

	if(rx1 < ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;
	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount1 = 0;
		if(ox2 == 4){
			MPIcount2 = 9;
			MPIcount2F = 8;
		}
		else{
			MPIcount2 = 7;
			MPIcount2F = 6;
		}

	}
	
	/* First, MPI part */
	/* Er */
	index = NoEr + MPIcount2;
	Value[index] = theta[7];
	Value[index+1] = theta[8];

	indexValue[index] = 3*(je-js)*Nx + count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 1;
		
	/* Fr1 */
	index = NoFr1 + MPIcount2F;
	Value[index] = phi[6];
	Value[index+1] = phi[7];

	indexValue[index] = 3*(je-js)*Nx + count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 1;			

	/* Fr2 */
	index = NoFr2 + MPIcount2F;
	Value[index] = psi[6];
	Value[index+1] = psi[7];

	indexValue[index] = 3*(je-js)*Nx + count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 2;		

	if(ox2 != 4){
					
	/* For Er */
		index = NoEr + MPIcount1;
		for(m=0; m<7; m++)
			Value[index+m] = theta[m];
				
		indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

		indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
		indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

			

		/* For Fr1 */
		index = NoFr1 + MPIcount1;
		for(m=0; m<6; m++)
			Value[index+m] = phi[m];
				
		indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

		indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;

		
						
		/* For Fr2 */
		index = NoFr2 + MPIcount1;
		for(m=0; m<6; m++)
			Value[index+m] = psi[m];
				
		indexValue[index] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

		indexValue[index+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+3] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

		indexValue[index+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+5] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

		
		
			
		/* other x2 boundary condition */
		if(ox2 == 1 || ox2 == 5){
				
			Value[NoEr+MPIcount1+4] += theta[9];
			Value[NoEr+MPIcount1+6] -= theta[10];
		
			Value[NoFr1+MPIcount1+4]+= phi[8];
			Value[NoFr1+MPIcount1+5]+= phi[9];
				
			Value[NoFr2+MPIcount1+4]+= psi[8];
			Value[NoFr2+MPIcount1+5]-= psi[9];
		}
		else if(ox2 == 2){
			Value[NoEr+MPIcount1+4] += theta[9];
			Value[NoEr+MPIcount1+6] += theta[10];
			
			Value[NoFr1+MPIcount1+4]+= phi[8];
			Value[NoFr1+MPIcount1+5]+= phi[9];
				
			Value[NoFr2+MPIcount1+4]+= psi[8];
			Value[NoFr2+MPIcount1+5]+= psi[9];
		}
		else if(ox2 == 3){

			/* Do nothing */
		}	
		

	}/* Non-periodic for x2 */
	else{
				
		/* For Er */
		index = NoEr + MPIcount1;
		Value[index] = theta[9];
		Value[index+1] = theta[10];

		for(m=0; m<7; m++)
			Value[index+m+2] = theta[m];

		indexValue[index] = 3*(i-is)+ count_Grids;
		indexValue[index+1] = 3*(i-is)+2+ count_Grids;
			
		indexValue[index+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+3] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

		indexValue[index+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
		indexValue[index+8] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;		


		/* For Fr1 */
		index = NoFr1 + MPIcount1;
		Value[index] = phi[8];
		Value[index+1] = phi[9];

		for(m=0; m<6; m++)
			Value[index+m+2] = phi[m];

		indexValue[index] = 3*(i-is)+ count_Grids;
		indexValue[index+1] = 3*(i-is)+1+ count_Grids;
				
		indexValue[index+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+3] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

		indexValue[index+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

		indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;

	
		/* For Fr2 */
		index = NoFr2 + MPIcount1;
		Value[index] = psi[8];
		Value[index+1] = psi[9];

		for(m=0; m<6; m++)
			Value[index+m+2] = psi[m];

		indexValue[index] = 3*(i-is)+ count_Grids;
		indexValue[index+1] = 3*(i-is)+2+ count_Grids;
			
		indexValue[index+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+3] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

		indexValue[index+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
		indexValue[index+5] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

		indexValue[index+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[index+7] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;


	}/* periodic for x2 */

	return;
} /* MPI for x direction and physics for y direction */


void ie_je_phy_phy()
{
	int m, i, j;
	i = ie;
	j = je;

	if(ox2 != 4){
		if(ox1 != 4){					
			/* For Er */
			for(m=0; m<7; m++)
				Value[NoEr+m] = theta[m];
				
			indexValue[NoEr] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoEr+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoEr+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoEr+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
			indexValue[NoEr+6] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;			

			/* For Fr1 */

			for(m=0; m<6; m++)
			Value[NoFr1+m] = phi[m];
				
			indexValue[NoFr1] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+1] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

			indexValue[NoFr1+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr1+3] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoFr1+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+5] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
						
			/* For Fr2 */

			for(m=0; m<6; m++)
				Value[NoFr2+m] = psi[m];
				
			indexValue[NoFr2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoFr2+2] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr2+3] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

			indexValue[NoFr2+4] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+5] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;


			/* other ox1 boundary condition */
			if(ox1 == 1 || ox1 == 5){
					
				Value[NoEr+4] += theta[7];
				Value[NoEr+5] -= theta[8];
			
				Value[NoFr1+4]+= phi[6];
				Value[NoFr1+5]-= phi[7];
		
				Value[NoFr2+4]+= psi[6];
				Value[NoFr2+5]+= psi[7];
			}
			else if(ox1 == 2){
				Value[NoEr+4] += theta[7];
				Value[NoEr+5] += theta[8];
			
				Value[NoFr1+4]+= phi[6];
				Value[NoFr1+5]+= phi[7];
				
				Value[NoFr2+4]+= psi[6];
				Value[NoFr2+5]+= psi[7];
			}
			else if(ox1 == 3){

				/* Do nothing */
			}
			
			/* other x2 boundary condition */
			if(ox2 == 1 || ox2 == 5){
				
				Value[NoEr+4] += theta[9];
				Value[NoEr+6] -= theta[10];
			
				Value[NoFr1+4]+= phi[8];
				Value[NoFr1+5]+= phi[9];
				
				Value[NoFr2+4]+= psi[8];
				Value[NoFr2+5]-= psi[9];
			}
			else if(ox2 == 2){
				Value[NoEr+4] += theta[9];
				Value[NoEr+6] += theta[10];
			
				Value[NoFr1+4]+= phi[8];
				Value[NoFr1+5]+= phi[9];
				
				Value[NoFr2+4]+= psi[8];
				Value[NoFr2+5]+= psi[9];
			}
			else if(ox2 == 3){

				/* Do nothing */
			}
			
		}/* non periodic for x1 */
		else{
			/* for Er */
			Value[NoEr] = theta[0];
			Value[NoEr+1] = theta[1];

			Value[NoEr+2] = theta[7];
			Value[NoEr+3] = theta[8];

			for(m=0; m<5; m++)
				Value[NoEr+4+m] = theta[2+m];
			
			indexValue[NoEr] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoEr+2] = 3*(j-js)*Nx+ count_Grids;
			indexValue[NoEr+3] = 3*(j-js)*Nx+1+ count_Grids;

			indexValue[NoEr+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoEr+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoEr+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
			indexValue[NoEr+8] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;				

			/* For Fr1 */

			Value[NoFr1] = phi[0];
			Value[NoFr1+1] = phi[1];

			Value[NoFr1+2] = phi[6];
			Value[NoFr1+3] = phi[7];

			for(m=0; m<4; m++)
				Value[NoFr1+4+m] = phi[2+m];
				
			indexValue[NoFr1] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+1] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

			indexValue[NoFr1+2] = 3*(j-js)*Nx+ count_Grids;
			indexValue[NoFr1+3] = 3*(j-js)*Nx+1+ count_Grids;

			indexValue[NoFr1+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr1+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoFr1+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
						
						
			/* For Fr2 */
			Value[NoFr2] = psi[0];
			Value[NoFr2+1] = psi[1];

			Value[NoFr2+2] = psi[6];
			Value[NoFr2+3] = psi[7];

			for(m=0; m<4; m++)
				Value[NoFr2+4+m] = psi[2+m];
				
			indexValue[NoFr2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+1] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoFr2+2] = 3*(j-js)*Nx+ count_Grids;
			indexValue[NoFr2+3] = 3*(j-js)*Nx+2+ count_Grids;

			indexValue[NoFr2+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr2+5] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

			indexValue[NoFr2+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+7] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;

			/* other x2 boundary condition */
			if(ox2 == 1 || ox2 == 5){
					
				Value[NoEr+6] += theta[9];
				Value[NoEr+8] -= theta[10];
			
				Value[NoFr1+6]+= phi[8];
				Value[NoFr1+7]+= phi[9];
				
				Value[NoFr2+6]+= psi[8];
				Value[NoFr2+7]-= psi[9];
			}
			else if(ox2 == 2){
				Value[NoEr+6] += theta[9];
				Value[NoEr+8] += theta[10];
			
				Value[NoFr1+6]+= phi[8];
				Value[NoFr1+7]+= phi[9];
				
				Value[NoFr2+6]+= psi[8];
				Value[NoFr2+7]+= psi[9];
			}
			else if(ox2 == 3){

				/* Do nothing */
			}

		}/* periodic for x1 */				

		}/* Non-periodic for x2 */
	else{
		if(ox1 != 4){					
			/* For Er */
			Value[NoEr] = theta[9];
			Value[NoEr+1] = theta[10];

			for(m=0; m<7; m++)
				Value[NoEr+m+2] = theta[m];

			indexValue[NoEr] = 3*(i-is)+ count_Grids;
			indexValue[NoEr+1] = 3*(i-is)+2+ count_Grids;
				
			indexValue[NoEr+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+3] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoEr+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoEr+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoEr+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
			indexValue[NoEr+8] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;			

			/* For Fr1 */

			Value[NoFr1] = phi[8];
			Value[NoFr1+1] = phi[9];

			for(m=0; m<6; m++)
				Value[NoFr1+m+2] = phi[m];

			indexValue[NoFr1] = 3*(i-is)+ count_Grids;
			indexValue[NoFr1+1] = 3*(i-is)+1+ count_Grids;
				
			indexValue[NoFr1+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+3] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

			indexValue[NoFr1+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr1+5] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoFr1+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+7] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
						
			/* For Fr2 */
	
			Value[NoFr2] = psi[8];
			Value[NoFr2+1] = psi[9];

			for(m=0; m<6; m++)
				Value[NoFr2+m+2] = psi[m];

			indexValue[NoFr2] = 3*(i-is)+ count_Grids;
			indexValue[NoFr2+1] = 3*(i-is)+2+ count_Grids;
			
			indexValue[NoFr2+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+3] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoFr2+4] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr2+5] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

			indexValue[NoFr2+6] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+7] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;


			/* other ox1 boundary condition */
			if(ox1 == 1 || ox1 == 5){
			
				Value[NoEr+6] += theta[7];
				Value[NoEr+7] -= theta[8];
			
				Value[NoFr1+6]+= phi[6];
				Value[NoFr1+7]-= phi[7];
			
				Value[NoFr2+6]+= psi[6];
				Value[NoFr2+7]+= psi[7];
			}
			else if(ox1 == 2){
				Value[NoEr+6] += theta[7];
				Value[NoEr+7] += theta[8];
			
				Value[NoFr1+6]+= phi[6];
				Value[NoFr1+7]+= phi[7];
				
				Value[NoFr2+6]+= psi[6];
				Value[NoFr2+7]+= psi[7];
			}
			else if(ox1 == 3){

				/* Do nothing */
			}
		


		}/* non periodic for x1 */
		else{
			/* for Er */
			Value[NoEr] = theta[9];
			Value[NoEr+1] = theta[10];

			Value[NoEr+2] = theta[0];
			Value[NoEr+3] = theta[1];

			Value[NoEr+4] = theta[7];
			Value[NoEr+5] = theta[8];

			for(m=0; m<5; m++)
				Value[NoEr+6+m] = theta[2+m];

			indexValue[NoEr] = 3*(i-is)+ count_Grids;
			indexValue[NoEr+1] = 3*(i-is)+2+ count_Grids;
			
			indexValue[NoEr+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+3] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoEr+4] = 3*(j-js)*Nx+ count_Grids;
			indexValue[NoEr+5] = 3*(j-js)*Nx+1+ count_Grids;

			indexValue[NoEr+6] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoEr+7] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoEr+8] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoEr+9] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
			indexValue[NoEr+10] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;				

			/* For Fr1 */
			Value[NoFr1] = phi[8];
			Value[NoFr1+1] = phi[9];

			Value[NoFr1+2] = phi[0];
			Value[NoFr1+3] = phi[1];

			Value[NoFr1+4] = phi[6];
			Value[NoFr1+5] = phi[7];

			for(m=0; m<4; m++)
				Value[NoFr1+6+m] = phi[2+m];

			indexValue[NoFr1] = 3*(i-is)+ count_Grids;
			indexValue[NoFr1+1] = 3*(i-is)+1+ count_Grids;
				
			indexValue[NoFr1+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+3] = 3*(j-js-1)*Nx+3*(i-is)+1+ count_Grids;

			indexValue[NoFr1+4] = 3*(j-js)*Nx+ count_Grids;
			indexValue[NoFr1+5] = 3*(j-js)*Nx+1+ count_Grids;

			indexValue[NoFr1+6] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr1+7] = 3*(j-js)*Nx+3*(i-is-1)+1+ count_Grids;

			indexValue[NoFr1+8] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr1+9] = 3*(j-js)*Nx+3*(i-is)+1+ count_Grids;
						
						
			/* For Fr2 */
			Value[NoFr2] = psi[8];
			Value[NoFr2+1] = psi[9];

			Value[NoFr2+2] = psi[0];
			Value[NoFr2+3] = psi[1];

			Value[NoFr2+4] = psi[6];
			Value[NoFr2+5] = psi[7];

			for(m=0; m<4; m++)
				Value[NoFr2+6+m] = psi[2+m];

			indexValue[NoFr2] = 3*(i-is)+ count_Grids;
			indexValue[NoFr2+1] = 3*(i-is)+2+ count_Grids;
				
			indexValue[NoFr2+2] = 3*(j-js-1)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+3] = 3*(j-js-1)*Nx+3*(i-is)+2+ count_Grids;

			indexValue[NoFr2+4] = 3*(j-js)*Nx+ count_Grids;
			indexValue[NoFr2+5] = 3*(j-js)*Nx+2+ count_Grids;

			indexValue[NoFr2+6] = 3*(j-js)*Nx+3*(i-is-1)+ count_Grids;
			indexValue[NoFr2+7] = 3*(j-js)*Nx+3*(i-is-1)+2+ count_Grids;

			indexValue[NoFr2+8] = 3*(j-js)*Nx+3*(i-is)+ count_Grids;
			indexValue[NoFr2+9] = 3*(j-js)*Nx+3*(i-is)+2+ count_Grids;			

		}/* periodic for x1 */	
	}/* periodic for x2 */

	return;
} /* physical for x and y direction */



/*-----------------------------------*/
/*------ie and j -------------------*/ 
/*-----------------------------------*/

void ie_j_MPI(int j)
{
	int m, i;
	i = ie;
	int shiftx, index;
	int MPIcount2F;


	if(rx1 < ID){	
		shiftx = -3 * Ny * Nx * (NGx - 1);
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;
	}
	else{
		shiftx = 3 * Nx * Ny;
		MPIcount1 = 0;
		MPIcount2 = 9;
		MPIcount2F = 8;
	}

	/* For MPI part */
	/* Er */
	index = NoEr + MPIcount2;
	Value[index] = theta[7];
	Value[index+1] = theta[8];

	indexValue[index] = 3*(j-js)*Nx + count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* Fr1 */
	index = NoFr1 + MPIcount2F;
	Value[index] = phi[6];
	Value[index+1] = phi[7];

	indexValue[index] = 3*(j-js)*Nx + count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 1;

	/* Fr2 */
	index = NoFr2 + MPIcount2F;
	Value[index] = psi[6];
	Value[index+1] = psi[7];

	indexValue[index] = 3*(j-js)*Nx + count_Grids + shiftx;
	indexValue[index+1] = indexValue[index] + 2;	

	
	/* For Er */
	index = NoEr + MPIcount1;
	Value[index] = theta[0];
	Value[index+1] =theta[1];

	for(m=0; m<5; m++)
		Value[index+2+m] = theta[2+m];

	Value[index+7] = theta[9];
	Value[index+8] =theta[10];

	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
	indexValue[index+6] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
	indexValue[index+7] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+8] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

		
	/* For Fr1 */
	index = NoFr1 + MPIcount1;
	Value[index] = phi[0];
	Value[index+1] =phi[1];

	for(m=0; m<4; m++)
		Value[index+2+m] = phi[2+m];

	Value[index+6] = phi[8];
	Value[index+7] = phi[9];

	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is)+1+ count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
					
	indexValue[index+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+7] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

	/* For Fr2 */
	index = NoFr2 + MPIcount1;
	Value[index] = psi[0];
	Value[index+1] =psi[1];

	for(m=0; m<4; m++)
		Value[index+2+m] = psi[2+m];

	Value[index+6] = psi[8];
	Value[index+7] = psi[9];

	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is-1)+2+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
	indexValue[index+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+7] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;


	return;
} /* MPI boundary condition for x */


void ie_j_phy(int j)
{
	int m, i;
	i = ie;
	if(ox1 == 4){

		/* For Er */
		Value[NoEr] = theta[0];
		Value[NoEr+1] =theta[1];

		Value[NoEr+2] = theta[7];
		Value[NoEr+3] = theta[8];

		for(m=0; m<5; m++)
			Value[NoEr+4+m] = theta[2+m];

		Value[NoEr+9] = theta[9];
		Value[NoEr+10] =theta[10];

		indexValue[NoEr] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

		indexValue[NoEr+2] = 3*(j-js)*Nx+ count_Grids;
		indexValue[NoEr+3] = 3*(j-js)*Nx +1+ count_Grids;

		indexValue[NoEr+4] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoEr+5] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoEr+6] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+7] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
		indexValue[NoEr+8] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
		indexValue[NoEr+9] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+10] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

		/* For Fr1 */

		Value[NoFr1] = phi[0];
		Value[NoFr1+1] =phi[1];

		Value[NoFr1+2] = phi[6];
		Value[NoFr1+3] = phi[7];

		for(m=0; m<4; m++)
			Value[NoFr1+4+m] = phi[2+m];

		Value[NoFr1+8] = phi[8];
		Value[NoFr1+9] =phi[9];

		indexValue[NoFr1] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+1] = 3*(j-js-1)*Nx + 3*(i-is)+1+ count_Grids;

		indexValue[NoFr1+2] = 3*(j-js)*Nx+ count_Grids;
		indexValue[NoFr1+3] = 3*(j-js)*Nx +1+ count_Grids;

		indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoFr1+6] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+7] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
			
		indexValue[NoFr1+8] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+9] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

		/* For Fr2 */

		Value[NoFr2] = psi[0];
		Value[NoFr2+1] =psi[1];

		Value[NoFr2+2] = psi[6];
		Value[NoFr2+3] = psi[7];

		for(m=0; m<4; m++)
			Value[NoFr2+4+m] = psi[2+m];

		Value[NoFr2+8] = psi[8];
		Value[NoFr2+9] =psi[9];

		indexValue[NoFr2] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

		indexValue[NoFr2+2] = 3*(j-js)*Nx+ count_Grids;
		indexValue[NoFr2+3] = 3*(j-js)*Nx +2+ count_Grids;

		indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is-1)+2+ count_Grids;

		indexValue[NoFr2+6] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+7] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
		
		indexValue[NoFr2+8] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+9] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;


	}/* Periodic boundary condition */
	else{
		/* For Er */
		Value[NoEr] = theta[0];
		Value[NoEr+1] =theta[1];

		for(m=0; m<5; m++)
			Value[NoEr+2+m] = theta[2+m];

		Value[NoEr+7] = theta[9];
		Value[NoEr+8] =theta[10];

		indexValue[NoEr] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

		indexValue[NoEr+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoEr+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoEr+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
		indexValue[NoEr+6] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
		indexValue[NoEr+7] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+8] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

		/* For Fr1 */

		Value[NoFr1] = phi[0];
		Value[NoFr1+1] =phi[1];

		for(m=0; m<4; m++)
			Value[NoFr1+2+m] = phi[2+m];

		Value[NoFr1+6] = phi[8];
		Value[NoFr1+7] =phi[9];

		indexValue[NoFr1] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+1] = 3*(j-js-1)*Nx + 3*(i-is)+1+ count_Grids;

		indexValue[NoFr1+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr1+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
					
		indexValue[NoFr1+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+7] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

		/* For Fr2 */

		Value[NoFr2] = psi[0];
		Value[NoFr2+1] =psi[1];


		for(m=0; m<4; m++)
			Value[NoFr2+2+m] = psi[2+m];

		Value[NoFr2+6] = psi[8];
		Value[NoFr2+7] =psi[9];

		indexValue[NoFr2] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

		indexValue[NoFr2+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr2+3] = 3*(j-js)*Nx + 3*(i-is-1)+2+ count_Grids;

		indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
		indexValue[NoFr2+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+7] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

		/* other ox1 boundary condition */
		if(ox1 == 1 || ox1 == 5){
					
			Value[NoEr+4] += theta[7];
			Value[NoEr+5] -= theta[8];
			
			Value[NoFr1+4]+= phi[6];
			Value[NoFr1+5]-= phi[7];
		
			Value[NoFr2+4]+= psi[6];
			Value[NoFr2+5]+= psi[7];
		}
		else if(ox1 == 2){
			Value[NoEr+4] += theta[7];
			Value[NoEr+5] += theta[8];
		
			Value[NoFr1+4]+= phi[6];
			Value[NoFr1+5]+= phi[7];
			
			Value[NoFr2+4]+= psi[6];
			Value[NoFr2+5]+= psi[7];
		}
		else if(ox1 == 3){

				/* Do nothing */
		}
		

	}/* non-periodic boundary condition */

	return;
} /* physical boundary for x */


/*-----------------------------------*/
/*------i and js -------------------*/ 
/*-----------------------------------*/

void i_js_MPI(int i)
{
	int m, j;
	j = js;
	int shifty, index;
	int MPIcount2F;
	
	if(lx2 > ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount1 = 0;
		MPIcount2 = 9;
		MPIcount2F = 8;

	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;
	}
	
	/* MPI part */
	/* Er */
	index = NoEr + MPIcount2;
	Value[index] = theta[0];
	Value[index+1] = theta[1];

	indexValue[index] = 3*(je-js)*Nx + 3*(i-is) + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 2;

	/* Fr1 */
	index = NoFr1  + MPIcount2F;
	
	Value[index] = phi[0];
	Value[index+1] = phi[1];

	indexValue[index] = 3*(je-js)*Nx + 3*(i-is) + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 1;
	

	/* Fr2 */
	index = NoFr2  + MPIcount2F;
	
	Value[index] = psi[0];
	Value[index+1] = psi[1];

	indexValue[index] = 3*(je-js)*Nx + 3*(i-is) + count_Grids - shifty;
	indexValue[index+1] = indexValue[index] + 2;



	/* For Er */
	index = NoEr + MPIcount1;
	for(m=0; m<9; m++)
		Value[index+m] = theta[m+2];

	indexValue[index] = 3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(i-is-1) + 1+ count_Grids;
	
	for(m=0; m<5; m++)
		indexValue[index+2+m] = 3*(i-is)+m+ count_Grids;

	indexValue[index+7] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+8] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

	

	/* For Fr1 */
	index = NoFr1 + MPIcount1;
	for(m=0; m<8; m++)
		Value[index+m] = phi[m+2];

	
	indexValue[index] = 3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(i-is-1) + 1+ count_Grids;

	indexValue[index+2] = 3*(i-is)+ count_Grids;
	indexValue[index+3] = 3*(i-is) + 1+ count_Grids;

	indexValue[index+4] = 3*(i-is)+3+ count_Grids;
	indexValue[index+5] = 3*(i-is)+4+ count_Grids;
				
	indexValue[index+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+7] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

	


	/* For Fr2 */
	index = NoFr2 + MPIcount1;
	for(m=0; m<8; m++)
		Value[index+m] = psi[m+2];


	indexValue[index] = 3*(i-is-1)+ count_Grids;
	indexValue[index+1] = 3*(i-is-1) + 2+ count_Grids;

	indexValue[index+2] = 3*(i-is)+ count_Grids;
	indexValue[index+3] = 3*(i-is) + 2+ count_Grids;

	indexValue[index+4] = 3*(i-is)+3+ count_Grids;
	indexValue[index+5] = 3*(i-is)+5+ count_Grids;
				
	indexValue[index+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+7] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

	


	return;
} /* MPI boundary condition for y */


void i_js_phy(int i)
{
	int m, j;
	j = js;
	/* The following is true no matter ix2==4 or not */
	/* For Er */
	for(m=0; m<9; m++)
		Value[NoEr+m] = theta[2+m];

	indexValue[NoEr] = 3*(i-is-1)+ count_Grids;
	indexValue[NoEr+1] = 3*(i-is-1) + 1+ count_Grids;
	
	for(m=0; m<5; m++)
		indexValue[NoEr+2+m] = 3*(i-is)+m+ count_Grids;

	indexValue[NoEr+7] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoEr+8] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

	/* For Fr1 */
	for(m=0; m<8; m++)
		Value[NoFr1+m] = phi[2+m];

	indexValue[NoFr1] = 3*(i-is-1)+ count_Grids;
	indexValue[NoFr1+1] = 3*(i-is-1) + 1+ count_Grids;

	indexValue[NoFr1+2] = 3*(i-is)+ count_Grids;
	indexValue[NoFr1+3] = 3*(i-is) + 1+ count_Grids;

	indexValue[NoFr1+4] = 3*(i-is)+3+ count_Grids;
	indexValue[NoFr1+5] = 3*(i-is)+4+ count_Grids;
				
	indexValue[NoFr1+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr1+7] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

	/* For Fr2 */

	for(m=0; m<8; m++)
		Value[NoFr2+m] = psi[2+m];

	indexValue[NoFr2] = 3*(i-is-1)+ count_Grids;
	indexValue[NoFr2+1] = 3*(i-is-1) + 2+ count_Grids;

	indexValue[NoFr2+2] = 3*(i-is)+ count_Grids;
	indexValue[NoFr2+3] = 3*(i-is) + 2+ count_Grids;

	indexValue[NoFr2+4] = 3*(i-is)+3+ count_Grids;
	indexValue[NoFr2+5] = 3*(i-is)+5+ count_Grids;
				
	indexValue[NoFr2+6] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr2+7] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

	if(ix2 == 4){
	
		Value[NoEr+9] = theta[0];
		Value[NoEr+10] = theta[1];
					
		indexValue[NoEr+9] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoEr+10] = 3*(je-js)*Nx+3*(i-is)+2+ count_Grids;
			
		Value[NoFr1+8] = phi[0];
		Value[NoFr1+9] = phi[1];

		indexValue[NoFr1+8] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr1+9] = 3*(je-js)*Nx+3*(i-is)+1+ count_Grids;
				
		Value[NoFr2+8] = psi[0];
		Value[NoFr2+9] = psi[1];

		indexValue[NoFr2+8] = 3*(je-js)*Nx+3*(i-is)+ count_Grids;
		indexValue[NoFr2+9] = 3*(je-js)*Nx+3*(i-is)+2+ count_Grids;				


	}
	else if(ix2 == 1 || ix2 == 5){
					
		Value[NoEr+2] += theta[0];
		Value[NoEr+4] -= theta[1];
			
		Value[NoFr1+2]+= phi[0];
		Value[NoFr1+3]+= phi[1];
				
		Value[NoFr2+2]+= psi[0];
		Value[NoFr2+3]-= psi[1];
	}
	else if(ix2 == 2){
		Value[NoEr+2] += theta[0];
		Value[NoEr+4] += theta[1];
			
		Value[NoFr1+2]+= phi[0];
		Value[NoFr1+3]+= phi[1];
				
		Value[NoFr2+2]+= psi[0];
		Value[NoFr2+3]+= psi[1];
	}
	else if(ix2 == 3){

		/* Do nothing */
	}
				

	return;
} /* physical boundary for y */



/*-----------------------------------*/
/*------i and je- -------------------*/ 
/*-----------------------------------*/

void i_je_MPI(int i)
{
	int m, j;
	j = je;
	int shifty, index;
	int MPIcount2F;
	
	if(rx2 < ID){	
		shifty = -3 * Ny * Nx * NGx * (NGy - 1);
		MPIcount1 = 2;
		MPIcount2 = 0;
		MPIcount2F = 0;
	}
	else{
		shifty = 3 * Nx * Ny * NGx;
		MPIcount1 = 0;
		MPIcount2 = 9;
		MPIcount2F = 8;
	}

	/* For MPI part */
	/* Er */
	index = NoEr + MPIcount2;
	Value[index] = theta[9];
	Value[index+1] = theta[10];

	indexValue[index] = 3*(i-is) + count_Grids + shifty;
	indexValue[index+1] = indexValue[index] + 2;

	/* Fr1 */
	index = NoFr1 + MPIcount2F;


	Value[index] = phi[8];
	Value[index+1] = phi[9];

	indexValue[index] = 3*(i-is) + count_Grids + shifty;
	indexValue[index+1] = indexValue[index] + 1;
	
	/* Fr2 */

	index = NoFr2 + MPIcount2F;


	Value[index] = psi[8];
	Value[index+1] = psi[9];

	indexValue[index] = 3*(i-is) + count_Grids + shifty;
	indexValue[index+1] = indexValue[index] + 2;
	
	
	
	/* For Er */
	index = NoEr + MPIcount1;	
	for(m=0; m<9; m++)
		Value[index+m] = theta[m];

	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2+ count_Grids;		

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
	indexValue[index+6] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;

	indexValue[index+7] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
	indexValue[index+8] = 3*(j-js)*Nx + 3*(i-is+1)+1+ count_Grids;

	
	/* For Fr1 */

	index = NoFr1 + MPIcount1;				
	for(m=0; m<8; m++)
		Value[index+m] = phi[m];


	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 1+ count_Grids;		

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
					
	indexValue[index+6] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
	indexValue[index+7] = 3*(j-js)*Nx + 3*(i-is+1)+1+ count_Grids;


	/* For Fr2 */

	index = NoFr2 + MPIcount1;				
	for(m=0; m<8; m++)
		Value[index+m] = psi[m];

	indexValue[index] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2+ count_Grids;		

	indexValue[index+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[index+3] = 3*(j-js)*Nx + 3*(i-is-1)+2+ count_Grids;

	indexValue[index+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[index+5] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
	indexValue[index+6] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
	indexValue[index+7] = 3*(j-js)*Nx + 3*(i-is+1)+2+ count_Grids;


	
	return;
} /* MPI boundary condition for y */


void i_je_phy(int i)
{
	int m, j;
	j = je;

	if(ox2 == 4){
		/* For Er */
		Value[NoEr] = theta[9];
		Value[NoEr+1] = theta[10];
					
		for(m=0; m<9; m++)
			Value[NoEr+2+m] = theta[m];

		indexValue[NoEr] = 3*(i-is)+ count_Grids;
		indexValue[NoEr+1] = 3*(i-is) + 2+ count_Grids;

		indexValue[NoEr+2] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+3] = 3*(j-js-1)*Nx + 3*(i-is) + 2+ count_Grids;		

		indexValue[NoEr+4] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoEr+5] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoEr+6] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+7] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
		indexValue[NoEr+8] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;

		indexValue[NoEr+9] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
		indexValue[NoEr+10] = 3*(j-js)*Nx + 3*(i-is+1)+1+ count_Grids;

		/* For Fr1 */

		Value[NoFr1] = phi[8];
		Value[NoFr1+1] = phi[9];
					
		for(m=0; m<8; m++)
			Value[NoFr1+2+m] = phi[m];

		indexValue[NoFr1] = 3*(i-is)+ count_Grids;
		indexValue[NoFr1+1] = 3*(i-is) + 1+ count_Grids;

		indexValue[NoFr1+2] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+3] = 3*(j-js-1)*Nx + 3*(i-is) + 1+ count_Grids;		

		indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoFr1+6] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+7] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
					
		indexValue[NoFr1+8] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
		indexValue[NoFr1+9] = 3*(j-js)*Nx + 3*(i-is+1)+1+ count_Grids;

		/* For Fr2 */

		Value[NoFr2] = psi[8];
		Value[NoFr2+1] = psi[9];
					
		for(m=0; m<8; m++)
			Value[NoFr2+2+m] = psi[m];

		indexValue[NoFr2] = 3*(i-is)+ count_Grids;
		indexValue[NoFr2+1] = 3*(i-is) + 2+ count_Grids;

		indexValue[NoFr2+2] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+3] = 3*(j-js-1)*Nx + 3*(i-is) + 2+ count_Grids;		

		indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is-1)+2+ count_Grids;

		indexValue[NoFr2+6] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+7] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
		indexValue[NoFr2+8] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
		indexValue[NoFr2+9] = 3*(j-js)*Nx + 3*(i-is+1)+2+ count_Grids;
	}/* End periodic of x2 */
	else{
		/* For Er */
			
		for(m=0; m<9; m++)
			Value[NoEr+m] = theta[m];

		indexValue[NoEr+0] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2+ count_Grids;		

		indexValue[NoEr+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoEr+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoEr+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoEr+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
		indexValue[NoEr+6] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;

		indexValue[NoEr+7] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
		indexValue[NoEr+8] = 3*(j-js)*Nx + 3*(i-is+1)+1+ count_Grids;

		/* For Fr1 */

					
		for(m=0; m<8; m++)
			Value[NoFr1+m] = phi[m];


		indexValue[NoFr1] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+1] = 3*(j-js-1)*Nx + 3*(i-is) + 1+ count_Grids;		

		indexValue[NoFr1+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr1+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

		indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
					
		indexValue[NoFr1+6] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
		indexValue[NoFr1+7] = 3*(j-js)*Nx + 3*(i-is+1)+1+ count_Grids;

		/* For Fr2 */

					
		for(m=0; m<8; m++)
			Value[NoFr2+m] = psi[m];

		indexValue[NoFr2] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+1] = 3*(j-js-1)*Nx + 3*(i-is) + 2+ count_Grids;		

		indexValue[NoFr2+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
		indexValue[NoFr2+3] = 3*(j-js)*Nx + 3*(i-is-1)+2+ count_Grids;

		indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
		indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
					
		indexValue[NoFr2+6] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
		indexValue[NoFr2+7] = 3*(j-js)*Nx + 3*(i-is+1)+2+ count_Grids;

		/* other x2 boundary condition */
		if(ox2 == 1 || ox2 == 5){
					
			Value[NoEr+4] += theta[9];
			Value[NoEr+6] -= theta[10];
			
			Value[NoFr1+4]+= phi[8];
			Value[NoFr1+5]+= phi[9];
				
			Value[NoFr2+4]+= psi[8];
			Value[NoFr2+5]-= psi[9];
		}
		else if(ox2 == 2){
			Value[NoEr+4] += theta[9];
			Value[NoEr+6] += theta[10];
			
			Value[NoFr1+4]+= phi[8];
			Value[NoFr1+5]+= phi[9];
				
			Value[NoFr2+4]+= psi[8];
			Value[NoFr2+5]+= psi[9];
		}
		else if(ox2 == 3){
			/* Do nothing */
		}
		

		}/* End non-periodic of x2 */

	return;
} /* physical boundary for y */



/*-----------------------------------*/
/*------i and j ---------------------*/ 
/*-----------------------------------*/

void i_j(int i, int j)
{
	int m;
	/* for Er */
	for(m=0; m<11; m++)
		Value[NoEr+m] = theta[m];

	indexValue[NoEr] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoEr+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

	indexValue[NoEr+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[NoEr+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

	indexValue[NoEr+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoEr+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
	indexValue[NoEr+6] = 3*(j-js)*Nx + 3*(i-is)+2 + count_Grids;

	indexValue[NoEr+7] = 3*(j-js)*Nx + 3*(i-is+1) + count_Grids;
	indexValue[NoEr+8] = 3*(j-js)*Nx + 3*(i-is+1) + 1 + count_Grids;

	indexValue[NoEr+9] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoEr+10] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

	/* For Fr1 */
	
	for(m=0; m<10; m++)
		Value[NoFr1+m] = phi[m];

	indexValue[NoFr1] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr1+1] = 3*(j-js-1)*Nx + 3*(i-is)+1+ count_Grids;

	indexValue[NoFr1+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[NoFr1+3] = 3*(j-js)*Nx + 3*(i-is-1)+1+ count_Grids;

	indexValue[NoFr1+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr1+5] = 3*(j-js)*Nx + 3*(i-is)+1+ count_Grids;
				
	indexValue[NoFr1+6] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
	indexValue[NoFr1+7] = 3*(j-js)*Nx + 3*(i-is+1)+1+ count_Grids;

	indexValue[NoFr1+8] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr1+9] = 3*(j-js+1)*Nx + 3*(i-is)+1+ count_Grids;

	/* For Fr2 */

	for(m=0; m<10; m++)
		Value[NoFr2+m] = psi[m];

	indexValue[NoFr2] = 3*(j-js-1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr2+1] = 3*(j-js-1)*Nx + 3*(i-is)+2+ count_Grids;

	indexValue[NoFr2+2] = 3*(j-js)*Nx + 3*(i-is-1)+ count_Grids;
	indexValue[NoFr2+3] = 3*(j-js)*Nx + 3*(i-is-1)+2+ count_Grids;

	indexValue[NoFr2+4] = 3*(j-js)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr2+5] = 3*(j-js)*Nx + 3*(i-is)+2+ count_Grids;
		
	indexValue[NoFr2+6] = 3*(j-js)*Nx + 3*(i-is+1)+ count_Grids;
	indexValue[NoFr2+7] = 3*(j-js)*Nx + 3*(i-is+1)+2+ count_Grids;

	indexValue[NoFr2+8] = 3*(j-js+1)*Nx + 3*(i-is)+ count_Grids;
	indexValue[NoFr2+9] = 3*(j-js+1)*Nx + 3*(i-is)+2+ count_Grids;

	return;
} /* no boundary condition for either direction */




#endif /* radMHD_INTEGRATOR */

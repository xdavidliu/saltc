#include "headers.h"

int CreateFilter(Mode *ms, int size, int lasing, Mode **msp){
	int i, added =0;
	for(i=0; i<size; i++)
		if( ms[i]->lasing == lasing){
			addArrayMode(msp, added, ms[i]);
			added++;
		}

	return added;
}

void FirstStep(Mode *ms, Mode m, Geometry geo, Vec vNh, Vec f, Vec dv, double c, double ftol, int printnewton){
	PetscPrintf(PETSC_COMM_WORLD, "Taking first step for mode \"%s\"...\n", m->name );

	int nh=0, ih, Nm;
	VecGetSize(vNh, &Nm); Nm /= NJ(geo);

	for(ih=0; ih<Nm; ih++){ // find nh of m
		if( ms[ih] == m) break;
		else nh++;
	}

	if(vNh != m->vpsi){ // update vpsi's from v
		int ih =0;
		for(ih=0; ih<Nm; ih++){
			ScatterRange((ms[ih])->vpsi, vNh, 0, ih*NJ(geo), NJ(geo) );
		}
	}

	while(1){
	if( LastProcess() ){ // try new c
		VecSetValue(vNh, offset(geo, nh)+Nxyzcr(geo)+1, c, INSERT_VALUES);
		if( vNh != m->vpsi) VecSetValue(m->vpsi, Nxyzcr(geo)+1, c, INSERT_VALUES);
	}
	AssembleVec(vNh);
	AssembleVec(m->vpsi);

	double fnorm = FormJf(ms, geo, vNh, f, ftol, printnewton);

	if(  fnorm < ftol) break;

	KSPSolve( m->ksp, f, dv);
	if(printnewton) PetscPrintf(PETSC_COMM_WORLD, "\n");

	double dc = -GetValue(dv, offset(geo, nh)+Nxyzcr(geo)+1 );

	if( cabs(dc)/c < 0.5){
		VecAXPY(vNh, -1.0, dv);

		if(vNh != m->vpsi){ // update vpsi's from v
			int ih =0;
			for(ih=0; ih<Nm; ih++){
				ScatterRange(vNh, (ms[ih])->vpsi, ih*NJ(geo), 0, NJ(geo) );
	
			}
		}
		// don't NewtonSolve here, that will be done immediately after
		break;
	}else if(c + dc < 0) c *= 0.5;
	else c = 0.5*(c + c+dc);
	}

	PetscPrintf(PETSC_COMM_WORLD, "First step for mode \"%s\" complete!\n", m->name );  
}

void Bundle(Mode *ms, int size, Geometry geo){
	int i, ih, Nh = size, Nj = 2*Nxyzc(geo)+2;
	if(Nh < 2) MyError("Bundle function is only for multimode!");

	Mat J; KSP ksp;
	CreateSquareMatrix( Nh*Nj, 0, &J);
	AllocateJacobian(J, geo);

	KSPCreate(PETSC_COMM_WORLD,&ksp);
	PC pc;
	KSPGetPC(ksp,&pc);
 	PCSetType(pc,PCLU);
  	PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS);
	// don't forget to change this in Setup too.

	KSPSetFromOptions(ksp);
	KSPSetOperators(ksp, J, J, SAME_PRECONDITIONER);
	// TODO: will probably want to merge all of this in with a generalized
	// multimode version of Mode::Setup

	for(i=0; i<SCRATCHNUM; i++){
		DestroyVec(&geo->vNhscratch[i]);
		MatGetVecs(J, &geo->vNhscratch[i], NULL);
	}

	for(ih=0; ih<size; ih++){
		Mode m = ms[ih];

		DestroyMat( &m->J); // bundle shares J and v
		m->J = J;
		KSPDestroy(&m->ksp);
		m->ksp = ksp;

		MoperatorGeneralBlochFill(geo, J, m->b, m->BCPeriod, m->k, ih);
		AddRowDerivatives(J, geo, m->ifix, ih);
	}

	AssembleMat(J);
	MatSetOption(J,MAT_NEW_NONZERO_LOCATIONS,PETSC_FALSE);
	MatStoreValues(J); 
}

int FindModeAtThreshold(Mode *ms, int size){
	int n = -1, ih;

	for(ih = 0; ih<size; ih++){
		Mode m = ms[ih];
		if( get_c(m) == 0.0 && m->lasing ){
			n = ih;
			break;
		}
	}
	return n;
}

// everything after Nm copied directly from ReadMode
void Creeper(double dD, double Dmax, double thresholdw_tol, double ftol, Mode *ms, int printnewton, int Nm, Geometry geo){
	int ih, i;
	for(ih=0; ih<Nm; ih++){
		Setup( ms[ih], geo); // TODO: bundle if multiple lasing modes
	}
    Vec f, dv;
    MatGetVecs( ms[0]->J, &dv, &f);

	for(; geo->D <= Dmax; geo->D = (geo->D+dD < Dmax? geo->D+dD: Dmax)){
		Mode *msh=NULL;
	  	int Nlasing = CreateFilter(ms, Nm, 1, &msh); // lasing sub-array

	  Vec vNh = ms[0]->vpsi, fNh = f, dvNh = dv;

	  if( Nlasing > 0){ // lasing modes
	  

	  
	  	  int nt = FindModeAtThreshold(msh, Nlasing);
	  
		  if( nt != -1 && Nlasing > 1){
		  	Bundle(msh, Nlasing, geo);
		  }

		  if(Nlasing > 1){	 // these vectors will have been properly created in the last block
			vNh = geo->vNhscratch[2];
			fNh = geo->vNhscratch[3];
			dvNh = geo->vNhscratch[4];
		  }

		  if(Nlasing == 1)
		  	vNh = msh[0]->vpsi;

		  if( nt != -1 ){
		  		geo->D += 0.5*dD;
				if(geo->D > Dmax) geo->D = Dmax;
		
		  		FirstStep(msh, msh[nt], geo, vNh, fNh, dvNh, 1.0, ftol, printnewton);
		  }
		  
		  
		  NewtonSolve(msh, geo,  vNh, fNh, dvNh, ftol, printnewton);  

		  
	  }

	  for(ih=0; ih<Nm; ih++){ // now nonlasing modes
		Mode m = ms[ih];
		if(m->lasing) continue;

		double wi_old = cimag(get_w(m));

		NewtonSolve(&m, geo,  m->vpsi, f, dv, ftol, printnewton);

	  
		double wi_new = cimag(get_w(m));

		if(wi_new > -thresholdw_tol && !m->lasing){

			ThresholdSearch(  wi_old, wi_new, geo->D-dD, geo->D, 
			msh, vNh, m, geo, f, dv, thresholdw_tol, ftol, printnewton);
	
		}
	  }

	  if(Nlasing>0) free(msh);	  
	  if(geo->D==Dmax) break;
	}

	DestroyVec(&f);
	DestroyVec(&dv);

	for(i=0; i<SCRATCHNUM; i++){ // cleanup
		VecSet( geo->vH, 1.0);
		VecSet( geo->vscratch[i], 0.0);
		VecSet( geo->vMscratch[i], 0.0);
		DestroyVec(&geo->vNhscratch[i]);
	}
	
}
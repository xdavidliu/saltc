#include "salt.h"

int CreateFilter(Mode *ms, int size, int lasing, Mode **msp){
	int i, added =0;
	for(i=0; i<size; i++)
		if( ms[i]->lasing == lasing){
			addArrayMode(msp, added, ms[i]);
			added++;
		}

	if(!added) *msp = NULL; // so thresholdsearch receives correct argument
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

	if( fabs(dc)/c < 0.5){
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

PetscErrorCode Bundle(Mode *ms, int size, Geometry geo){
	int i, ih, Nh = size, Nj = 2*Nxyzc(geo)+2;
	if(Nh < 2) MyError("Bundle function is only for multimode!");

	Mat J; KSP ksp;
	CreateSquareMatrix( Nh*Nj, 0, &J);
	AllocateJacobian(J, geo);

	KSPCreate(PETSC_COMM_WORLD,&ksp);
	KSPSetFromOptions(ksp);
	KSPSetOperators(ksp, J, J); 
	// for petsc 3.4 and before, put SAME_PRECONDITIONER as fourth argument
	// for 3.5 and above, have only 3 arguments

	// TODO: will probably want to merge all of this in with a generalized
	// multimode version of Mode::Setup

	for(i=0; i<SCRATCHNUM; i++){
		if(geo->vNhscratch[i])
			VecDestroy(&geo->vNhscratch[i]);
		MatCreateVecs(J, &geo->vNhscratch[i], NULL);
	}

	// when Bundle is called in the middle of Creeper, the modes
	// will have separate J's and ksps, so need to destroy each of them
	if(size == 2){ 
		for(ih=0; ih<size;ih++){
			Mode m = ms[ih];
			if(m->J){
				PetscErrorCode ierr = MatDestroy( &m->J); CHKERRQ(ierr);
			}
			if(m->ksp){
				KSPDestroy( &m->ksp);	
			}	
		}
	}else{ // size > 2; they will all have shared J and ksp
		   // except the one that just hit threshold
		   // but I'm destroying that one's J and ksp after
		   // thresholdSearch
		if( ms[0]->J ){
			PetscErrorCode ierr = MatDestroy( &ms[0]->J); CHKERRQ(ierr);
		}
		if( ms[0]->ksp) KSPDestroy( &ms[0]->ksp);
	}

	for(ih=0; ih<size; ih++){
		Mode m = ms[ih];
		m->J = J;// bundle shares J and v
		m->ksp = ksp;
		MoperatorGeneralBlochFill(geo, J, m->b, m->k, ih);
		AddRowDerivatives(J, geo, m->ifix, ih);
	}

	AssembleMat(J);
	MatSetOption(J,MAT_NEW_NONZERO_LOCATIONS,PETSC_FALSE);
	MatStoreValues(J); 
	return 0;
}

int FindModeAtThreshold(Mode *ms, int size){
	int n = -1, ih;

	for(ih = 0; ih<size; ih++){
		Mode m = ms[ih];
		if( get_c(m) == 0.0 && m->lasing ){
	
			if(n == -1)
				n = ih;
			else
				MyError("found two modes at threshold! FirstStep does not support multimode yet");
		}
	}
	return n;
}

void ComplexScale( Vec w, dcomp a, Vec scratch, Geometry geo){

	TimesI(geo, w, scratch);
	VecScale(w, creal(a));
	VecAXPY(w, cimag(a), scratch);

}


void ComplexPointwiseMult(Vec w, Vec u, Vec v, Vec scratch0, Vec scratch1, Geometry geo){
// w = u .* v
// i.e. wR = uR vR - uI vI
// wI = uR vI + uI vR

	int Nxyzc = xyzcGrid(&geo->gN);

	VecCopy(u, scratch1);
	ScatterRange(u, scratch1, Nxyzc, 0, Nxyzc );
	// scratch1 is now [uI; uI]
	
	TimesI(geo, v, scratch0);
	VecPointwiseMult(scratch1, scratch0, scratch1);
	// scratch1 is now [ -uI vI; uI vR]

	// intricate shuffling of scratch0 and scratch1 here is necessary to allow for the case that w = v (w = u I haven't tested yet, but it should work)
	VecCopy(u, scratch0);
	ScatterRange(u, scratch0, 0, Nxyzc, Nxyzc );
	VecPointwiseMult(scratch0, scratch0, v);
	// scratch0 is now [uR vR; uR vI]

	VecAXPY(scratch1, 1.0, scratch0); 
	VecCopy(scratch1, w);
	
}

void OutputDEps( Geometry geo, Mode *ms){


	int i, Nxyzc = xyzcGrid(&geo->gN), lasing = ms[0]->lasing && !(ms[1]->lasing);
	// 8/5/15: two possibilities: both lasing -> forces poles together. one lasing one not -> forces real parts together, prevents Im w0 (of lasing mode) from moving from zero and forcing nonlasing to real axis.

	PetscPrintf(PETSC_COMM_WORLD, "DEBUG: output_deps called!, lasing = %i\n", lasing);

	dcomp A[2];
	Vec pQP[2] = { geo->vscratch[4], geo->vscratch[5] };
	// the p vector in quadratic programming
	// DON'T USE scratch4 and scratch5 after this!!

	for(i=0; i<2; i++){ 
	
		VecCopy(geo->vH, geo->vscratch[1]);
		VecSet(geo->vscratch[0], 0.0);
		ScatterRange(geo->vscratch[0], geo->vscratch[1], Nxyzc, Nxyzc, Nxyzc );
		// H is [HR; HR]. Make it [HR, 0] so can use ComplexPointwiseMult with it
		VecPointwiseMult(geo->vscratch[1], geo->vf, geo->vscratch[1]);

		dcomp yw = gamma_w(ms[i], geo), ywprime = -csqr(yw) / geo->y;
		dcomp a = geo->D*(2.0*yw + get_w(ms[i])*ywprime );
		// no issue with geo->D != Dmax here...

		ComplexScale( geo->vscratch[1], a, geo->vscratch[2], geo);
		VecAXPY( geo->vscratch[1], 2.0, geo->veps);
		// 2 Ep + 2 D yw H F + w D yw' H F

		ComplexPointwiseMult( pQP[i], ms[i]->vpsi, ms[i]->vpsi, geo->vscratch[2], geo->vscratch[3], geo);
		// now p[i] = psi[i].^2

		ComplexPointwiseMult(geo->vscratch[1], pQP[i], geo->vscratch[1], geo->vscratch[2], geo->vscratch[3], geo);

		double AR, AI;
		VecSet(geo->vscratch[0], 0.0);
		ScatterRange(geo->vscratch[1], geo->vscratch[0], 0, 0, Nxyzc );
		VecSum(geo->vscratch[0], &AR);
					
		VecSet(geo->vscratch[0], 0.0);
		ScatterRange(geo->vscratch[1], geo->vscratch[0], Nxyzc, Nxyzc, Nxyzc );
		VecSum(geo->vscratch[0], &AI);

		A[i] = AR + ComplexI*AI;
		ComplexScale( pQP[i], get_w(ms[i]) / A[i], geo->vscratch[2], geo);
		// now p[i] is the true p vector in Quadratic programming
		// the factor of w is consistent. p is  +psi.^2 / integral[ psi.^2 ( 2 eps / w + ddw eps ) ]
		// hence dw = -p^T deps

	}

	int extra = lasing ? 3 : 2;

	Mat Mqp; // matrix for linear problem for QP
	CreateSquareMatrix( Nxyzcr(geo)+extra, 0, &Mqp);
	int Ns, Ne;
	MatGetOwnershipRange(Mqp, &Ns, &Ne);
	int range = Ne - Ns;
	int 	*nnzd = malloc( range*sizeof(int) ),
		*nnzo = malloc( range*sizeof(int) );

	for(i=0; i<range; i++){
		nnzd[i] = extra+1;
		nnzo[i] = extra+1;
		//"extra" columns on right, a single element for the identity matrix
		//and extra + 1 for both on proc and off proc to be conservative
	}

	if(LastProcess()){ for(i=0; i<extra; i++){
		nnzd[range-1-i] = range;
		nnzo[range-1-i] = Nxyzcr(geo)+extra-range;
	}} // set last three rows have full nonzeros

	if(GetSize() > 1) MatMPIAIJSetPreallocation(Mqp, 0, nnzd, 0, nnzo);
	else MatSeqAIJSetPreallocation(Mqp, 0, nnzd);
	free(nnzd); free(nnzo);

	for(i=Ns; i<Ne && i < Nxyzcr(geo); i++){
		MatSetValue(Mqp, i, i, 1.0, INSERT_VALUES);
	} // add the 1's to diagonal, except last three
	
	int ns, ne; // lowercase for Nxyzcr+2, upper case for +extra
	VecGetOwnershipRange(pQP[0], &ns, &ne);
	
	const double *p0array, *p1array;

	VecGetArrayRead(pQP[0], &p0array);
	VecGetArrayRead(pQP[1], &p1array);

	dcomp w0 = get_w(ms[0]), w1 = get_w(ms[1]);
	double w1minusw0R = creal(  w1 - w0  ),
		w1minusw0I = cimag(  w1 - w0  ),
		w1I = cimag( w1 ); // for use in lasing version

	for(i=ns; i<ne && i < Nxyzcr(geo); i++){

		int row, column;
		column = Nxyzcr(geo); 
		// first out of the two or three columns
		// constant, but put code here for clarity
		// technically should use static or const here but whatever

		if( ir(geo, i)==0 ) row = i + Nxyzc;
		else row = i - Nxyzc;
		// for second column, RI blocks are switched: 4 columns look like [qR, qI; -qI, qR]

		double qval = (p1array[i-ns] - p0array[i-ns]) / w1minusw0R; // normalized
		if( ir(geo,i)==1) qval *= -1.0; // insert qR and -qI (see notes)

		MatSetValue(Mqp, i, column, qval, INSERT_VALUES);
		MatSetValue(Mqp, column, i, qval, INSERT_VALUES);
		// qval's row block not switched
		// this row says qR depsR - qI depsI = w1R - w0R

		if(!lasing){

			double qval2 = (p1array[i-ns] - p0array[i-ns]) / w1minusw0I; 
			// second column of qvals has dwI instead of dwR in denominator
			// also no factor of -1, and use switched row, not i
			MatSetValue(Mqp, row, column+1, qval2, INSERT_VALUES);
			MatSetValue(Mqp, column+1, row, qval2, INSERT_VALUES);

		}else{
			MatSetValue(Mqp, row, column+1, 1.0*p1array[i-ns] / w1I, INSERT_VALUES);
			MatSetValue(Mqp, row, column+2, -1.0*p0array[i-ns], INSERT_VALUES); 
			
			// third column prevents lasing mode from leaving real axis (may also result conveniently in dH = 0; since spatial hole burning term's job is to do this), and second column forces second mode to real axis.
			// second column says (p1^T deps)_I = w1I
			// third column says -( p0^T deps)_I = 0
			// columns in reverse order so bqp easier to construct


			MatSetValue(Mqp, column+1, row, 1.0*p1array[i-ns] / w1I, INSERT_VALUES);
			MatSetValue(Mqp, column+2, row, -1.0*p0array[i-ns], INSERT_VALUES);
			// matrix is symmetric, so must add transposed elements of course
		}

	}

	VecRestoreArrayRead(pQP[0], &p0array);
	VecRestoreArrayRead(pQP[1], &p1array);

	AssembleMat(Mqp);

	Vec bqp, yqp; // RHS and LHS for linear solve
	MatCreateVecs(Mqp, &bqp, &yqp);
	VecSet(bqp, 0.0);

	// set to 1.0 because normalized. w2-w1 divided in the matrix. This keeps things well-conditioned for the case that w2 is very close to w1	
	VecSetValue(bqp, Nxyzcr(geo)-1+1, 1.0, INSERT_VALUES);		
	VecSetValue(bqp, Nxyzcr(geo)-1+2, 1.0, INSERT_VALUES);
	// works for both lasing and non-lasing; for lasing 3rd element is zero.

	KSP ksp;
	KSPCreate(PETSC_COMM_WORLD,&ksp);
	KSPSetFromOptions(ksp);
	KSPSetOperators(ksp, Mqp, Mqp); // SAME_PRECONDITIONER here (doesn't really matter; I'm only solving once) 

	AssembleVec(bqp);
	AssembleVec(yqp); // KSP is picky about assembling vectors

	KSPSolve( ksp, bqp, yqp);
	KSPDestroy( &ksp);

	// remove last "extra" elements; works for both lasing (extra = 3) and nonlasing (extra = 2)
	ScatterRange(yqp, geo->vscratch[0], 0, 0, Nxyzcr(geo) );
	VecCopy( geo->veps, geo->vscratch[1]);
	VecAXPY( geo->vscratch[1], 1.0,  geo->vscratch[0]);
	Output(geo->vscratch[1], "VecEpsNew", "EpsNew");

	VecDestroy(&bqp);
	VecDestroy(&yqp);
	MatDestroy(&Mqp);


}




// everything after Nm copied directly from ReadMode
int Creeper(double dD, double Dmax, double ftol, Mode *ms, int printnewton, int Nm, Geometry geo){
	
	double hugeval = 1.0e20;
	if(Dmax < 0.0) Dmax = hugeval; // hack, to instruct Creeper to stop upon threshold
	Mode *msh; // lasing mode subarray
	int ih, i, Nlasing, NlasingOld;

	Nlasing = CreateFilter(ms, Nm, 1, &msh);
	NlasingOld = Nlasing;
	if(Nlasing > 1) Bundle(msh, Nlasing, geo);

	for(ih=0; ih<Nm; ih++){
		if( !ms[ih]->lasing || Nlasing == 1)
			Setup( ms[ih], geo);
	}
    Vec f, dv;
//    MatCreateVecs( ms[0]->J, &dv, &f); 
// 9/17/14: replaced MatCreateVecs here with VecDuplicate. 2-mode lasing with 1-mode nonlasing no longer broken. Not sure why I didn't do this before.
	VecDuplicate(geo->vscratch[0], &dv);
	VecDuplicate(geo->vscratch[0], &f);

	PetscPrintf(PETSC_COMM_WORLD, "DEBUG: only ThresholdSearching when Dmax < 0. If mode gets outputted, it will be treated as lasing when read!\n");

	for(; geo->D <= Dmax; geo->D = (geo->D+dD < Dmax? geo->D+dD: Dmax)){
	  	Nlasing = CreateFilter(ms, Nm, 1, &msh); // lasing sub-array
	  	Vec vNh = ms[0]->vpsi, fNh = f, dvNh = dv;
	 	if( Nlasing > 0){ // lasing modes  
	  	  int nt = FindModeAtThreshold(msh, Nlasing);
	  
		  // this is not called if we start from a threshold
		  if( nt != -1 && Nlasing > 1 && !msh[nt]->J ){
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

	
	  Mode mthreshold_nonlasing = 0;
	  for(ih=0; ih<Nm; ih++){ // now nonlasing modes

		Mode m = ms[ih];
		if(m->lasing || m == mthreshold_nonlasing) continue;
		double wi_old = cimag(get_w(m));

		NewtonSolve(&m, geo,  m->vpsi, f, dv, ftol, printnewton);

		double wi_new = cimag(get_w(m));

		// recall that for Dmax < 0 specified, we set Dmax to hugeval before
		if(wi_new > 0.0 && !m->lasing && Dmax == hugeval){

			ThresholdSearch(  wi_old, wi_new, geo->D-dD, geo->D, 
			msh, vNh, fNh, dvNh, m, geo, f, dv, ftol, printnewton);
			ih = -1; // reset to recalculate the rest of the lasing modes
			mthreshold_nonlasing = m;

			// now there will 2 or more lasing modes, so this threshold mode will join a multimode bundle
			if(Nlasing > 0){ 
				MatDestroy( &m->J);
				m->J = 0;
				KSPDestroy( &m->ksp);
				m->ksp = 0;
			}
		}
	  }

	  if(Nlasing>0) free(msh);	  
	  if(geo->D==Dmax || (Dmax == hugeval && mthreshold_nonlasing != 0) ) break;
	}


	

/*
	PetscPrintf(PETSC_COMM_WORLD, "DEBUG: outputting old Eps vector\n");
	Output(geo->veps, "VecEps", "Eps");

	PetscPrintf(PETSC_COMM_WORLD, "DEBUG: outputting Fprof vector\n");
	Output(geo->vf, "VecFprof", "Fprof");
*/


	//=======================


	VecDestroy(&f);
	VecDestroy(&dv);

	Nlasing = CreateFilter(ms, Nm, 1, &msh);
	if(Nlasing > 0){
		MatDestroy(&msh[0]->J);
		KSPDestroy(&msh[0]->ksp);
		// these share the same J and ksp, so only one need to be destroyed
	}

	for(i=0; i<Nm; i++){
		if(!ms[i]->lasing || Nlasing == 1){
			MatDestroy(&ms[i]->J);
			KSPDestroy(&ms[i]->ksp);		
		}

		ms[i]->J = 0;
		ms[i]->ksp = 0;
	}


	// 070315: output deps using perturbation theory and quadratic
	// programming method	
	// 071915: do this AFTER destroying KSP objects, don't want to carry around TWO LU factorized matrices! Too much of a memory hog.

	int output_deps = 0;
	PetscOptionsGetInt(PETSC_NULL,PETSC_NULL,"-output_deps", &output_deps,NULL);
	if( output_deps == 1 && Nm == 2){
		OutputDEps( geo, ms);
	}

	// 8/3/15: recoding EpsTilde; forgot to commit it first time
	// for looking at "passive" poles with single lasing mode on to determine stability of that lasing mode
	int output_epstilde = 0;
	PetscOptionsGetInt(PETSC_NULL,PETSC_NULL,"-output_epstilde", &output_epstilde,NULL);
	if( output_epstilde == 1){
		
		int Nxyzc = xyzcGrid(&geo->gN);
		VecCopy(geo->vH, geo->vscratch[1]);
		VecSet(geo->vscratch[0], 0.0);
		ScatterRange(geo->vscratch[0], geo->vscratch[1], Nxyzc, Nxyzc, Nxyzc );
		// H is [HR; HR]. Make it [HR, 0] so can use ComplexPointwiseMult with it
		VecPointwiseMult(geo->vscratch[1], geo->vf, geo->vscratch[1]);

		dcomp yw = gamma_w(ms[0], geo);

		ComplexScale( geo->vscratch[1], geo->D * yw, geo->vscratch[2], geo);
		// no issue with geo->D != Dmax here...
		VecAXPY( geo->vscratch[1], 1.0, geo->veps);
		// Ep + D yw H F

		PetscPrintf(PETSC_COMM_WORLD, "Outputting EpsTilde...\n");
		Output(geo->vscratch[1], "VecEpsTilde", "EpsTilde");
	}





	for(i=0; i<SCRATCHNUM; i++){ // cleanup
		VecSet( geo->vH, 1.0);
		VecSet( geo->vscratch[i], 0.0);
		VecSet( geo->vMscratch[i], 0.0);
		if(geo->vNhscratch[i]){
			VecDestroy(&geo->vNhscratch[i]);
			geo->vNhscratch[i] = 0;
		}
	}
	return Nlasing - NlasingOld;
}

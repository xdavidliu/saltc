#include "headers.h"



void FillBop(Geometry *geo, Mat Bop, dcomp w){


	VecCopy(geo->veps, geo->vscratch[0]);
	TimesI(geo, geo->vscratch[0], geo->vscratch[1]);
	
	Complexfun b;
	CreateComplexfun(&b, geo->vscratch[0], geo->vscratch[1]);
	int i; 
	for(i=b.ns; i<b.ne; i++)
		setc(&b, i, csqr(w)* valc(&b, i) );

	SetJacobian(geo, Bop, geo->vscratch[0], -2, 0, 0);
	SetJacobian(geo, Bop, geo->vscratch[1], -2, 1, 0);	
	
	DestroyComplexfun(&b);
	
	AssembleMat(Bop);

}

void Passive(int BCPeriod, int bx, int by, int bz, double kx, double ky, double kz, double wreal, double wimag, char *modeout){

    	tv t1, t2, t3;
	Geometry Geo;
	CreateGeometry(&Geo);
	Geometry *geo = &Geo; // to make consistent with other functions
	
	gettimeofday(&t1, NULL);

	int	i, b[3][2];

	for(i=0; i<3; i++) b[i][1] = 0;

	b[0][0] = bx;
	b[1][0] = by;
	b[2][0] = bz;

        Mat Mop;
	CreateSquareMatrix(2*Nxyzc(geo), 26, &Mop);




	double k[3] = {kx, ky, kz};




	MoperatorGeneralBlochFill(geo, Mop, b, BCPeriod, k, 0);
	AssembleMat(Mop);










	dcomp guess = -csqr(wreal + ComplexI * wimag);


	Mat Bop; CreateSquareMatrix(2*Nxyzc(geo), 2, &Bop);



	FillBop(geo, Bop, 1.0);







	EPS evps; 
        EPSCreate(PETSC_COMM_WORLD, &evps);
        EPSSetOperators(evps, Mop, Bop);
	EPSSetTarget(evps, creal(guess));
	EPSSetWhichEigenpairs(evps, EPS_TARGET_REAL);


	

        EPSSetFromOptions(evps);
    


	gettimeofday(&t2, NULL);
        EPSSolve(evps);


	


        int nconv, j=0;
	EPSGetConverged(evps, &nconv);
	
	dcomp w; 

        Vec v, vi;
        MatGetVecs(Mop, &v, &vi);

	if(nconv>0) for(i=0; i<nconv; i++){
		double lr, li;
		EPSGetEigenpair(evps, i, &lr, &li, v, vi);
		w = csqrt(-lr - ComplexI * li);
		
		//==============
		// tests if the eigenvector is conjugated. if unconjugated, vi should be
		// ( vI; -vR), i.e. the block version of -iv. Hence, if conjugated,
		// then || TimesI(v) + vi || > 1e-5 or something
		

		ScatterRange(v, geo->vscratch[0], 0, 0, xyzcrGrid(&geo->gN) );
		TimesI(geo, geo->vscratch[0], geo->vscratch[2]);
		
		VecSet(geo->vscratch[1], 0.0); // annoying last two elements
		ScatterRange(vi, geo->vscratch[1], 0, 0, xyzcrGrid(&geo->gN) );
		VecAXPY(geo->vscratch[2], -1.0, geo->vscratch[1]);
		
		double dvnorm, vnorm;
		VecNorm(geo->vscratch[2], NORM_2, &dvnorm);
		VecNorm(geo->vscratch[0], NORM_2, &vnorm);
		int fake = dvnorm / vnorm < 1e-5;
		//==============

		
		if( cimag(w) > 0.0 || fake ) continue; // could use ^ (XOR) here, but the positive frequency ones are exact duplicates.
		else j++;


		if(cimag(w) > 0.0) w = conj(w);


		Mode M, *m = &M;
		CreateMode(m, geo, 0, b, BCPeriod, k);
		ScatterRange(v, m->vpsi, 0, 0, xyzcrGrid(&geo->gN) );


		Fix(m, geo);
		
		double psinorm, psifnorm;
		VecNorm(m->vpsi, NORM_2, &psinorm);
		VecPointwiseMult(geo->vscratch[0], m->vpsi, geo->vf);
		VecNorm(geo->vscratch[0], NORM_2, &psifnorm);

		PetscPrintf(PETSC_COMM_WORLD, "found mode #%i: w = %1.8g + (%1.8g) i; pumped fraction = %g\n", j, creal(w), cimag(w), psifnorm/psinorm );
		SetLast2(m->vpsi, creal(w), cimag(w) ); // make sure to get psinorm before doing this



		sprintf(m->name, "%s%i", modeout, j);
		Write(m, geo);
		DestroyMode(m);
	}
	EPSDestroy(&evps);
	DestroyMat(&Mop);	DestroyMat(&Bop);
	DestroyVec(&v);	DestroyVec(&vi);
	DestroyGeometry(geo);

	gettimeofday(&t3, NULL);	
	
	PetscPrintf(PETSC_COMM_WORLD, "Formation and setup: %g s\nEPSSolve and Output: %g s\n", 
		dt(t1, t2), dt(t2, t3) );

} 


int main(int argc, char** argv){ 

	SlepcInitialize(&argc, &argv, PETSC_NULL, PETSC_NULL); 


	double wguess_real, wguess_imag;
	OptionsGetDouble("-wreal", &wguess_real);
	OptionsGetDouble("-wimag", &wguess_imag);

	int bl[3], BCPeriod;
	OptionsGetInt("-BCPeriod", &BCPeriod);
	OptionsXYZInt("-b", bl);

	double k[3] = {0, 0, 0};
	OptionsXYZDouble("-k", k);

	char s[PETSC_MAX_PATH_LEN];
	OptionsGetString("-passiveout", s);

	Passive(BCPeriod, bl[0], bl[1], bl[2], k[0], k[1], k[2], wguess_real, wguess_imag, s);

		
SlepcFinalize();	
}

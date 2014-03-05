#include "salt.h"

Mode *ReadModes(Geometry geo, char **namesin, char **namesout, int Nm){
	Mode *ms;
	int i;
	for(i=0; i<Nm; i++){
		double D;
		Mode m = ModeRead(namesin[i], geo, &D);
		if(i==0) geo->D = D;
		else if(D != geo->D)
			MyError("The input modes should all be at the same pump strength!");

		sprintf(m->name, "%s", namesout[i]);
		addArrayMode(&ms, i, m);
	}
	return ms;
}

Geometry ReadCreateGeometry(){
	int i, N[3], Npml[3], Nc, LowerPML;
	double h[3], wa, y;
	char option[PETSC_MAX_PATH_LEN];
	const char x[3] = {'x', 'y', 'z'};

	for(i=0; i<3; i++){
		sprintf(option, "%s%c", "-N", x[i]);
		PetscOptionsGetInt(PETSC_NULL,option, &N[i], NULL);
		sprintf(option, "%s%c", "-Npml", x[i]);
		PetscOptionsGetInt(PETSC_NULL,option, &Npml[i], NULL);
		sprintf(option, "%s%c", "-h", x[i]);
		PetscOptionsGetReal(PETSC_NULL,option, &h[i], NULL);

		PetscOptionsGetInt(PETSC_NULL,"-Nc", &Nc,NULL);
		PetscOptionsGetInt(PETSC_NULL,"-LowerPML", &LowerPML,NULL);
		PetscOptionsGetReal(PETSC_NULL,"-wa", &wa,NULL); 
		PetscOptionsGetReal(PETSC_NULL,"-gamma", &y,NULL); 
	}

	char epsfile[PETSC_MAX_PATH_LEN], fproffile[PETSC_MAX_PATH_LEN];
	PetscOptionsGetString(PETSC_NULL,"-epsfile", epsfile, PETSC_MAX_PATH_LEN, NULL); 
	PetscOptionsGetString(PETSC_NULL,"-fproffile", fproffile, PETSC_MAX_PATH_LEN, NULL); 

	Vec veps, vfprof;
	CreateVec(N[0]*N[1]*N[2], &veps);
	VecDuplicate(veps, &vfprof);
	FILE *fp;
	fp = fopen(epsfile, "r");
	if(fp==NULL){
		char message[PETSC_MAX_PATH_LEN];
		sprintf(message, "failed to read %s", epsfile);
		MyError(message);
	}
	ReadVectorC(fp, N[0]*N[1]*N[2], veps);
	fclose(fp);

	fp = fopen(fproffile, "r");
	if(fp==NULL){
		char message[PETSC_MAX_PATH_LEN];
		sprintf(message, "failed to read %s", fproffile);
		MyError(message);
	}
	ReadVectorC(fp, N[0]*N[1]*N[2], vfprof);
	fclose(fp);

	double *eps, *fprof;
	VecGetArray(veps, &eps);
	VecGetArray(vfprof, &fprof);

	Geometry geo = CreateGeometry(N, h, Npml, Nc, LowerPML, eps, fprof, wa, y);    

	VecRestoreArray(veps, &eps);
	VecRestoreArray(vfprof, &fprof);
	VecDestroy(&veps);
	VecDestroy(&vfprof);
	return geo;
}

void mainPassive(){

	int i, BCPeriod=0, bl[3] = {0}, nev=0;
	double k[3] = {0},wreal = 0., wimag=0., modenorm=1.;
	char option[PETSC_MAX_PATH_LEN];
	const char x[3] = {'x', 'y', 'z'};

	for(i=0; i<3; i++){
		sprintf(option, "%s%c", "-b", x[i]);
		PetscOptionsGetInt(PETSC_NULL,option, &bl[i], NULL);

		sprintf(option, "%s%c", "-k", x[i]);
		PetscOptionsGetReal(PETSC_NULL,option, &k[i], NULL);
	}
	char modeout[PETSC_MAX_PATH_LEN] = "";
	PetscOptionsGetReal(PETSC_NULL,"-norm", &modenorm,NULL); 
	PetscOptionsGetReal(PETSC_NULL,"-wreal", &wreal,NULL); 
	PetscOptionsGetReal(PETSC_NULL,"-wimag", &wimag,NULL); 
	PetscOptionsGetInt(PETSC_NULL,"-nev", &nev,NULL);
	PetscOptionsGetInt(PETSC_NULL,"-BCPeriod", &BCPeriod,NULL);
	PetscOptionsGetString(PETSC_NULL,"-passiveout", modeout, PETSC_MAX_PATH_LEN, NULL); 

	Geometry geo = ReadCreateGeometry();
	
	int added;
	Mode *ms = Passive(&added, BCPeriod, bl, k, wreal, wimag, modenorm, nev, geo);
	for(i = 0; i<added; i++){
		if(added == 1)
			sprintf(ms[i]->name, "%s", modeout);
		else 
			sprintf(ms[i]->name, "%s%i", modeout, i);

		Write(ms[i], geo);
		VecDestroy(&ms[i]->vpsi);
		// no need to DestroyMode here because J and ksp not created
	}
	free(ms);
	DestroyGeometry(geo);
}

void mainCreeper(double Dmax){
	int i, ih, Nm = 0, printnewton = 0;
	double dD = 0.0, thresholdw_tol=0., ftol = 0.0;

	Geometry geo;
	PetscOptionsGetReal(PETSC_NULL,"-thresholdw_tol", &thresholdw_tol,NULL); 
	PetscOptionsGetReal(PETSC_NULL,"-dD", &dD,NULL); 
	PetscOptionsGetReal(PETSC_NULL,"-newtonf_tol", &ftol,NULL); 

	char optionin[PETSC_MAX_PATH_LEN] = "-in0",
		 optionout[PETSC_MAX_PATH_LEN] = "-out0",
		 name[PETSC_MAX_PATH_LEN], name2[PETSC_MAX_PATH_LEN],
		 **namesin = NULL, **namesout = NULL;

	i=0;
	while(1){
		PetscBool flg;
		PetscOptionsGetString(PETSC_NULL,optionin, name, PETSC_MAX_PATH_LEN, &flg); 
		if( !flg ) break;

		PetscOptionsGetString(PETSC_NULL,optionout, name2, PETSC_MAX_PATH_LEN, &flg); 
		if( !flg )
			MyError("number of -out less than number of -in!");

		if(i==0){
			namesin = (char **) malloc( 1*sizeof(char*) );
			namesout = (char **) malloc( 1*sizeof(char*) );
		}else{
			namesin = (char **) realloc( namesin, (i+1)*sizeof(char*) );
			namesout = (char **) realloc( namesout, (i+1)*sizeof(char*) );
		}

		namesin[i] = (char *) malloc( PETSC_MAX_PATH_LEN * sizeof(char) );
		namesout[i] = (char *) malloc( PETSC_MAX_PATH_LEN * sizeof(char) );
		sprintf(namesin[i], "%s", name);
		sprintf(namesout[i], "%s", name2);

		i++;
		sprintf(optionin, "-in%i", i);
		sprintf(optionout, "-out%i", i);
	}
	Nm = i;
	PetscOptionsGetInt(PETSC_NULL,"-printnewton", &printnewton,NULL);
	geo = ReadCreateGeometry();
	Mode *ms = ReadModes(geo, namesin, namesout, Nm);

	// hack, so that SALT.jl can access Creeper using just a pointer to modes.
	Creeper(dD, Dmax, thresholdw_tol, ftol, ms, printnewton, Nm, geo);    
	for(ih=0; ih<Nm; ih++){
		Write(ms[ih], geo);
		VecDestroy(&(ms[ih]->vpsi) ); // J and ksp destroyed in Creeper
		free(ms[ih]);
	}
	DestroyGeometry(geo);

	for(i=0; i<Nm; i++){
		free( namesin[i]);
		free( namesout[i]);
	}
	free(namesin);
	free(namesout);
}

int main(int argc, char** argv){ 
	SlepcInitialize(&argc, &argv, PETSC_NULL, PETSC_NULL); 
	double Dmax;
	PetscOptionsGetReal(PETSC_NULL,"-Dmax", &Dmax,NULL); 

	if(Dmax == 0.0){ // Passive
		mainPassive();
	}else{ // Creeper
		mainCreeper(Dmax);
	}

	SlepcFinalize();
	return 0;
}

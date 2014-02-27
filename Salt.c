#include "headers.h"



void Salt(int *N, int *M, double *h, int *Npml, int Nc, int LowerPML, char *epsfile, char *fproffile, double wa, double y,  // <-- Geometry parameters
int BCPeriod, int *bl, double *k, double wreal, double wimag, double modenorm, int nev, char *modeout,  // <--- Passive parameters
double dD, double Dmax, double thresholdw_tol, double ftol, char **namesin, char **namesout, int printnewton, int Nm // <--- Creeper parameters
)
{

	Geometry Geo, *geo = &Geo;

	CreateGeometry(geo, N, M, h, Npml, Nc, LowerPML, epsfile, fproffile, wa, y);	


	if(Dmax == 0.0)
		Passive(BCPeriod, bl, k, wreal, wimag, modenorm, nev, modeout, geo);		
	else
		Creeper(dD, Dmax, thresholdw_tol, ftol, namesin, namesout, printnewton, Nm, geo);		




	DestroyGeometry(geo);


}

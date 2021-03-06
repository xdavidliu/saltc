#include <slepc.h>
#include <math.h>
#include <stdio.h>

#include <sys/time.h>
typedef struct timeval tv;

double dt(tv t1, tv t2);

#include <complex.h>
typedef double complex dcomp;
static const dcomp ComplexI = I;

static const char Output_Suffix[PETSC_MAX_PATH_LEN] = "_file.m";

int GetRank();
int GetSize();
int LastProcess();
void ScatterRange (Vec x, Vec y, int ix, int iy, int N);
void SetLast2(Vec f, double val1, double val2);
double GetFromLast(Vec v, int ir);
void GetLast2(Vec f, double *val1, double *val2);

void CreateVec(int N, Vec *x);
void AssembleVec(Vec x);
void AssembleMat(Mat M);

typedef struct Grid_s{

	int N[3], Nc, Nr;
} Grid;

void CreateGrid(Grid *g, int* M, int Mc, int Mr);

int xyzGrid(Grid *g);
int xyzcGrid(Grid *g);
int xyzcrGrid(Grid *g);

typedef struct Point_s{
	int ix[3], ir, ic;
	Grid G;
} Point;

void CreatePoint_i(Point *p, int i, Grid *H);

int convert(Point *p, int Nc);
int project(Point *p, int Nc);
int projectmedium(Point *p, Grid *gm, int LowerPML);
int xyz(Point *p);
int xyzc(Point *p);
int xyzcr(Point *p);

#define SCRATCHNUM 7
typedef struct Geometry_s{

	int Npml[3], Nc, LowerPML;
	double interference, h[3];
	Vec vepspml;

	Grid gN, gM; // technically could remove gM, but keep it around in case need later

	Vec vscratch[SCRATCHNUM], vMscratch[SCRATCHNUM], vNhscratch[SCRATCHNUM], vH, veps, vIeps, vf, vfM; // keeping vfM saves time with collect and stamp
	double D, wa, y;
} *Geometry;

Geometry CreateGeometry(int N[3], double h[3], int Npml[3], int Nc, int LowerPML, double *eps, double *epsI, double *fprof, double wa, double y);

void SetPump(Geometry geo, double D);
void DestroyGeometry(Geometry geo);
void InterpolateVec(Geometry geo, Vec vM, Vec vN);
void CollectVec(Geometry geo, Vec vN, Vec vM);
void Stamp(Geometry geo, Vec vN, int ic, int ir, Vec scratchM);
void TimesI(Geometry geo, Vec v, Vec Iv);
void VecDotMedium(Geometry geo, Vec v1, Vec v2, Vec w, Vec scratchM);
int Last2(Geometry geo, int i);
void SetJacobian(Geometry geo, Mat J, Vec v, int jc, int jr, int jh);

void MoperatorGeneralBlochFill(Geometry geo, Mat A,  int b[3][2], double k[3], int ih);

int Nxyz(Geometry geo);
int Nxyzc(Geometry geo);
int Nxyzcr(Geometry geo);
int Mxyz(Geometry geo);

int NJ(Geometry geo);
int offset(Geometry geo, int ih);
int ir(Geometry geo, int i);

typedef struct Mode_s{
	Vec vpsi;
	char name[PETSC_MAX_PATH_LEN];
	double k[3];
	int ifix, b[3][2], lasing;
	Mat J;
	KSP ksp; // one ksp per J seems faster
} *Mode;

void ClearMode(Mode m);
void Setup(Mode m, Geometry geo);

void Write(Mode m, const Geometry geo);
double get_c(Mode m);
dcomp get_w(Mode m);
dcomp gamma_w(Mode m, Geometry geo);
void Fix(Mode m, Geometry geo, double norm);

Mode GetMode(Mode *ms, int n);
void addArrayMode(Mode **ma, int old_size, Mode m);

void CreateSquareMatrix(int N, int nz, Mat *A);
double GetValue(Vec v, int i);
void ReadVectorC(FILE *fp, int N, Vec v);

PetscErrorCode MyError(const char* message);
double sqr(const double a);
dcomp csqr(dcomp a);

void Output(Vec A, const char* name, const char* variable_name);
void OutputMat(Mat A, const char* name, const char* variable_name);

void NewtonSolve(Mode *ms, Geometry geo, Vec v, Vec f, Vec dv, double ftol, int printnewton);
void ThresholdSearch(double wimag_lo, double wimag_hi, double D_lo, double D_hi, Mode *msh, Vec vNh, Vec fNh, Vec dvNh, Mode m, Geometry geo, Vec f, Vec dv, double ftol, int printnewton);

double FormJf(Mode *ms, Geometry geo, Vec v, Vec f, double ftol, int printnewton);

typedef struct Vecfun_s{
// always Nxyzcr()+2!

	Vec u;
	int ns, ne; // should keep these, since they are used by the val() function
				// every single time.	 
	double* a;
} Vecfun;

void CreateVecfun(Vecfun *fun, Vec w);

double valr(Vecfun *fun, int i);
void setr(Vecfun *fun, int i, double val);
void DestroyVecfun(Vecfun *fun);

typedef struct Complexfun_s{
	Vec u, v;
	double *a, *b;
	int Nxyzc, ns, ne;
}Complexfun;

dcomp valc(Complexfun *fun, int i);
void setc(Complexfun *fun,int i, dcomp val);

void CreateComplexfun(Complexfun *fun, Vec w, Vec x);
void DestroyComplexfun(Complexfun *fun);

dcomp pmlval(int i, int* N, int* Npml, double* h, int LowerPML, int k);
void AllocateJacobian(Mat J, Geometry geo);
void AddRowDerivatives(Mat J, Geometry geo, int ifix, int ih);

Mode *Passive(int *added, int *bl, double *k, double wreal, double wimag, double modenorm, int nev,  Geometry geo);
int Creeper(double dD, double Dmax, double ftol, Mode *ms, int printnewton, int Nm, Geometry geo);


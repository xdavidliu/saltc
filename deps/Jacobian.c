#include "salt.h"

void VecSetComplex(Vec vR, Vec vI, int i, int ir, dcomp val, InsertMode addv){
	VecSetValue(vR, i, ir? cimag(val) : creal(val), addv );
	VecSetValue(vI, i, ir? creal(val) : -cimag(val), addv );
}

void Isolate(Vec v, Grid *gN, int ic, int ir){
	int ns, ne, i;
	VecGetOwnershipRange(v, &ns, &ne);
	double *a;
	VecGetArray(v, &a);

	for(i=ns; i<ne && i<xyzcrGrid(gN); i++){
		Point p;
		CreatePoint_i(&p, i, gN);
		if(p.ic != ic || p.ir != ir) a[i-ns] = 0.0;
	}
	VecRestoreArray(v, &a);
}

void Stamp(Geometry geo, Vec vN, int ic, int ir, Vec scratchM){
	Isolate(vN, &geo->gN, ic, ir);
	CollectVec(geo, vN, scratchM);
	InterpolateVec(geo, scratchM, vN);
}

void LinearDerivative(Mode m, Geometry geo, Vec dfR, Vec dfI, int ih){
	Complexfun eps;
	CreateComplexfun(&eps, geo->veps, geo->vIeps);
	Vecfun f, H;
	CreateVecfun(&f, geo->vf);
	CreateVecfun(&H, geo->vH);

	dcomp mw = get_w(m), yw = gamma_w(m, geo);

	int i;
	for(i=eps.ns; i<eps.ne; i++){
		dcomp val = csqr(mw) * (valc(&eps, i) + geo->D * yw * valr(&f, i) * valr(&H, i) );
		VecSetComplex(dfR, dfI, i+offset(geo, ih), ir(geo, i), val, INSERT_VALUES);
	}
	DestroyVecfun(&f);
	DestroyVecfun(&H);
	DestroyComplexfun(&eps);
}

double hcross(int ixyzcr, Mode *ms, double w[2], double c[2], Geometry geo){
	// for two modes near degeneracy, Nc = 1, sequential only!
	// pass in w and c for efficiency
	// no need to check fprof == 0 here; assume that's already done before calling this routine

	double psi[2][2]; // [im][ir], so can VecGetValues two at a time
	int im, ixyz = ixyzcr % Nxyz(geo), ind[2] = {ixyz, ixyz+ Nxyzc(geo)};

	for(im=0; im<2; im++)
		VecGetValues(ms[im]->vpsi, 2, ind, psi[im]);

	double G12 = sqr(geo->gampar) / ( sqr(geo->gampar) + sqr(w[1] - w[0]) );
	return 2*geo->G0 * G12 * c[0]*c[1]*( psi[0][0]*psi[1][0] + psi[0][1]*psi[1][1] );
}

void TensorDerivative(Mode m, Mode mj, Geometry geo, int jc, int jr, Vec df, Vec vpsibra, Vec vIpsi, int ih){
	double mjc = get_c(mj);
	dcomp mw = get_w(m), yw = gamma_w(m, geo), yjw = gamma_w(mj, geo);

	Vecfun f, H, psibra;
	CreateVecfun(&f, geo->vf);
	CreateVecfun(&H, geo->vH);
	CreateVecfun(&psibra, vpsibra);

	Complexfun psi;
	CreateComplexfun(&psi, m->vpsi, vIpsi);

	int i;
	for(i=f.ns; i<f.ne; i++){
		if( valr(&f, i) == 0.0) continue;		
		dcomp ket_term = -csqr(mw ) * sqr(mjc) * sqr(cabs(yjw)) * 2.0
			* sqr(valr(&H, i) ) * geo->D * valr(&f, i) * yw * valc(&psi, i);	
		double val = valr(&psibra, i) * (ir(geo, i)? cimag(ket_term) : creal(ket_term) );
	
		VecSetValue(df, i+offset(geo, ih), val, INSERT_VALUES);
	}
	DestroyVecfun(&f);
	DestroyVecfun(&H);
	DestroyVecfun(&psibra);
	DestroyComplexfun(&psi);
}

void TensorDerivativeCross(Mode *ms, Geometry geo, int jr, int jh, Vec df, Vec vIpsi){
// as with all cross routines, only for Nc = 1, Nm = 2, sequential
	// this block same as ColumnDerivativeCross
	AssembleVec(df);
	double w[2], c[2];
	dcomp yw[2];
	int i, ih;
	for(i=0; i<2; i++){
		w[i] = creal( get_w(ms[i]));
		c[i] = get_c(ms[i]);
		yw[i] = gamma_w( ms[i], geo);
	}          
	
	double *dfdpsi, *psijp1;
	VecGetArray(df, &dfdpsi);
	VecGetArray(ms[(jh+1)%2]->vpsi, &psijp1);
	Vecfun f, H;
	CreateVecfun(&f, geo->vf);
	CreateVecfun(&H, geo->vH);
	
	double G12 = sqr(geo->gampar) / ( sqr(geo->gampar) + sqr(w[1] - w[0]) );
	for(ih=0; ih<2; ih++){
		Complexfun psi;
		TimesI(geo, ms[ih]->vpsi, vIpsi);
		CreateComplexfun(&psi,ms[ih]->vpsi, vIpsi);

		for(i=0; i<Nxyz(geo); i++){
			dcomp ksqDHsq_ywpsi = sqr(w[ih])*geo->D* sqr(valr(&H, i) )
				* yw[ih] * valc(&psi, i);
			dcomp dfdpsi_cross = ksqDHsq_ywpsi * 2*geo->G0*G12 * c[0]*c[1]*psijp1[i+jr*Nxyz(geo)]; 
			dfdpsi[i + ih*NJ(geo)] += creal(dfdpsi_cross);
			dfdpsi[i + ih*NJ(geo) + Nxyz(geo)] += cimag(dfdpsi_cross);
		}
		DestroyComplexfun(&psi);
	}

	DestroyVecfun(&H);
	DestroyVecfun(&f);
	VecRestoreArray(df, &dfdpsi);
	VecRestoreArray(ms[(jh+1)%2]->vpsi, &psijp1);
}

void ColumnDerivative(Mode m, Mode mj, Geometry geo, Vec dfR, Vec dfI, Vec vIpsi, Vec vpsisq, int ih){
	// vIpsi is for m, vpsisq is for mj
	// use pointers so can check whether ih = jh

	// purposely don't set df = 0 here to allow multiple ih's
	double mjc = get_c(mj);
	dcomp mw = get_w(m), yw = gamma_w(m, geo),
			mjw = get_w(mj), yjw = gamma_w(mj, geo);

	Complexfun psi, eps;
	CreateComplexfun(&psi,m->vpsi, vIpsi);
	CreateComplexfun(&eps,geo->veps, geo->vIeps);

	Vecfun f,H, psisq;
	CreateVecfun(&f, geo->vf);
	CreateVecfun(&H, geo->vH);
	CreateVecfun(&psisq, vpsisq);

	int i;
	for(i=psi.ns; i<psi.ne; i++){
		dcomp dfdk = 0.0, dfdc = 0.0, 
			DfywHpsi = geo->D * valr(&f, i) * yw * valr(&H, i) * valc(&psi, i);

		if(m == mj)
			dfdk += ( -csqr(mw)*yw / geo->y +2.0*mw ) * DfywHpsi + 2.0*mw* valc(&eps, i)*valc(&psi, i);
		// note: adding dcomp to a double ignores the imaginary part

		if(m->lasing && valr(&f, i) != 0.0){
			dcomp dHdk_term = -sqr(mjc) * -2.0*(mjw-geo->wa)
			 /sqr(geo->y) * sqr(sqr(cabs(yjw)));
			dHdk_term *= csqr(mw)*DfywHpsi * valr(&H, i) * valr(&psisq, i);
			dfdk += dHdk_term;
			dfdc = csqr(mw) * DfywHpsi * valr(&H, i);
			dfdc *= (-2.0*mjc)*sqr(cabs(yjw)) * valr(&psisq, i);
		}
	
		if( !m->lasing)
			VecSetComplex(dfR, dfI, i+offset(geo, ih), ir(geo, i), dfdk, INSERT_VALUES);
		else{
			VecSetValue(dfR, i+offset(geo, ih), ir(geo, i)? cimag(dfdk) : creal(dfdk), INSERT_VALUES );
			VecSetValue(dfI, i+offset(geo, ih), ir(geo, i)? cimag(dfdc) : creal(dfdc), INSERT_VALUES );
		}
	}

	DestroyComplexfun(&eps);
	DestroyComplexfun(&psi);
	DestroyVecfun(&f);
	DestroyVecfun(&H);
	DestroyVecfun(&psisq);
}

void ColumnDerivativeCross(Vec dfR, Vec dfI, Vec vIpsi, Mode *ms, Geometry geo){
// for two modes near degeneracy, Nc = 1, sequential only!
// cross term; can't put this in ColumnDerivative because need both w[2] and c[2]

	AssembleVec(dfR); AssembleVec(dfI);
	double w[2], c[2];
	dcomp yw[2];
	int i, ih;
	for(i=0; i<2; i++){
		w[i] = creal( get_w(ms[i]));
		c[i] = get_c(ms[i]);
		yw[i] = gamma_w( ms[i], geo);
	}          

	double G12 = sqr(geo->gampar) / ( sqr(geo->gampar) + sqr(w[1] - w[0]) ),
		*dfdk, *dfdc;
	VecGetArray(dfR, &dfdk);
	VecGetArray(dfI, &dfdc);
	for(ih=0; ih<2; ih++){

		Vecfun f, H;
		CreateVecfun(&f, geo->vf);
		CreateVecfun(&H, geo->vH);
		Complexfun psi;
		TimesI(geo, ms[ih]->vpsi, vIpsi);
		CreateComplexfun(&psi,ms[ih]->vpsi, vIpsi);

		for(i=0; i<Nxyzc(geo); i++){ // sequential only
			if( valr(&f, i) == 0) continue;

			dcomp ksqDHsqhcross_ywpsi = sqr(w[ih])*geo->D * sqr(valr(&H, i))
				* hcross(i, ms, w, c, geo) * yw[ih] * valc(&psi, i);
			dcomp dfdk_cross = 2.0 * ksqDHsqhcross_ywpsi * 
					(w[ih] - w[(ih+1)%2])/sqr(geo->gampar) * G12, 
				dfdc_cross = -2.0 * ksqDHsqhcross_ywpsi / c[ih];

			dfdk[ih*NJ(geo) + i] += creal( dfdk_cross); 
			dfdk[ih*NJ(geo) + i + Nxyzc(geo)] += cimag( dfdk_cross);
			dfdc[ih*NJ(geo) + i] += creal( dfdc_cross); 
			dfdc[ih*NJ(geo) + i + Nxyzc(geo)] += cimag( dfdc_cross);
			// same as VecSetValue in ColumnDerivative
		}
		DestroyVecfun(&H);
		DestroyVecfun(&f);
		DestroyComplexfun(&psi);
	}
	VecRestoreArray(dfR, &dfdk);
	VecRestoreArray(dfI, &dfdc);
}

void ComputeGain(Geometry geo, Mode *ms, int Nh){
	// TODO: skip points where fprof[i] = 0, saves some time	

	VecSet(geo->vH, 0.0);
	Vecfun H;
	CreateVecfun(&H, geo->vH);
	int i, ih;
	for(ih=0; ih<Nh; ih++){
		Mode m = ms[ih];
		dcomp yw = gamma_w(m, geo);
		double mc = get_c(m);

		// do not change this from vscratch[3], or the hack below for single mode Column derivative will fail!
		VecSqMedium(geo, m->vpsi, geo->vscratch[3], geo->vMscratch[0]);

		Vecfun psisq;
		CreateVecfun(&psisq ,geo->vscratch[3]);
		for(i=H.ns; i<H.ne; i++)
			setr(&H, i, valr(&H, i) + sqr(cabs(yw)) *sqr(mc) * valr(&psisq, i) ) ;
	}

	if(Nh == 2 && GetSize()==1 && geo->Nc==1 && geo->gampar > 0.0){ // cross term
		double w[2], c[2];
		for(i=0; i<2; i++){
			w[i] = creal( get_w(ms[i]));
			c[i] = get_c(ms[i]);
		}		

		for(i=H.ns; i<H.ne; i++)
			setr(&H, i, valr(&H, i) + hcross(i%Nxyz(geo), ms, w, c, geo) );
	}
	
	for(i=H.ns; i<H.ne; i++)
		setr(&H, i, 1.0 / (1.0 + valr(&H, i) ) );
	DestroyVecfun(&H);
}

double FormJf(Mode* ms, Geometry geo, Vec v, Vec f, double ftol, int printnewton){
	Mode m = ms[0];
	int lasing = m->lasing, Nm;
	VecGetSize(v, &Nm); Nm /= NJ(geo);
	Mat J = m->J; // for multimode, all m share same J

	if(lasing)
		ComputeGain(geo, ms, Nm); // do this before naming scratch vectors!
	// ================== name scratch vectors ================== //
	Vec vpsisq = geo->vscratch[3], // only form this later if needed
		vIpsi = geo->vscratch[2];

	Vec dfR, dfI;
	if(Nm == 1){
		dfR = geo->vscratch[0];
		dfI = geo->vscratch[1];
	}else{
		dfR = geo->vNhscratch[0];
		dfI = geo->vNhscratch[1];
	}

	// =========== linear J to compute residual ========= //
	MatRetrieveValues(J);

	int ih, jh, kh, ir, jr, jc;
	for(ih=0; ih<Nm; ih++){
		m = ms[ih];
		VecSet(dfR, 0.0);	
		VecSet(dfI, 0.0);
		LinearDerivative(m, geo, dfR, dfI, ih);
	  	SetJacobian(geo, J, dfR, -2, 0, ih);
		SetJacobian(geo, J, dfI, -2, 1, ih); 
	}

	// row derivatives already added in add placeholders!

	AssembleMat(J);
	MatMult(J, v, f);
	for(kh = 0; kh<Nm; kh++) for(ir=0; ir<2; ir++)
		VecSetValue(f, kh*NJ(geo) + Nxyzcr(geo)+ir, 0.0, INSERT_VALUES);

	AssembleVec(f);
	double fnorm;
	VecNorm(f, NORM_2, &fnorm);

	if( printnewton ) PetscPrintf(PETSC_COMM_WORLD, "|f| = %1.6e;", fnorm);
	// no \n here to make room for timing printf statement immediately afterwards

	if(Nm==2) //DEBUG
		PetscPrintf(PETSC_COMM_WORLD, " DEBUG: c = (%g, %g)", get_c(ms[0]), get_c(ms[1]) );

	if(fnorm < ftol )
		return fnorm;   		// TODO: deleted old integral routine. Write new one here.

	// =============== column derivatives ====================

	for(jh=0; jh<Nm; jh++){
		Mode mj = ms[jh];

		VecSet(dfR, 0.0);
		VecSet(dfI, 0.0);

		if(Nm > 1)  // hack: only recompute vpsisq if ComputeGain didn't already do it, i.e. for multimode
			VecSqMedium(geo, mj->vpsi, vpsisq, geo->vMscratch[0]);

		for(ih=0; ih<Nm; ih++){
			Mode mi = ms[ih];
			TimesI(geo, mi->vpsi, vIpsi);
			ColumnDerivative(mi, mj, geo, dfR, dfI, vIpsi, vpsisq, ih);
		}

		if(Nm == 2 && GetSize()==1 && geo->Nc==1 && geo->gampar > 0.0){
			ColumnDerivativeCross(dfR, dfI, vIpsi, ms, geo);
		}

		SetJacobian(geo, J, dfR, -1, 0, jh);
		SetJacobian(geo, J, dfI, -1, 1, jh);
	}

	//================ tensor derivatives ================

	if(lasing){
		Vec vpsibra = vpsisq; vpsisq = 0;

		for(jh=0; jh<Nm; jh++){
			Mode mj = ms[jh];

			for(jr=0; jr<2; jr++) for(jc=0; jc< geo->gN.Nc; jc++){
				VecCopy(mj->vpsi, vpsibra);
				Stamp(geo, vpsibra, jc, jr, geo->vMscratch[0]);

				VecSet(dfR, 0.0);
				ih = 0;
				for(ih=0; ih<Nm; ih++){
					Mode mi = ms[ih];
					TimesI(geo, mi->vpsi, vIpsi);
					TensorDerivative(mi, mj, geo, jc, jr, dfR, vpsibra, vIpsi, ih);
				}
				SetJacobian(geo, J, dfR, jc, jr, jh);
			}
		}
	}
	AssembleMat(J);
	return fnorm;
}

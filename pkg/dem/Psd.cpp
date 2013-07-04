#include<woo/pkg/dem/Psd.hpp>
#include<woo/pkg/dem/Funcs.hpp>
#include<woo/pkg/dem/Sphere.hpp>


WOO_PLUGIN(dem,(PsdSphereGenerator));
CREATE_LOGGER(PsdSphereGenerator);

void PsdSphereGenerator::postLoad(PsdSphereGenerator&,void*){
	if(psdPts.empty()) return;
	for(int i=0; i<(int)psdPts.size()-1; i++){
		if(psdPts[i][0]<0 || psdPts[i][1]<0) throw std::runtime_error("PsdSphereGenerator.psdPts: negative values not allowed.");
		if(psdPts[i][0]>psdPts[i+1][0]) throw std::runtime_error("PsdSphereGenerator.psdPts: diameters (the x-component) must be increasing ("+to_string(psdPts[i][0])+">="+to_string(psdPts[i+1][0])+")");
		if(psdPts[i][1]>psdPts[i+1][1]) throw std::runtime_error("PsdSphereGenerator.psdPts: passing values (the y-component) must be increasing ("+to_string(psdPts[i][1])+">"+to_string(psdPts[i+1][1])+")");
	}
	Real maxPass=psdPts.back()[1];
	if(maxPass!=1.0){
		LOG_INFO("Normalizing psdPts so that highest value of passing is 1.0 rather than "<<maxPass);
		for(Vector2r& v: psdPts) v[1]/=maxPass;
	}
	weightPerBin.resize(psdPts.size());
	std::fill(weightPerBin.begin(),weightPerBin.end(),0);
	weightTotal=0;
}

/* find radius for the next particle to be generated, such that the PSD is satisfied;
	return radius and *bin*; bin is used internally, and should be passed to the saveBinMassRadius
	afterwards, which does bookkeeping about particle which have been generated already.
 */
std::tuple<Real,int> PsdSphereGenerator::computeNextRadiusBin(){
	// some common checks here
	if(psdPts.empty()) throw std::runtime_error("PsdSphereGenerator.psdPts is empty.");

	// compute now
	Real r;
	Real maxBinDiff=-Inf; int maxBin=-1;
	// find the bin which is the most below the expected percentage according to the psdPts
	// we use mass or number automatically: weightTotal and weightPerBin accumulates number or mass
	if(weightTotal<=0) maxBin=0;
	else{
		for(size_t i=0;i<psdPts.size();i++){
			Real binDesired=psdPts[i][1]-(i>0?psdPts[i-1][1]:0.);
			Real binDiff=binDesired-weightPerBin[i]*1./weightTotal;
			LOG_TRACE("bin "<<i<<" (d="<<psdPts[i][0]<<"): should be "<<binDesired<<", current "<<weightPerBin[i]*1./weightTotal<<", diff="<<binDiff);
			if(binDiff>maxBinDiff){ maxBinDiff=binDiff; maxBin=i; }
		}
	}
	LOG_TRACE("** maxBin="<<maxBin<<", d="<<psdPts[maxBin][0]);
	assert(maxBin>=0);

	if(discrete){
		// just pick the radius found
		r=psdPts[maxBin][0]/2.; 
	} else {
		// here we are between the maxBin and the previous one (interpolate between them, add to maxBin anyways)
		if(maxBin==0){ r=psdPts[0][0]/2.; }
		else{
			Real a=psdPts[maxBin-1][0]/2., b=psdPts[maxBin][0]/2.;
			Real rnd=Mathr::UnitRandom();
			// size grows linearly with size, mass with 3rd power; therefore 2nd-order power-law for mass
			// interpolate on inverse-power-law
			// r=(a^-2+rnd*(b^-2-a^-2))^(-1/2.)
			if(mass) r=sqrt(1/(1/(a*a)+rnd*(1/(b*b)-1/(a*a))));
			// interpolate linearly
			else r=a+rnd*(b-a);
			LOG_TRACE((mass?"Power-series":"Linear")<<" interpolation "<<a<<".."<<b<<" @ "<<rnd<<" = "<<r)
		}
	}
	assert(r>=.5*psdPts[0][0]);
	return std::make_tuple(r,maxBin);
}

/*
	Save information about generated particle; *bin* is the value returned by computeNextRadiusBin.
   This function saves real mass if mass-based PSD is used, otherwise unit mass is used.
	genDiamMass holds true mass, however, in any case.
*/
void PsdSphereGenerator::saveBinMassRadius(int bin, Real m, Real r){
	weightPerBin[bin]+=(mass?m:1.);
	weightTotal+=(mass?m:1.);
	lastM=(mass?m:1.);
	lastBin=bin;
	if(save) genDiamMass.push_back(Vector2r(2*r,m));
}

void PsdSphereGenerator::revokeLast(){
	weightPerBin[lastBin]-=lastM;
	weightTotal-=lastM;
	ParticleGenerator::revokeLast(); // removes from genDiamMass, if needed
}

vector<ParticleGenerator::ParticleAndBox>
PsdSphereGenerator::operator()(const shared_ptr<Material>&mat){
	if(mass && !(mat->density>0)) woo::ValueError("PsdSphereGenerator: material density must be positive (not "+to_string(mat->density));
	int bin; Real r;
	std::tie(r,bin)=computeNextRadiusBin();

	shared_ptr<Particle> sphere;
	sphere=DemFuncs::makeSphere(r,mat);
	Real m=sphere->shape->nodes[0]->getData<DemData>().mass;

	saveBinMassRadius(bin,m,r);

	return vector<ParticleAndBox>({{sphere,AlignedBox3r(Vector3r(-r,-r,-r),Vector3r(r,r,r))}});
};

py::tuple PsdSphereGenerator::pyInputPsd(bool scale, bool cumulative, int num) const {
	Real factor=1.; // no scaling at all
	if(scale){
		if(mass) for(const auto& vv: genDiamMass) factor+=vv[1]; // scale by total mass of all generated particles
		else factor=genDiamMass.size(); //  scale by number of particles
	}
	py::list dia, frac; // diameter and fraction axes
	if(cumulative){
		if(psdPts[0][1]>0.){ dia.append(psdPts[0][0]); frac.append(0); }
		for(size_t i=0;i<psdPts.size();i++){
			dia.append(psdPts[i][0]); frac.append(psdPts[i][1]*factor);
			if(discrete && i<psdPts.size()-1){ dia.append(psdPts[i+1][0]); frac.append(psdPts[i][1]*factor); }
		}
	} else {
		if(discrete){
			// points shown as bars with relative width given in *num*
			Real wd=(psdPts.back()[0]-psdPts.front()[0])/num;
			for(size_t i=0;i<psdPts.size();i++){
				Vector2r offset=(i==0?Vector2r(0,wd):(i==psdPts.size()-1?Vector2r(-wd,0):Vector2r(-wd/2.,wd/2.)));
				Vector2r xx=psdPts[i][0]*Vector2r::Ones()+offset;
				Real y=factor*(psdPts[i][1]-(i==0?0.:psdPts[i-1][1]));
				dia.append(xx[0]); frac.append(0.);
				dia.append(xx[0]); frac.append(y);
				dia.append(xx[1]); frac.append(y);
				dia.append(xx[1]); frac.append(0.);
			}
		}
		else{
			dia.append(psdPts[0][0]); frac.append(0);
			Real xSpan=(psdPts.back()[0]-psdPts[0][0]);
			for(size_t i=0;i<psdPts.size()-1;i++){
				Real dx=(psdPts[i+1][0]-psdPts[i][0]);
				Real y=factor*(psdPts[i+1][1]-psdPts[i][1])*1./(num*dx/xSpan);
				dia.append(psdPts[i][0]); frac.append(y);
				dia.append(psdPts[i+1][0]); frac.append(y);
			}
			dia.append(psdPts.back()[0]); frac.append(0.);
		}
	}
	return py::make_tuple(dia,frac);
}


/**********************************************
              PsdClumpGenerator
**********************************************/

WOO_PLUGIN(dem,(PsdClumpGenerator));
CREATE_LOGGER(PsdClumpGenerator);


vector<ParticleGenerator::ParticleAndBox>
PsdClumpGenerator::operator()(const shared_ptr<Material>&mat){
	if(mass && !(mat->density>0)) woo::ValueError("PsdClumpGenerator: material density must be positive (not "+to_string(mat->density));
	if(clumps.empty()) woo::ValueError("PsdClumpGenerator.clump may not be empty.");
	int bin; Real r;
	std::tie(r,bin)=computeNextRadiusBin();
	LOG_TRACE("Next radius is "<<r);
	vector<Real> prob(clumps.size()); Real probSum=0;
	// sum probabilities of all clumps, then pick one based on probability
	
	// sum probabilities, with interpolation for each clump
	for(size_t i=0; i<clumps.size(); i++){
		if(!clumps[i]) woo::ValueError("PsdCLumpGenerator.clumps["+to_string(i)+"] is None.");
		const SphereClumpGeom& C(*clumps[i]);
		const auto& pf(C.scaleProb); // probability function
		if(!pf.empty()){
			auto I=std::lower_bound(pf.begin(),pf.end(),r,[](const Vector2r& rProb, const Real& r)->bool{ return rProb[0]<r; });
			// before the first point: return the first value
			if(I==pf.begin()){ prob[i]=(*pf.begin())[1]; LOG_TRACE("SphereClumpGeom #"<<i<<": lower_bound("<<r<<") found at 0: rad="<<pf[0][0]<<", prob="<<pf[0][1]); }
			// beyond the last point: return the last value
			else if(I==pf.end()){ prob[i]=(*pf.rbegin())[1]; LOG_TRACE("SphereClumpGeom #"<<i<<": lower_bound("<<r<<") found beyond end: last rad="<<(*pf.rbegin())[0]<<", last prob="<<(*pf.rbegin())[1]); }
			// somewhere in the middle; interpolate linearly
			else {
				const Vector2r& A=pf[I-pf.begin()-1]; const Vector2r& B=*I;
				prob[i]=A[1]+(B[1]-A[1])*((r-A[0])/(B[0]-A[0]));
				LOG_TRACE("SphereClumpGeom #"<<i<<": lower_bound("<<r<<") found at index "<<(I-pf.begin())<<", interpolate rad="<<A[0]<<"…"<<A[1]<<", prob="<<A[1]<<"…"<<B[1]<<" → "<<prob[i]);
			}
		} else { prob[i]=1.; }
		probSum+=prob[i];
	}
	LOG_TRACE("Probability sum "<<probSum);
	if(probSum==0.) throw std::runtime_error("PsdClumpGenerator: no clump had non-zero probability for radius "+to_string(r)+" (check PsdSphereGenerator.pstPts and SphereClumpGeom.scaleProb).");

	// pick randomly any candidate clump in that point, based on the probability function:
	// pick random number in 0…probSum and iterate until we get to our clump
	Real randSum=Mathr::UnitRandom()*probSum;
	size_t cNo; Real cSum=0.;
	for(cNo=0; cNo<clumps.size(); cNo++){
		cSum+=prob[cNo];
		if(cSum>=randSum) break;
		if(cNo==clumps.size()-1){
			LOG_FATAL("Random clump not selected in the loop?! randSum="<<randSum<<", cSum="<<cSum<<", probSum="<<probSum<<", size="<<clumps.size());
			throw std::logic_error("Random clump not selected?! (information above)");
		}
	}
	LOG_DEBUG("Chose clump #"<<cNo<<" for radius "<<r<<" (cumulative prob "<<cSum<<", random "<<randSum<<", total prob "<<probSum<<")");
	assert(cNo>=0 && cNo<clumps.size());

	// construct the clump based on its defintion
	const auto& C(*(clumps[cNo]));
	assert(C.equivRad>0);
	Real scale=r/C.equivRad;
	LOG_DEBUG("Clump will be scaled "<<scale<<"× so that its equivRad "<<C.equivRad<<" becomes "<<r);

	/* make individual spheres; the clump itself is constructed in RandomFactory (using Clump::makeClump)
	   that means that we don't know what the exact mass is here :|
	   perhaps the interface will need to be changed to take that into account properly
	   for now, suppose sphere never intersect (which is what Clump::makeClump supposes as well)
	   and sum masses of constituents
	*/
	vector<ParticleAndBox> ret(C.centers.size());
	Real mass=0.;
	Quaternionr ori(AngleAxisr(Mathr::UnitRandom()*2*M_PI,Vector3r::Random().normalized()));
	if(ori.norm()>0) ori.normalize();
	else ori=Quaternionr::Identity(); // very unlikely that q has all zeros
	for(size_t i=0; i<C.centers.size(); i++){
		shared_ptr<Particle> sphere=DemFuncs::makeSphere(C.radii[i]*scale,mat);
		Vector3r center=ori*(C.centers[i]*scale);
		Vector3r halfSize=(C.radii[i]*scale)*Vector3r::Ones();
		// the sphere is zero-centered by default, but now we need to change that a bit
		sphere->shape->nodes[0]->pos=center;
		ret[i]=ParticleAndBox{sphere,AlignedBox3r(center-halfSize,center+halfSize)};
		mass+=sphere->shape->nodes[0]->getData<DemData>().mass;
	}
	saveBinMassRadius(bin,mass,r);
	if(save) genClumpNo.push_back(cNo);
	return ret;
}

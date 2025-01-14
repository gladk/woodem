#include<woo/pkg/dem/Clump.hpp>
#include<woo/pkg/dem/Funcs.hpp>
#include<woo/lib/base/Volumetric.hpp>

WOO_PLUGIN(dem,(ClumpData)(SphereClumpGeom));

WOO_IMPL__CLASS_BASE_DOC_ATTRS_PY(woo_dem_SphereClumpGeom__CLASS_BASE_DOC_ATTRS_PY);
WOO_IMPL__CLASS_BASE_DOC_ATTRS_PY(woo_dem_ClumpData__CLASS_BASE_DOC_ATTRS_PY);

WOO_IMPL_LOGGER(ClumpData);
WOO_IMPL_LOGGER(SphereClumpGeom);

void SphereClumpGeom::postLoad(SphereClumpGeom&,void* attr){
	if(attr==NULL){
		if(centers.size()!=radii.size()) throw std::runtime_error("SphereClumpGeom: centers and radii must have the same length (len(centers)="+to_string(centers.size())+", len(radii)="+to_string(radii.size())+").");
		// valid data, and volume has not been computed yet
		if(!centers.empty() && isnan(volume)) recompute(div);
	} else {
		// centers/radii/div changed, try to recompute
		recompute(div,/*failOk*/true,/*fastOnly*/true);
	}
}

vector<shared_ptr<SphereClumpGeom>> SphereClumpGeom::fromSpherePack(const shared_ptr<SpherePack>& sp, int div){
	std::map<int,std::list<int>> cIx; // map clump to all its spheres
	size_t N=sp->pack.size();
	int noClump=-1;
	for(size_t i=0; i<N; i++){
		if(sp->pack[i].clumpId<0) cIx[noClump--].push_back(i); // standalone spheres, get negative "ids"
		else cIx[sp->pack[i].clumpId].push_back(i);
	}
	vector<shared_ptr<SphereClumpGeom>> ret;
	ret.reserve(cIx.size());
	// store iterators in flat array so that the loop can be parallelized
	vector<std::map<int,std::list<int>>::iterator> cIxIter; cIxIter.reserve(cIx.size());
	for(std::map<int,std::list<int>>::iterator I=cIx.begin(); I!=cIx.end(); ++I) cIxIter.push_back(I);
	#ifdef WOO_OPENMP
		#pragma omp parallel for schedule(guided)
	#endif
	for(size_t i=0; i<cIxIter.size(); i++){
		const auto& ci(*cIxIter[i]);
		auto cg=make_shared<SphereClumpGeom>();
		cg->div=div;
		cg->centers.clear(); cg->centers.reserve(ci.second.size());
		cg->radii.clear();   cg->radii.reserve(ci.second.size());
		for(const int& i: ci.second){
			cg->centers.push_back(sp->pack[i].c);
			cg->radii.push_back(sp->pack[i].r);
		}
		assert(cg->centers.size()==ci.second.size()); assert(cg->radii.size()==ci.second.size());
		cg->recompute(div);
		assert(cg->isOk());
		ret.push_back(cg);
	}
	return ret;
}

#if 0
void SphereClumpGeom::ensureApproxPos(){
	if(isOk()) return;
	pos=Vector3r::Zero();
	for(const auto& c: centers): pos+=c;
	c/=centers.size();
}
#endif

void SphereClumpGeom::translate(const Vector3r& offset){
	for(auto& c: centers) c+=offset;
}

shared_ptr<ShapeClump> SphereClumpGeom::copy() const {
	auto ret=make_shared<SphereClumpGeom>();
	ret->centers=centers;
	ret->radii=radii;
	return ret;
}


void SphereClumpGeom::recompute(int _div, bool failOk, bool fastOnly){
	if((centers.empty() && radii.empty()) || centers.size()!=radii.size()){
		if(failOk) { makeInvalid(); return;}
		throw std::runtime_error("SphereClumpGeom.recompute: centers and radii must have the same length (len(centers)="+to_string(centers.size())+", len(radii)="+to_string(radii.size())+"), and may not be empty.");
	}
	div=_div;
	// one single sphere: simple
	if(centers.size()==1){
		pos=centers[0];
		ori=Quaternionr::Identity();
		volume=(4/3.)*M_PI*pow(radii[0],3);
		inertia=Vector3r::Constant((2/5.)*volume*pow(radii[0],2));
		equivRad=radii[0];
		return;
	}
	volume=0;
	Vector3r Sg=Vector3r::Zero();
	Matrix3r Ig=Matrix3r::Zero();
	if(_div<=0){
		// non-intersecting: Steiner's theorem
		for(size_t i=0; i<centers.size(); i++){
			const Real& r(radii[i]); const Vector3r& x(centers[i]);
			Real v=(4/3.)*M_PI*pow(r,3);
			volume+=v;
			Sg+=v*x;
			Ig+=woo::Volumetric::inertiaTensorTranslate(Vector3r::Constant((2/5.)*v*pow(r,2)).asDiagonal(),v,-1.*x);
		}
	} else {
		// intersecting: grid sampling
		Real rMin=Inf; AlignedBox3r aabb;
		for(size_t i=0; i<centers.size(); i++){
			aabb.extend(centers[i]+Vector3r::Constant(radii[i]));
			aabb.extend(centers[i]-Vector3r::Constant(radii[i]));
			rMin=min(rMin,radii[i]);
		}
		if(rMin<=0){
			if(failOk){ makeInvalid(); return; }
			throw std::runtime_error("SphereClumpGeom.recompute: minimum radius must be positive (not "+to_string(rMin)+")");
		}
		Real dx=rMin/_div; Real dv=pow(dx,3);
		long nCellsApprox=(aabb.sizes()/dx).prod();
		 // don't compute anything, it would take too long
		if(fastOnly && nCellsApprox>1e5){ makeInvalid(); return; }
		if(nCellsApprox>1e8) LOG_WARN("SphereClumpGeom: space grid has "<<nCellsApprox<<" cells, computing inertia can take a long time.");
		Vector3r x;
		for(x.x()=aabb.min().x()+dx/2.; x.x()<aabb.max().x(); x.x()+=dx){
			for(x.y()=aabb.min().y()+dx/2.; x.y()<aabb.max().y(); x.y()+=dx){
				for(x.z()=aabb.min().z()+dx/2.; x.z()<aabb.max().z(); x.z()+=dx){
					for(size_t i=0; i<centers.size(); i++){
						if((x-centers[i]).squaredNorm()<pow(radii[i],2)){
							volume+=dv;
							Sg+=dv*x;
							Ig+=dv*(x.dot(x)*Matrix3r::Identity()-x*x.transpose())+/*along princial axes of dv; perhaps negligible?*/Matrix3r(Vector3r::Constant(dv*pow(dx,2)/6.).asDiagonal());
							break;
						}
					}
				}
			}
		}
	}
	woo::Volumetric::computePrincipalAxes(volume,Sg,Ig,pos,ori,inertia);
	equivRad=(inertia.array()/volume).sqrt().mean(); // mean of radii of gyration
}

std::tuple<vector<shared_ptr<Node>>,vector<shared_ptr<Particle>>> SphereClumpGeom::makeParticles(const shared_ptr<Material>& mat, const Vector3r& clumpPos, const Quaternionr& clumpOri, int mask, Real scale){
	ensureOk();
	assert(centers.size()==radii.size());
	const auto N=centers.size();
	if(N==1){ // fast path for a single sphere (do not clump at all)
		auto s=DemFuncs::makeSphere(radii[0]*scale,mat);
		s->mask=mask;
		s->shape->nodes[0]->pos=(isnan(clumpPos.maxCoeff())?centers[0]:clumpPos); // natural or forced position
		return std::make_tuple(vector<shared_ptr<Node>>({s->shape->nodes[0]}),vector<shared_ptr<Particle>>({s}));
	}
	vector<shared_ptr<Particle>> par(N);
	auto n=make_shared<Node>();
	auto cd=make_shared<ClumpData>();
	n->setData<DemData>(cd);
	n->pos=(isnan(clumpPos.maxCoeff())?pos:clumpPos);
	n->ori=clumpOri;
	cd->nodes.resize(N);
	cd->relPos.resize(N);
	cd->relOri.resize(N);
	for(size_t i=0; i<N; i++){
		par[i]=DemFuncs::makeSphere(radii[i]*scale,mat);
		par[i]->mask=mask;
		cd->nodes[i]=par[i]->shape->nodes[0];
		cd->nodes[i]->getData<DemData>().setClumped(n); // sets flag and assigned master node
		cd->relPos[i]=(centers[i]-pos)*scale;
		cd->relOri[i]=ori.conjugate(); // nice to set, but not really important
	}
	// sets particles in global space based on relPos, relOri
	ClumpData::applyToMembers(n);
	// set clump properties
	assert(!isnan(volume));
	cd->setClump(); assert(cd->isClump());
	// scale = length scale (but not density scale)
	cd->mass=mat->density*volume*pow(scale,3);
	cd->inertia=mat->density*inertia*pow(scale,5);
	cd->equivRad=equivRad;
	return std::make_tuple(vector<shared_ptr<Node>>({n}),par);
}




shared_ptr<Node> ClumpData::makeClump(const vector<shared_ptr<Node>>& nn, shared_ptr<Node> centralNode, bool intersecting){
	if(nn.empty()) throw std::runtime_error("ClumpData::makeClump: 0 nodes.");
	/* TODO? check that nodes are unique */
	shared_ptr<ClumpData> clump;
	auto cNode=(centralNode?centralNode:make_shared<Node>());
	if(!centralNode || !centralNode->hasData<DemData>()){
		clump=make_shared<ClumpData>();
		clump->setClump();
		cNode->setData<DemData>(clump);
	}else{
		if(!centralNode->getData<DemData>().isA<ClumpData>()) throw std::runtime_error("ClumpData::makeClump: centralNode has DemData attached, but must have a ClumpData attached instead.");
		clump=static_pointer_cast<ClumpData>(centralNode->getDataPtr<DemData>());
		clump->setClump();
	}

	size_t N=nn.size();
	if(N==1){
		LOG_DEBUG("Treating 1-clump specially.");
		cNode->pos=nn[0]->pos;
		cNode->ori=nn[0]->ori;
		clump->nodes.push_back(nn[0]);
		auto& d0=nn[0]->getData<DemData>();
		d0.setClumped(cNode);
		clump->relPos.push_back(Vector3r::Zero());
		clump->relOri.push_back(Quaternionr::Identity());
		clump->mass=d0.mass;
		clump->inertia=d0.inertia;
		clump->equivRad=(clump->inertia.array()/clump->mass).sqrt().mean();
		return cNode;
	}

	/* clumps with more than one particle */

	if(intersecting) woo::NotImplementedError("Self-intersecting clumps not yet implemented; use SphereClumpGeom instead.");
	double M=0; // mass
	Vector3r Sg=Vector3r::Zero(); // static moment, for getting clump's centroid
	Matrix3r Ig=Matrix3r::Zero(); // tensors of inertia; is upper triangular
	// first loop: compute clump's centroid and principal orientation
	for(const auto& n: nn){
		const auto& dem=n->getData<DemData>();
		if(dem.isClumped()) woo::RuntimeError("Node "+lexical_cast<string>(n)+": already clumped.");
		if(dem.parRef.empty()) woo::RuntimeError("Node "+lexical_cast<string>(n)+": back-references (demData.parRef) empty (Node does not belong to any particle)");
		M+=dem.mass;
		Sg+=dem.mass*n->pos;
		Ig+=woo::Volumetric::inertiaTensorTranslate(woo::Volumetric::inertiaTensorRotate(dem.inertia.asDiagonal(),n->ori.conjugate()),dem.mass,-1.*n->pos);
	}
	if(M>0){
		woo::Volumetric::computePrincipalAxes(M,Sg,Ig,cNode->pos,cNode->ori,clump->inertia);
		clump->mass=M;
		clump->equivRad=(clump->inertia.array()/clump->mass).sqrt().mean();
	} else {
		// nodes have no mass; in that case, centralNode is used
		if(!centralNode) throw std::runtime_error("ClumpData::makeClump: no nodes with mass, therefore centralNode must be given, of which position will be used instead");
		clump->equivRad=NaN;
		// do not touch clump->mass or clump->inertia here
		// if centralNode was given, it is user-provided
	}
	// block massless nodes (unless node was given by the user)
	if(!centralNode && !(clump->mass>0)) clump->setBlockedAll();

	clump->nodes.reserve(N); clump->relPos.reserve(N); clump->relOri.reserve(N);
	for(size_t i=0; i<N; i++){
		const auto& n=nn[i];
		auto& dem=n->getData<DemData>();
		clump->nodes. push_back(n);
		clump->relPos.push_back(cNode->ori.conjugate()*(n->pos-cNode->pos));
		clump->relOri.push_back(cNode->ori.conjugate()*n->ori);
		#ifdef WOO_DEBUG
			AngleAxisr aa(*(clump->relOri.rbegin()));
		#endif
		LOG_TRACE("relPos="<<clump->relPos.rbegin()->transpose()<<", relOri="<<aa.axis()<<":"<<aa.angle());
		dem.setClumped(cNode);
	}
	return cNode;
}

py::tuple ClumpData::pyForceTorqueFromMembers(const shared_ptr<Node>& node){
	if(!node->hasData<DemData>()) throw std::runtime_error(node->pyStr()+": Node.dem==None.");
	if(!node->getData<DemData>().isA<ClumpData>()) throw std::runtime_error(node->pyStr()+": Node.dem is not a ClumpData instance (the node is not clump's master node).");
	if(!node->getData<DemData>().isClump()) throw std::runtime_error(node->pyStr()+": Node.isClump is False, even though Node.dem is a ClumpData instance (programming error?).");
	Vector3r f(Vector3r::Zero()), t(Vector3r::Zero());
	ClumpData::forceTorqueFromMembers(node,f,t);
	return py::make_tuple(f,t);
}


void ClumpData::forceTorqueFromMembers(const shared_ptr<Node>& node, Vector3r& F, Vector3r& T){
	ClumpData& clump=node->getData<DemData>().cast<ClumpData>();
	for(const auto& n: clump.nodes){
		const DemData& dyn=n->getData<DemData>();
		F+=dyn.force;
		T+=dyn.torque+(n->pos-node->pos).cross(dyn.force);
		// cerr<<"f="<<F.transpose()<<", t="<<T.transpose()<<endl;
	}
}

void ClumpData::applyToMembers(const shared_ptr<Node>& node, bool reset){
	ClumpData& clump=node->getData<DemData>().cast<ClumpData>();
	const Vector3r& clumpPos(node->pos); const Quaternionr& clumpOri(node->ori);
	assert(clump.nodes.size()==clump.relPos.size()); assert(clump.nodes.size()==clump.relOri.size());
	for(size_t i=0; i<clump.nodes.size(); i++){
		const shared_ptr<Node>& n(clump.nodes[i]);
		DemData& nDyn(n->getData<DemData>());
		assert(nDyn.isClumped());
		n->pos=clumpPos+clumpOri*clump.relPos[i];
		n->ori=clumpOri*clump.relOri[i];
		nDyn.vel=clump.vel+clump.angVel.cross(n->pos-clumpPos);
		nDyn.angVel=clump.angVel;
		if(reset) nDyn.force=nDyn.torque=Vector3r::Zero();
	}
}

void ClumpData::resetForceTorque(const shared_ptr<Node>& node){
	ClumpData& clump=node->getData<DemData>().cast<ClumpData>();
	for(size_t i=0; i<clump.nodes.size(); i++){
		DemData& nDyn(clump.nodes[i]->getData<DemData>());
		nDyn.force=nDyn.torque=Vector3r::Zero();
	}
}


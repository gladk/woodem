/* the base class, Impose, is declared in the Particle.hpp file for simplicity */
#pragma once
#include<woo/pkg/dem/Particle.hpp>

struct HarmonicOscillation: public Impose{
	void velocity(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE {
		// http://en.wikipedia.org/wiki/Simple_harmonic_motion
		Real omega=2*M_PI*freq;
		Real vMag=amp*omega*cos(omega*(scene->time-t0));
		Vector3r& vv(n->getData<DemData>().vel);
		if(!perpFree) vv=dir*vMag;
		else{ /*subtract projection on dir*/ vv-=vv.dot(dir)*dir; /* apply new value instead */ vv+=vMag*dir; }
	}
	void postLoad(HarmonicOscillation&,void*){ dir.normalize(); }
	#define woo_dem_HarmonicOscillation__CLASS_BASE_DOC_ATTRS_CTOR \
		HarmonicOscillation,Impose,"Impose `harmonic oscillation <http://en.wikipedia.org/wiki/Harmonic_oscillation#Simple_harmonic_oscillator>`__ around initial center position, with given frequency :obj:`freq` (:math:`f`) and amplitude :obj:`amp` (:math:`A`), by prescribing velocity. Nodal velocity magnitude along :obj:`dir` is :math:`x'(t)=A\\omega\\cos(\\omega(t-t_0))` (with :math:`\\omega=2\\pi f`), which is the derivative of the harmonic motion equation :math:`x(t)=A\\sin(\\omega(t-t_0))`. The motion starts in zero (for :math:`t_0=0`); to reverse the direction, either reverse :obj:`dir` or assign :math:`t_0=\\frac{1}{2f}=\\frac{\\pi}{\\omega}`.", \
		((Real,freq,NaN,,"Frequence of oscillation")) \
		((Real,amp,NaN,,"Amplitude of oscillation")) \
		((Vector3r,dir,Vector3r::UnitX(),,"Direcrtion of oscillation (normalized automatically)")) \
		((Real,t0,0,,"Time when the oscillator is in the center position (phase)")) \
		((bool,perpFree,false,,"If true, only velocity in the *dir* sense will be prescribed, velocity in the perpendicular sense will be preserved.")) \
		,/*ctor*/ what=Impose::VELOCITY; 
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_HarmonicOscillation__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(HarmonicOscillation);

struct CircularOrbit: public Impose{
	void velocity(const Scene* scene, const shared_ptr<Node>&n) WOO_CXX11_OVERRIDE;
	#define woo_dem_CircularOrbit__CLASS_BASE_DOC_ATTRS_CTOR \
		CircularOrbit,Impose,"Imposes circular orbiting around the local z-axis; the velocity is prescribed using approximated midstep position in an incremental manner. This can lead to unstabilities (such as changing radius) when used over millions of steps, but does not require radius to be given explicitly (see also :obj:`StableCircularOrbit`).", \
		((shared_ptr<Node>,node,make_shared<Node>(),,"Local coordinate system.")) \
		((bool,rotate,false,,"Impose rotational velocity so that orientation relative to the local z-axis is always the same.\n\n.. warning:: This is not yet implemented.")) \
		((Real,omega,NaN,,"Orbiting angular velocity.")) \
		((Real,angle,0,,"Cumulative angle turned, incremented at every step.")) \
		,/*ctor*/ what=Impose::VELOCITY;
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_CircularOrbit__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(CircularOrbit);

struct StableCircularOrbit: public CircularOrbit {
	void velocity(const Scene* scene, const shared_ptr<Node>&n) WOO_CXX11_OVERRIDE;
	#define woo_dem_StableCircularOrbit__CLASS_BASE_DOC_ATTRS \
		StableCircularOrbit,CircularOrbit,"Impose circular orbiting around local z-axis, enforcing constant radius of orbiting.", \
		((Real,radius,NaN,,"Radius, i.e. enforced distance from the rotation axis."))
	WOO_DECL__CLASS_BASE_DOC_ATTRS(woo_dem_StableCircularOrbit__CLASS_BASE_DOC_ATTRS);
};
WOO_REGISTER_OBJECT(StableCircularOrbit);

struct AlignedHarmonicOscillations: public Impose{
	void velocity(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE {
		Vector3r& vv(n->getData<DemData>().vel);
		for(int ax:{0,1,2}){
			if(isnan(freqs[ax])||isnan(amps[ax])) continue;
			Real omega=2*M_PI*freqs[ax];
			vv[ax]=amps[ax]*omega*cos(omega*scene->time);
		}
	}
	#define woo_dem_AlignedHarmonicOscillations__CLASS_BASE_DOC_ATTRS_CTOR \
		AlignedHarmonicOscillations,Impose,"Imposes three independent harmonic oscillations along global coordinate system axes.", \
		((Vector3r,freqs,Vector3r(NaN,NaN,NaN),,"Frequencies for individual axes. NaN value switches that axis off, the component will not be touched")) \
		((Vector3r,amps,Vector3r::Zero(),,"Amplitudes along individual axes.")) \
		,/*ctor*/ what=Impose::VELOCITY; 
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_AlignedHarmonicOscillations__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(AlignedHarmonicOscillations);


struct ConstantForce: public Impose{
	void force(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE;
	#define woo_dem_ConstantForce__CLASS_BASE_DOC_ATTRS_CTOR \
		ConstantForce,Impose,"Impose constant force, which is added to other acting forces.", \
		((Vector3r,F,Vector3r::Zero(),,"Applied force (in global coordinates)")) \
		,/*ctor*/ what=Impose::FORCE; 
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_ConstantForce__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(ConstantForce);

struct RadialForce: public Impose{
	void force(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE;
	#define woo_dem_RadialForce__CLASS_BASE_DOC_ATTRS_CTOR \
		RadialForce,Impose,"Impose constant force towards an axis in 3d.", \
		((shared_ptr<Node>,nodeA,,,"First node defining the axis")) \
		((shared_ptr<Node>,nodeB,,,"Second node defining the axis")) \
		((Real,F,0,,"Magnitude of the force applied. Positive value means away from the axis given by *nodeA* and *nodeB*.")) \
		,/*ctor*/ what=Impose::FORCE; 
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_RadialForce__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(RadialForce);

struct Local6Dofs: public Impose{
	void velocity(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE { doImpose(scene,n,/*velocity*/true); }
	void force(const Scene* scene, const shared_ptr<Node>& n)   WOO_CXX11_OVERRIDE { doImpose(scene,n,/*velocity*/false); }
	void doImpose(const Scene* scene, const shared_ptr<Node>& n, bool velocity);
	void postLoad(Local6Dofs&,void*){
		for(int i=0;i<6;i++) if(whats[i]!=0 && whats[i]!=Impose::FORCE && whats[i]!=Impose::VELOCITY) throw std::runtime_error("Local6Dofs.whats components must be 0, "+to_string(Impose::FORCE)+" or "+to_string(Impose::VELOCITY)+" (whats["+to_string(i)+"] invalid: "+lexical_cast<string>(whats.transpose())+")");
	}
	#define woo_dem_Local6Dofs__CLASS_BASE_DOC_ATTRS_CTOR \
		Local6Dofs,Impose,"Impose force or velocity along all local 6 axes given by the *trsf* matrix.", \
		((Quaternionr,ori,Quaternionr::Identity(),,"Local coordinates rotation")) \
		((Vector6r,values,Vector6r::Zero(),,"Imposed values; their meaning depends on the *whats* vector")) \
		((Vector6i,whats,Vector6i::Zero(),,"Meaning of *values* components: 0 for nothing imposed (i.e. zero force), 1 for velocity, 2 for force values")) \
		,/*ctor*/ what=Impose::VELOCITY | Impose::FORCE;
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_Local6Dofs__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(Local6Dofs);

struct VariableAlignedRotation: public Impose{
	void velocity(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE;
	void postLoad(VariableAlignedRotation&,void*);
	size_t _interpPos; // cookie for interpolation routine, does not need to be saved
	#define woo_dem_VariableAlignedRotation__CLASS_BASE_DOC_ATTRS_CTOR \
		VariableAlignedRotation,Impose,"Impose piecewise-linear angular velocity along :obj:`axis`, based on the :obj:`timeAngVel`.", \
		((int,axis,0,,"Rotation axis.")) \
		((vector<Vector2r>,timeAngVel,,,"Angular velocity values in time. Time values must be increasing.")) \
		((bool,wrap,false,,"Wrap time around the last time value (float modulo), if greater.")) \
		, /*ctor*/ what=Impose::VELOCITY; _interpPos=0; 
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_VariableAlignedRotation__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(VariableAlignedRotation);

struct InterpolatedMotion: public Impose{
	void velocity(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE;
	void postLoad(InterpolatedMotion&,void*);
	size_t _interpPos; // cookies for interpolation routine, does not need to be saved
	#define woo_dem_InterpolatedMotion__CLASS_BASE_DOC_ATTRS_CTOR \
		InterpolatedMotion,Impose,"Impose linear and angular velocity such that given positions and orientations are reached in at given time-points.\n\n.. youtube:: D_pc3RU5IXc\n\n", \
			((vector<Vector3r>,poss,,,"Positions which will be interpolated between.")) \
			((vector<Quaternionr>,oris,,,"Orientations which will be interpolated between.")) \
			((vector<Real>,times,,,"Times at which given :obj:`positions <poss>` and :obj:`orientations <oris>` should be reached.")) \
			((Real,t0,0,,"Time offset to add to all time points.")) \
			, /*ctor*/ what=Impose::VELOCITY; _interpPos=0;
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_InterpolatedMotion__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(InterpolatedMotion);

struct ReadForce: public Impose {
	void readForce(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE;
	#define woo_dem_ReadForce__CLASS_BASE_DOC_ATTRS_CTOR \
		ReadForce,Impose,"Sum forces and torques acting on all nodes with this imposition; this imposition does not change the behavior of particles in any way.", \
		((shared_ptr<Node>,node,,,"Reference CS for forces and torque (they are recomputed as if acting on this point); if not given, everything is summed in global CS.")) \
		((OpenMPAccumulator<Vector3r>,F,,AttrTrait<Attr::noSave>().readonly(),"Summary force")) \
		((OpenMPAccumulator<Vector3r>,T,,AttrTrait<Attr::noSave>().readonly(),"Summary torque")) \
		,/*ctor*/ what=Impose::READ_FORCE
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_ReadForce__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(ReadForce);

struct VelocityAndReadForce: public Impose{
	void velocity(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE;
	void readForce(const Scene* scene, const shared_ptr<Node>& n) WOO_CXX11_OVERRIDE;
	void postLoad(VelocityAndReadForce&,void*){ dir.normalize(); workIx=-1; }
	#define woo_dem_VelocityAndReadForce__CLASS_BASE_DOC_ATTRS_CTOR \
		VelocityAndReadForce,Impose,"Impose velocity in one direction, and optionally read and sum force on all nodes with this imposition (force is not changed in any way). Velocity lateral to the imposition may be free or zero depending on :obj:`latBlock`.", \
		((Vector3r,dir,Vector3r::UnitX(),AttrTrait<Attr::triggerPostLoad>(),"Direction (automatically normalized) for prescribed velocity and force readings.")) \
		((Real,vel,0.,AttrTrait<>(),"Prescribed velocity magnitude.")) \
		((bool,latBlock,true,,"Whether lateral velocity (perpendicular to :obj:`vel`) is set to zero, or left free.")) \
		((OpenMPAccumulator<Real>,sumF,,AttrTrait<>().readonly(),"Summary force on nodes with this imposition, in the direction of :obj:`vel`.")) \
		((Real,dist,0,,"Cumulative displacement of this imposition.")) \
		((string,energyName,"",AttrTrait<Attr::triggerPostLoad>(),"If given, and :obj:`~woo.core.Scene.trackEnergy` is ``True``, cumulate work done by this imposition under this name")) \
		((int,workIx,-1,AttrTrait<Attr::hidden|Attr::noSave>(),"Index for fast access to the energy.")) \
		, /*ctor*/ what=Impose::VELOCITY | Impose::READ_FORCE
	WOO_DECL__CLASS_BASE_DOC_ATTRS_CTOR(woo_dem_VelocityAndReadForce__CLASS_BASE_DOC_ATTRS_CTOR);
};
WOO_REGISTER_OBJECT(VelocityAndReadForce);


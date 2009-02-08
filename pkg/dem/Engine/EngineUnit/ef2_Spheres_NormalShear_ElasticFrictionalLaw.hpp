// 2009 © Václav Šmilauer <eudoxos@arcig.cz>
#include<yade/pkg-common/ConstitutiveLaw.hpp>
/* Experimental constitutive law using the ConstitutiveLawDispatcher.
 * Has only purely elastic normal and shear components. */
class ef2_Spheres_NormalShear_ElasticFrictionalLaw: public ConstitutiveLaw {
	virtual void go(shared_ptr<InteractionGeometry>&, shared_ptr<InteractionPhysics>&, Interaction*, MetaBody*);
	NEEDS_BEX("Force","Momentum");
	FUNCTOR2D(SpheresContactGeometry,NormalShearInteraction);
	REGISTER_CLASS_AND_BASE(ef2_Spheres_NormalShear_ElasticFrictionalLaw,ConstitutiveLaw);
};
REGISTER_SERIALIZABLE(ef2_Spheres_NormalShear_ElasticFrictionalLaw);

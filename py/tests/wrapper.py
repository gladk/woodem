# encoding: utf-8
# 2009 © Václav Šmilauer <eudoxos@arcig.cz>

"""
This test module covers python/c++ transitions, for both classes deriving from Object,
but also for other classes that we wrap (like miniEigen).
"""

import unittest
from yade.wrapper import *
from miniEigen import *
from yade._customConverters import *
from math import *
from yade import system
from yade import utils
from yade import *
from yade.dem import *
from yade.gl import *
from yade.core import *
from yade.pre import *
try: from yade.sparc import *
except: pass
try: from yade.voro import *
except: pass
try: from yade.cld import *
except: pass

allClasses=system.childClasses('Object')

class TestObjectInstantiation(unittest.TestCase):
	def setUp(self):
		pass # no setup needed for tests here
	def testClassCtors(self):
		"Core: correct types are instantiated"
		# correct instances created with Foo() syntax
		for r in allClasses:
			obj=eval(r)();
			self.assert_(obj.__class__.__name__==r,'Failed for '+r)
	def testRootDerivedCtors_attrs_few(self):
		"Core: class ctor's attributes"
		# attributes passed when using the Foo(attr1=value1,attr2=value2) syntax
		gm=Shape(color=1.); self.assert_(gm.color==1.)
	def testDispatcherCtor(self):
		"Core: dispatcher ctors with functors"
		# dispatchers take list of their functors in the ctor
		# same functors are collapsed in one
		cld1=LawDispatcher([Law2_L6Geom_FrictPhys_IdealElPl(),Law2_L6Geom_FrictPhys_IdealElPl()]); self.assert_(len(cld1.functors)==1)
		# two different make two different
		cld2=LawDispatcher([Law2_L6Geom_FrictPhys_IdealElPl(),Law2_L6Geom_FrictPhys_LinEl6()]); self.assert_(len(cld2.functors)==2)
	def testContactLoopCtor(self):
		"Core: ContactLoop special ctor"
		# ContactLoop takes 3 lists
		id=ContactLoop([Cg2_Facet_Sphere_L6Geom(),Cg2_Sphere_Sphere_L6Geom()],[Cp2_FrictMat_FrictPhys()],[Law2_L6Geom_FrictPhys_IdealElPl()],)
		self.assert_(len(id.geoDisp.functors)==2)
		self.assert_(id.geoDisp.__class__==CGeomDispatcher)
		self.assert_(id.phyDisp.functors[0].__class__==Cp2_FrictMat_FrictPhys)
		self.assert_(id.lawDisp.functors[0].__class__==Law2_L6Geom_FrictPhys_IdealElPl)
	def testParallelEngineCtor(self):
		"Core: ParallelEngine special ctor"
		pe=ParallelEngine([InsertionSortCollider(),[BoundDispatcher(),ForceResetter()]])
		self.assert_(pe.slaves[0].__class__==InsertionSortCollider)
		self.assert_(len(pe.slaves[1])==2)
		pe.slaves=[]
		self.assert_(len(pe.slaves)==0)
	##		
	## testing incorrect operations that should raise exceptions
	##
	def testWrongFunctorType(self):
		"Core: dispatcher and functor type mismatch is detected"
		# dispatchers accept only correct functors
		self.assertRaises(TypeError,lambda: LawDispatcher([Bo1_Sphere_Aabb()]))
	def testInvalidAttr(self):
		'Core: invalid attribute access raises AttributeError'
		# accessing invalid attributes raises AttributeError
		self.assertRaises(AttributeError,lambda: Sphere(attributeThatDoesntExist=42))
		self.assertRaises(AttributeError,lambda: Sphere().attributeThatDoesntExist)
	##
	## attribute flags
	##
	def testTriggerPostLoad(self):
		'Core: Attr::triggerPostLoad'
		# RadialEngine normalizes axisDir automatically
		# anything else could be tested
		te=RadialForce();
		te.axisDir=(0,2,0)
		self.assert_(te.axisDir==(0,1,0))
	def testHidden(self):
		'Core: Attr::hidden'
		# hidden attributes are not wrapped in python at all
		self.assert_(not hasattr(Contact(),'stepLastSeen'))
	def testNoSave(self):
		'Core: Attr::noSave'
		# update bound of the particle
		O.scene.fields=[DemField()]
		O.dem.par.append(utils.sphere((0,0,0),1))
		O.dem.collectNodes()
		O.scene.engines=[InsertionSortCollider([Bo1_Sphere_Aabb()]),Leapfrog(reset=True)]
		O.step()
		O.saveTmp(quiet=True)
		mn0=Vector3(O.dem.par[0].shape.bound.min)
		O.reload()
		mn1=Vector3(O.dem.par[0].shape.bound.min)
		# check that the minimum is not saved
		self.assert_(not isnan(mn0[0]))
		self.assert_(isnan(mn1[0]))
	def _testReadonly(self):
		'Core: Attr::readonly'
		self.assertRaises(AttributeError,lambda: setattr(Particle(),'id',3))
	
class TestEigenWrapper(unittest.TestCase):
	def assertSeqAlmostEqual(self,v1,v2):
		"floating-point comparison of vectors/quaterions"
		self.assertEqual(len(v1),len(v2));
		for i in range(len(v1)): self.assertAlmostEqual(v1[i],v2[i],msg='Component '+str(i)+' of '+str(v1)+' and '+str(v2))
	def testVector2(self):
		"Math: Vector2 operations"
		v=Vector2(1,2); v2=Vector2(3,4)
		self.assert_(v+v2==Vector2(4,6))
		self.assert_(Vector2().UnitX.dot(Vector2().UnitY)==0)
		self.assert_(Vector2().Zero.norm()==0)
	def testVector3(self):
		"Math: Vector3 operations"
		v=Vector3(3,4,5); v2=Vector3(3,4,5)
		self.assert_(v[0]==3 and v[1]==4 and v[2]==5)
		self.assert_(v.squaredNorm()==50)
		self.assert_(v==(3,4,5)) # comparison with list/tuple
		self.assert_(v==[3,4,5])
		self.assert_(v==v2)
		x,y,z,one=Vector3().UnitX,Vector3().UnitY,Vector3().UnitZ,Vector3().Ones
		self.assert_(x+y+z==one)
		self.assert_(x.dot(y)==0)
		self.assert_(x.cross(y)==z)
	def testQuaternion(self):
		"Math: Quaternion operations"
		# construction
		q1=Quaternion((0,0,1),pi/2)
		q2=Quaternion(Vector3(0,0,1),pi/2)
		q1==q2
		x,y,z,one=Vector3().UnitX,Vector3().UnitY,Vector3().UnitZ,Vector3().Ones
		self.assertSeqAlmostEqual(q1*x,y)
		self.assertSeqAlmostEqual(q1*q1*x,-x)
		self.assertSeqAlmostEqual(q1*q1.conjugate(),Quaternion().Identity)
		self.assertSeqAlmostEqual(q1.toAxisAngle()[0],(0,0,1))
		self.assertAlmostEqual(q1.toAxisAngle()[1],pi/2)
	def testMatrix3(self):
		"Math: Matrix3 operations"
		#construction
		m1=Matrix3(1,0,0,0,1,0,0,0,1)
		# comparison
		self.assert_(m1==Matrix3().Identity)
		# rotation matrix from quaternion
		m1=Matrix3(Quaternion(Vector3(0,0,1),pi/2))
		# multiplication with vectors
		self.assertSeqAlmostEqual(m1*Vector3().UnitX,Vector3().UnitY)
		# determinant
		m2=Matrix3(-2,2,-3,-1,1,3,2,0,-1)
		self.assertEqual(m2.determinant(),18)
		# inverse 
		inv=Matrix3(-0.055555555555556,0.111111111111111,0.5,0.277777777777778,0.444444444444444,0.5,-0.111111111111111,0.222222222222222,0.0)
		m2inv=m2.inverse()
		self.assertSeqAlmostEqual(m2inv,inv)
		# matrix-matrix multiplication
		self.assertSeqAlmostEqual(Matrix3().Identity*Matrix3().Identity,Matrix3().Identity)
		m3=Matrix3(1,2,3,4,5,6,-1,0,3)
		m33=m3*m3
		self.assertSeqAlmostEqual(m33,Matrix3(6,12,24,18,33,60,-4,-2,6))
		
	# not really wm3 thing, but closely related
	# no way to test this currently, as State::se3 is not serialized (State::pos and State::ori are serialized instead...)
	#def testSe3Conversion(self):
	#	return
	#	pp=State()
	#	pp.se3=(Vector3().Zero,Quaternion().Identity)
	#	self.assert_(pp['se3'][0]==Vector3().Zero)
	#	self.assert_(pp['se3'][1]==Quaternion().Identity)
	#	pp.se3=((1,2,3),Quaternion((1,1,1),pi/4))
	#	self.assert_(pp['se3'][0]==(1,2,3))
	#	self.assert_(pp['se3'][0]==pp.pos)
	#	self.assert_(pp['se3'][1]==Quaternion((1,1,1),pi/4))
	#	self.assert_(pp['se3'][1]==pp.ori)
		
	

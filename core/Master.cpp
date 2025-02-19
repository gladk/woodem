#include<woo/core/Master.hpp>
#include<woo/core/Scene.hpp>
#include<woo/lib/base/Math.hpp>
#include<woo/lib/pyutil/caller.hpp>
#include<woo/lib/multimethods/FunctorWrapper.hpp>
#include<woo/lib/multimethods/Indexable.hpp>
#include<cstdlib>
#include<boost/filesystem/operations.hpp>
#include<boost/filesystem/convenience.hpp>
#include<boost/filesystem/exception.hpp>
#include<boost/algorithm/string.hpp>
#include<boost/thread/mutex.hpp>
#include<boost/version.hpp>
#include<boost/python.hpp>


#include<woo/lib/object/ObjectIO.hpp>
#include<woo/core/Timing.hpp>

#ifdef __MINGW64__
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include<process.h>
	#include<Windows.h>
	int getpid(){ return _getpid(); }
#else
	#include<unistd.h> // for getpid
#endif

namespace fs=boost::filesystem;

class RenderMutexLock: public boost::mutex::scoped_lock{
	public:
	RenderMutexLock(): boost::mutex::scoped_lock(Master::instance().renderMutex){/* cerr<<"Lock renderMutex"<<endl; */}
	~RenderMutexLock(){/* cerr<<"Unlock renderMutex"<<endl;*/ }
};

WOO_IMPL_LOGGER(Master);

#ifdef __MINGW64__
/* make sure that both sets of env vars define variable *name* the same way */
void crtWin32EnvCheck(const char* name){
	char win[1024]; int len=GetEnvironmentVariable(name,win,1024);
	char* crt=getenv(name);
	bool winDef=len>0;
	bool crtDef=(crt!=NULL);
	if(winDef!=crtDef){
		cerr<<"WARN: env var "<<name<<" is "<<(winDef?"":"NOT")<<" defined in win32 but "<<(crtDef?"":"NOT")<<" defined in the CRT."<<endl;
		return;
	}
	// both defined, compare value
	if(winDef){
		string w(win), c(crt);
		if(w!=c) cerr<<"WARN: env var "<<name<<" is \""<<w<<"\" in win32 but \""<<c<<"\" in CRT."<<endl;
	}
}
#endif


Master::Master(){
	LOG_DEBUG_EARLY("Constructing woo::Master.");
	startupLocalTime=boost::posix_time::microsec_clock::local_time();
	#ifdef __MINGW64__
		// check that (some) env vars set from python will work under windows.
		crtWin32EnvCheck("WOO_TEMP");
		crtWin32EnvCheck("OMP_NUM_THREADS");
	#endif
	
	fs::path tmp; // assigned in each block
	if(getenv("WOO_TEMP")){
		// use WOO_TEMP_PREFIX if defined
		// this is used on Windows, where _cxxInternal.pyd must be copied to the temp directory
		// since it is hardlinked (not symlinked) from individual modules, hence must be on the same
		// therefore, the tempdir must be created before _cxxInternal is imported, in python
		// but picked up here
		tmpFileDir=getenv("WOO_TEMP");
		tmp=fs::path(tmpFileDir);
		if(!fs::exists(tmp)) throw std::runtime_error("Provided temp directory WOO_TEMP="+tmpFileDir+" does not exist.");
		LOG_DEBUG_EARLY("Using temp dir"<<getenv("WOO_TEMP"));
	} else {
		tmp=fs::unique_path(fs::temp_directory_path()/"woo-tmp-%%%%%%%%");
		tmpFileDir=tmp.string();
		LOG_DEBUG_EARLY("Creating temp dir "<<tmpFileDir);
		if(!fs::create_directory(tmp)) throw std::runtime_error("Creating temporary directory "+tmpFileDir+" failed.");
	}

	// write pid file
	// this checks, as side-effect, that we have write permissions for the temporary directory
	std::ofstream pidfile;
	pidfile.open((tmp/"pid").string().c_str());
	if(!pidfile.is_open()) throw std::runtime_error("Error opening pidfile "+(tmp/"pid").string()+" for writing.");
	pidfile<<getpid()<<endl;
	if(pidfile.bad()) throw std::runtime_error("Error writing to pidfile "+(tmp/"pid").string()+".");
	pidfile.close();

	tmpFileCounter=0;

	defaultClDev=Vector2i(-1,-1);
	
	// create the initial master scene
	scene=make_shared<Scene>();
	scene->postLoad(*scene,NULL); // this would not get called otherwise, causing troubles

	api=0; // set from config.cxx to the real value
	usesApi=0; // 0 means not set
}


Master::~Master(){ }


void Master::pyRegisterClass(){
	py::class_<Master,boost::noncopyable>("Master",py::no_init)
		.add_property("realtime",&Master::getRealTime,"Return clock (human world) time the simulation has been running.")
		// tmp storage
		.def("loadTmpAny",&Master::loadTmp,(py::arg("name")=""),"Load any object from named temporary store.")
		.def("deepcopy",&Master::deepcopy,(py::arg("obj")),"Return a deep-copy of given object; this performs serialization+deserialization using temporary (RAM) storage; all objects are therefore created anew.")
		.def("saveTmpAny",&Master::saveTmp,(py::arg("obj"),py::arg("name")="",py::arg("quiet")=false),"Save any object to named temporary store; *quiet* will supress warning if the name is already used.")
		.def("lsTmp",&Master::pyLsTmp,"Return list of all memory-saved simulations.")
		.def("rmTmp",&Master::rmTmp,py::arg("name"),"Remove memory-saved simulation.")
		.def("tmpToFile",&Master::pyTmpToFile,(py::arg("mark"),py::arg("fileName")),"Save XML of :obj:`saveTmp`'d simulation into *fileName*.")
		.def("tmpToString",&Master::pyTmpToString,(py::arg("mark")=""),"Return XML of :obj:`saveTmp <Master.saveTmp>`'d simulation as string.")

		.def("plugins",&Master::pyPlugins,"Return list of all plugins registered in the class factory.")
		#ifdef WOO_OPENCL
			.def_readwrite("defaultClDev",&Master::defaultClDev,"Default OpenCL platform/device couple (as ints), set internally from the command-line arg.")
		#endif
		.def_readonly("confDir",&Master::confDir,"Directory for storing various local configuration files (automatically set at startup)")
		.add_property("scene",&Master::pyGetScene,&Master::pySetScene)

		.add_property("api",&Master::api_get,&Master::api_set,"Current version of API (application programming interface) so that we can warn about possible incompatibilities, when comparing with :obj:`usesApi`. The number uses two decimal places for each part (major,minor,api), so e.g. 10177 is API 1.01.77. The correspondence with version number is loose.")
		.add_property("usesApi",&Master::usesApi_get,&Master::usesApi_set,"API version this script is using; compared with :obj:`api` at some places to give helpful warnings. This variable can be set either from integer (e.g. 10177) or a Vector3i like ``(1,1,77)``.")
		.def("checkApi",&Master::checkApi,(py::arg("minApi"),py::arg("msg"),py::arg("pyWarn")=true),"Check whether the :obj:`currently used API <woo.core.Master.usesApi>` is at least *minApi*. If smaller, issue warning (which is either Python's ``DeprecationWarning`` or c++-level warning depending on *pyWarn*) with link to the API changes page. Also issue ``FutureWarning`` (or c++-level warning, depending on *pyWarn*) if :obj:`~woo.core.Master.usesApi` is not set.")
		.add_property("usesApi_locations",py::make_getter(&Master::usesApi_locations,py::return_value_policy<py::return_by_value>()))

		.def("reset",&Master::pyReset,"Set empty main scene")

		.add_property("cmaps",&Master::pyLsCmap,"List available colormaps (by name)")
		.add_property("cmap",&Master::pyGetCmap,&Master::pySetCmap,"Current colormap as (index,name) tuple; set by index or by name alone.")

		// throw on deprecated attributes
		.add_property("dt",&Master::err_dt)
		.add_property("engines",&Master::err_engines)
		.add_property("cell",&Master::err_cell)
		.add_property("periodic",&Master::err_periodic)
		.add_property("trackEnergy",&Master::err_trackEnergy)
		.add_property("energy",&Master::err_energy)
		.add_property("tags",&Master::err_tags)

		.def("childClassesNonrecursive",&Master::pyListChildClassesNonrecursive,"Return list of all classes deriving from given class, as registered in the class factory")
		.def("isChildClassOf",&Master::pyIsChildClassOf,"Tells whether the first class derives from the second one (both given as strings).")

		.add_property("timingEnabled",&Master::timingEnabled_get,&Master::timingEnabled_set,"Globally enable/disable timing services (see documentation of the :obj:`timing module <woo.timing>`).")
		// setting numThreads by hand crashes OpenMP, is that a bug?
		// in any case, we disable it here just to make sure
		.add_property("numThreads",&Master::numThreads_get /*,&Master::numThreads_set*/,"Maximum number of threads openMP can use.")
		.add_property("compiledPyModules",&Master::pyCompiledPyModules) // we might not use to-python converters, since _customConverters have not yet been imported

		.def("exitNoBacktrace",&Master::pyExitNoBacktrace,(py::arg("status")=0),"Disable SEGV handler and exit, optionally with given status number.")
		.def("disableGdb",&Master::pyDisableGdb,"Revert SEGV and ABRT handlers to system defaults.")
		.def("tmpFilename",&Master::tmpFilename,"Return unique name of file in temporary directory which will be deleted when woo exits.")
		.add_property("tmpFileDir",&Master::getTmpFileDir,"Directory for temporary files; created automatically at startup.")
		.add_static_property("instance",py::make_function(&Master::instance,py::return_value_policy<py::reference_existing_object>()))
	;
}


const map<string,set<string>>& Master::getClassBases(){return classBases;}

Real Master::getRealTime(){ return (boost::posix_time::microsec_clock::local_time()-startupLocalTime).total_milliseconds()/1e3; }
boost::posix_time::time_duration Master::getRealTime_duration(){return boost::posix_time::microsec_clock::local_time()-startupLocalTime;}

string Master::getTmpFileDir(){ return tmpFileDir; }

std::string Master::tmpFilename(){
	assert(!tmpFileDir.empty());
	boost::mutex::scoped_lock lock(tmpFileCounterMutex);
	return tmpFileDir+"/tmp-"+lexical_cast<string>(tmpFileCounter++);
}

const shared_ptr<Scene>& Master::getScene(){return scene;}
void Master::setScene(const shared_ptr<Scene>& s){
	if(!s) throw std::runtime_error("Scene must not be None.");
	GilLock lock; // protect shared_ptr deletion here
	scene=s;
}

/* inheritance (?!!) */
bool Master::isInheritingFrom(const string& className, const string& baseClassName){
	return (classBases[className].find(baseClassName)!=classBases[className].end());
}
bool Master::isInheritingFrom_recursive(const string& className, const string& baseClassName){
	if(classBases[className].find(baseClassName)!=classBases[className].end()) return true;
	FOREACH(const string& parent,classBases[className]){
		if(isInheritingFrom_recursive(parent,baseClassName)) return true;
	}
	return false;
}

shared_ptr<Object> Master::deepcopy(shared_ptr<Object> obj){
	std::stringstream ss;
	woo::ObjectIO::save<shared_ptr<Object>,boost::archive::binary_oarchive>(ss,"woo__Object",obj);
	auto ret=make_shared<Object>();
	woo::ObjectIO::load<shared_ptr<Object>,boost::archive::binary_iarchive>(ss,"woo__Object",ret);
	return ret;
}

/* named temporary store */
void Master::saveTmp(shared_ptr<Object> obj, const string& name, bool quiet){
	if(memSavedSimulations.count(name)>0 && !quiet){  LOG_INFO("Overwriting in-memory saved simulation "<<name); }
	std::ostringstream oss;
	woo::ObjectIO::save<shared_ptr<Object>,boost::archive::binary_oarchive>(oss,"woo__Object",obj);
	memSavedSimulations[name]=oss.str();
}
shared_ptr<Object> Master::loadTmp(const string& name){
	if(memSavedSimulations.count(name)==0) throw std::runtime_error("No memory-saved simulation "+name);
	std::istringstream iss(memSavedSimulations[name]);
	auto obj=make_shared<Object>();
	woo::ObjectIO::load<shared_ptr<Object>,boost::archive::binary_iarchive>(iss,"woo__Object",obj);
	return obj;
}

/* PLUGINS */
// registerFactorable
bool Master::registerClassFactory(const std::string& name, FactoryFunc factory){
	classnameFactoryMap[name]=factory;
	return true;
}

shared_ptr<Object> Master::factorClass(const std::string& name){
	auto I=classnameFactoryMap.find(name);
	if(I==classnameFactoryMap.end()) throw std::runtime_error("Master.factorClassByName: Class '"+name+"' not known.");
	return (I->second)();
}


void Master::registerPluginClasses(const char* module, const char* fileAndClasses[]){
	assert(fileAndClasses[0]!=NULL); // must be file name
	for(int i=1; fileAndClasses[i]!=NULL; i++){
		LOG_DEBUG_EARLY("Plugin "<<fileAndClasses[0]<<", class "<<module<<"."<<fileAndClasses[i]);	
		modulePluginClasses.push_back({module,fileAndClasses[i]});
	}
}

void Master::pyRegisterAllClasses(){

	LOG_DEBUG("called with "<<modulePluginClasses.size()<<" module+class pairs.");
	std::map<std::string,py::object> pyModules;
	py::object wooScope=boost::python::import("woo");

	auto synthesizePyModule=[&](const string& modName){
		py::object m(py::handle<>(PyModule_New(("woo."+modName).c_str())));
		m.attr("__file__")="<synthetic>";
		wooScope.attr(modName.c_str())=m;
		pyModules[modName.c_str()]=m;
		// http://stackoverflow.com/questions/11063243/synethsized-submodule-from-a-import-b-ok-vs-import-a-b-error/11063494
		py::extract<py::dict>(py::getattr(py::import("sys"),"modules"))()[("woo."+modName).c_str()]=m;
		LOG_DEBUG_EARLY("Synthesized new module woo."<<modName);
	};

	// this module is synthesized for core.Master; other synthetic modules are created on-demand in the loop below
	synthesizePyModule("core");
	py::scope core(py::import("woo.core"));

	this->pyRegisterClass();
	woo::ClassTrait::pyRegisterClass();
	woo::AttrTraitBase::pyRegisterClass();
	woo::TimingDeltas::pyRegisterClass();
	Object().pyRegisterClass(); // virtual method, therefore cannot be static

	// http://boost.2283326.n4.nabble.com/C-sig-How-to-create-package-structure-in-single-extension-module-td2697292.html

	list<pair<string,shared_ptr<Object>>> pythonables;
	for(const auto& moduleName: modulePluginClasses){
		string module(moduleName.first);
		string name(moduleName.second);
		shared_ptr<Object> obj;
		try {
			LOG_DEBUG("Factoring class "<<name);
			obj=factorClass(name);
			assert(obj);
			// needed for Master::childClasses
			for(int i=0;i<obj->getBaseClassNumber();i++) classBases[name].insert(obj->getBaseClassName(i));
			// needed for properly initialized class indices
			Indexable* indexable=dynamic_cast<Indexable*>(obj.get());
			if(indexable) indexable->createIndex();
			//
			if(pyModules.find(module)==pyModules.end()){
				try{
					// module existing as file, use it
					pyModules[module]=py::import(("woo."+module).c_str());
				} catch (py::error_already_set& e){
					// PyErr_Print shows error and clears error indicator
					if(getenv("WOO_DEBUG")) PyErr_Print();
					// this only clears the the error indicator
					else PyErr_Clear(); 
					synthesizePyModule(module);
				}
			}
			pythonables.push_back(make_pair(module,obj));
		}
		catch (std::runtime_error& e){
			/* FIXME: this catches all errors! Some of them are not harmful, however:
			 * when a class is not factorable, it is OK to skip it; */	
			 cerr<<"Caught non-critical error for class "<<module<<"."<<name;
		}
	}

	// handle Object specially
	// py::scope _scope(pyModules["core"]);
	// Object().pyRegisterClass(pyModules["core"]);

	/* python classes must be registered such that base classes come before derived ones;
	for now, just loop until we succeed; proper solution will be to build graphs of classes
	and traverse it from the top. It will be done once all classes are pythonable. */
	for(int i=0; i<11 && pythonables.size()>0; i++){
		if(i==10) throw std::runtime_error("Too many attempts to register python classes. Run again with WOO_DEBUG=1 to get better diagnostics.");
		LOG_DEBUG_EARLY_FRAGMENT(endl<<"[[[ Round "<<i<<" ]]]: ");
		std::list<string> done;
		for(auto I=pythonables.begin(); I!=pythonables.end(); ){
			const std::string& module=I->first;
			const shared_ptr<Object>& s=I->second;
			const std::string& klass=s->getClassName();
			try{
				LOG_DEBUG_EARLY_FRAGMENT("{{"<<klass<<"}}");
				py::scope _scope(pyModules[module]);
				s->pyRegisterClass();
				auto prev=I++;
				pythonables.erase(prev);
			} catch (...){
				LOG_DEBUG_EARLY("["<<klass<<"]");
				if(getenv("WOO_DEBUG")) PyErr_Print();
				else PyErr_Clear();
				I++;
			}
		}
	}
}


py::list Master::pyListChildClassesNonrecursive(const string& base){
	py::list ret;
	for(auto& i: getClassBases()){ if(isInheritingFrom(i.first,base)) ret.append(i.first); }
	return ret;
}

bool Master::pyIsChildClassOf(const string& child, const string& base){
	return (isInheritingFrom_recursive(child,base));
}

void Master::rmTmp(const string& name){
	if(memSavedSimulations.count(name)==0) throw runtime_error("No memory-saved object named "+name);
	memSavedSimulations.erase(name);
}

py::list Master::pyLsTmp(){
	py::list ret;
	for(const auto& i: memSavedSimulations) ret.append(i.first);
	return ret;
}

void Master::pyTmpToFile(const string& mark, const string& filename){
	if(memSavedSimulations.count(mark)==0) throw runtime_error("No memory-saved simulation named "+mark);
	boost::iostreams::filtering_ostream out;
	if(boost::algorithm::ends_with(filename,".bz2")) out.push(boost::iostreams::bzip2_compressor());
	out.push(boost::iostreams::file_sink(filename));
	if(!out.good()) throw runtime_error("Error while opening file `"+filename+"' for writing.");
	LOG_INFO("Saving memory-stored '"<<mark<<"' to file "<<filename);
	out<<memSavedSimulations[mark];
}

std::string Master::pyTmpToString(const string& mark){
	if(memSavedSimulations.count(mark)==0) throw runtime_error("No memory-saved simulation named "+mark);
	return memSavedSimulations[mark];
}


py::list Master::pyLsCmap(){ py::list ret; for(const CompUtils::Colormap& cm: CompUtils::colormaps) ret.append(cm.name); return ret; }
py::tuple Master::pyGetCmap(){ return py::make_tuple(CompUtils::defaultCmap,CompUtils::colormaps[CompUtils::defaultCmap].name); } 
void Master::pySetCmap(py::object obj){
	py::extract<int> exInt(obj);
	py::extract<string> exStr(obj);
	py::extract<py::tuple> exTuple(obj);
	if(exInt.check()){
		int i=exInt();
		if(i<0 || i>=(int)CompUtils::colormaps.size()) woo::IndexError(boost::format("Colormap index out of range 0…%d")%(CompUtils::colormaps.size()));
		CompUtils::defaultCmap=i;
		return;
	}
	if(exStr.check()){
		int i=-1; string s(exStr());
		for(const CompUtils::Colormap& cm: CompUtils::colormaps){ i++; if(cm.name==s){ CompUtils::defaultCmap=i; return; } }
		woo::KeyError("No colormap named `"+s+"'.");
	}
	if(exTuple.check() && py::extract<int>(exTuple()[0]).check() && py::extract<string>(exTuple()[1]).check()){
		int i=py::extract<int>(exTuple()[0]); string s=py::extract<string>(exTuple()[1]);
		if(i<0 || i>=(int)CompUtils::colormaps.size()) woo::IndexError(boost::format("Colormap index out of range 0…%d")%(CompUtils::colormaps.size()));
		CompUtils::defaultCmap=i;
		if(CompUtils::colormaps[i].name!=s) LOG_WARN("Given colormap name ignored, does not match index");
		return;
	}
	woo::TypeError("cmap can be specified as int, str or (int,str)");
}

// only used below with POSIX signal handlers
#ifndef __MINGW64__
	void termHandlerNormal(int sig){cerr<<"Woo: normal exit."<<endl; raise(SIGTERM);}
	void termHandlerError(int sig){cerr<<"Woo: error exit."<<endl; raise(SIGTERM);}
#endif

void Master::pyExitNoBacktrace(int status){
	#ifndef __MINGW64__
		if(status==0) signal(SIGSEGV,termHandlerNormal); /* unset the handler that runs gdb and prints backtrace */
		else signal(SIGSEGV,termHandlerError);
	#endif
	// flush all streams (so that in case we crash at exit, unflushed buffers are not lost)
	fflush(NULL);
	// attempt exit
	exit(status);
}

shared_ptr<woo::Object> Master::pyGetScene(){ return getScene(); }
void Master::pySetScene(const shared_ptr<Object>& s){
	if(!s) woo::ValueError("Argument is None");
	if(!s->isA<Scene>()) woo::TypeError("Argument is not a Scene instance");
	setScene(static_pointer_cast<Scene>(s));
}


py::object Master::pyGetInstance(){
	//return py::object(py::ref(Master::getInstance()));
	return py::object();
}

void Master::pyReset(){
	getScene()->pyStop();
	setScene(make_shared<Scene>());
}

py::list Master::pyPlugins(){
	py::list ret;
	for(auto& i: getClassBases()) ret.append(i.first);
	return ret;
}

int Master::numThreads_get(){
	#ifdef WOO_OPENMP
		return omp_get_max_threads();
	#else
		return 1;
	#endif
}

void Master::numThreads_set(int i){
	#ifdef WOO_OPENMP
		omp_set_num_threads(i);
		if(i!=omp_get_max_threads()) throw std::runtime_error("woo.core.Master: numThreads set to "+to_string(i)+" but is "+to_string(i)+" (are you setting numThreads inside parallel section?)");
	#else
		if(i==1) return;
		else LOG_WARN("woo.core.Master: compiled without OpenMP support, setting numThreads has no effect.");
	#endif
}

void Master::api_set(int a){
	if(a==api) return; // no change
	if(api>0) woo::ValueError("woo.master.api was already set to "+to_string(api)+", refusing to set it again to "+to_string(a)+".");
	api=a;
}
int Master::api_get() const { return api; }

void Master::usesApi_set(py::object o){
	py::extract<int> ia(o);
	py::extract<Vector3i> va(o);
	int ua=-1;
	if(ia.check()){ ua=ia(); } 
	if(va.check()){ Vector3i a=va(); ua=a[0]*10000+a[1]*100+a[2]; }
	if(ua==-1) woo::ValueError("API must be given either as integer or Vector3i (or anything convertible), e.g. 10151 or (1,1,51).");
	usesApi_locations.push_back(to_string(ua)+": "+woo::pyCallerInfo());
	if(usesApi>0 && ua!=usesApi){
		LOG_WARN("Updating woo.master.usesApi which was set to different values previously:");
		for(const auto& l: usesApi_locations) LOG_WARN("  "<<l);
	}
	usesApi=ua;
}

int Master::usesApi_get() const { return usesApi; }


void Master::checkApi(int minApi, const string& msg, bool pyWarn) const{
	if(usesApi==0){
		const char* m="Script did not set woo.master.usesApi, all functions with changed APIs will pessimistically warn about possible functionality changes. See https://woodem.org/api.html for details.";
		if(pyWarn){ PyErr_WarnEx(PyExc_FutureWarning,m,/*stacklevel*/1); }
		else{ LOG_WARN(m); }
	}
	if(usesApi<minApi){
		string m("\n"+woo::pyCallerInfo()+":\n   possible API incompatibility:\n   "+msg+"\n   (woo.master.api="+to_string(api)+" > woo.master.usesApi="+to_string(usesApi)+"; this call requires at least minApi="+to_string(minApi)+").\n   See https://woodem.org/api.html#api-"+to_string(minApi)+" for details.");
		if(pyWarn){ PyErr_WarnEx(PyExc_DeprecationWarning,m.c_str(),/*stacklevel*/1); }
		else{ LOG_WARN(m); }
	}
}


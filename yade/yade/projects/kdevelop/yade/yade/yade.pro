# File generated by kdevelop's qmake manager. 
# ------------------------------------------- 
# Subdir relative project main directory: ./yade/yade
# Target is an application:  

HEADERS += Chrono.hpp \
           Omega.hpp \
           Singleton.hpp \
           Tree.hpp \
           Actor.hpp \
           ActorParameter.hpp \
           FrontEnd.hpp 
SOURCES += Chrono.cpp \
           Omega.cpp \
           yade.cpp \
           FrontEnd.cpp 
LIBS += -lSerialization \
-lMath \
-lBody \
-lEngine \
-lGeometry \
-lInteraction \
-lMultiMethods \
-lFactory \
-lboost_filesystem \
-lboost_date_time \
-lglut \
-lQGLViewer \
-rdynamic
INCLUDEPATH = $(YADEINCLUDEPATH)
MOC_DIR = $(YADECOMPILATIONPATH)
UI_DIR = $(YADECOMPILATIONPATH)
OBJECTS_DIR = $(YADECOMPILATIONPATH)
QMAKE_LIBDIR = ../../toolboxes/Libraries/Serialization/$(YADEDYNLIBPATH) \
../../toolboxes/Libraries/Math/$(YADEDYNLIBPATH) \
../../yade/Body/$(YADEDYNLIBPATH) \
../../yade/Engine/$(YADEDYNLIBPATH) \
../../yade/Geometry/$(YADEDYNLIBPATH) \
../../yade/Interaction/$(YADEDYNLIBPATH) \
../../yade/MultiMethods/$(YADEDYNLIBPATH) \
../../yade/Factory/$(YADEDYNLIBPATH) \
$(YADEDYNLIBPATH)
DESTDIR = $(YADEBINPATH)
CONFIG += debug \
warn_on
TEMPLATE = app

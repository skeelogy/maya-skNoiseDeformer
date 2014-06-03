#Makefile for compiling Maya plugin as a .so shared library
#author: Skeel Lee
#contact: skeel@skeelogy.com
#since: 1 Jun 2014

#EXAMPLE USAGE:

#Make a release build:
# > make

#Copy compiled release plugin to installation directory $(MAYA_APP_DIR)/$(MAYA_VERSION)/plugins/$(PLATFORM)$(BITS):
# > make install MAYA_VERSION=2014-x64

#Clean release build as needed:
# > make clean

#Make a debug build:
# > make BUILD=debug

#Copy compiled debug plugin to installation directory $(MAYA_APP_DIR)/$(MAYA_VERSION)/plugins/$(PLATFORM)$(BITS):
# > make install MAYA_VERSION=2014-x64 BUILD=debug

#Clean up temp files as needed:
# > make clean

#======================================
#VARIABLES
#======================================

TARGETNAME = skNoiseDeformer
TARGETEXT = .so
OBJDIR = obj
OUTDIR = bin
BUILD ?= release

#basic attributes for compilation
CXX = g++412
CXXFLAGS += -c -Wall
INCLUDEPATHS +=

#basic attributes for linking
LINKER ?= $(CXX)
LDFLAGS +=
LIBPATHS +=
LIBS +=

#flags based on BUILD
ifeq ($(BUILD), debug)
	CXXFLAGS += -g -O0 -DDEBUG
	LDFLAGS += -g -O0
	DEBUGSUFFIX ?= _d
else
	CXXFLAGS += -O3
	LDFLAGS += -O3
endif

#get PLATFORM and BITS
PLATFORM = $(shell uname)
ifeq ($(shell getconf LONG_BIT), 32)
	BITS = 32
else
	BITS = 64
endif

#flags for compilation/linking based on platform
ifeq ($(PLATFORM), Linux)
	ifeq ($(BITS), 32)
		CXXFLAGS += -DLINUX
	else
		CXXFLAGS += -DLINUX_64
	endif
endif

#flags for compilation/linking based on bits
ifeq ($(BITS), 32)
	CXXFLAGS += -m32
	LDFLAGS += -m32
else
	CXXFLAGS += -m64
	LDFLAGS += -m64
endif

#flags for shared library
ifeq ($(TARGETEXT), .so)
	CXXFLAGS += -fPIC
	LDFLAGS += -shared
endif

#flags for Maya
CXXFLAGS += -pthread -pipe -D_BOOL -DREQUIRE_IOSTREAM -Wno-deprecated -fno-gnu-keywords
LDFLAGS += -pthread -pipe -D_BOOL -DLINUX -DREQUIRE_IOSTREAM -Wno-deprecated -fno-gnu-keywords -Wl,-Bsymbolic
INCLUDEPATHS += -I$(MAYA_LOCATION)/include
LIBS += -lOpenMaya -lFoundation
LIBPATHS += -L$(MAYA_LOCATION)/lib

#find cpp files and define their corresponding .o files
SRCFULLPATH = $(shell find . -name "*.cpp")
SRC = $(notdir $(SRCFULLPATH))
OBJ = $(SRC:%.cpp=%.o)

#final compiled target name and installation directory
TARGET = $(TARGETNAME)$(DEBUGSUFFIX)$(TARGETEXT)
INSTALLDIR = $(MAYA_APP_DIR)/$(MAYA_VERSION)/plugins/$(PLATFORM)$(BITS)

#======================================
#TARGETS
#======================================

all:
	@echo
	@echo "> Building $(BUILD) version for $(PLATFORM)$(BITS)..."
	@echo
	make $(TARGET)
	@echo "> BUILD DONE."
	@echo

#target for linking
$(TARGET): $(addprefix ./$(OBJDIR)/$(PLATFORM)$(BITS)/$(BUILD)/, $(OBJ))
	@echo
	@echo "> Linking object files..."
	@mkdir -p ./$(OUTDIR)/$(PLATFORM)$(BITS)
	$(LINKER) $(LDFLAGS) $(LIBPATHS) $(LIBS) $(addprefix ./$(OBJDIR)/$(PLATFORM)$(BITS)/$(BUILD)/, $(OBJ)) -o ./$(OUTDIR)/$(PLATFORM)$(BITS)/$(TARGET)
	@echo "> Linking done."
	@echo

#target for compiling (finds .cpp and .h files only in current directory)
./$(OBJDIR)/$(PLATFORM)$(BITS)/$(BUILD)/%.o: ./%.cpp ./%.h
	@echo
	@echo "> Compiling $<..."
	@mkdir -p ./$(OBJDIR)/$(PLATFORM)$(BITS)/$(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDEPATHS) $< -o $@
	@echo "> Compiling done: $@ created."

#target for copying the compiled plugin to an installation path
ifdef MAYA_VERSION
install: ./$(OUTDIR)/$(PLATFORM)$(BITS)/$(TARGET)
	@echo
	@echo "> Installing $(TARGET)..."
	@mkdir -p $(INSTALLDIR)
	cp ./$(OUTDIR)/$(PLATFORM)$(BITS)/$(TARGET) $(INSTALLDIR)
	@echo
	@echo "> $(TARGET) installed to $(INSTALLDIR)."
	@echo
else
install:
	@echo "MAYA_VERSION not defined. Unable to install."
endif

#target for clean
clean:
	rm -rf ./$(OUTDIR)/$(PLATFORM)$(BITS)/$(TARGET)
	rm -rf ./$(OBJDIR)/$(PLATFORM)$(BITS)/$(BUILD)/*
	@echo "> Project cleaned for $(BUILD) mode."

#phony targets
.PHONY: all clean
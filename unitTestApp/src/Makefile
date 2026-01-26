TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

# The unit tests link against libopcua (the classes under test are not recompiled).
# - Library symbols used by tests must be exported (DEVOPCUA_API, see
#   devOpcuaSup/devOpcuaAPI.h), otherwise Windows DLL builds fail to link.
# - Flags that change class layouts (HAS_SECURITY, HAS_XMLPARSER) must match
#   the library build.

# Windows builds define NOMINMAX (macros min, max clash with numeric_limits<>)
USR_CXXFLAGS_WIN32 += -DNOMINMAX

DEVSUP_SRC = $(TOP)/devOpcuaSup

TESTSRC = $(TOP)/unitTestApp/src
CLIENT_SRC = $(DEVSUP_SRC)/$(CLIENT_SUBDIR)

# Add client specific tests
SRC_DIRS += $(TESTSRC)/$(CLIENT_SUBDIR)
USR_INCLUDES += -I$(DEVSUP_SRC) -I$(CLIENT_SRC)

ifeq ($($(CLIENT)_USE_CRYPTO),YES)
USR_CXXFLAGS += -DHAS_SECURITY
endif
ifeq ($($(CLIENT)_USE_XMLPARSER),YES)
USR_CXXFLAGS += -DHAS_XMLPARSER
USR_INCLUDES_Linux += -I/usr/include/libxml2
ifneq ($(filter -D_MINGW,$(OP_SYS_CPPFLAGS)),)
USR_INCLUDES += -I$(MSYSTEM_PREFIX)/include/libxml2
USR_SYS_LIBS += xml2
endif
endif

#==================================================
# Build tests executables

GTESTPROD_HOST += UpdateTest
UpdateTest_SRCS += UpdateTest.cpp
GTESTS += UpdateTest

GTESTPROD_HOST += UpdateQueueTest
UpdateQueueTest_SRCS += UpdateQueueTest.cpp
GTESTS += UpdateQueueTest

GTESTPROD_HOST += RequestQueueBatcherTest
RequestQueueBatcherTest_SRCS += RequestQueueBatcherTest.cpp
GTESTS += RequestQueueBatcherTest

GTESTPROD_HOST += RegistryTest
RegistryTest_SRCS += RegistryTest.cpp
GTESTS += RegistryTest

GTESTPROD_HOST += LinkParserTest
LinkParserTest_SRCS += LinkParserTest.cpp
LinkParserTest_LIBS += opcua
GTESTS += LinkParserTest

GTESTPROD_HOST += ElementTreeTest
ElementTreeTest_SRCS += ElementTreeTest.cpp
ElementTreeTest_LIBS += opcua
GTESTS += ElementTreeTest

#==================================================
# Tests for different client libraries
# are in separate directories, added by reading
# Makefile fragments

# Include client specific Makefile fragment (config part)
ifdef T_A
include $(TESTSRC)/$(CLIENT_SUBDIR)/Makefile.config
endif

USR_LIBS += Com

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# Windows DLL builds: the tests link against opcua.dll, which is installed
# in bin/<arch> - put it next to the test executables (there is no RPATH)

ifeq ($(OS_CLASS)$(SHARED_LIBRARIES),WIN32YES)
OPCUA_DLL = $(SHRLIB_PREFIX)opcua$(SHRLIB_SUFFIX_BASE)

$(GTESTLISTS) : $(OPCUA_DLL)

$(OPCUA_DLL) : $(INSTALL_SHRLIB)/$(OPCUA_DLL)
	@$(ECHO) "Installing shared library $@ for the tests"
	$(INSTALL) -d -m 555 $< $(@D)
endif

ifdef UASDK

# Try to fight DLL Hell by putting SSL DLLs next to the binaries

SSLLIB_INSTALLS += $(foreach lib, $(UASDK_SSL_LIBS), \
    $(SHRLIB_PREFIX)$(lib)$(SHRLIB_SUFFIX_BASE) )

$(GTESTLISTS) : $(SSLLIB_INSTALLS)

$(SHRLIB_PREFIX)%$(SHRLIB_SUFFIX_BASE) : $(UASDK_SHRLIB_DIR)/$(SHRLIB_PREFIX)%$(SHRLIB_SUFFIX_BASE)
	@$(ECHO) "Installing shared library $@ from Unified Automation SDK"
	$(INSTALL) -d -m 555 $< $(@D)

endif

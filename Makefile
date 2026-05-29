# Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

# Directories to build, any order
DIRS += configure
DIRS += $(wildcard *Sup)
DIRS += $(filter-out unitTestApp, $(wildcard *App))
DIRS += $(wildcard *Top)
DIRS += $(wildcard iocBoot)

# unitTestApp is only built as part of the test or maintenance targets
ifneq ($(filter unitTestApp runtests tapfiles test-results clean uninstall realclean,$(MAKECMDGOALS)),)
DIRS += unitTestApp
endif

# The build order is controlled by these dependency rules:

# All dirs except configure depend on configure
$(foreach dir, $(filter-out configure, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += configure))

# Any *App dirs depend on all *Sup dirs
$(foreach dir, $(filter %App, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += $(filter %Sup, $(DIRS))))

# Any *Top dirs depend on all *Sup and *App dirs (except unitTestApp)
$(foreach dir, $(filter %Top, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += $(filter %Sup, $(DIRS)) $(filter-out unitTestApp, $(filter %App, $(DIRS)))))

# iocBoot depends on all *App dirs (except unitTestApp)
iocBoot_DEPEND_DIRS += $(filter-out unitTestApp, $(filter %App,$(DIRS)))

# Add any additional dependency rules here:

# On test targets, build unitTestApp before running tests
ifneq ($(filter unitTestApp,$(DIRS)),)
unitTestApp$(DIVIDER)runtests unitTestApp$(DIVIDER)tapfiles unitTestApp$(DIVIDER)test-results: unitTestApp
endif

include $(TOP)/configure/RULES_TOP

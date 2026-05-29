# Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

# Directories to build, any order
DIRS += configure
DIRS += $(wildcard *Sup)
DIRS += $(filter-out unitTestApp, $(wildcard *App))
DIRS += $(wildcard *Top)
DIRS += $(wildcard iocBoot)

# unitTestApp is only built as part of the 'runtests' target
ifneq ($(filter unitTestApp runtests tapfiles test-results clean uninstall realclean,$(MAKECMDGOALS)),)
DIRS += $(wildcard unitTestApp)
endif

# The build order is controlled by these dependency rules:

# All dirs except configure depend on configure
$(foreach dir, $(filter-out configure, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += configure))

# Any *App dirs depend on all *Sup dirs
$(foreach dir, $(filter %App, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += $(filter %Sup, $(DIRS))))

# Any *Top dirs depend on all *Sup and *App dirs
$(foreach dir, $(filter %Top, $(DIRS)), \
    $(eval $(dir)_DEPEND_DIRS += $(filter %Sup %App, $(DIRS))))

# iocBoot depends on all *App dirs
iocBoot_DEPEND_DIRS += $(filter %App,$(DIRS))

# Add any additional dependency rules here:

# unitTestApp targets should build the app first
unitTestApp_DEPEND_DIRS += $(filter-out unitTestApp, $(filter %Sup %App,$(DIRS)))
unitTestApp.runtests: unitTestApp.all
unitTestApp.tapfiles: unitTestApp.all
unitTestApp.test-results: unitTestApp.all

include $(TOP)/configure/RULES_TOP

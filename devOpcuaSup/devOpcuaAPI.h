/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * DEVOPCUA_API marks symbols of libopcua that are used from outside the library,
 * in particular by the unit tests (which link against libopcua).
 *
 * In Windows DLL builds, EPICS defines EPICS_BUILD_DLL and EPICS_CALL_DLL for all
 * objects, libraries and executables alike. The library sources therefore also
 * define BUILDING_DEVOPCUA_API (see devOpcuaSup/Makefile) to export the symbols,
 * while all other code imports them.
 * Unlike epicsExportSharedSymbols, this does not depend on the order of includes.
 *
 * With GCC, the symbols get default visibility (like epicsShareClass), so a build
 * with -fvisibility=hidden shows missing exports on Linux, too.
 */

#ifndef DEVOPCUA_DEVOPCUAAPI_H
#define DEVOPCUA_DEVOPCUAAPI_H

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(BUILDING_DEVOPCUA_API) && defined(EPICS_BUILD_DLL)
#    define DEVOPCUA_API __declspec(dllexport)
#  elif !defined(BUILDING_DEVOPCUA_API) && defined(EPICS_CALL_DLL)
#    define DEVOPCUA_API __declspec(dllimport)
#  endif
#elif __GNUC__ >= 4
   // Like epicsShareClass: keeps the symbols visible when building with -fvisibility=hidden
#  define DEVOPCUA_API __attribute__((visibility("default")))
#endif

#ifndef DEVOPCUA_API
#  define DEVOPCUA_API
#endif

#endif // DEVOPCUA_DEVOPCUAAPI_H

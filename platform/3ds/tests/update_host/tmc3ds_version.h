#pragma once
/* Host stand-in for the header CMake generates (platform/3ds/CMakeLists.txt).
 * run-update-tests.py passes the real version with -DTMC_PORT_VERSION. */
#ifndef TMC_PORT_VERSION
#define TMC_PORT_VERSION "0.0.0-dev"
#endif
#define TMC_PORT_VERSION_LABEL TMC_PORT_VERSION
#define TMC_UPSTREAM_VERSION "host"
#define TMC_PORT_IS_BETA 0

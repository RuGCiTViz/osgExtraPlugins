cmake_minimum_required(VERSION 2.8.12)

SET(OSG_INCLUDE_DIR "$ENV{UOSG_OP_OT}/vc$ENV{MSVCV}$(PlatformName)/include" CACHE PATH "path to osg generated include files")
SET(OUTPUT_LIBDIR "$ENV{UOSG_OP_OT}/vc$ENV{MSVCV}$(PlatformName)/bin/osgPlugins-3.6.5" CACHE PATH "output path to osgPlugins-x.x.x")
SET(OUTPUT_BINDIR "$ENV{UOSG_OP_OT}/vc$ENV{MSVCV}$(PlatformName)/bin/osgPlugins-3.6.5" CACHE PATH "output path to osg/bin")
SET(CMAKE_INSTALL_PREFIX "$ENV{PROJ_SOURCE_FOLDER}_vc$ENV{MSVCV}$ENV{TP}" CACHE PATH "Install path prefix, prepended onto install directories")
set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo" CACHE STRING "Config types to build")
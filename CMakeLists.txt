cmake_minimum_required(VERSION 3.18)

# Renamed from TSP_ACO to tspaco (2026-09-08): dpkg-deb rejects package names
# containing underscores, so the Linux CI job failed at packaging time.
# "tspaco" is the internal binary/target name only - the user-visible app
# name is still "ACO TSP Solver" (set via DevRes.xml / product info, not
# this name). Solution, project, target and executable are all "tspaco",
# so the build output is Out/tspaco/<Config>/tspaco(.exe/.app).
set(SOLUTION_NAME tspaco)

#BEGIN common part for all solutions
project(${SOLUTION_NAME})
set(SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR})
if (WIN32)
	set(HOME_ROOT $ENV{USERPROFILE})
	string(REPLACE "\\" "/" HOME_ROOT "${HOME_ROOT}")
else()
	set(HOME_ROOT $ENV{HOME})
endif()
set(NATID_SDK_ROOT ${HOME_ROOT}/natID.SDK)
include(${NATID_SDK_ROOT}/DevEnv/Common.cmake) 
#END common part for all solutions



include(${NATID_SDK_ROOT}/DevEnv/natGUI.cmake)
include(${NATID_SDK_ROOT}/DevEnv/MatrixLib.cmake)

include(tspaco.cmake)
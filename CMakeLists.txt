cmake_minimum_required(VERSION 3.18)

# Solution name, executable name and target name are all TSP_ACO, so the build
# output is Out/TSP_ACO/<Config>/TSP_ACO(.exe/.app). Keeping them identical is
# what lets packaging/TSP_ACO.xml and the CI workflow use one name everywhere.
set(SOLUTION_NAME TSP_ACO)

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

include(TSP_ACO.cmake)
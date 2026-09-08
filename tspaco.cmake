# Renamed from TSP_ACO.cmake (2026-09-08) - dpkg-deb rejects underscores in
# package names, so the executable/target name had to lose the underscore.
set(APP_NAME tspaco)

file(GLOB APP_SOURCES  ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
file(GLOB APP_INCS  ${CMAKE_CURRENT_LIST_DIR}/src/*.h)
file(GLOB APP_INC_TD  ${NATID_SDK_INC}/td/*.h)

file(GLOB APP_INC_GUI ${NATID_SDK_INC}/gui/*.h)
file(GLOB APP_INC_THREAD  ${NATID_SDK_INC}/thread/*.h)
file(GLOB APP_INC_CNT  ${NATID_SDK_INC}/cnt/*.h)
file(GLOB APP_INC_FO  ${NATID_SDK_INC}/fo/*.h)
file(GLOB APP_INC_XML  ${NATID_SDK_INC}/xml/*.h)

#Application icon
set(APP_PLIST  ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/AppIcon.plist)
if(WIN32)
	set(APP_WINAPP_ICON ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/winAppIcon.rc)
else()
	set(APP_WINAPP_ICON ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/winAppIcon.cpp)
endif()

# add executable
add_executable(${APP_NAME} ${APP_INCS} ${APP_SOURCES} ${APP_INC_TD} ${APP_INC_THREAD} 
				${APP_INC_CNT} ${APP_INC_FO} ${APP_INC_GUI} ${APP_INC_XML} ${APP_WINAPP_ICON})

source_group("inc"            FILES ${APP_INCS})
source_group("inc\\td"        FILES ${APP_INC_TD})
source_group("inc\\cnt"        FILES ${APP_INC_CNT})
source_group("inc\\fo"        FILES ${APP_INC_FO})
source_group("inc\\gui"        FILES ${APP_INC_GUI})
source_group("inc\\thread"        FILES ${APP_INC_THREAD})
source_group("inc\\xml"        FILES ${APP_INC_XML})
source_group("src"            FILES ${APP_SOURCES})

target_link_libraries(${APP_NAME} debug ${MU_LIB_DEBUG} debug ${NATGUI_LIB_DEBUG} debug ${MATRIX_LIB_DEBUG}
										optimized ${MU_LIB_RELEASE} optimized ${NATGUI_LIB_RELEASE} optimized ${MATRIX_LIB_RELEASE})

setTargetPropertiesForGUIApp(${APP_NAME} ${APP_PLIST})

setAppIcon(${APP_NAME} ${CMAKE_CURRENT_LIST_DIR})

setIDEPropertiesForGUIExecutable(${APP_NAME} ${CMAKE_CURRENT_LIST_DIR})

setPlatformDLLPath(${APP_NAME})

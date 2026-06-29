set(TARGET_NAME osgdb_qcrop)
set(TARGET_SRC
	ReaderWriterQcrop.cpp
)
set(TARGET_H
)

set(DYNAMIC_OPENSCENEGRAPH 1)

add_definitions(-D_USRDLL)

SETUP_PLUGIN(${TARGET_NAME})
target_link_libraries(${TARGET_NAME} ${OPENSCENEGRAPH_LIBRARIES})

SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY FOLDER "Plugins")

# these don't seem to work, cmake places them in plugins\qcrop\osgdb_qcrop.vcxproj - but debugger will not start unless they are in plugins\qcrop\osgdb_qcrop.vcxproj.user
# LV 2026/6/24 - cmake version 3.23.1 - Visual Studio Community 2022 (64-bit) - Current Version 17.14.35 (June 2026)

# SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_COMMAND "$(OSG_BUILD)\bin\osgviewer.exe")
# SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_COMMAND_DEBUG "$(OSG_BUILD)\bin\osgviewerd.exe")
# 
# SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_COMMAND_ARGUMENTS "--image dog_left_eye.jpg.lb.qcrop")
# SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_ENVIRONMENT "OSG_NOTIFY_LEVEL=NOTICE")
# SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$(TargetDir)")

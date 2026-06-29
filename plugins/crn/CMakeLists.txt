set(TARGET_NAME osgdb_crn)
set(TARGET_SRC
	ReaderWriterCRN.cpp
)
set(TARGET_H
	inc/crn_decomp.h
	inc/dds_defs.h
	inc/crnlib.h
)

set(DYNAMIC_OPENSCENEGRAPH 1)

add_definitions(-D_USRDLL)

SETUP_PLUGIN(${TARGET_NAME})
target_link_libraries(${TARGET_NAME} ${OPENSCENEGRAPH_LIBRARIES})

SET_PROPERTY(TARGET ${TARGET_NAME} PROPERTY FOLDER "Plugins")

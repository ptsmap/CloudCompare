
target_sources( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/ccVSGWindow.h
		${CMAKE_CURRENT_LIST_DIR}/ccVSGWindowInterface.h
		${CMAKE_CURRENT_LIST_DIR}/qCC_vsgWindow.h
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGCameraAdapter.h
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGCameraManipulator.h
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGPointCloudBuilder.h
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGSceneBuilder.h
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGShaders.h
)

target_include_directories( ${PROJECT_NAME}
	PUBLIC
		${CMAKE_CURRENT_LIST_DIR}
)

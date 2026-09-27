
target_sources( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/ccVSGWindow.cpp
		${CMAKE_CURRENT_LIST_DIR}/ccVSGWindowInterface.cpp
		${CMAKE_CURRENT_LIST_DIR}/ccVSGWindowSignalEmitter.cpp
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGCameraAdapter.cpp
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGCameraManipulator.cpp
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGMeshBuilder.cpp
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGOverlayBuilder.cpp
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGPointCloudBuilder.cpp
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGSceneBuilder.cpp
		${CMAKE_CURRENT_LIST_DIR}/vsg/ccVSGShaders.cpp
)

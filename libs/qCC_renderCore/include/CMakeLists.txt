
target_sources( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/ccRenderBackend.h
		${CMAKE_CURRENT_LIST_DIR}/ccRenderCapabilities.h
		${CMAKE_CURRENT_LIST_DIR}/ccRenderCommandSink.h
		${CMAKE_CURRENT_LIST_DIR}/ccViewInterface.h
		${CMAKE_CURRENT_LIST_DIR}/qCC_renderCore.h
)

target_include_directories( ${PROJECT_NAME}
	PUBLIC
		${CMAKE_CURRENT_LIST_DIR}
)

project( QCC_DB_LIB )

add_library( ${PROJECT_NAME} SHARED )

add_subdirectory( include )
add_subdirectory( src )

# ccViewSignalEmitter is only referenced through the derived emitters of the
# other libraries, so AUTOMOC does not scan it: generate its moc code here.
qt6_wrap_cpp( QCC_DB_MOC_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/include/ccViewSignalEmitter.h )
target_sources( ${PROJECT_NAME} PRIVATE ${QCC_DB_MOC_SOURCES} )

add_subdirectory( extern/CCCoreLib EXCLUDE_FROM_ALL )

target_link_libraries( ${PROJECT_NAME}
	PUBLIC
    	CCCoreLib
		QCC_RENDER_CORE_LIB
		Qt6::Widgets
		Qt6::OpenGL
		CC_FBO_LIB
)

target_compile_definitions( ${PROJECT_NAME} PRIVATE QCC_DB_LIBRARY_BUILD QT_NO_KEYWORDS )

set_target_properties( ${PROJECT_NAME} PROPERTIES
	CXX_VISIBILITY_PRESET hidden
)

InstallSharedLibrary( TARGET CCCoreLib )
InstallSharedLibrary( TARGET ${PROJECT_NAME} )

include( cmake/InstallCGALDependencies.cmake )

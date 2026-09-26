# Ordered by dependency
add_subdirectory( qCC_renderCore )
add_subdirectory( CCFbo )
add_subdirectory( qCC_db )
add_subdirectory( qCC_io )
add_subdirectory( qCC_glWindow )

if ( CC_RENDER_VSG )
	add_subdirectory( qCC_vsgWindow )
endif()

add_subdirectory( CCPluginStub )
add_subdirectory( CCPluginAPI )
add_subdirectory( CCAppCommon )

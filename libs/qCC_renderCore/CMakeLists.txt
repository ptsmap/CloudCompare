project( QCC_RENDER_CORE_LIB )

add_library( ${PROJECT_NAME} SHARED )

add_subdirectory( include )
add_subdirectory( src )

target_compile_definitions( ${PROJECT_NAME} PRIVATE CCRENDERCORE_LIB_BUILD )

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

InstallSharedLibrary( TARGET ${PROJECT_NAME} )

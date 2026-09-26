project( QCC_VSG_LIB )

add_library( ${PROJECT_NAME} SHARED )

add_subdirectory( include )
add_subdirectory( src )

target_compile_definitions( ${PROJECT_NAME} PRIVATE CCVSGWINDOW_LIB_BUILD )

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        QCC_RENDER_CORE_LIB
        QCC_DB_LIB
        Qt6::Widgets
        vsg::vsg
        vsgQt::vsgQt
)

InstallSharedLibrary( TARGET ${PROJECT_NAME} )

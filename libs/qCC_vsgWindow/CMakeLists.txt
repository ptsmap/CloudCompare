project( QCC_VSG_LIB )

add_library( ${PROJECT_NAME} SHARED )

add_subdirectory( include )
add_subdirectory( src )

target_compile_definitions( ${PROJECT_NAME} PRIVATE CCVSGWINDOW_LIB_BUILD )

# qCC_glWindow include dir: only needed for ccGuiParameters.h (the persistent
# display parameters). qCC_vsgWindow does not use the OpenGL renderer itself.
target_include_directories( ${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../qCC_glWindow/include
)

# freetype: used to rasterize the glyph atlas of the 2D overlay font (M5.2).
# Optional - when it is missing the VSG text support is simply disabled.
find_path( CC_VSG_FREETYPE_INCLUDE_DIR
    NAMES ft2build.h
    PATHS /opt/homebrew/include/freetype2 /usr/local/include/freetype2 /usr/include/freetype2
)
find_library( CC_VSG_FREETYPE_LIBRARY
    NAMES freetype
    PATHS /opt/homebrew/lib /usr/local/lib /usr/lib
)

if( CC_VSG_FREETYPE_INCLUDE_DIR AND CC_VSG_FREETYPE_LIBRARY )
    message( STATUS "VSG: freetype found -> text support enabled" )
    target_compile_definitions( ${PROJECT_NAME} PRIVATE CC_VSG_HAS_FREETYPE )
    target_include_directories( ${PROJECT_NAME} PRIVATE ${CC_VSG_FREETYPE_INCLUDE_DIR} )
    target_link_libraries( ${PROJECT_NAME} PRIVATE ${CC_VSG_FREETYPE_LIBRARY} )
else()
    message( STATUS "VSG: freetype NOT found -> text support disabled" )
endif()

target_link_libraries( ${PROJECT_NAME}
    PUBLIC
        QCC_RENDER_CORE_LIB
        QCC_DB_LIB
        # only for ccGui::Parameters() (the persistent display parameters):
        # qCC_vsgWindow does not use any of the OpenGL rendering of qCC_glWindow
        QCC_GL_LIB
        Qt6::Widgets
        vsg::vsg
        vsgQt::vsgQt
)

InstallSharedLibrary( TARGET ${PROJECT_NAME} )

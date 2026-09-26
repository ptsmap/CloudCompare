# ------------------------------------------------------------------------------
# Qt
# ------------------------------------------------------------------------------

set( CMAKE_AUTOMOC ON )
set( CMAKE_AUTORCC ON )

# FIXME Eventually turn this on when we've completed the move to targets
#set( CMAKE_AUTOUIC ON )
find_package( Qt6
    COMPONENTS
        Concurrent
        Core
        Gui
        OpenGL
		OpenGLWidgets
        PrintSupport
        Svg
        Widgets
    REQUIRED
)

# turn on QStringBuilder for more efficient string construction
#	see https://doc.qt.io/qt-6/qstring.html#more-efficient-string-construction
add_definitions( -DQT_USE_QSTRINGBUILDER )
				

# ------------------------------------------------------------------------------
# OpenGL
# ------------------------------------------------------------------------------

if ( UNIX )
	set(OpenGL_GL_PREFERENCE GLVND)
endif()

if ( MSVC )
	# Where to find OpenGL libraries
	set(WINDOWS_OPENGL_LIBS "C:\\Program Files (x86)\\Windows Kits\\8.0\\Lib\\win8\\um\\x64" CACHE PATH "WindowsSDK libraries" )
	list( APPEND CMAKE_PREFIX_PATH ${WINDOWS_OPENGL_LIBS} )
endif()
				
# ------------------------------------------------------------------------------
# OpenMP
# ------------------------------------------------------------------------------

if ( NOT APPLE )
	find_package(OpenMP QUIET)
	if (OPENMP_FOUND)
		message(STATUS "OpenMP found")
		set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
		set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
	endif()
endif()

# ------------------------------------------------------------------------------
# VulkanSceneGraph (VSG render backend)
#
# If the dependencies of the VSG backend can't be found we simply disable it
# (instead of failing the whole configuration) so that building the legacy
# OpenGL backend always keeps working.
# ------------------------------------------------------------------------------

if ( CC_RENDER_VSG )
	find_package( vsg 1.1 QUIET )

	if ( NOT vsg_FOUND )
		message( WARNING "vsg not found - the VSG render backend is disabled."
		                 " Set vsg_DIR to the directory containing vsgConfig.cmake to enable it." )
		set( CC_RENDER_VSG FALSE )
	endif()
endif()

if ( CC_RENDER_VSG )
	# vsgQt provides the QWindow based integration used to embed a VSG window
	# inside the Qt interface. It must be built against the same Qt version as
	# CloudCompare (i.e. with -DQT_PACKAGE_NAME=Qt6).
	set( vsgQt_DIR "" CACHE PATH "Directory containing vsgQtConfig.cmake" )

	if ( vsgQt_DIR )
		list( APPEND CMAKE_PREFIX_PATH "${vsgQt_DIR}" )
	endif()

	find_package( vsgQt QUIET )

	if ( NOT vsgQt_FOUND )
		message( WARNING "vsgQt not found - the VSG render backend is disabled."
		                 " Build vsgQt against Qt6 and set vsgQt_DIR accordingly." )
		set( CC_RENDER_VSG FALSE )
	endif()
endif()

if ( CC_RENDER_VSG )
	# Optional: vsgXchange provides readers/writers for 3rd party images and
	# models (PNG/JPEG/fonts/...) used by the VSG backend.
	find_package( vsgXchange QUIET )

	message( STATUS "VSG backend enabled: vsg=${vsg_VERSION} vsgXchange=${vsgXchange_FOUND}" )
endif()


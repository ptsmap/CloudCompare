cmake_minimum_required(VERSION 3.5)

project(qCC_vsgWindow)

# 查找VSG依赖
find_package(vsg REQUIRED)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VSG_INCLUDE_DIRS}
)

# 源文件
set(SRC
    src/ccVSGWindow.cpp
    src/ccVSGWindowStereo.cpp
    src/ccVSGUtils.cpp
)

# 头文件
set(HEADERS
    include/ccVSGWindow.h
    include/ccVSGWindowStereo.h
    include/ccVSGUtils.h
)

# 创建库
add_library(qCC_vsgWindow ${SRC} ${HEADERS})

target_link_libraries(qCC_vsgWindow
    PRIVATE
    vsg::vsg
)

# 安装规则
install(TARGETS qCC_vsgWindow DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
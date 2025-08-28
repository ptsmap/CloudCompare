# CloudCompare 渲染引擎迁移至VSG的代码修改分析

## 需要修改的核心模块
1. **qCC_glWindow模块** - 主要OpenGL渲染实现

## 需要修改的关键文件
- `libs/qCC_glWindow/src/ccGLWindow.cpp`
- `libs/qCC_glWindow/src/ccGLWindowStereo.cpp`
- `libs/qCC_glWindow/include/ccGLWindow.h`
- `libs/qCC_glWindow/include/ccGLWindowStereo.h`
- `libs/qCC_glWindow/src/ccGLUtils.cpp`

## 需要替换的OpenGL关键功能
1. **窗口管理**
   - QOpenGLWidget/QWindow替换为VSG窗口
   - OpenGL上下文管理

2. **渲染管线**
   - glViewport/glClear等基础函数
   - 纹理处理(QOpenGLTexture)
   - 帧缓冲对象(FBO)管理

3. **3D渲染**
   - 顶点缓冲/索引缓冲
   - 着色器程序
   - 矩阵变换

4. **立体显示支持**
   - Oculus VR支持
   - 立体渲染管线

## 具体TODO列表

### 1. 基础架构迁移
- [ ] 创建VSG窗口类继承体系
- [ ] 实现VSG渲染上下文管理
- [ ] 移植Qt事件处理系统

### 2. 渲染功能替换
- [ ] 替换glViewport/glClear等基础函数
- [ ] 实现VSG纹理系统
- [ ] 移植帧缓冲管理代码

### 3. 3D渲染功能
- [ ] 移植顶点/索引缓冲代码
- [ ] 实现VSG着色器系统
- [ ] 移植矩阵变换代码

### 4. 立体显示支持
- [ ] 移植Oculus VR支持
- [ ] 实现VSG立体渲染管线

### 5. 兼容性处理
- [ ] 保持现有接口兼容性
- [ ] 处理平台特定代码(MacOS)
- [ ] 更新CMake构建系统
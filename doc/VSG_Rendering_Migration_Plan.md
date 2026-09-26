# CloudCompare 渲染引擎迁移至 VulkanSceneGraph (VSG) 方案设计

| 项目 | 内容 |
|---|---|
| 文档版本 | v1.0（初版方案） |
| 编写日期 | 2026-09-26 |
| 目标仓库 | `CloudCompareVSG/CloudCompare` |
| 渲染引擎 | `CloudCompareVSG/VulkanSceneGraph` (VSG **1.1.14**, SOVERSION 16, C++17) |
| 目标 | 以 VSG(Vulkan) 渲染后端替换/并存于现有 OpenGL 渲染子系统 |
| 相关文件 | `CloudCompare/VSG_Migration_TODO.md`、`CloudCompare/CodeModificationAnalysis.md`（旧草稿，本文档取代之） |

---

## 0. 摘要（TL;DR）

1. **替换边界不在 `ccGLWindow` / `QOpenGLWidget` 这一层。** OpenGL 已经渗透到 `qCC_db` 的数据对象里（`ccPointCloud::drawMeOnly()`、`ccMesh::drawMeOnly()`、`cc2DLabel`、`ccCameraSensor`、`ccOctree` 等），全仓约 **1125 处** 低层 `glXXX()` 调用分布在 ~38 个文件中。只换窗口库无法完成迁移。
2. **不能机械地把 `glXXX()` 翻译成 VSG 调用。** 现有代码大量依赖 OpenGL 固定管线：`glBegin/glEnd`（~75 处）、`glMatrixMode`（~51 处）、`glPushMatrix`（~25 处）、`glPushAttrib`（~53 处）。Vulkan 没有矩阵栈、没有属性栈、没有 immediate mode。
3. **推荐路线：新增"后端无关渲染抽象层" + VSG 场景图镜像，OpenGL 与 VSG 双后端长期并存。** 保留 `ccHObject` 数据模型与相机数学，重建"场景同步 + 渲染提交"层。
4. **VSG 后端应采用"增量同步的可执行场景图"，而不是"每帧遍历 ccHObject 树逐对象下发命令"。** 后者会放弃 VSG 的视锥裁剪、编译期资源分配、`PagedLOD`/`DatabasePager` 分页等核心优势。
5. **三个高危平台特性必须先做技术验证（Spike）**：点云 **PointSize**（MoltenVK/Metal 不支持 >1）、**线宽 wideLines**（多数设备不支持 >1）、**中文字体 SDF**（`vsg::Text` 需 SDF 字体图集）。这三项直接决定交互体验能否对齐。
6. **建议分 8 个里程碑推进**，第一个可交互里程碑（空窗口 + 相机 + 点云 + 网格）预估 6~8 人周，完整功能对齐预估 6~9 人月（含后处理与插件迁移）。

---

## 1. 目标、范围与非目标

### 1.1 目标

- **G1**：引入 VSG(Vulkan) 渲染后端，可渲染点云、网格、折线等核心实体，性能不低于现有 OpenGL 后端（大数据量场景应显著优于）。
- **G2**：保留 CloudCompare 的全部交互语义（旋转/平移/缩放/pivot/点大小/裁剪面/拾取/标签/比例尺/色标），UI 层与插件层改动最小化。
- **G3**：过渡期内 OpenGL 与 VSG **双后端并存且可运行时/编译期切换**，确保任何阶段都有可交付、可回退的版本。
- **G4**：为后续大规模数据（十亿点级）铺路，利用 `PagedLOD` + `DatabasePager` 做外存分页。

### 1.2 范围内

- `libs/qCC_glWindow`（窗口/相机/交互/绘制调度）
- `libs/qCC_db` 中与绘制直接相关的 `draw()` / `drawMeOnly()` 路径与 GPU 缓存
- `libs/CCFbo`（离屏渲染、后处理）
- `libs/CCPluginAPI` / `CCPluginStub` 暴露给插件的窗口与 GL 接口
- CMake 构建系统与依赖管理
- `qCC` / `ccViewer` 两个应用壳

### 1.3 非目标（一期不做）

- 算法层（CCCoreLib、PCL、八叉树算法）**不动**，只复用其结果。
- 文件格式 I/O（`qCC_io`）**不动**。
- 立体显示（Quad-buffer / `ccGLWindowStereo`）一期降级为"不支持"或"单视口分屏"，见 §7.9。
- 不追求一次性删除 OpenGL 代码；OpenGL 后端在过渡期内继续维护。

---

## 2. 现状盘点：CloudCompare 渲染子系统

### 2.1 分层架构（现状）

```
┌──────────────────────────────────────────────────────────────┐
│  应用层                                                        │
│  qCC (MainWindow / ccMainAppInterface)   ccViewer             │
│  plugins (core/Standard/*, core/IO/*)                         │
└───────────────────────────┬──────────────────────────────────┘
                            │ 持有 ccGLWindowInterface*
┌───────────────────────────▼──────────────────────────────────┐
│  窗口/渲染层  libs/qCC_glWindow                                │
│  ┌────────────────────────────────────────────────────────┐  │
│  │ ccGLWindowInterface : ccGenericGLDisplay                │  │
│  │   · 场景根 (global DB / window own DB)                  │  │
│  │   · 相机 + ccViewportParameters + 投影矩阵生成           │  │
│  │   · 绘制调度 doPaintGL()/fullRenderingPass()            │  │
│  │   · FBO / 拾取 / 文本 / 灯光 / 交互 / 立体              │  │
│  │   ≈ 1597 行 .h + 7456 行 .cpp，28 个纯虚函数             │  │
│  └───────┬──────────────────────────────┬─────────────────┘  │
│          │                              │                    │
│  ccGLWindow                     ccGLWindowStereo              │
│  (QOpenGLWidget)                (QWindow + 自管 GL Context)    │
│                                                               │
│  辅助：ccGLUtils / ccGuiParameters / ccRenderingTools          │
│        ccGLWindowSignalEmitter（为非 QObject 类提供信号）        │
└───────────────────────────┬──────────────────────────────────┘
                            │ 递归调用 obj->draw(context)
┌───────────────────────────▼──────────────────────────────────┐
│  数据对象层  libs/qCC_db                                       │
│  ccHObject::draw()  →  压矩阵栈 / 裁剪面 / 颜色                │
│                     →  子类 drawMeOnly(context)                │
│                     →  递归 children                          │
│  ccPointCloud / ccMesh / ccPolyline / ccFacet / ccImage /      │
│  cc2DLabel / cc2DViewportLabel / ccGBLSensor / ccCameraSensor /│
│  ccOctreeProxy  ← 这些类内部直接调用 QOpenGLFunctions_2_1       │
└───────────────────────────┬──────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────┐
│  GPU 资源层                                                    │
│  ccPointCloud 内嵌 VBO(chunk) / ccMesh 全局静态 streaming VBO  │
│  ccMaterialDB (QOpenGLTexture) / ccGLSLHelper (shader 缓存)    │
│  ccOctree display list / ccGLWindowInterface 文字纹理池         │
│  CCFbo: ccFrameBufferObject / ccShader / ccGlFilter            │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 关键类职责

| 文件 | 行数 | 职责 | 迁移影响 |
|---|---:|---|---|
| `libs/qCC_glWindow/include/ccGLWindowInterface.h` | 1597 | **实际主渲染器**：场景根、相机、投影、绘制调度、FBO、拾取、文本、灯光、立体、交互 | 必须拆分 |
| `libs/qCC_glWindow/src/ccGLWindowInterface.cpp` | 7456 | 上述实现，含 ~300 处 `glXXX()` | 必须重写 |
| `libs/qCC_glWindow/include/ccGLWindow.h` | ~220 | `QOpenGLWidget` 适配，事件转发 | 后端专有 |
| `libs/qCC_glWindow/src/ccGLWindowStereo.cpp` | — | `QWindow` + 自建 `QOpenGLContext` + 手动 swapBuffers | 一期降级 |
| `libs/qCC_glWindow/include/ccGuiParameters.h` | ~159 | 全局显示参数（颜色、灯光、LOD 阈值、点/线宽、文字、拾取） | 可复用（后端无关） |
| `libs/qCC_glWindow/src/ccRenderingTools.cpp` | — | 深度图可视化 + 标量场色标绘制 | 需后端化 |
| `libs/qCC_glWindow/src/ccGLUtils.cpp` | ~176 | 2D 纹理贴图、标准视角矩阵 | 视角矩阵可复用 |
| `libs/qCC_db/src/ccHObject.cpp` | `draw()` 748-855 | 树遍历 + 矩阵栈 + 裁剪面 + 递归 | **核心改造点** |
| `libs/qCC_db/src/ccPointCloud.cpp` | ~105 处 GL | VBO 分块缓存 + GLSL 快速路径 + immediate 回退 | **核心改造点** |
| `libs/qCC_db/src/ccMesh.cpp` | ~53 处 GL | 4 个全局静态 streaming VBO + 三角形展开 | **核心改造点** |
| `libs/qCC_db/src/cc2DLabel.cpp` | ~78 处 GL | 3D marker + 2D 连线/文本/面板 | 改造点 |
| `libs/qCC_db/src/ccCameraSensor.cpp` | ~50 处 GL | immediate-mode 视锥/箭头 | 改造点 |
| `libs/qCC_db/src/ccOctree.cpp` | ~33 处 GL | display list 缓存八叉树单元 | 改造点 |
| `libs/CCFbo/src/ccFrameBufferObject.cpp` | ~74 处 GL | FBO 封装（3D 层缓存/后处理/拾取/截图） | 后端专有重写 |

### 2.3 渲染主循环与 Pass 顺序

`ccGLWindow::paintGL()` → `ccGLWindowInterface::doPaintGL()`
（`src/ccGLWindowInterface.cpp:4794-4963`）：

```
initPaintGL()
  → 构造 CC_DRAW_CONTEXT
  → 判断是否需要重绘（无 FBO / FBO 失效 / 截图模式 / LOD 渐进中）
  → fullRenderingPass(左眼或单眼)
  → [立体] fullRenderingPass(右眼)
  → swapGLBuffers()
  → 更新 auto-pivot / LOD，按 ~50ms 节奏调度下一层
```

单次 `fullRenderingPass()`（`:5217-5532`）顺序：

| # | Pass | 内容 | VSG 对应 |
|---|---|---|---|
| 1 | Background | 清 depth/color，纯色或渐变 | `RenderGraph::clearValues` |
| 2 | 3D | 点大小/线宽/深度测试 → 灯光 → shader → LOD → 投影/模型视图 → global DB → window DB → pivot → `drawing3D` 回调 | 主 `RenderGraph` + 场景图 |
| 3 | — | 结束 FBO | — |
| 4 | Post | 可选 GL filter（EDL / SSAO / Bilateral） | 离屏 `RenderGraph` + 全屏三角形 |
| 5 | Blit | FBO 输出纹理铺到屏幕 | `vsg::CopyImageViewToWindow` |
| 6 | Foreground | 2D 对象递归、色标、比例尺、方向轴、GL filter banner、消息、热区、LOD 指示器 | 嵌套 `RenderGraph`（独立 `ViewportState` + 正交投影） |

### 2.4 可绘制对象体系

```
ccDrawableObject            ← draw() 唯一纯虚入口 (ccDrawableObject.h:47-50)
        ▲
     ccHObject  (+ ccObject)   draw(): 可见性 → 压矩阵 → 默认颜色 → 裁剪面
        ▲                              → drawMeOnly(ctx) → 递归 children → 包围盒
        │
   ccShiftedObject
     ├─ ccGenericPointCloud ─ ccPointCloud
     ├─ ccGenericMesh ────── ccMesh ─ ccGenericPrimitive
     └─ ccPolyline
   ccHObject 其他：ccFacet / ccImage / cc2DLabel / cc2DViewportLabel
              ccSensor ─ ccGBLSensor / ccCameraSensor / ccOctreeProxy
```

**关键事实**：`ccDrawableObject` **本身不含** VBO/VAO/纹理 ID/shader（`ccDrawableObject.h:361-408`），GPU 资源分散在 `ccPointCloud`（内嵌 chunk VBO）、`ccMesh.cpp`（4 个静态全局 VBO）、`ccMaterialDB`（纹理）、`ccGLSLHelper`（shader 缓存）、`ccOctree`（display list）。这既是坏消息（分散）也是好消息（数据类本身相对干净，GPU 资源可以整体外置）。

### 2.5 OpenGL 侵入面统计

**显式 OpenGL include**（`QOpenGL*` / `QtOpenGL*` / `GL/glew.h` / `QGLWidget`）：约 **27 个文件 / 39 条**。
注意 `libs/qCC_db/include/ccIncludeGL.h:28-133` 本身包装了大量 2.1 固定管线调用，因此显式 include 数量**严重低估**真实耦合。

**低层 `glXXX()` 调用点**（保守扫描）：

| 目录 | 调用点 | 涉及文件 | 热点文件 |
|---|---:|---:|---|
| `libs/qCC_db` | ~508 | ~22 | `ccPointCloud.cpp`(105)、`cc2DLabel.cpp`(78)、`ccMesh.cpp`(53)、`ccCameraSensor.cpp`(50)、`ccOctree.cpp`(33) |
| `libs/qCC_glWindow` | ~358 | 5 | `ccGLWindowInterface.cpp`(~300) |
| `libs/CCFbo` | ~74 | 2 | `ccFrameBufferObject.cpp`、`ccShader.cpp` |
| `plugins` | ~185 | 9 | qSSAO(42)、qEDL(37)、qPCV(30)、qCompass(54) |
| **合计** | **~1125** | **~38** | — |

**固定管线使用量**：`glBegin` ~75、`glMatrixMode` ~51、`glPushMatrix` ~25、`glPushAttrib` ~53。
**VAO**：全仓未发现 `glBindVertexArray` / `QOpenGLVertexArrayObject`。

### 2.6 支撑设施

| 设施 | 现状 | VSG 迁移要点 |
|---|---|---|
| 离屏渲染 | `CCFbo::ccFrameBufferObject`（`glGenFramebuffers` / `glFramebufferTexture2D`），用于 3D 层缓存、后处理、颜色拾取、高清截图 | 用离屏 `vsg::RenderGraph` + `vsg::ImageView`；回读用 `vsg::CopyImageToBuffer` |
| Shader | `ccShader`（`QOpenGLShaderProgram` 薄封装，读 `.vert/.frag`）；`ccGLSLHelper` 按 attribute 位组合**动态拼接 GLSL 1.20** 并缓存 | 改为预编译 SPIR-V 或运行时 `vsg::ShaderCompiler`（需 glslang）；组合爆炸需靠 `ShaderSet` 变体管理 |
| 颜色拾取 | 离屏 FBO 以 **24-bit RGB 编码实体 ID** + `glReadPixels`（`ccColorBasedEntityPicking.h`） | Vulkan 建议 `R32_UINT` 整数 attachment，直接编码 UniqueID，精度更高 |
| 点/三角拾取 | CPU 投影 + 八叉树 ray picking，另有 PBO 深度读回（`ccGLWindowInterface.cpp:4145-4252`） | CPU 侧算法可完全复用；深度读回改为 depth image copy to buffer |
| 文本 | `ccGLWindowInterface::renderText()`（`:3785-3953`），逐次构造纹理 | `vsg::Text` + SDF 字体；屏幕空间文本走独立 overlay `RenderGraph` + 正交投影 |
| 材质/纹理 | `ccMaterial` / `ccMaterialDB` 持 `QOpenGLTexture` | `vsg::DescriptorImage` + `vsg::Sampler`；需要迁移图片解码（无 vsgXchange，见 §3.6） |
| 后处理 | `ccGlFilter`（`clone/init/shade/getTexture`），EDL / SSAO / Bilateral | 重写为 VSG 后处理 pass |

### 2.7 相机与交互语义

- 相机状态在 `ccViewportParameters`（`qCC_db/include/ccViewportParameters.h:123-165`）：旋转矩阵 `viewMat`、pivot point、camera center、focal distance、FOV、aspect、zNear/zFar、透视/正交、object-centered/viewer-centered。
- 投影矩阵由 `ccGLWindowInterface` 根据可见对象包围盒动态计算 near/far（`:1524-1702`），透视用 `ccGL::Frustum`，正交用 `ccGL::Ortho`。
- 交互映射（`ccGLWindowInterface.cpp:6381-7036`）：

| 输入 | 行为 |
|---|---|
| 左键拖动 | 旋转（虚拟 trackball）/ 标签拖动 / 矩形框选 |
| 右键拖动 | 平移 pan |
| 中键拖动 | 相机缩放 |
| 双击 | 从深度反投影 → 设置 pivot |
| 滚轮 | 相机缩放；**Alt = 点大小**；**Ctrl = near/far 裁剪**；**Shift = FOV** |

> **注意**：CC 的"pivot-point + object-centered"相机模型与 VSG `vsg::Trackball` 的"eye/center/up + 屏幕空间 rotate"模型**语义不同**。直接套用 `Trackball` 会丢失 pivot、object-centered view、手动缩放语义，需要自定义操控器（§5.6）。

### 2.8 插件耦合

- `ccMainAppInterface` 直接暴露 `getActiveGLWindow()` / `createGLWindow()` / `destroyGLWindow()` / `disableAllBut(ccGLWindowInterface*)`（`CCPluginAPI/include/ccMainAppInterface.h:37-62, 189-255`）。
- `ccGLPluginInterface` 直接返回 `ccGlFilter*`（`CCPluginStub/include/ccGLPluginInterface.h:26-52`）。
- **直接发出低层 GL 调用的插件 6 个**：qEDL、qSSAO、qPCV、qSRA、qCompass、qCloudLayers。
- **直接使用 `ccGLWindowInterface*` 的 Standard 插件约 11 个**：qBroom、qCanupo、qJSonRPCPlugin、qSRA、qMasonry、qCompass、qMPlane、qHPR、qCloudLayers、qAnimation、qColorimetricSegmenter。
- 源码层有渲染耦合的插件模块约 **22 个**；**构建层则是全部插件都被绑定 OpenGL**（`AddPlugin` → `CCPluginAPI` → PUBLIC `QCC_GL_LIB`，`plugins/cmake/Plugins.cmake:94-99`）。

### 2.9 CMake 现状与问题

依赖顺序（`libs/CMakeLists.txt:1-8`）：
```
CCFbo → qCC_db → qCC_io → qCC_glWindow → CCPluginStub → CCPluginAPI → CCAppCommon
```
- `QCC_DB_LIB` PUBLIC 链接 `Qt6::OpenGL` + `CC_FBO_LIB`（`libs/qCC_db/CMakeLists.txt:10-16`）
- `QCC_GL_LIB` PUBLIC 链接 `QCC_DB_LIB` + `Qt6::OpenGLWidgets`，PRIVATE `CC_FBO_LIB`
- Qt OpenGL 组件全局 REQUIRED（`cmake/CMakeExternalLibs.cmake:10-20`）
- **没有任何渲染后端开关**（无 `QCC_OPENGL` / `RENDER_BACKEND`）
- 顶层 `CMakeLists.txt:64-68` 已经硬编码了 VSG：
  ```cmake
  set(VSG_DIR "/Users/gsl/work/pointsMap/CloudCompareVSG/VulkanSceneGraph")
  find_package(VSG REQUIRED)     # ← 大小写与目标名都不对，VSG 导出的是 vsg::vsg
  include_directories(${VSG_INCLUDE_DIRS})
  link_directories(${VSG_LIBRARY_DIRS})
  ```
  这在协作/CI 环境下不可用，必须改为 `find_package(vsg REQUIRED)` + 链接 `vsg::vsg`。

### 2.10 已有 VSG 原型诊断（`libs/qCC_vsgWindow`）

已存在 4 个文件（`ccVSGWindow.h/.cpp`、`ccVSGWindowInterface.h/.cpp`）+ `CMakeLists.txt`，但**不可编译、未接入构建**，属于废弃草稿：

| # | 缺陷 | 位置 |
|---|---|---|
| 1 | 未加入 `libs/CMakeLists.txt`，不在目标图中 | `libs/CMakeLists.txt` |
| 2 | `CMakeLists.txt` 引用不存在的 `ccVSGWindowStereo.cpp/.h`、`ccVSGUtils.cpp/.h`，且遗漏实际存在的源文件 | `libs/qCC_vsgWindow/CMakeLists.txt:15-26` |
| 3 | `#include "qCC_vsgWindow.h"` 文件不存在；`CCVSGWINDOW_LIB_API` 未定义 | `ccVSGWindowInterface.h:4` |
| 4 | `#include <vsg/viewer/Viewer.h>`、`<vsg/ui/Window.h>` **头文件不存在**（正确路径为 `<vsg/app/Viewer.h>`、`<vsg/app/Window.h>`） | `ccVSGWindow.h:5-6` |
| 5 | `ccVSGWindow : vsg::Inherit<vsg::Window, ccVSGWindow>` —— `vsg::Window` 不是 `QWidget`，无法嵌入 Qt MDI；应基于 `vsgQt::Window`（`QWindow`）+ `QWidget::createWindowContainer` | `ccVSGWindow.h:12` |
| 6 | 同时 `Q_OBJECT` 与 `vsg::Object` 多继承，未继承 `QObject`，元对象系统不可用 | `ccVSGWindow.h:14` |
| 7 | `redraw(bool)` 与基类 `ccGenericGLDisplay` 要求的签名不一致 | `ccVSGWindowInterface.h:112` |
| 8 | 调用了 `ccViewportParameters` 不存在的方法 `setCameraPosition()` / `moveCamera()` / `setZoom()` / `setViewDirection()` | `ccVSGWindow.cpp:112-151, 239-250` |
| 9 | 场景 DB → VSG 场景图转换、相机、交互全是 TODO / 空实现 | `ccVSGWindow.cpp:64-77, 258-326` |

**结论**：该原型提供了"接口长什么样"的雏形，但不具备可运行链路，建议按本文档方案**重写**而非修补。

---

## 3. 现状盘点：VulkanSceneGraph 能力

### 3.1 模块地图（`include/vsg`）

| 目录 | 职责 | CloudCompare 会用到的代表类 |
|---|---|---|
| `core` | 对象系统 / 内存 / 数据 | `Object`、`Inherit<>`、`ref_ptr`、`Data`、`Array`（`vec3Array`/`vec4Array`/`ubvec4Array`/`uintArray`）、`Visitor`、observer_ptr |
| `maths` | 纯数学 | `vec2/3/4`、`mat3/4`、`dmat4`、`quat`、`box`、`sphere`、`plane`、`transform` |
| `nodes` | 场景图 | `Group`、`MatrixTransform`、`LOD`、`PagedLOD`、`Switch`、`StateGroup`、`CullNode/CullGroup`、`DepthSorted`、`Bin`、`Geometry`、`VertexDraw`、`VertexIndexDraw` |
| `commands` | vkCmd* 叶子命令 | `Draw`、`DrawIndexed`、`BindVertexBuffers`、`BindIndexBuffer`、`CopyImageToBuffer`、`CopyImageViewToWindow`、`PipelineBarrier` |
| `state` | 管线/描述符/资源 | `GraphicsPipeline`、`ShaderStage/ShaderModule`、`InputAssemblyState`、`RasterizationState`、`DepthStencilState`、`ColorBlendState`、`MultisampleState`、`VertexInputState`、`ViewportState`、`DescriptorSet/Buffer/Image`、`Sampler`、`BufferInfo`、`ResourceHints`、`ViewDependentState` |
| `vk` | Vulkan 对象薄封装 | `Instance`、`Device`、`Swapchain`、`RenderPass`、`Framebuffer`、`CommandBuffer`、`Context`、`MemoryBufferPools` |
| `app` | 应用层 | `Viewer`、`Window`、`WindowTraits`、`Camera`、`Perspective/Orthographic`、`LookAt`、`CommandGraph`、`RenderGraph`、`SecondaryCommandGraph`、`RecordTraversal`、`CompileManager`、`Trackball`、`CloseHandler`、`UpdateOperations` |
| `ui` | 事件抽象 | `PointerEvent`、`ButtonPressEvent`、`ScrollWheelEvent`、`KeyEvent`、`FrameStamp` |
| `utils` | 高层工具 | `Builder`、`ShaderSet`、`GraphicsPipelineConfigurator`、`SharedObjects`、`ComputeBounds`、`LineSegmentIntersector`、`PolytopeIntersector`、`CommandLine` |
| `text` | SDF 文本 | `Text`、`TextGroup`、`Font`、`StandardLayout`、`CpuLayoutTechnique`/`GpuLayoutTechnique` |
| `lighting` | 光源/阴影 | `AmbientLight`、`DirectionalLight`、`PointLight`、`SpotLight`、`SoftShadows` |
| `io` | 序列化 / 分页 | `Options`、`read/write`、`ReaderWriter`、`DatabasePager`、`spirv`、`glsl` |
| `threading` | 线程原语 | `OperationQueue`、`OperationThreads`、`Barrier`、`Latch` |
| `platform` | 平台窗口（不在 `all.h` 中） | `macos/MacOS_Window.h`（Cocoa + QuartzCore + MoltenVK）、`xcb`、`win32`、`android` |

> `raytracing` / `meshshaders` / `animation` 一期不使用。

### 3.2 最小可运行骨架（已在本仓库 `viewer3D/` 中验证的调用序列）

```cpp
#include <vsg/all.h>

// 1. 窗口
auto traits = vsg::WindowTraits::create();
traits->windowTitle = "CloudCompareVSG";
traits->width = 1280; traits->height = 800;
traits->depthFormat = VK_FORMAT_D32_SFLOAT;          // CC 需要精确深度反投影
traits->samples = VK_SAMPLE_COUNT_4_BIT;             // 可选 MSAA
auto window = vsg::Window::create(traits);

// 2. 相机
auto perspective = vsg::Perspective::create(fovDeg, aspect, nearDist, farDist);
auto lookAt      = vsg::LookAt::create(eye, center, up);
auto camera      = vsg::Camera::create(perspective, lookAt,
                       vsg::ViewportState::create(0, 0, w, h));

// 3. 场景（见 §4.3）
auto scene = vsg::Group::create();

// 4. Viewer
auto viewer = vsg::Viewer::create();
viewer->addWindow(window);
auto cmdGraph = vsg::createCommandGraphForView(window, camera, scene);
viewer->assignRecordAndSubmitTaskAndPresentation({cmdGraph});
viewer->addEventHandlers({ vsg::CloseHandler::create(viewer),
                           vsg::Trackball::create(camera) });
viewer->compile();

// 5. 帧循环（Qt 驱动时由 vsgQt::Viewer 的 QTimer 调用）
while (viewer->advanceToNextFrame()) {
    viewer->handleEvents();
    viewer->update();
    viewer->recordAndSubmit();
    viewer->present();
}
```

参考：本仓 `viewer3D/src/VsgQtWidget.cpp:56-70`（帧循环）、`vsgQt/examples/vsgqtviewer/main.cpp`（Qt 嵌入完整模板）。

### 3.3 关键 API 速查

| 需求 | API |
|---|---|
| 几何（无索引） | `vsg::VertexDraw`（`vertexCount/firstVertex` + `assignArrays`） |
| 几何（有索引） | `vsg::VertexIndexDraw`（`indexCount/firstIndex` + `assignArrays/assignIndices`） |
| 图元拓扑 | `vsg::InputAssemblyState(VK_PRIMITIVE_TOPOLOGY_POINT_LIST / TRIANGLE_LIST / LINE_LIST)` |
| 管线配置（推荐） | `vsg::GraphicsPipelineConfigurator(shaderSet)`：`enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, 12)`、`assignArray(...)`、`init()`、`copyTo(stateGroup)` |
| 内置 ShaderSet | `createFlatShadedShaderSet()` / `createPhongShaderSet()` / `createPhysicsBasedRenderingShaderSet()` / `createTextShaderSet()` |
| 属性命名约定 | `"vsg_Vertex"`(vec3)、`"vsg_Normal"`(vec3)、`"vsg_Color"`(vec4 或 ubvec4)、`"vsg_TexCoord0"`(vec2)、`"vsg_Translation_scaleDistance"`(billboard) |
| 描述符命名约定 | `"material"`、`"diffuseMap"`、`"displacementMap"`；`VIEW_DESCRIPTOR_SET 0` / `MATERIAL_DESCRIPTOR_SET 1` |
| 动态数据上传 | `data->properties.dataVariance = DYNAMIC_DATA` + `data->dirty()`；`BufferInfo::requiresCopy(deviceID)`；`vsg::TransferTask` 每帧同步 |
| 包围盒 | `vsg::ComputeBounds`；`scene->accept(computeBounds)` |
| 点拾取 | `vsg::LineSegmentIntersector::create(camera, x, y)`；`scene->accept(*isector)` → `intersections[0]->nodePath / arrays / indexRatios` |
| 框选 | `vsg::PolytopeIntersector::create(camera, xMin, yMin, xMax, yMax)` → `Intersection::indices` |
| 离屏→窗口 | `vsg::CopyImageViewToWindow(srcImageView, window)` |
| 回读 | `vsg::CopyImageToBuffer` → host visible buffer |
| 资源 hint | `vsg::ResourceHints`（`minimumBufferSize`、`numLightsRange`、`dataTransferHint`、`viewportStateHint`）传给 `viewer->compile(hints)` |
| 海量点分页 | `vsg::PagedLOD` + `vsg::DatabasePager`（`Viewer::compile()` 检测到 `containsPagedLOD` 会自动创建 pager） |
| 文本 | `vsg::Text` + `vsg::Font`（SDF 图集）+ `StandardLayout`，必须调用 `text->setup()` |
| HUD/Overlay | 嵌套 `vsg::RenderGraph`，各自独立的 `viewportState` + `renderArea` + `clearValues` |
| Qt 嵌入 | `vsgQt::Window`（`QWindow` 子类）+ `vsgQt::Viewer`（`QTimer` 驱动）+ `QWidget::createWindowContainer` |

### 3.4 内建着色器的真实形态

- 仓库中**没有** `.vert` / `.frag` / `.glsl` 文件。
- 内建 ShaderSet 由构建期工具 `vsgshaderset` 生成：
  `src/vsg/utils/shaders/{pbr,phong,flat}_ShaderSet.cpp`（5.76MB / 554KB / 155KB）
  `src/vsg/text/shaders/text_ShaderSet.cpp`（124KB）
  内容是 VSG 二进制序列化流（`#vsgb 1.1.11`）内嵌 GLSL + 预编译变体，**静态链接进 libvsg**。
- 变体 define 组合在 `CMakeLists.txt:134-216` 声明（PBR 约 50+ 组合）。
- 运行时编译新 GLSL 需要 **glslang ≥ 14.0**（`VSG_SUPPORTS_ShaderCompiler`，默认 ON，找不到则关闭并告警）。

> **含义**：CloudCompare 现有 `ccGLSLHelper` 的"运行时拼 GLSL 源码"做法，在 VSG 下要么预生成变体（构建期），要么依赖 glslang（运行时开销 + 部署依赖）。推荐**预生成有限变体集 + ShaderSet 变体缓存**。

### 3.5 相机与交互能力

- `vsg::Camera` = `ProjectionMatrix` + `ViewMatrix` + `ViewportState`，且**继承自 `vsg::Node`**（可直接放入场景图）。
- `vsg::Perspective(fovY_deg, aspect, near, far)` / `vsg::Orthographic(l,r,b,t,n,f)`。
- `vsg::LookAt(eye, center, up)` / `LookDirection(position, quat)`。
- **全仓只有 `vsg::Trackball` 一个相机操控器**（无 Orbit / Fly / AnimationPath 类）。可定制点：
  - 成员变量：`rotateButtonMask`(默认 BUTTON_MASK_1)、`panButtonMask`(2)、`zoomButtonMask`(3)、`zoomScale`、`supportsThrow`
  - 虚函数：`rotate(angle, axis)`、`zoom(ratio)`、`pan(delta)`
  - 事件虚函数：`apply(ButtonPressEvent&)` / `apply(MoveEvent&)` / `apply(ScrollWheelEvent&)` / `apply(FrameEvent&)` …
  - `addKeyViewpoint(KeySymbol, LookAt, duration)`、`setViewpoint(LookAt, duration)` 做平滑视点动画
- 本仓 `viewer3D/src/FreeCADStyleManipulator.h` 已给出"派生 Trackball 改写交互"的可运行范例。

### 3.6 生态依赖盘点（关键差距）

| 依赖 | 状态 | 应对 |
|---|---|---|
| **Qt 集成** | ✅ 已有：`CloudCompareVSG/vsgQt`（`include/vsgQt/{Window,Viewer,KeyboardMap}.h`，导出目标 `vsgQt::vsgQt`） | 直接使用 |
| **vsgXchange**（第三方模型/图像读写：assimp、freetype、stb、curl…） | ❌ **缺失** | 影响纹理/材质图片解码与字体加载。方案：① 引入 vsgXchange；② 或自实现 `vsg::ReaderWriter` 挂到 `Options::readerWriters`，用 Qt 的 `QImage` 解码后填 `vsg::Data`（推荐，省一个依赖） |
| **vsgPoints**（十亿级点云分页） | ❌ **缺失** | VSG 自带 `PagedLOD` + `DatabasePager` + `TileDatabase`，可自行实现分页器（见 §7.6） |
| **vsgImGui**（图形窗口内 UI） | ❌ 缺失 | 一期不需要（CC 的 UI 是 Qt Widget）；HUD 用 `vsg::Text` + 嵌套 RenderGraph |
| **vsgExamples** | ❌ 缺失 | 本仓 `viewer3D/` 可作参考实现 |

### 3.7 平台与构建

- 必需：Vulkan ≥ 1.1.70.0、Threads、C++17、CMake ≥ 3.10。
- 可选：glslang ≥ 14.0（运行时 GLSL 编译）、SPIRV-Tools-opt（SPIR-V 优化）、`VSG_SUPPORTS_Windowing`（平台原生窗口）。
- **macOS**：`MacOS_Window.h` 走 Cocoa + QuartzCore，Vulkan 由 **MoltenVK** 提供（Metal 转译）。`VSG_MAX_DEVICES 1`（默认单设备）。
- 使用方式：`find_package(vsg REQUIRED)` → `target_link_libraries(app PRIVATE vsg::vsg)`。本仓实证：`viewer3D/CMakeLists.txt:11,30-34`。

---

## 4. 核心设计决策

### 4.1 决策 D1：不采用"逐调用替换"，采用"抽象层 + 双后端"

| 路线 | 描述 | 评价 |
|---|---|---|
| A. 直接替换 | 把 `glXXX()` 逐个改成 VSG 调用 | ❌ 不可行：矩阵栈、属性栈、immediate mode 在 Vulkan 无对应物；~1125 处调用无法一一映射 |
| B. 抽象层 + 双后端 | 抽后端无关渲染提交层，OpenGL 后端复用现有代码，VSG 后端新建 | ✅ **采纳**（过渡期安全、可回退、可分阶段） |
| C. 场景图镜像 | `ccHObject` 树 → VSG 节点树，由 VSG 自己遍历/裁剪/LOD | ✅ **采纳**（作为 VSG 后端的具体实现方式） |

**B + C 组合**：B 提供接口兼容与可回退；C 提供 VSG 后端的实现，且是发挥 VSG 性能优势的必要条件。

### 4.2 决策 D2：VSG 后端采用"增量同步的可执行场景图"，而非"每帧遍历 CC 树"

**为什么不做"每帧遍历 + 即时提交"**：
- VSG 的核心优势是**编译一次、重复 record**：`viewer->compile()` 阶段完成管线创建、描述符分配、`BufferInfo` 上传；之后每帧只做 traversal + `vkCmd*`。
- 若每帧遍历 `ccHObject` 树并重建命令，等于把 VSG 降级为"Vulkan 版的 immediate mode"，失去视锥裁剪、`Bin` 排序、批量提交、`PagedLOD` 分页，性能反而可能劣于现有 OpenGL 后端。

**采用**：
```
ccHObject 树 ──(dirty/revision 驱动，增量)──► VSG 场景图 ──(每帧)──► viewer->recordAndSubmit()
```
每帧只更新：相机矩阵、push constant（点大小/线宽/灯光/裁剪面）、`Bin` 排序由 VSG 自动完成。

### 4.3 决策 D3：引入 `ccRenderBackend` 抽象，而非复制第二套窗口接口

现有 `qCC_vsgWindow` 草稿走的是"复制一套 `ccVSGWindowInterface`"的路线，会导致 picking/interaction 枚举双份漂移、插件仍只认 `ccGLWindowInterface*`、交互逻辑复制几千行。**改为向上抽公共基类**。

### 4.4 决策 D4：相机数学保留 CloudCompare 语义，只做矩阵适配

`ccViewportParameters`（pivot / object-centered / focal distance）是 CloudCompare 用户体验的核心，不能换成 VSG `Trackball` 的模型。做法：
- 保留 `ccViewportParameters` 作为相机状态的**唯一真源（single source of truth）**；
- VSG 后端每帧由 `ccViewportParameters` 计算出 `dmat4 view` 与 `dmat4 proj`，写入 `vsg::LookAt`（或自定义 `ViewMatrix` 子类）与 `vsg::Perspective/Orthographic`；
- 交互事件由 VSG 后端自己处理（派生 `vsg::Trackball` 或独立 `ccVSGCameraManipulator`），但**最终只修改 `ccViewportParameters`**，不直接改 `vsg::Camera`。

这样保证 OpenGL / VSG 两个后端的相机行为完全一致，且现有"保存/恢复视点""多窗口同步视角"等功能无需改动。

---

## 5. 目标架构设计

### 5.1 总体分层

```
┌────────────────────────────────────────────────────────────────────┐
│ L4  应用层（几乎不改）                                               │
│     qCC / ccViewer / plugins                                        │
│     通过 ccViewInterface*（后端无关）+ ccRenderCapabilities 访问      │
└──────────────────────────────┬─────────────────────────────────────┘
┌──────────────────────────────▼─────────────────────────────────────┐
│ L3  窗口/视图层                                                      │
│     ccViewInterface（新，后端无关：scene DB / viewport params /       │
│                       picking mode / redraw / signals / render2Image）│
│        ▲                                    ▲                       │
│   ccGLWindowInterface                ccVSGWindowInterface           │
│   （现有，收缩后）                     （新，VSG 后端）                │
│        ▲                                    ▲                       │
│   ccGLWindow                         ccVSGWindow                    │
│   (QOpenGLWidget)                    (vsgQt::Window + container)    │
└──────────────────────────────┬─────────────────────────────────────┘
┌──────────────────────────────▼─────────────────────────────────────┐
│ L2  渲染后端抽象  libs/qCC_renderCore（新）                           │
│   ccRenderBackend         后端能力注册/创建/销毁                      │
│   ccRenderCommandSink     后端无关绘制指令接收器（2D/3D/overlay）      │
│   ccGpuResourceCache      按 (entity, revision, backend) 索引的资源   │
│   ccRenderCapabilities    能力查询（pointSize/wideLines/stereo/…）    │
│   ccPostProcessEffect     后端无关后处理描述（取代 ccGlFilter 绑定）   │
└──────────────────────────────┬─────────────────────────────────────┘
┌──────────────────────────────▼─────────────────────────────────────┐
│ L1  后端实现                                                         │
│  ┌── OpenGL 后端（现有代码，逐步收敛）──┐  ┌── VSG 后端（新）──────┐ │
│  │ ccGLRenderSink  → glXXX             │  │ VSGSceneBuilder       │ │
│  │ ccGLResourceCache (VBO/FBO/Texture) │  │  ccHObject→vsg::Node  │ │
│  │ CCFbo / ccShader / ccGlFilter       │  │ VSGPointCloudBuilder  │ │
│  └─────────────────────────────────────┘  │ VSGMeshBuilder        │ │
│                                           │ VSGOverlayBuilder     │ │
│                                           │ VSGPicker             │ │
│                                           │ VSGPostProcess        │ │
│                                           │ VSGShaders(SPIR-V)    │ │
│                                           └───────────────────────┘ │
└──────────────────────────────┬─────────────────────────────────────┘
┌──────────────────────────────▼─────────────────────────────────────┐
│ L0  数据模型（不动）  libs/qCC_db + CCCoreLib                        │
│     ccHObject 树 / ccPointCloud / ccMesh / ccOctree / ccMaterial    │
│     ccViewportParameters（相机状态真源）                              │
└────────────────────────────────────────────────────────────────────┘
```

### 5.2 新增库 `libs/qCC_renderCore`（后端无关）

| 类 | 职责 | 关键接口 |
|---|---|---|
| `ccViewInterface` | 后端无关 3D 视图契约 | `setSceneDB/getSceneDB`、`setViewportParameters/getViewportParameters`、`setPickingMode/getPickingMode`、`setInteractionMode`、`redraw/refresh`、`renderToImage`、`toPerspective/toOrtho`、`setView`、`getScreenSize`、信号（entityPicked / pointPicked / mouseMoved …） |
| `ccRenderBackend` | 后端注册与创建 | `static register(name, factory)`、`create(name, parent)`、`name()`、`capabilities()` |
| `ccRenderCapabilities` | 能力位 | `hasPointSize`、`hasWideLines`、`hasStereo`、`maxTextureSize`、`preferredDepthFormat`、`hasIntegerPicking` |
| `ccRenderCommandSink` | **过渡期**绘制指令接收器（用于尚未迁移的 `drawMeOnly`） | `begin(PASS_3D/2D)`、`pushTransform/popTransform`、`setClipPlanes`、`drawPoints(DrawCmd)`、`drawMesh(DrawCmd)`、`drawLines(DrawCmd)`、`drawText`、`end()` |
| `ccGpuResourceCache` | GPU 资源按 `(entityID, revision, backendID)` 索引 | `acquire(key)`、`invalidate(entity)`、`setMemoryBudget(bytes)`、`stats()` |
| `ccPostProcessEffect` | 后端无关后处理描述 | `id()`、`inputs(){color, depth}`、`outputs()`、`apply(backendContext)` |

> `ccRenderCommandSink` 是**过渡脚手架**：用于 OpenGL 后端尚未拆干净的阶段，以及插件自定义 drawable（qSRA / qCompass）。VSG 后端在迁移完成后可不再依赖它（走 §5.3 的同步器），但仍保留以支持自定义插件 drawable。

### 5.3 场景同步器：VSG 后端核心

```cpp
// libs/qCC_vsgWindow/vsg/ccVSGSceneBuilder.h
class ccVSGSceneBuilder : public vsg::Inherit<vsg::Object, ccVSGSceneBuilder>
{
public:
    void setRoot(ccHObject* root);
    // ccHObject 树变更时调用（由 ccHObject 的 modification 信号 / 显式 invalidate 触发）
    void invalidate(ccHObject* obj, ChangeFlags flags);
    // 在渲染线程空闲点执行增量重建
    void update(vsg::ref_ptr<vsg::Viewer> viewer);
    vsg::ref_ptr<vsg::Node> sceneRoot() const { return _root; }

private:
    struct Entry {
        vsg::ref_ptr<vsg::Group>      node;      // 对应该 ccHObject 的子图根
        vsg::ref_ptr<vsg::MatrixTransform> transform;
        uint64_t revision = 0;                   // 几何/颜色/SF/可见性 revision
    };
    std::unordered_map<ccHObject*, Entry> _map;
    vsg::ref_ptr<vsg::Group> _root;
    vsg::ref_ptr<vsg::SharedObjects> _shared;    // 复用 pipeline/descriptor，避免重复编译
};
```

**映射规则**：

| ccHObject | VSG 节点 |
|---|---|
| `ccHObject`（分组/可见性/临时变换） | `vsg::MatrixTransform`（`m_glTrans`） + `vsg::Group`；visible ⇄ `vsg::Switch` 或直接从父 `Group` 移除 |
| `ccPointCloud` | `vsg::Group`（分 chunk）+ 每 chunk 一个 `vsg::StateGroup` → `vsg::VertexDraw`（`POINT_LIST`） |
| `ccMesh` | `vsg::StateGroup` → `vsg::VertexIndexDraw`（`TRIANGLE_LIST`）；线框模式另一个 `StateGroup`（`LINE_LIST`） |
| `ccPolyline` | `vsg::StateGroup` → `vsg::VertexDraw`（`LINE_STRIP`） |
| `ccFacet` / `ccImage` / `cc2DLabel` | 3D 部分进主图；2D 部分进 overlay 图（§5.5） |
| `ccSensor`（GBL/Camera） | `vsg::VertexDraw`（`LINE_LIST`）+ `MatrixTransform` |
| `ccOctreeProxy` | `vsg::VertexDraw`（`LINE_LIST`）表示 cell；深度大时映射为 `vsg::LOD` |
| 大规模点云（可选 P2） | `vsg::PagedLOD` + `DatabasePager` |

**增量策略**：
- `ccPointCloud` / `ccMesh` 已有 dirty flag 机制（`ccPointCloud.h:166-181`），扩展为统一 `revision` 计数器。
- 位置/可见性/变换变化 → 只更新 `MatrixTransform` 或 `Switch`，不动 `vsg::Data`。
- 颜色/SF 变化 → `data->dirty()` 触发 `TransferTask` 增量上传，**不重建节点**。
- 几何拓扑变化（点数/面数变化）→ 重建 `VertexDraw`。

### 5.4 `CC_DRAW_CONTEXT` 改造

现状：`CC_DRAW_CONTEXT`（`qCC_db/include/ccGLDrawContext.h`）内含 `QOpenGLFunctions_2_1*`、`glW` / `glH`、`pass`、LOD 状态等，是 GL 泄漏到数据层的通道。

改造：
```
CC_DRAW_CONTEXT（保留，仅后端无关部分）
   ├─ pass (BACKGROUND / 3D / FOREGROUND_2D)
   ├─ picking 模式与 ID
   ├─ LOD / 显示参数
   └─ ccRenderCommandSink* sink        ← 新增，替代直接 gl 调用
CC_GL_DRAW_CONTEXT : CC_DRAW_CONTEXT   ← 新增，GL 专用（仅 OpenGL 后端可见）
```
迁移期：`ccHObject::drawMeOnly()` 内部判断 `sink != nullptr` 走新路径，否则走 legacy GL 路径。逐类迁移完成后删除 legacy 分支。

### 5.5 2D / Overlay 设计

CloudCompare 的 foreground 层（标签、比例尺、方向轴、色标、消息、热区）本质是**屏幕空间 2D 绘制**。VSG 方案：

```
CommandGraph
  └─ RenderGraph (主 3D；perspective camera；清 color+depth)
  └─ RenderGraph (overlay 2D；Orthographic 以像素为单位；不清 color，可选清 depth)
        viewportState = 全屏；renderArea = 全屏
        ├─ vsg::Text（标签/消息/比例尺文字）
        ├─ vsg::VertexDraw（LINE_LIST：连接线、比例尺刻线、方向轴）
        └─ vsg::StateGroup + DescriptorImage（色标渐变条 / 图片）
```
- 正交投影：`vsg::Orthographic(0, w, h, 0, -1, 1)`（注意 Vulkan NDC z ∈ [0,1]）。
- 文本：`vsg::Text` + `vsg::Font`。字体需 SDF 图集 —— 见风险 R3。

### 5.6 相机与交互适配

```
Qt/VSG 事件 (vsg::PointerEvent / ScrollWheelEvent)
     │
     ▼
ccVSGCameraManipulator : vsg::Inherit<vsg::Visitor, ccVSGCameraManipulator>
     │  复用 CC 的语义：左键旋转(虚拟 trackball)、右键 pan、中键 zoom、
     │  双击设 pivot、Alt+滚轮=点大小、Ctrl+滚轮=near/far、Shift+滚轮=FOV
     ▼
修改 ccViewportParameters（唯一真源）
     │
     ▼
ccVSGWindow::updateCamera()
     │  view  = f(pivot, viewMat, cameraCenter, focalDistance, objectCentered)
     │  proj  = Perspective/Orthographic(fov, aspect, near, far)   ← 注意 Vulkan z∈[0,1]
     ▼
vsg::LookAt / 自定义 ViewMatrix  +  vsg::Perspective
```
- 优先**派生 `vsg::Trackball`** 并覆写 `rotate/zoom/pan` 与事件 `apply`，复用其 `supportsThrow`（惯性）、`addWindow`、窗口过滤逻辑（参考本仓 `viewer3D/src/FreeCADStyleManipulator.h`）。
- **Vulkan 深度范围差异（实测修正）**：VSG 用的不是普通 Vulkan [0,1] 深度，而是 **reverse depth**——近平面映射到 NDC z = **1**，远平面映射到 **0**（OpenGL 是 -1 / +1），且 **Y 轴翻转**。
  - 依据：`include/vsg/maths/transform.h:140` 的 `perspective()` 与 `:167` 的 `orthographic()` 注释明确写 "Reverse depth convention: 1 to 0 depth range"，矩阵元素 `m[2][2]=zNear/(zFar-zNear)`、`m[2][3]=-1` 也印证；`perspective()` 的 `m[1][1] = -f` 说明 Y 被翻转（Vulkan 裁剪空间 Y 向下）。
  - 因此 CC 里"从深度反投影得到世界坐标"（双击设 pivot、深度点选）的代码必须按 **z ∈ [1..0]** 重算，且深度比较方向相反（`VK_COMPARE_OP_GREATER`）——按 [0,1] 或 [-1,1] 计算都会得到错误结果。
  - 投影矩阵本身交给 `vsg::perspective()` / `vsg::orthographic()` 构造即可，不要自己按 GL 公式写。

### 5.7 拾取设计

| CC 拾取类型 | 现状 | VSG 方案 |
|---|---|---|
| **实体拾取** | 离屏 FBO + 24-bit RGB 编码 ID + `glReadPixels` | 离屏 `RenderGraph`，attachment 用 **`R32_UINT`**（或 RGBA8）。绘制 ID pass 时用 `VK_PRIMITIVE_TOPOLOGY` 与当前显示模式一致，shader 输出 entityID。回读用 `vsg::CopyImageToBuffer`。收益：ID 空间从 2^24 提升到 2^32，且不受颜色混合/抖动影响 |
| **矩形框选实体** | 复用实体拾取 + CPU 扫描像素矩形 | 同实体拾取，只回读矩形区域（可用 scissor + 小尺寸 attachment 优化） |
| **点拾取** | CPU 投影 + 八叉树 | **完全复用** CPU 算法；或用 `vsg::LineSegmentIntersector`（对 POINT_LIST 图元支持有限，建议保留 CPU 路径） |
| **三角拾取** | CPU ray-triangle（八叉树加速） | 同上，可试 `vsg::LineSegmentIntersector`（对 TRIANGLE_LIST 支持良好，返回 `indexRatios`） |
| **深度反投影** | `glReadPixels` depth + PBO | depth image → `CopyImageToBuffer` → host buffer，注意 [0,1] NDC 换算 |
| **标签拾取** | 颜色编码 | 同实体拾取 |

**对外协议保持不变**：`ccPickingHub` / `ccOverlayDialog` 的信号语义不变，插件无感。

### 5.8 后处理（EDL / SSAO / Bilateral）

- 抽象为 `ccPostProcessEffect`：声明输入（color / depth）、输出、参数集。
- OpenGL 实现：包装现有 `ccGlFilter`（FBO + GLSL）。
- VSG 实现：额外一个离屏 `RenderGraph`（color + depth attachment）→ 主 pass 渲染到该离屏 → 后处理 pass（全屏三角形 + `DescriptorImage` 采样 color/depth）→ `vsg::CopyImageViewToWindow` 到窗口。
- 一期只迁移 SSAO（视觉收益最大），EDL 与 Bilateral 排后。

### 5.9 插件兼容层

1. `ccMainAppInterface` 新增 `getActiveViewWindow()` 返回 `ccViewInterface*`；旧的 `getActiveGLWindow()` 保留但在 VSG-only 构建下返回 `nullptr` 并标记 `CC_DEPRECATED`。
2. 插件 metadata 增加 `requiresBackends = {OpenGL|VSG|Any}`，VSG-only 构建下自动跳过仅 OpenGL 的插件（qEDL/qSSAO/qPCV 等），并在 About 对话框提示。
3. 对直接调用 GL 的自定义 drawable（qSRA、qCompass），引导改用 `ccRenderCommandSink` API。
4. 构建层：`CCPluginAPI` 不再 PUBLIC 链接 `QCC_GL_LIB`，改为链接 `QCC_RENDER_CORE_LIB`；OpenGL 专用能力通过单独的 `QCC_GL_LIB` 可选链接。

### 5.10 线程模型与帧驱动

- 使用 **`vsgQt::Viewer`**（`QTimer` + `continuousUpdate`）驱动帧循环，天然融入 Qt 事件循环，与现有 `ccGLWindow` 的 `update()`/`repaint()` 语义一致。
- `ccVSGWindow::redraw()` → 标记 dirty，`vsgQt::Viewer::request()` 触发下一帧。
- 场景同步（`ccVSGSceneBuilder::update()`）放在 `viewer->update()` 之前，主线程执行，避免与 record 并发冲突。
- 大数据上传：静态数据走 `viewer->compile()` 期的 `TransferTask`；动态数据走 `DYNAMIC_DATA` + `dirty()`。

---

## 6. 构建系统集成方案

### 6.1 新增开关

```cmake
# cmake/CMakeExternalLibs.cmake 或顶层
set(CC_RENDER_BACKEND "Both" CACHE STRING "Render backend: OpenGL | VSG | Both")
set_property(CACHE CC_RENDER_BACKEND PROPERTY STRINGS OpenGL VSG Both)
```
- `OpenGL`：现状，VSG 代码不参与编译（保证零风险交付）。
- `Both`：**默认**，双后端共存，可用环境变量/命令行 `--render-backend=vsg` 或设置项切换，运行时可回退。
- `VSG`：纯 Vulkan 构建，OpenGL 代码不编译。

### 6.2 依赖修正

```cmake
# 顶层 CMakeLists.txt（替换现有 64-68 行硬编码）
if (CC_RENDER_BACKEND STREQUAL "OpenGL" OR CC_RENDER_BACKEND STREQUAL "Both")
    # 现有 Qt OpenGL 依赖
endif()
if (CC_RENDER_BACKEND STREQUAL "VSG" OR CC_RENDER_BACKEND STREQUAL "Both")
    find_package(vsg 1.1 REQUIRED)                 # 目标：vsg::vsg
    find_package(vsgQt REQUIRED)                   # 目标：vsgQt::vsgQt
    # 可选：find_package(vsgXchange QUIET)
endif()
```
- 删除硬编码的 `/Users/gsl/work/pointsMap/...` 绝对路径。
- `vsgQt` 建议以 git submodule 或 `FetchContent` 引入，避免本地路径依赖。
- MSAA / depth format 通过 `WindowTraits` 配置；设备选择、debug layer 通过 `CommandLine` 或环境变量暴露。

### 6.3 目标与目录

```
libs/
  qCC_renderCore/          (新)  ccViewInterface / ccRenderBackend /
                                 ccRenderCommandSink / ccGpuResourceCache /
                                 ccRenderCapabilities / ccPostProcessEffect
  qCC_glWindow/            (改造) ccGLWindowInterface : public ccViewInterface
  qCC_vsgWindow/           (重写) ccVSGWindowInterface : public ccViewInterface
      vsg/
        ccVSGSceneBuilder.{h,cpp}
        ccVSGPointCloudBuilder.{h,cpp}
        ccVSGMeshBuilder.{h,cpp}
        ccVSGOverlayBuilder.{h,cpp}       // 2D: 标签/比例尺/trihedron/色标/消息
        ccVSGPicker.{h,cpp}
        ccVSGPostProcess.{h,cpp}
        ccVSGCameraManipulator.{h,cpp}
        ccVSGShaders.{h,cpp}              // ShaderSet 定义 + 内嵌 SPIR-V
        ccVSGUtils.{h,cpp}
  CCFbo/                   (保持 OpenGL 后端使用；VSG 后端不依赖)
```

`libs/CMakeLists.txt` 增加：
```cmake
add_subdirectory( qCC_renderCore )
add_subdirectory( qCC_glWindow )      # 或按开关裁剪
add_subdirectory( qCC_vsgWindow )     # 或按开关裁剪
```

### 6.4 长期清理（P2/P3）

- `QCC_DB_LIB` 不再 PUBLIC 链接 `Qt6::OpenGL` 与 `CC_FBO_LIB`（改为仅 OpenGL 后端 PRIVATE）。
- `cmake/CMakeExternalLibs.cmake` 中 Qt OpenGL 组件改为按后端条件 REQUIRED。
- 完全移除 `ccIncludeGL.h` 在 `qCC_db` 中的使用。

---

## 7. 分阶段实施计划

> 工作量单位以"人周（PW）"估算，按 1 名熟悉 CC + Vulkan 的工程师计。
> 每个里程碑都必须满足：**可编译、可运行、可回退（OpenGL 后端不受影响）**。

### M0 — 技术验证 Spike（2~3 PW）

**目标**：用最小代价验证三个高危特性，决定后续技术方案。

| # | 任务 | 产出 | 判定 |
|---|---|---|---|
| M0.1 | 用 `vsgQt::Window` + `vsgQt::Viewer` 做一个嵌入 `QMainWindow` 的 VSG 窗口，加载示例场景 | 可运行 demo（`viewer3D` 已有类似代码可直接复用） | 编译通过、稳定渲染 |
| M0.2 | **PointSize 验证**：自定义 PointList ShaderSet，用 `gl_PointSize` 渲染 100 万点，在 macOS(MoltenVK) 与 Windows/Linux 上分别验证点大小是否可动态设置 | 验证报告 | 若 Metal 不支持 → 启用 R1 缓解方案（billboard quad） |
| M0.3 | **WideLines 验证**：`vkCmdSetLineWidth` / `VkPhysicalDeviceFeatures::wideLines` 支持度矩阵 | 支持度矩阵 | 若不支持 → 线宽用 quad 扩展 |
| M0.4 | **SDF 字体验证**：用 `vsg::Text` + 自带 TTF（`libs/CCAppCommon` 下有 .ttf）渲染中英文，验证 `vsg::Font` 生成流程是否依赖 vsgXchange/freetype | 验证报告 | 若依赖 → 决定引入 vsgXchange 还是自实现 |
| M0.5 | 深度回读验证：depth attachment → `CopyImageToBuffer` → 反投影世界坐标，验证 [0,1] NDC 换算 | 单元验证 | 精度满足 CC 需求 |

**验收**：一份《技术可行性验证报告》+ 3 个可运行 mini-demo。

---

### M1 — 骨架与构建（3~4 PW）

| # | 任务 |
|---|---|
| M1.1 | 修正顶层 CMake：移除硬编码 VSG 路径，改为 `find_package(vsg)` + `find_package(vsgQt)`；引入 `CC_RENDER_BACKEND` 开关 |
| M1.2 | 新建 `libs/qCC_renderCore`，落地 `ccViewInterface` / `ccRenderBackend` / `ccRenderCapabilities` / `ccRenderCommandSink`（先只定义接口，无实现） |
| M1.3 | `ccGLWindowInterface` **机械改造**：改为 `public ccViewInterface`，把已在 `ccViewInterface` 中声明的方法改为 `override`；删除重复定义。**不改变任何行为**（纯重构，用视觉回归测试保证） |
| M1.4 | 重写 `libs/qCC_vsgWindow`：`ccVSGWindowInterface : ccViewInterface` + `ccVSGWindow : QWidget`（内含 `vsgQt::Window` 经 `QWidget::createWindowContainer`）+ `vsgQt::Viewer` |
| M1.5 | 加入 `libs/CMakeLists.txt`；`qCC` 中新增"新建 VSG 视图"菜单项（调试用） |

**验收**：CloudCompare 可同时打开一个 OpenGL 视图和一个 VSG 空视图；OpenGL 视图视觉回归零差异；VSG 视图显示纯色背景 + FPS 计数。

---

### M2 — 相机与交互对齐（2~3 PW）

| # | 任务 |
|---|---|
| M2.1 | `ccVSGCameraManipulator`（派生 `vsg::Trackball`）：左键旋转（复用 CC 虚拟 trackball 算法）、右键 pan、中键 zoom |
| M2.2 | `ccViewportParameters` ⇄ `vsg::Camera` 双向同步（含 object-centered / viewer-centered、pivot、focal distance、near/far 自动计算） |
| M2.3 | Vulkan NDC z∈[0,1] 适配：`Perspective`/`Orthographic` 与深度反投影 |
| M2.4 | 滚轮全部语义：zoom / Alt 点大小 / Ctrl near-far / Shift FOV；双击设 pivot |
| M2.5 | 多窗口视角同步、"标准视角"（`setView(CC_TOP_VIEW)` 等）复用 `ccGLUtils` 的矩阵 |

**验收**：在同一数据集上，OpenGL 与 VSG 视图的相机操作逐项对比，视角/缩放/pivot 行为一致（截图对比 + 手工核对）。

---

### M3 — 点云渲染（3~4 PW）

| # | 任务 |
|---|---|
| M3.1 | 自定义 **PointCloud ShaderSet**（`POINT_LIST`）：attribute `vsg_Vertex`(vec3) / `vsg_Color`(vec4 或 ubvec4) / `vsg_Normal`(可选)；uniform/push-constant：点大小、灯光开关、裁剪面 |
| M3.2 | `ccVSGPointCloudBuilder`：分 chunk（沿用现有 ~2^16 分块策略）→ `vsg::vec3Array` + `vsg::ubvec4Array` → `vsg::VertexDraw`；用 `SharedObjects` 复用管线 |
| M3.3 | 颜色来源：RGB / 标量场（SF）+ color ramp 纹理（`DescriptorImage`，1024×1 RGBA8） / 单色 / 法线 LUT 纹理 |
| M3.4 | `ccVSGSceneBuilder` 增量同步：`MatrixTransform`、`Switch`（可见性）、revision 驱动重建、`dirty()` 增量上传 |
| M3.5 | 点大小 per-cloud / 全局；`gl_PointSize` 路径与（如需要）billboard quad 路径双实现 |
| M3.6 | 内存预算与 `ResourceHints` 配置；千万元级点云冒烟测试 |

**验收**：加载 500 万 / 2000 万点云，颜色模式（RGB / SF / 单色）与 OpenGL 后端视觉一致；FPS 不低于 OpenGL 后端。

---

### M4 — 网格、折线与传感器（3~4 PW）

| # | 任务 |
|---|---|
| M4.1 | `ccVSGMeshBuilder`：三角面 → `VertexIndexDraw`（索引 uint32）；顶点法线 / 三角面法线两种模式 |
| M4.2 | 线框模式（`LINE_LIST` 展开）与"点+面"混合模式 |
| M4.3 | 材质：`ccMaterial` → `vsg::DescriptorImage` + `Sampler`；纹理坐标 `vsg_TexCoord0`；无 vsgXchange 时用 `QImage` 解码填 `vsg::Data` |
| M4.4 | `ccPolyline`（`LINE_STRIP`，宽线见 R1/R2）、`ccFacet`、`ccSensor`（GBL/Camera 视锥与坐标轴） |
| M4.5 | 半透明：`vsg::DepthSorted` + `Bin`（`ASCENDING`/`DESCENDING` 排序） |
| M4.6 | 双面光照（`VSG_TWO_SIDED_LIGHTING`）与背面剔除开关 |

**验收**：典型网格模型（含纹理、材质、SF 着色）与 OpenGL 后端视觉一致。

---

### M5 — 2D 覆盖层（3 PW）

| # | 任务 |
|---|---|
| M5.1 | overlay `RenderGraph` + `Orthographic`（像素坐标）；与 3D pass 的合成与清除策略 |
| M5.2 | `vsg::Text` + SDF 字体：标签文字、屏幕消息（LOWER_LEFT / UPPER_CENTER / SCREEN_CENTER） |
| M5.3 | `cc2DLabel`（3D marker + 2D 引线 + 面板）、`cc2DViewportLabel`（ROI 虚线框） |
| M5.4 | 比例尺、方向轴（trihedron）、透视/正交状态提示 |
| M5.5 | 标量场色标（渐变条 + 刻度文字） |
| M5.6 | `ccImage`（2D 图片叠加） |

**验收**：所有 2D 元素与 OpenGL 后端位置/字体/颜色一致（截图逐项对比）。

---

### M6 — 拾取与离屏渲染（3 PW）

| # | 任务 |
|---|---|
| M6.1 | 实体拾取：离屏 `RenderGraph` + `R32_UINT` attachment + ID shader + `CopyImageToBuffer` 回读 |
| M6.2 | 矩形框选实体（scissor + 区域回读） |
| M6.3 | 点/三角拾取：复用 CPU 八叉树算法（不改）；可选接入 `vsg::LineSegmentIntersector` 对比 |
| M6.4 | 深度反投影（双击设 pivot）；depth image 回读与 [0,1] 换算 |
| M6.5 | `renderToImage()` / 高清截图（离屏大尺寸 RenderGraph + 回读 + 保存为 QImage） |
| M6.6 | `ccPickingHub` / `ccOverlayDialog` 对接；交互工具（分割、裁剪、变换）端到端验证 |

**验收**：所有拾取模式与交互工具（点选、框选、分割、裁剪、配准交互）功能等价于 OpenGL 后端。

---

### M7 — 后处理与 LOD / 分页（4 PW）

| # | 任务 |
|---|---|
| M7.1 | 后处理框架：离屏 color+depth → 全屏三角形 pass → `CopyImageViewToWindow` |
| M7.2 | SSAO 迁移（Vulkan shader）；EDL、Bilateral 排后（或仅在 OpenGL 后端保留） |
| M7.3 | LOD：CC 现有渐进式 LOD 语义映射到 `vsg::LOD`（`minimumScreenHeightRatio`） |
| M7.4 | 大规模点云分页（可选）：ccOctree → `vsg::PagedLOD` + `DatabasePager` + 自定义 `ReaderWriter`；`ResourceHints::numDatabasePagerReadThreads` |
| M7.5 | 性能调优：`SharedObjects`、`ResourceHints`、descriptor pool、MSAA、`DYNAMIC_VIEWPORTSTATE` |

**验收**：3000 万+ 点云流畅交互；SSAO 视觉对齐；性能基准达到 §9 目标。

---

### M8 — 插件与收尾（3~4 PW）

| # | 任务 |
|---|---|
| M8.1 | `ccMainAppInterface::getActiveViewWindow()`；`getActiveGLWindow()` 标记 deprecated |
| M8.2 | 插件 metadata `requiresBackends`；VSG-only 构建下自动跳过 GL-only 插件并提示 |
| M8.3 | 迁移 qHPR / qAnimation / qColorimetricSegmenter / qM3C2 等仅用窗口 API 的插件 |
| M8.4 | qSRA / qCompass 等自定义 GL drawable → `ccRenderCommandSink` |
| M8.5 | `CCPluginAPI` 不再 PUBLIC 链接 `QCC_GL_LIB` |
| M8.6 | 立体显示：一期降级为"不支持"并给出 UI 提示；P2 评估多 `View` 方案 |
| M8.7 | 文档更新（本文档 → 用户文档 / 编译文档）；删除 `VSG_Migration_TODO.md` 与 `CodeModificationAnalysis.md` 两份旧草稿 |

**验收**：全部 core 插件在 VSG 后端下可用或明确标注不可用；完整回归测试通过。

---

### 里程碑总览与工作量

| 里程碑 | 内容 | 工作量(PW) | 累计 |
|---|---|---:|---:|
| M0 | 技术验证 Spike | 2~3 | 3 |
| M1 | 骨架与构建 | 3~4 | 7 |
| M2 | 相机与交互 | 2~3 | 10 |
| M3 | 点云渲染 | 3~4 | 14 |
| M4 | 网格/折线/传感器 | 3~4 | 18 |
| M5 | 2D 覆盖层 | 3 | 21 |
| M6 | 拾取与离屏 | 3 | 24 |
| M7 | 后处理与 LOD | 4 | 28 |
| M8 | 插件与收尾 | 3~4 | 32 |
| **合计** | | **26~32 PW** | ≈ **6~8 人月** |

> 若不含后处理（M7.1/M7.2）与分页（M7.4），核心功能对齐约 **20~22 PW（5 人月）**。

---

## 8. 风险登记表

| ID | 风险 | 影响 | 概率 | 缓解措施 |
|---|---|---|---|---|
| **R1** | ~~**PointSize**：Metal/MoltenVK 不支持 `gl_PointSize > 1`~~ —— **M0 实测：结论相反，设备支持** | ~~高~~ → **已解除** | ~~高~~ | **M0 实测（Apple M2 / MoltenVK 1.2.9 / Vulkan 1.2.283）：`pointSizeRange = [1 .. 511]`，granularity=1。可直接用 `POINT_LIST` + `gl_PointSize`。** 仍需保留 billboard quad 回退分支（其它 GPU/驱动可能不同），并在 M3 用截图对比做最终确认 |
| **R2** | **线宽**：`wideLines` 特性多数设备不支持，折线粗度丢失 | 中 | **已确认发生** | **M0 实测：`wideLines = NOT supported`，`lineWidthRange = [1 .. 1]`。** 必须实现 quad 扩展（CPU 生成三角带/triangle strip）来画粗线；这是 M4 的既定工作量，不再是"可能" |
| **R3** | **SDF 字体**：`vsg::Text` 需要 SDF 图集；生成依赖 vsgXchange/freetype；中文字形量大 | 中（标签/消息全靠它） | 中 | **M0 实测：freetype 2.14.3 可用，系统自带中日韩字体且覆盖完整（Hiragino Sans GB 29352 字形，中/文/点/云 全部命中，可正常栅格化）。** 但**已安装的 vsgXchange 1.1.6 与 vsg 1.1.14 ABI 不兼容，无法用于生成 `vsg::Font`**（见 R15）。方案：① 重建 vsgXchange；② 自写 freetype → `vsg::Font` 构建器（约 200 行，无新增依赖）；③ 屏幕 2D 文字退化为 Qt overlay |
| **R4** | **深度精度与 NDC 差异**：VSG 用 **reverse depth**（near→NDC z=1，far→0，且 Y 翻转），与 GL 的 [-1,1] 完全不同。反投影、深度点选、near/far 裁剪、深度回读都会算错 | 高 | 中 | 投影矩阵统一由 `vsg::perspective()`/`orthographic()` 构造；深度相关代码一律按 z∈[1..0] 处理并在适配层封装换算函数；用 D32_SFLOAT + 动态 near/far（已有逻辑复用）；M6 专项验证 |
| **R5** | **Shader 变体爆炸**：CC 的 GLSL 按 attribute 位组合动态生成，Vulkan 下无法运行时随意拼 | 中 | 中 | 收敛到有限变体集（点/线/面 × 颜色模式 × 法线 × 纹理），构建期用 `vsgshaderset` 预生成并内嵌；或依赖 glslang 运行时编译并缓存 `ShaderSet::variants` |
| **R6** | **插件 GL 直调**导致 VSG-only 构建下大量插件失效 | 中 | 高（确定会发生） | 插件 metadata 声明后端能力；VSG-only 构建自动跳过；提供 `ccRenderCommandSink` 迁移路径；一期默认 `Both` 构建规避 |
| **R7** | **性能不达预期**：场景同步开销、同步阻塞、资源编译卡顿 | 中 | 中 | 增量同步 + `SharedObjects` + 分帧编译；设置 `ResourceHints`；大数据用 `PagedLOD`；建立 §9 基准持续追踪 |
| **R8** | **MoltenVK / macOS 稳定性**：Metal 转译层的特性限制与驱动差异 | 中 | 中 | M0 在 macOS 优先验证；`VSG_SUPPORTS_Windowing` 与 `VSG_MAX_DEVICES` 配置；明确最低 macOS 版本与 MoltenVK 版本 |
| **R9** | **工作量低估**：`ccGLWindowInterface.cpp` 7456 行、~1125 处 GL 调用的语义迁移 | 高 | 中 | 严格按里程碑交付；每里程碑可回退；优先保核心（点云/网格/交互），非核心（后处理/立体）后置或降级 |
| **R10** | **双后端代码漂移**：两套实现行为逐渐不一致 | 中 | 中 | 共享层最大化（相机数学、拾取 CPU 算法、色标、显示参数）；建立视觉回归对比测试（§9.2） |
| **R11** | **立体显示 / VR 丢失**（quad-buffer） | 低 | 确定 | 一期明确不支持并 UI 提示；P2 评估 `vsg::View` 多视口 + 多 eye 方案 |
| **R12** | **vsgXchange 缺失**导致纹理/字体解码需自研 | 低（本工作区 `/usr/local` 已安装 `vsgXchange`，`find_package(vsgXchange)` 命中） | 低 | 自实现 `vsg::ReaderWriter`，用 `QImage`/`QFont` 解码后填 `vsg::Data`（CC 已依赖 Qt，无新增依赖） |
| **R13** | **macOS 工具链**：Qt 6.8.2 在 macOS 26 上链接 `-framework AGL`（Apple 已移除该框架二进制），导致**所有**库链接失败；homebrew `ccache` 与 `fmt` 版本不匹配导致崩溃 | 高（完全阻塞构建） | 已发生，已解决 | 见附录 D：改用 homebrew Qt6 + 本地 AGL stub 框架 + `brew reinstall ccache`。**这是环境问题，非本次改造引入** |
| **R14** | **子模块漂移**：`CCCoreLib` 子模块停留在 2025-02-24 的 Qt5 版本，而 master 期望 2026-09-23 的 Qt6 版本；`MeshIO`/`quazip`/`hidapi`/`cc3DFin`/`qG3Point` 未初始化 | 高（阻塞配置） | 已发生，已解决 | `git submodule update --init <paths>`；CI 中显式初始化子模块 |
| **R15** | **vsg / vsgXchange 版本不一致**：`/usr/local` 头文件为 vsg **1.1.14** 而静态库 `libvsg.a` 运行时自报 **1.1.11**；vsgXchange 1.1.6 引用的 `vsg::Data::computeValueCountIncludingMipmaps(ulong,ulong,ulong,uint)` 在当前 libvsg 中已变成无参成员函数，导致**链接失败** | 中（阻塞字体方案，且头文件/库版本不一致有隐患） | 已发生 | 统一重建并安装 vsg 1.1.14 + vsgXchange（同一源码树、同一编译器）；或改用自写 freetype 构建器绕开 vsgXchange |
| **R16** | **PointSize 实测结论仅来自单台机器**（Apple M2 / MoltenVK 1.2.9）。其它 GPU、驱动、Windows/Linux 上可能不同 | 中 | 中 | `ccRenderCapabilities::pointSizeSupported` 必须在**运行时**由 `VkPhysicalDeviceLimits::pointSizeRange` 决定，不能写死；M3 验收时做点云截图对比 |

---

## 9. 验证与验收标准

### 9.1 性能基准（必须在同一机型上 OpenGL vs VSG 对比）

| 场景 | 指标 | 目标 |
|---|---|---|
| 500 万点（RGB） | 稳定帧率 / 旋转交互帧率 | ≥ OpenGL 后端 |
| 2000 万点（单色 + SF） | 交互帧率、加载时间 | ≥ OpenGL 后端，加载时间 ≤ 1.2× |
| 5000 万点（PagedLOD 分页后） | 交互帧率、内存占用 | OpenGL 后端不可用时 VSG 可用；内存可控 |
| 100 万三角面网格 | 帧率、拾取延迟 | ≥ OpenGL 后端 |
| 首帧 / 场景切换 | 编译卡顿时长 | 建立基线，M7 优化后 ≤ 1.5× OpenGL |
| 100 万个实体（`ccHObject` 节点数） | 同步耗时 | 增量同步 ≤ 16ms（除首次全量） |

### 9.2 视觉回归对比

- 建立固定数据集 + 固定视点 + 固定显示参数的截图集（点云/网格/标签/色标/后处理各若干张）。
- OpenGL 与 VSG 后端各渲染一次，做像素级 diff（允许抗锯齿/点大小差异阈值），输出差异报告。
- 纳入 CI（可选：headless 用 SwiftShader / lavapipe 做冒烟，不追求像素一致）。

### 9.3 功能验收清单

- [ ] 点云：RGB / 标量场 / 单色 / 法线着色；点大小；可见性；裁剪；LOD
- [ ] 网格：实体 / 线框 / 点面混合；顶点法线 / 面法线；SF 着色；材质；纹理
- [ ] 折线、facet、传感器（GBL / Camera）
- [ ] 2D：标签、视口标签、图片、比例尺、方向轴、色标、屏幕消息
- [ ] 相机：旋转 / 平移 / 缩放 / pivot / 标准视角 / 透视正交切换 / 多窗口同步
- [ ] 拾取：实体 / 矩形 / 点 / 三角 / 标签 / 深度反投影
- [ ] 交互工具：分割、裁剪、变换、配准、量测
- [ ] 离屏：`renderToImage()`、高清截图
- [ ] 后处理：SSAO（EDL 可选）
- [ ] 插件：core 插件全部可用或明确标注

---

## 10. 与现有草稿文档的关系

| 文件 | 处置 |
|---|---|
| `CloudCompare/VSG_Migration_TODO.md` | 内容被本文档 §7 取代。建议 M8 阶段删除，或改造为"执行看板"链接到本文档 |
| `CloudCompare/CodeModificationAnalysis.md` | 分析过于粗浅（只列了 5 个 glWindow 文件，遗漏 `qCC_db` 的 ~508 处 GL 调用）。已被 §2 取代，建议删除 |
| `libs/qCC_vsgWindow/*` | 不可编译草稿。按 §5/§7 重写（M1.4），保留接口命名风格以延续约定 |

---

## 附录 A：关键文件路径索引

### CloudCompare

| 主题 | 路径 |
|---|---|
| 主渲染器接口 | `libs/qCC_glWindow/include/ccGLWindowInterface.h`（1597 行） |
| 主渲染器实现 | `libs/qCC_glWindow/src/ccGLWindowInterface.cpp`（7456 行） |
| Qt 窗口适配 | `libs/qCC_glWindow/include/ccGLWindow.h` / `src/ccGLWindow.cpp` |
| 立体窗口 | `libs/qCC_glWindow/include/ccGLWindowStereo.h` / `src/ccGLWindowStereo.cpp` |
| 显示参数 | `libs/qCC_glWindow/include/ccGuiParameters.h` |
| 深度图/色标 | `libs/qCC_glWindow/src/ccRenderingTools.cpp` |
| GL include 包装 | `libs/qCC_db/include/ccIncludeGL.h:28-133` |
| 绘制上下文 | `libs/qCC_db/include/ccGLDrawContext.h` |
| 对象树绘制 | `libs/qCC_db/src/ccHObject.cpp:748-855` |
| 可绘制基类 | `libs/qCC_db/include/ccDrawableObject.h:47-50, 361-408` |
| 点云绘制 | `libs/qCC_db/src/ccPointCloud.cpp:3386-4197`；VBO `:75-193`、`:5944-6206` |
| 网格绘制 | `libs/qCC_db/src/ccMesh.cpp:1730-2408` |
| 折线绘制 | `libs/qCC_db/src/ccPolyline.cpp:173-367` |
| 2D 标签 | `libs/qCC_db/src/cc2DLabel.cpp:1037-1400` |
| 相机传感器 | `libs/qCC_db/src/ccCameraSensor.cpp:1420-1622` |
| 视口参数（相机真源） | `libs/qCC_db/include/ccViewportParameters.h:123-165` |
| FBO 封装 | `libs/CCFbo/src/ccFrameBufferObject.cpp:62-315` |
| Shader 封装 | `libs/CCFbo/include/ccShader.h:28-59` |
| 后处理接口 | `libs/CCFbo/include/ccGlFilter.h:28-98` |
| 颜色拾取 | `libs/qCC_db/include/ccColorBasedEntityPicking.h:28-99` |
| 插件主接口 | `libs/CCPluginAPI/include/ccMainAppInterface.h:37-62, 189-255` |
| GL 插件接口 | `libs/CCPluginStub/include/ccGLPluginInterface.h:26-52` |
| 插件 CMake | `plugins/cmake/Plugins.cmake:94-99` |
| 顶层 CMake（VSG 硬编码） | `CMakeLists.txt:64-68` |
| 库依赖顺序 | `libs/CMakeLists.txt:1-8` |
| Qt OpenGL 全局依赖 | `cmake/CMakeExternalLibs.cmake:10-20` |
| VSG 原型（待重写） | `libs/qCC_vsgWindow/{include,src}/*` |

### VulkanSceneGraph

| 主题 | 路径 |
|---|---|
| Viewer | `include/vsg/app/Viewer.h`（**注意：不是 `vsg/viewer/Viewer.h`**） |
| 帧循环实现 | `src/vsg/app/Viewer.cpp:278-`（compile）、`:814-846`（recordAndSubmit） |
| 窗口/配置 | `include/vsg/app/Window.h:37`（create）、`include/vsg/app/WindowTraits.h:47-99` |
| 相机 | `include/vsg/app/Camera.h:26`、`ProjectionMatrix.h:49,101`、`ViewMatrix.h:53,107` |
| 操控器 | `include/vsg/app/Trackball.h:27-138` |
| 渲染图 | `include/vsg/app/CommandGraph.h:66`、`RenderGraph.h:28-78`、`SecondaryCommandGraph.h:24` |
| 场景节点 | `include/vsg/nodes/{Group,MatrixTransform,Switch,LOD,PagedLOD,StateGroup,VertexDraw,VertexIndexDraw,DepthSorted,Bin}.h` |
| 几何命令 | `include/vsg/commands/{Draw,DrawIndexed,BindVertexBuffers,BindIndexBuffer,CopyImageToBuffer,CopyImageViewToWindow}.h` |
| 管线配置 | `include/vsg/utils/GraphicsPipelineConfigurator.h:97-163` |
| ShaderSet | `include/vsg/utils/ShaderSet.h:25-213` |
| 内置 ShaderSet 生成 | `CMakeLists.txt:134-233`、`src/vsg/utils/shaders/*_ShaderSet.cpp` |
| 数据/动态上传 | `include/vsg/core/Data.h:59-221`、`state/BufferInfo.h:30-98`、`app/TransferTask.h:24-48` |
| 拾取 | `include/vsg/utils/{LineSegmentIntersector,PolytopeIntersector}.h` |
| 包围盒 | `include/vsg/utils/ComputeBounds.h:22-77` |
| 文本 | `include/vsg/text/{Text,Font,TextGroup,StandardLayout}.h` |
| 资源 hint | `include/vsg/state/ResourceHints.h:23-88` |
| 分页 | `include/vsg/io/DatabasePager.h`、`nodes/PagedLOD.h:35-104`、`nodes/TileDatabase.h` |
| macOS 窗口 | `include/vsg/platform/macos/MacOS_Window.h` |
| 构建/依赖 | `CMakeLists.txt:1-98, 243-245`、`cmake/vsgMacros.cmake`、`src/vsg/CMakeLists.txt:279-479` |

### 工作区参考实现

| 主题 | 路径 |
|---|---|
| Qt+VSG 完整 demo | `CloudCompareVSG/viewer3D/`（`VsgQtWidget.cpp:56-70` 帧循环、`FreeCADStyleManipulator.h` 自定义操控器、`ModelLoader.cpp` / `PointCloudLoader.cpp` PCD 解析） |
| vsgQt 库 | `CloudCompareVSG/vsgQt/`（`include/vsgQt/Window.h`、`include/vsgQt/Viewer.h`、`examples/vsgqtviewer/main.cpp`） |

---

## 附录 B：VSG 关键代码模板

### B.1 点云节点（`POINT_LIST`）

```cpp
// 1) ShaderSet（自定义，基于 flat，加 POINT_LIST 与点大小）
auto shaderSet = vsg::createFlatShadedShaderSet(options);
// 或通过 ShaderSet 定义 + 内嵌 SPIR-V 自建 ccVSGShaders::createPointCloudShaderSet()

// 2) 数据
auto verts = vsg::vec3Array::create(count);
auto cols  = vsg::ubvec4Array::create(count);
// ... 填充 ...
verts->properties.dataVariance = vsg::DYNAMIC_DATA;   // 需频繁更新时

// 3) 管线
auto config = vsg::GraphicsPipelineConfigurator::create(shaderSet);
config->enableArray("vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3), VK_FORMAT_R32G32B32_SFLOAT);
config->enableArray("vsg_Color",  VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::ubvec4), VK_FORMAT_R8G8B8A8_UNORM);
config->assignArray(arrays, "vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, verts);
config->assignArray(arrays, "vsg_Color",  VK_VERTEX_INPUT_RATE_VERTEX, cols);
config->shaderHints->defines.push_back("VSG_POINT_SIZE");    // 自定义 define
config->init();

auto stateGroup = vsg::StateGroup::create();
config->copyTo(stateGroup, sharedObjects);                    // SharedObjects 复用管线
stateGroup->add(vsg::InputAssemblyState::create(VK_PRIMITIVE_TOPOLOGY_POINT_LIST));

// 4) 绘制
auto draw = vsg::VertexDraw::create();
draw->assignArrays(arrays);
draw->vertexCount = count;
draw->instanceCount = 1;
stateGroup->addChild(draw);
```

### B.2 相机每帧同步

```cpp
void ccVSGWindow::updateCamera(const ccViewportParameters& vp)
{
    // CC 语义 → 矩阵（复用现有 ccViewportParameters::getViewMatrix / 投影计算）
    vsg::dmat4 view = computeViewMatrix(vp);      // pivot / object-centered / focalDistance
    double nearD, farD;
    computeNearFar(vp, visibleBBox, nearD, farD); // 复用 :1524-1702 逻辑

    _lookAt->set(view);                            // vsg::LookAt::set(dmat4)  (ViewMatrix.h:93)
    if (vp.perspective)
        _proj = vsg::Perspective::create(vp.fov_deg, aspect, nearD, farD);
    else
        _proj = vsg::Orthographic::create(l, r, b, t, nearD, farD);
    _camera->projectionMatrix = _proj;             // vsg 已处理 Vulkan z∈[0,1]
}
```

### B.3 实体拾取（整数 attachment）

```cpp
// 离屏 pick target
auto pickImage  = vsg::Image::create();   // VK_FORMAT_R32_UINT
auto pickView   = vsg::ImageView::create(pickImage);
auto pickGraph  = vsg::RenderGraph::create(window, pickView /* 离屏 view */);
pickGraph->addChild(pickScene);           // 用 ID-shader 的镜像场景（复用同一几何，只换 pipeline）

// 回读
auto copyCmd = vsg::CopyImageToBuffer::create(pickImage, hostVisibleBuffer);
// 在下一帧栅栏后读 hostVisibleBuffer → entityID
```

### B.4 2D Overlay

```cpp
auto overlayCam = vsg::Camera::create(
        vsg::Orthographic::create(0, w, h, 0, 0.0, 1.0),   // 像素坐标
        vsg::LookAt::create({0,0,1}, {0,0,0}, {0,1,0}),
        vsg::ViewportState::create(VkExtent2D{w, h}));

auto overlayView  = vsg::View::create(overlayCam, overlayScene);
auto overlayGraph = vsg::RenderGraph::create(window, overlayView);
overlayGraph->clearValues.clear();   // 不清除主 3D 结果

commandGraph->addChild(mainRenderGraph);
commandGraph->addChild(overlayGraph);
```

---

## 附录 C：待办清单（可直接拆解为 Issue）

**M0**
- [ ] Spike：vsgQt 嵌入 Qt 窗口
- [ ] Spike：PointSize 支持度（macOS/Windows/Linux）
- [ ] Spike：wideLines 支持度
- [ ] Spike：SDF 字体与中文
- [ ] Spike：深度回读与 [0,1] NDC 反投影

**M1**
- [ ] 移除 CMake 中 VSG 硬编码路径；`find_package(vsg)` + `find_package(vsgQt)`
- [ ] 新增 `CC_RENDER_BACKEND` 缓存变量
- [ ] 新建 `libs/qCC_renderCore`（`ccViewInterface` / `ccRenderBackend` / `ccRenderCapabilities` / `ccRenderCommandSink`）
- [ ] `ccGLWindowInterface` 继承 `ccViewInterface`（纯重构，视觉回归验证）
- [ ] 重写 `ccVSGWindow` / `ccVSGWindowInterface`（vsgQt::Window + createWindowContainer）
- [ ] `qCC` 增加"新建 VSG 视图"调试入口
- [ ] 加入 `libs/CMakeLists.txt`

**M2**
- [ ] `ccVSGCameraManipulator`（派生 `vsg::Trackball`）
- [ ] `ccViewportParameters` ⇄ `vsg::Camera` 双向同步
- [ ] Vulkan NDC z∈[0,1] 适配
- [ ] 滚轮/双击全部语义
- [ ] 标准视角与多窗口同步

**M3**
- [ ] PointCloud ShaderSet（POINT_LIST + 点大小）
- [ ] `ccVSGPointCloudBuilder`（chunk + SharedObjects）
- [ ] RGB / SF(color ramp 纹理) / 单色 / 法线 LUT
- [ ] `ccVSGSceneBuilder` 增量同步与 revision
- [ ] billboard quad 回退路径（按 R1 结论）
- [ ] 大点云冒烟与内存预算

**M4**
- [ ] `ccVSGMeshBuilder`（VertexIndexDraw + 顶点/面法线）
- [ ] 线框与混合模式
- [ ] 材质/纹理（无 vsgXchange 时用 QImage 解码）
- [ ] Polyline / Facet / Sensor
- [ ] 半透明：`DepthSorted` + `Bin`
- [ ] 双面光照 / 背面剔除

**M5**
- [ ] overlay RenderGraph + Orthographic
- [ ] `vsg::Text` + SDF 字体（标签/消息）
- [ ] `cc2DLabel` / `cc2DViewportLabel`
- [ ] 比例尺 / 方向轴 / 状态提示
- [ ] 标量场色标
- [ ] `ccImage`

**M6**
- [ ] 实体拾取（R32_UINT + CopyImageToBuffer）
- [ ] 矩形框选
- [ ] 点/三角拾取（复用 CPU 八叉树）
- [ ] 深度反投影
- [ ] `renderToImage()` / 高清截图
- [ ] `ccPickingHub` / `ccOverlayDialog` 对接与交互工具端到端验证

**M7**
- [ ] 后处理框架
- [ ] SSAO 迁移
- [ ] LOD → `vsg::LOD`
- [ ] 分页：`PagedLOD` + `DatabasePager`（可选）
- [ ] 性能调优（SharedObjects / ResourceHints / MSAA / viewport hint）

**M8**
- [ ] `getActiveViewWindow()` + 旧接口 deprecated
- [ ] 插件 metadata `requiresBackends`
- [ ] 迁移仅用窗口 API 的插件
- [ ] 自定义 GL drawable → `ccRenderCommandSink`
- [ ] `CCPluginAPI` 解耦 `QCC_GL_LIB`
- [ ] 立体显示降级提示
- [ ] 文档更新；删除两份旧草稿 md

---

## 附录 D：开发环境记录（macOS 26 arm64）与 M1 进展

> 2026-09-26 在 `vsg` 分支上完成 M1.1 / M1.2 时实测记录。
> **以下三个问题均为环境既有问题，与本次改造无关**（`CCFbo`、`qCC_db` 在修复前同样构建失败）。

### D.1 三个阻塞问题与处置

| # | 问题 | 现象 | 处置 |
|---|---|---|---|
| 1 | **Qt 6.8.2 链接 `-framework AGL`** | `ld: framework 'AGL' not found`。Apple 在 macOS 26 上只保留 `AGL.framework` 空壳（`Versions/A/` 下无二进制），Qt6 的 `FindWrapOpenGL.cmake` 仍回退为 `-framework AGL` | 在本仓库外建 stub 框架 `CloudCompareVSG/.qt-agl-stub/AGL.framework`（含空 dylib），配置时加 `-DCMAKE_SHARED_LINKER_FLAGS=-F<stub>` / `-DCMAKE_MODULE_LINKER_FLAGS=-F<stub>`。并**改用 homebrew Qt6**（`/opt/homebrew/opt/qt@6`） |
| 2 | **ccache 崩溃** | `dyld: Library not loaded: /opt/homebrew/opt/fmt/lib/libfmt.11.dylib`（ccache 4.11.1 链接 fmt 11，实际只有 fmt 12.1.0），所有编译 `Abort trap: 6`。项目 `cmake/CMakeSetCompilerOptions.cmake` 无条件启用 ccache | `brew reinstall ccache`（升级到 4.14）。如需临时规避：`-DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND` |
| 3 | **子模块缺失/漂移** | `plugins/core/IO/CMakeLists.txt` 报 `MeshIO does not contain a CMakeLists.txt`；`CCCoreLib` 停留在 2025-02-24 的 **Qt5** 版本（`find_dependency(Qt5 Concurrent)` 与 Qt6 冲突） | `git submodule update --init <hidapi, MeshIO, quazip, cc3DFin, qG3Point>`；`git submodule update libs/qCC_db/extern/CCCoreLib`（切到 45a98b62，2026-09-23，Qt6） |
| 4 | **qMPlane 插件的 Qt6 不兼容**（与 VSG 无关，但阻塞主程序构建） | `QHBoxLayout::setMargin()` 在 Qt6 已移除；`endl` 需改为 `Qt::endl` | 已在 `ccMPlaneDlg.cpp` / `ccMPlaneDlgController.cpp` 做最小兼容修改。若后续还有类似问题，建议单开一个"Qt6 兼容性"提交 |

### D.2 可用的配置/构建命令

```bash
cd CloudCompare

STUB=/Users/gsl/work/pointsMap/CloudCompareVSG/.qt-agl-stub
VSGQT=/Users/gsl/work/pointsMap/CloudCompareVSG/vsgQt/install-qt6/lib/cmake/vsgQt

cmake -S . -B build-hbqt \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@6" \
  -DvsgQt_DIR="$VSGQT" \
  -DCC_RENDER_BACKEND=Both \
  -DCMAKE_SHARED_LINKER_FLAGS="-F$STUB" \
  -DCMAKE_MODULE_LINKER_FLAGS="-F$STUB" \
  -DCMAKE_EXE_LINKER_FLAGS="-F$STUB"

cmake --build build-hbqt --target QCC_RENDER_CORE_LIB -j8
cmake --build build-hbqt --target QCC_VSG_LIB -j8
cmake --build build-hbqt --target CloudCompare -j8
```

配置成功的标志输出：
```
-- Render backends: OpenGL=TRUE VSG=TRUE
-- VSG backend enabled: vsg=1.1.14 vsgXchange=1
```

### D.3 vsgQt 构建（Qt6）

工作区自带的 `vsgQt` 源码默认绑 Qt5 且未构建，须显式指定 Qt6：

```bash
cd CloudCompareVSG/vsgQt
cmake -S . -B build-qt6 \
  -DQT_PACKAGE_NAME=Qt6 \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@6" \
  -DVSGQT_BUILD_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/install-qt6"
cmake --build build-qt6 -j8
cmake --install build-qt6
```

产物：`install-qt6/lib/libvsgQt.a`、`install-qt6/lib/cmake/vsgQt/vsgQtConfig.cmake`（目标 `vsgQt::vsgQt`）。
> 注意：`vsgQt` 必须与 CloudCompare 使用**同一个 Qt**，否则 ABI 不兼容。

### D.4 M1 已完成项

- [x] 基于 master（含与 `origin/master` 的 merge 提交 `7a50e302`）创建并切换到 `vsg` 分支
- [x] **M1.1** 顶层 `CMakeLists.txt` 移除 VSG 硬编码绝对路径；新增 `CC_RENDER_BACKEND`（OpenGL / VSG / Both，默认 Both）；`cmake/CMakeExternalLibs.cmake` 新增 `find_package(vsg 1.1)` + `find_package(vsgQt)` + `find_package(vsgXchange)`，**找不到时自动降级关闭 VSG 后端并告警**（不破坏既有 OpenGL 构建）
- [x] **M1.2** 新建 `libs/qCC_renderCore`（排在依赖链最前，无 CC 依赖）：
  - `ccViewInterface.h` —— 后端无关 3D 视图根接口；`PICKING_MODE` / `INTERACTION_FLAG` / `MessagePosition` / `MessageType` / `PivotVisibility` 枚举统一定义在此，避免两个后端枚举漂移
  - `ccRenderCapabilities.h` —— 后端能力（pointSize / wideLines / integerPicking / MSAA …）
  - `ccRenderCommandSink.h` —— 过渡期后端无关绘制指令（`beginPass` / `setTransform` / `draw` / `drawIndexed`）
  - `ccRenderBackend.h` + `src/ccRenderBackend.cpp` —— 后端抽象与注册表（默认优先 VSG，回退 OpenGL）
- [x] **M1.3** `ccGenericGLDisplay` 改为 `public ccViewInterface`；`ccGLWindowInterface` 删除自带的 5 组枚举（`PICKING_MODE` / `INTERACTION_FLAG` / `MessagePosition` / `MessageType` / `PivotVisibility`）与文件末尾的 `Q_DECLARE_OPERATORS_FOR_FLAGS`，改为从 `ccViewInterface` 继承
  - 继承后 `ccGLWindowInterface::ENTITY_PICKING` 等写法仍可解析，**现有代码零改动**
  - `ccGLWindowInterface` 新增 `backendName()`（返回 "OpenGL"）与 `renderCapabilities()` 实现
  - `qCC_db` 的 CMake 增加 `QCC_RENDER_CORE_LIB` 依赖
  - 遗留事项：`plugins/core/Standard/qMPlane/tests/mocks/ccGenericGLDisplayMock.h` 未实现新增的纯虚函数（测试默认不构建，`BUILD_TESTING=OFF`）
- [x] **M1.4** 重写 `libs/qCC_vsgWindow`
  - `ccVSGWindowInterface : public ccViewInterface` —— VSG 后端逻辑，持有 `vsgQt::Viewer` / `vsg::Camera` / 场景根 `vsg::Group`；`initializeViewer()` 建立 `createCommandGraphForView` 渲染图并 `compile()`
  - `ccVSGWindow : public QWidget, public ccVSGWindowInterface` —— 用 `vsgQt::Window` + `QWidget::createWindowContainer` 嵌入 Qt，由 `vsgQt::Viewer` 的 `QTimer` 驱动帧循环
  - 链接 `vsg::vsg` + `vsgQt::vsgQt`（两者均为静态库，已成功链入 dylib）
- [x] **M1.5** 接入构建 + qCC 调试入口
  - `libs/CMakeLists.txt` 按 `CC_RENDER_VSG` 开关条件加入 `qCC_vsgWindow`
  - `qCC` 条件链接 `QCC_VSG_LIB` 并定义 `CC_RENDER_VSG_ENABLED`
  - `MainWindow` 在 `menu3DViews` 增加 **"New VSG 3D view (debug)"** 动作，创建 `ccVSGWindow` 并加入 MDI 区
- [x] 构建验证：`QCC_RENDER_CORE_LIB` / `QCC_VSG_LIB` / `CC_FBO_LIB` / `QCC_DB_LIB` / `CCCoreLib` / **`CloudCompare.app`** 全部构建通过；`otool -L` 确认主程序已链接 `@rpath/libQCC_VSG_LIB.dylib`

### D.5 下一步

- **M0（建议立即做）** 三个技术验证 Spike：PointSize（MoltenVK/Metal 不支持 >1）、wideLines、SDF 中文字体 —— 这三项直接决定 M3/M5 的实现方案
- **M2** `ccVSGCameraManipulator`（派生 `vsg::Trackball`）+ `ccViewportParameters` ⇄ `vsg::Camera` 双向同步 + Vulkan NDC z∈[0,1] 适配
- **M3** `ccVSGSceneBuilder`（`ccHObject` 树 → VSG 场景图增量同步）+ 点云渲染
- 遗留：`ccGenericGLDisplayMock`（qMPlane 测试）需补齐新增纯虚函数

### D.6 M0 Spike 结果（2026-09-26 实测）

验证程序：`CloudCompareVSG/vsgSpike/`（独立 CMake 工程，不进入 CloudCompare 构建）

```bash
cd CloudCompareVSG/vsgSpike
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
./build/vsgSpike
```

测试环境：Apple M2 / macOS 26.6 / Vulkan loader 1.3.290 / **MoltenVK 1.2.9** / Vulkan API 1.2.283 / vsg 1.1.14(头文件)

| Spike | 判定 | 实测数据 | 对方案的影响 |
|---|---|---|---|
| **1. PointSize** | ✅ **支持**（推翻原假设 R1） | `pointSizeRange = [1.000 .. 511.000]`，`pointSizeGranularity = 1.000` | 点云可直接用 `POINT_LIST` + `gl_PointSize`，**不需要** billboard quad 主路径。但仍保留回退分支，且**必须运行时**由 `pointSizeRange` 决定（见 R16） |
| **2. wideLines** | ❌ **不支持**（R2 确认发生） | `wideLines` feature = NOT supported；`lineWidthRange = [1.000 .. 1.000]`，granularity 0 | 粗线**必须**用 quad 扩展（CPU 生成三角带）。这是 M4（`ccPolyline`、网格线框）的既定工作量 |
| **3. 字体 / CJK** | ⚠️ **freetype 与字体都可用，但 vsgXchange 不可用** | freetype **2.14.3**；Hiragino Sans GB（29352 字形）与 STHeiti Light（52268 字形）对 `中/文/点/云` 全部命中且可正常栅格化（32px 下 26x31 / 24x29）；Arial Bold 对 CJK 全部返回 0（符合预期） | 中文显示**可行**。但已装 vsgXchange 1.1.6 与 vsg 1.1.14 ABI 不兼容（见 R15），需重建 vsgXchange 或自写 freetype → `vsg::Font` 构建器 |

**附带发现（环境问题，见 R15）**

- `/usr/local` 的 vsg **头文件为 1.1.14**，但 `libvsg.a` 运行时自报 **1.1.11** —— 版本不一致。
- 静态库符号佐证：`/usr/local/lib/libvsg.a` 定义的是无参版 `vsg::Data::computeValueCountIncludingMipmaps() const`；而工作区自编译的 `VulkanSceneGraph/lib/libvsg.a` 定义的是 4 参旧版 `...(ulong,ulong,ulong,uint)`。vsgXchange 1.1.6 引用的是 4 参版 → 与 /usr/local 的库不匹配。
- 建议：统一从同一源码树重建并安装 vsg + vsgXchange，消除头文件/库版本错位。

### D.7 M2 相机适配实现说明

新增文件（`libs/qCC_vsgWindow/`）：

| 文件 | 作用 |
|---|---|
| `include/vsg/ccVSGCameraAdapter.h` / `src/vsg/ccVSGCameraAdapter.cpp` | `ccVSGViewMatrix` / `ccVSGProjectionMatrix`：直接把 CC 算好的矩阵喂给 `vsg::Camera`；`toVSGMatrix()` 负责 OpenGL 列主序 → VSG 行主序转换 |
| `include/vsg/ccVSGCameraManipulator.h` / `src/vsg/ccVSGCameraManipulator.cpp` | CC 语义的相机操控器（**不继承 `vsg::Trackball`**，而是直接继承 `vsg::Visitor`） |

关键设计：

- **单向数据流**：操控器只修改 `ccViewportParameters`（唯一真源），从不直接改 `vsg::Camera`；`updateCamera()` 再从参数推导两个矩阵。这样两个后端行为天然一致。
- **为什么不用 `vsg::Trackball`**：CC 的旋转是"把鼠标位置投影到单位球 + `ccGLMatrixd::FromToRotation`"的虚拟轨迹球，且旋转中心是 pivot point（object-centered）而非屏幕中心；`vsg::Trackball` 的 rotate/zoom/pan 模型与之不同，套用会丢失 pivot 与 object-centered 语义。
- **复刻自 OpenGL 后端的算法**：
  - 旋转：`convertMousePositionToOrientation()`（对应 `ccGLWindowInterface.cpp:1953`）+ `FromToRotation` + `viewMat = rotMat * viewMat`（对应 `:3369`）
  - 平移：`u = (dx*pixSize, -dy*pixSize, 0)`，object-centered 时取反，再 `moveCamera()`（对应 `:6541-6570`）
  - 投影：near/far 由可见包围盒 8 角点在相机空间的 -z 范围推出（对应 `:1550-1702`），再用 `vsg::perspective()/orthographic()` 构造 **reverse depth** 矩阵
- **按键映射**：`BUTTON_MASK_1`(左)=旋转、`BUTTON_MASK_3`(右)=平移、`BUTTON_MASK_2`(中)=缩放（VSG 的 mask 值为 256/512/1024）

已知待办：
- 缩放系数、滚轮 Alt/Ctrl/Shift 修饰键（点大小 / near-far / FOV）需在与 OpenGL 后端对比时调优
- 双击设 pivot（依赖深度反投影）属 M6
- 场景包围盒目前用 `getBB_recursive()`，M3 引入 `ccVSGSceneBuilder` 后可改用 VSG 场景图的包围体

**未做的验证（延后）**

- PointSize 的**端到端渲染**验证（画一个 `gl_PointSize=32` 的点并回读像素确认）本次只做了设备能力查询。设备能力是 Vulkan 规范中的权威指标，最终确认放在 **M3 验收**（点云截图与 OpenGL 后端对比）。

# CloudCompare 渲染引擎迁移至VSG的TODO列表

## 1. 分析现有渲染架构
- [ ] 分析qCC_glWindow模块中的OpenGL实现
- [ ] 识别核心渲染流程和关键接口
- [ ] 绘制当前渲染架构图

## 2. VSG库集成
- [ ] 将VSG库路径(/Users/gsl/work/pointsMap/CloudCompareVSG/VulkanSceneGraph)添加到项目依赖
- [ ] 更新CMake配置以包含VSG
- [ ] 验证VSG初始化流程

## 3. 创建VSG渲染后端
- [ ] 设计VSG渲染接口类
- [ ] 实现基础渲染功能(点云、网格等)
- [ ] 添加VSG特定的资源管理

## 4. 替换OpenGL调用
- [ ] 逐步替换qCC_glWindow中的OpenGL调用
- [ ] 保持接口兼容性
- [ ] 处理平台特定代码(特别是MacOS)

## 5. 测试与验证
- [ ] 单元测试关键渲染功能
- [ ] 性能基准测试
- [ ] 跨平台验证(Windows/Linux/MacOS)

## 6. 优化与清理
- [ ] 移除废弃的OpenGL代码
- [ ] 性能优化
- [ ] 文档更新

## 注意事项
- 保持向后兼容性
- 注意MacOS平台的Vulkan支持
- 分阶段实施，确保每个步骤都经过充分测试
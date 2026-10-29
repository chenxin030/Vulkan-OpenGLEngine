# 基于 Vulkan / OpenGL 的三维渲染引擎

使用 C++20 实现的小型三维引擎，包含双图形后端、PBR 渲染、场景编辑器、Archetype ECS 和独立 Player。工业场景将模型资源、层级动画、对象池与场景持久化串成完整的编辑和运行流程。

项目正式名称为“基于 Vulkan / OpenGL 的三维渲染引擎”，CMake 项目及构建目标为 `VulkanOpenGLEngine`，生成程序为 `VulkanOpenGLEngine.exe`。

## 项目要点

- **Vulkan / OpenGL 双后端**：通过统一 Renderer 接口消费场景快照，各自管理 GPU 资源、绘制和同步；同一场景可切换后端运行。
- **完整的离屏渲染流程**：方向光阴影、金属度/粗糙度 PBR、HDR、IBL、法线贴图、MSAA 和色调映射，最终将 LDR 图像显示在编辑器视口中。默认请求 4× MSAA，根据设备能力回退。
- **Archetype ECS**：以固定容量 Chunk 和按列组件存储组织数据，使用 index/generation 实体句柄，支持实体增删和组件迁移。
- **显式系统组合与固定步长**：场景通过 `systems` 清单选择运行逻辑，统一以固定 60 Hz 推进模拟；编辑器 Play 和独立 Player 复用 SceneRuntime。
-  **可编辑、可保存、可运行的场景**：提供 Assets、Hierarchy、Inspector、拾取和操纵轴，支持模型拖入、父子层级、场景保存与 Play / Stop。Play 使用编辑场景的副本，停止后恢复编辑数据。

## 构建方式

### 环境依赖

| 依赖                                  | 要求与用途                                                             |
| ------------------------------------- | ---------------------------------------------------------------------- |
| Visual Studio 2022 / Build Tools 2022 | 安装“使用 C++ 的桌面开发”工作负载及 Windows SDK，编译 C++20            |
| CMake                                 | 3.24 或更新版本                                                        |  |
| Vulkan SDK                            | 提供 Vulkan 头文件、库与 `glslc`；安装后确认 `VULKAN_SDK` 环境变量可用 |
| 显卡驱动                              | 运行所选后端需要 Vulkan 或 OpenGL 4.5 支持                             |
| GLM                                   | 单独准备头文件，目录下应包含 `glm/glm.hpp`                             |

### 配置与编译

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_CXX_FLAGS="/IC:/Libraries/glm"

cmake --build build --config RelWithDebInfo --target VulkanOpenGLEngine --parallel
```

## 启动与使用

### 编辑器

```powershell
# 空场景，默认使用 Vulkan
.\build\RelWithDebInfo\VulkanOpenGLEngine.exe

# 打开普通场景
.\build\RelWithDebInfo\VulkanOpenGLEngine.exe vulkan --scene assets/scenes/phase5-demo.scene.json

# 使用 OpenGL 打开工业场景
.\build\RelWithDebInfo\VulkanOpenGLEngine.exe opengl --scene assets/industrial/industrial.scene.json
```

- Assets 浏览模型与场景；拖入 GLB 创建实例，打开 `.scene.json` 切换场景。
- Hierarchy 选择、筛选实体，Inspector 和操纵轴编辑变换。导入模型的整体变换通过根实体编辑。
- 通过 **Scene Systems** 菜单选择 Animation / Industrial；选择 Industrial 会同时启用其依赖 Animation。
- 编辑态只更新基础父子变换；点击 Play 后执行场景系统，Stop 丢弃运行副本。工业专用的 MachineMotion / CargoRoute 属性面板已移除，规则仍支持序列化和运行。
- 右键配合 WASD 移动相机，Q / E 升降。编辑器相机只接收视口手势，UI 输入经捕获处理；Escape 关闭程序。

### 直接“游玩”

```powershell
.\build\RelWithDebInfo\VulkanOpenGLEngine.exe vulkan --play assets/industrial/industrial.scene.json
.\build\RelWithDebInfo\VulkanOpenGLEngine.exe opengl --play assets/scenes/phase5-demo.scene.json
.\build\RelWithDebInfo\VulkanOpenGLEngine.exe --help
```

Player 加载场景后直接创建 Runtime，不再进入编辑器。相对场景路径以当前工作目录为起点；从其他目录启动时可传绝对路径。

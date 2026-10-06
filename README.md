# engine-rebuild

**这是练习工程，不是作品集。**

从零重写 mini 引擎的核心。参照 `game-engine-portfolio`，但**先自己写**，卡住了才回去看它。

---

## 为什么它在这里，而不在 portfolio 仓库里

`docs/04` 第 0 节：面试官打开 GitHub，**第一眼看的是 commit 历史**，不是代码。
portfolio 仓库的提交历史本身就是作品集资产 —— 练习代码混进去会稀释它，而**稀释不可逆**
（改写历史意味着强制推送）。

**写得好，它是第三个作品；写得不好，删掉它，portfolio 毫发无伤。** 这是把练习和作品隔开的全部理由。

放在 workspace 目录下（而不是 `E:\Desktop\DSH\`）是因为 **DSH 沙箱只允许写工作区内的路径**。

---

## 构建

```powershell
cd "E:\Desktop\DSH\DeepSeek Harness\engine-rebuild"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\Rebuild.exe
```

- 第一次配置会联网拉 GLFW / ImGui / stb（**GitHub 要开代理**），之后缓存在 `build\_deps`
- 报奇怪的错就删 `build\CMakeCache.txt` 和 `build\CMakeFiles` 再配置 —— **千万别删 `build\_deps`**
- 贴图是**生成**的（`assets/` 已忽略），需要时手动生成一次：
  `cmake --build build --target DemoTexture` → `assets\checkerboard.ppm`

---

## 进度

| 天 | 交付 | 状态 |
|---|---|---|
| **D3** | **窗口能开、能关、能拖大小** | ✔ `a7717f5` |
| D4 | Instance + 验证层 + Debug Messenger + Surface + 物理/逻辑设备 | ✔ `a7717f5` |
| D5 | Swapchain + ImageView + RenderPass + Framebuffer + 命令缓冲 + 同步 → **清屏色** | ✔ `7e3773b` `667291f` |
| D6 | 三角形 + 交换链重建 + ImGui | ✔ `abc9752` `bf2298d` |
| D11 | 顶点/索引缓冲 + staging 两段式上传 | ✔ `c65e91b` |
| D12 | 深度缓冲 + UBO/MVP + 描述符集 | ✔ `01f1606` `12d66c5` |
| D13 | 抽 `Renderer` 接口 + 工厂（**`main.cpp` 不再含 `vulkan`**） | ✔ `67439fc` |
| D18 | 贴图：stb + 两次布局转换 + 采样器 | ✔ `eecad24` |
| D19 | mip 链（含**目标层级**的 barrier）+ 各向异性 | ✔ `dd9bd9e` |
| D25 | 多物体 + `model` 改 push constant | ✔ `b35e7ba` |
| D26 | `Camera` + `InputState` + `GlfwInput` | ✔ `9529b2f` |
| D27 | `CameraController` + 接进主循环（WASD 飞起来） | ✔ `9529b2f` |
| D32 | ECS：`Entity` + `Registry` + `Components` + `Systems` | ✔ `732d810` |
| D33 | `MeshLibrary` + `GpuMesh` + 懒惰上传 | ✔ `6dd9412` |
| D34 | 热重载：`FileWatch` + `ReloadTexture` + 失败回退 | ✔ `abc1d69` |
| D35 | 练习：加圆柱体 + `Bob` 与 `BobSystem`（形状 A→B） | ✔ `89cfe4a` |
| D38 | 接 VMA + 每处主机写入后 flush | ✔ `a68d088` |
| D41–D42 | 第二个后端（OpenGL）画出同一个场景 | ⬜ |

> 验收记录（证据、命令、判据）在 `notes\` 下，每个增量一份；`notes/` 不入库。
>
> **收口（D36 拍板）**：做完 **D38** 与 **D41–D42** 即结题 —— 其余学习日降级为读 / 跑，
> 省下的时间给作品集。理由与账目见 `notes\rebuild-scope.md`。

---

## 三条纪律

1. **每个增量必须编译通过再往下走。** 一次性搬两千行，编译报错会糊成一团，分不清是哪个改动引起的 ——
   这是 mini-engine 已验证过两次的教训。
2. **一个增量一次提交。** `030d6ab`（3 个增量、18 个文件）就是反例，它**已经无法拆分**了。
3. **卡住了才回去看原版。** 顺序反了就等于抄 —— 而"抄"学不到任何东西。

---

## 提交前（两条自查）

```powershell
git branch --show-current          # 必须输出 main；空输出 = 游离 HEAD，立刻停手
git status --short                 # 应当只剩这次故意改的文件
```

> 游离 HEAD 是**真踩过**的坑：D32–D35 的提交一度全部落在游离链上，`main` 还停在 D32 之前
> （因为每次只看提交输出的最后两行，没看分支名）。游离链是新提交时的恢复办法：
> `git branch -f main HEAD; git checkout main`，恢复后逐个 verify：
> `git merge-base --is-ancestor <hash> main`。

**文件编码**：仓库对 BOM **不作要求**。VS 默认带 BOM 保存，脚本/工具写入的不带；
两种都能编译、都能被 Obsidian 正常读取，因此**不为此翻动已有文件**（纯空白改动会搅浑历史）。

---

## 验收命令（每个阶段都该是空的）

```powershell
# 应用层不含任何后端头 —— 注意必须限定在 include 行
findstr /i "#include" src\main.cpp | findstr /i "vulkan opengl"
```

> 直接搜全文会被**注释**匹配到。mini-engine 的 `main.cpp:10` 就有一条注释写着这条命令本身，
> 于是那条验证**永远通不过失败这一关** —— 一条永远"通过"的验证，等于没有验证。

---

## 卡住时去哪查

| 想做的事 | 看哪 |
|---|---|
| 窗口 + 主循环 | `game-engine-portfolio\mini-engine\src\main.cpp` |
| Vulkan 引导链 | `vulkan_renderer.cpp` 的 `CreateInstance` → `CreateLogicalDevice` |
| 交换链 / RenderPass / 管线 | `CreateSwapChain` / `CreateRenderPass` / `CreateGraphicsPipeline` |
| 贴图上传（两次布局转换） | `RecordImageTransition` / `UploadTexture` |
| mip 链 | `RecordMipChain` |
| ECS | `scene\Entity.h` / `Registry.{h,cpp}` / `Systems.cpp` |
| 热重载 | `ReloadTexture` / `WriteTextureDescriptors` |
| 逐日清单 | `portfolio-main\docs\12-reading-list.md` |

**用函数名定位，不要硬套行号** —— 行号随提交漂移（`proj[1][1]` 在 `bace27c` 是 1224，在 `main` 上是 1432）。

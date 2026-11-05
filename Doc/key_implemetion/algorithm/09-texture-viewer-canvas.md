# TextureViewer 二维画布与纹理预览

总体入口见 [Editor 架构](../../architecture/app/editor.md#textureviewer-多纹理检查)。源码：[EditorTextureViewer.cpp](../../../src/Editor/TextureViewer/Private/EditorTextureViewer.cpp)、[TextureViewerPreview.cpp](../../../src/Editor/TextureViewer/Private/TextureViewerPreview.cpp)、[TextureViewer.sf](../../../shaders/editor/TextureViewer.sf)。本文对应 DrawCanvas、TextureViewerPreview::Render 和 MainVS/MainPS；生命周期见 [工程专题](../performance/08-ui-rendering-and-window-lifetime.md#textureviewer-来源与资源生命周期)。

## 坐标与交互

输入为鼠标屏幕坐标、画布客户区、每张图的逻辑中心及所选 Mip 尺寸，输出为 ImGui 图元和下一次交互状态。屏幕和画布均 X 向右、Y 向下；逻辑单位是预览纹理像素，Z=1 时一个纹理像素占一个界面单位，不保证在任意系统 DPI 下等于一个物理显示像素。

设画布屏幕中心为 C，平移为 P=(m_PanX,m_PanY)，缩放为 Z=m_Zoom，图片逻辑中心为 W，则其显示中心 S=C+P+ZW。边长为所选 Mip 的宽高乘 Z。新图令 W=-P/Z，因此 S=C，不受先前画布平移/缩放影响。中心和平移使用 double，提交顶点时转 float。

- 拖图：逻辑中心增量 ΔW=MouseDelta/Z，画布不动。
- 拖空白：ΔP=MouseDelta，所有图片与网格一起移动。
- 中键拖动：在空白或任意图片上都只执行 ΔP=MouseDelta，不改变当前选择或图片逻辑中心；复用同一个 Canvas InvisibleButton 的 Middle 输入和当前原生 Window 捕获，不向场景重发输入。
- 选择：先测试已选图片，再逆序测试其他图片的显示矩形；选中图片最后绘制并加黄色边框。同一份 Id 同步列表选择。透明像素也属于图片矩形，不做逐像素 Alpha 拾取。
- 滚轮：Z'=clamp(Z×1.15^wheel,1/128,64)。鼠标 M 对应的逻辑点 Q=(M-C-P)/Z；令 P'=M-C-Z'Q，则缩放后仍有 M=C+P'+Z'Q，鼠标下内容保持固定。缩放只改变合成尺寸，不重建 GPU 预览。

每帧遍历图片并绘制，时间 O(N)，持久 UI 数据 O(N)，未实现空间索引或任意图片数量的性能保证。所有图片共享同一 ImGui 画布，不创建原生子窗口或独立渲染环境。

## 自适应细分网格

DrawCanvas 用 d=2^floor(log2(64/Z)) 作为主网格逻辑间距，故屏幕间距 dZ 维持在约 32～64 单位。细网格间距为 d/8，其 Alpha 为 22×clamp((dZ/8-4)/4,0,1)，主网格 Alpha 为 55（以 255 为满值）。缩放到细线太密时将其淡出；跨越二次幂档位时改用下一组主/细间距，不通过累积细分生成无限多顶点。

将画布左右/上下边界用 W=(S-C-P)/Z 反变换到逻辑空间，从 floor(min/d) 到 max 枚举主线；细线同理。全部绘制受画布剪裁，只生成可见范围，工作量由视口尺寸和当前屏幕间距决定，与已经平移过的距离无关。这是 CPU 线条分级，不使用硬件细分着色器；二次幂切档可能有轻微亮度变化。逻辑坐标并非真正数学无限，极端 double/float 精度及 ImGui 顶点容量仍是边界。

逻辑原点始终映射到 C+P。普通两层网格跳过坐标为零的线，再单独画 X（红色）和 Y（蓝色）主轴，线宽固定为 2 个界面单位、Alpha=235，区别于普通单宽灰线。可见轴的正向边缘标注 +X/+Y，交点标注 0；2D 画布 Y 沿原约定向下。主轴跟随画布平移及鼠标锚点缩放，不固定在窗口中心，不在每个网格周期重复。轴和标签受 Canvas 剪裁，并位于图片背后；原点移出视口时不伪造一组屏幕中心轴。没有新增 GPU 预览 Pass 或纹理重绘。

## 纹理预览和子资源

每张图片持有自己的 RGBA8 渲染目标，宽高为 max(1,源尺寸>>Mip)。全屏三角形 MainVS 由顶点 Id 产生位置和 UV，无顶点缓冲。OpenGL 保留 clip Y，Vulkan/D3D 按 VIEWER_FLIP_Y 翻转；Vulkan 现有 RHI 还会设置负高度原生 viewport，组合后的图像方向以实际 RGBA 非对称测试核对。

MainPS 用 TEXTURE_KIND 三个变体分别采样 Texture2D、TextureCube、Texture2DArray，Settings.x/y 对应 Mip 和 Slice。使用 GlobalNearestSampler 与 SampleLevel，Mip 是显式整数级别；数组选择独立 Layer，Cube 选择单个面，不把 Cube SRV 强制转换成 2D SRV。

Cube 将 q=2uv-1、p=(q.x,-q.y) 转为采样方向，面定义与既有环境光预计算一致：

| 面 | 方向 |
| --- | --- |
| +X | (1,p.y,-p.x) |
| -X | (-1,p.y,p.x) |
| +Y | (p.x,1,-p.y) |
| -Y | (p.x,-1,p.y) |
| +Z | (p.x,p.y,1) |
| -Z | (-p.x,p.y,-1) |

Render 注册源/输出为 external texture，通过现有 SHADER_PARAMETER_RDG_TEXTURE 声明源读取，通过 render-target slots 声明颜色写入。当前保守声明整个源 SRV，实际采样仅访问所选面/层/Mip；没有绕过 RDG 状态或 Fence，也没有为本模块修改 RenderCore 的视图/访问参数接口。

预览 pass 不启用混合，直接保存采样 RGBA，因此源 Alpha 保留；画布随后沿既有 ImGui Alpha 混合显示。2D 的读回用不对称且含透明度的测试图片逐字节验证，包括上下方向。RGBA8 不是完整数值检查：浮点 HDR 超出 [0,1] 会截断，未增加曝光、色调映射、单通道选择、精确浮点探针或额外 sRGB 显示管理，采样遵循源 SRV 自身格式语义。

## 支持边界及验证

当前仅支持有 ShaderResource 用途的单采样彩色 2D、Cube、2DArray，格式须在 GFormatInfoMap 中具有合法彩色像素描述。当前表没有整数 R16_UInt/R32_UInt，Viewer 按不支持格式拒绝，不将其错误绑定到浮点采样 Shader。未加载、深度/模板、多重采样或其他维度在来源页给出不能添加的原因。文件候选在确认时调用既有 TextureAsset 加载器，实际解码格式沿用原实现。尚未建立材质来源适配，仅在宿主保留 TODO；不把来源列表当成任意 GPU 中间资源捕获器。

每张图分配所选 Mip 原尺寸的 RGBA8 输出，没有额外分辨率上限、虚拟纹理或分块流送；同时打开许多大纹理会占用显存。切换 Mip 会调整画布中这张图的实际尺寸，几何中心保持原位置；Frame selected 只居中，不自动缩放适配。

真实 Loop 输入与 GPU 像素、数组层、生成环境 Cube 面和 Mip，以及三个后端 Shader/native pipeline 检查见 [验证记录](../../dev/bugfix/故障与修复记录-2026-11-04.md#textureviewer-多纹理画布与来源选择)。后者只说明 Shader/管线创建，不能代替另外两个后端的完整 GUI 像素验证。

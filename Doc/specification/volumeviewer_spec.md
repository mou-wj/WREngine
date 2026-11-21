# VolumeViewer

在编辑器的 View 栏选择 **Volume viewer**。窗口内置地面、球体、两个遮挡物和平行光，可以在未打开工程时使用。视图支持右键环绕、中键平移、滚轮缩放和 **Reset camera**。关闭窗口后可从 View 栏重新打开。同一应用的主视口和 GIViewer 各自拥有独立的渲染输入。

预览场景使用 Engine 的基础网格和灯光组件。VolumeViewer 持有组件、资产引用、相机及颜色目标，经既有 RenderInterface 构建延迟渲染图。颜色目标随窗口内容区尺寸变化，按比例限制在 1280×720 以内。ImGui 在图形提交后采样这个纹理。关闭帧先停止提交预览图片，关闭或调整尺寸时才按纹理记录的最后访问 Fence 等待，再释放或替换目标。

基础预览使用既有的 GBuffer、场景深度、表面光照和颜色 Pass。测试场景及参数由界面模块持有，介质参数的类型由 Renderer 的 Volume 目录定义。 [预览入口](../../src/Editor/VolumeViewer/Private/EditorVolumeViewer.cpp) · [介质参数](../../src/Framework/Renderer/Private/Volume/VolumeRendering.h)

隔离 Debug 构建验证了模块链接、Launcher 启动、首帧提交和 View 栏入口。交互验证停在工具栏，预览窗口的轨道操作、缩放和退出尚未进行界面复验。Launcher 日志仍有已有的 Vulkan Shader 模块诊断。

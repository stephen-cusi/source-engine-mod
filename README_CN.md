# Source Engine
[![GitHub Actions Status](https://github.com/nillerusr/source-engine/actions/workflows/build.yml/badge.svg)](https://github.com/nillerusr/source-engine/actions/workflows/build.yml) [![GitHub Actions Status](https://github.com/nillerusr/source-engine/actions/workflows/tests.yml/badge.svg)](https://github.com/nillerusr/source-engine/actions/workflows/tests.yml)

# QQ 群
- 群号：768616259

# zzh Fork 的仓库

Information from [wikipedia](https://wikipedia.org/wiki/Source_(game_engine)):

Source 是由 Valve 开发的 3D 游戏引擎。  
它作为 GoldSrc 的继任者首次亮相，于 2004 年 6 月发布《半条命：起源》（Half-Life: Source），随后发布了《反恐精英：起源》（Counter-Strike: Source）和《半条命 2》（Half-Life 2）。  
Source 引擎没有明确的版本号，而是以增量更新的方式发布。

当前项目的源代码基于 2018 年泄露的《军团要塞 2》（TF2）代码。不要将其用于商业用途。

此项目使用 WAF 构建系统。如果您有 WAF 相关的问题，请查看 [WAF 官方文档](https://waf.io/book)

# 特性：
- 支持现代工具链  
- 修复了许多未定义的行为  
- 支持触控（即使在 Windows/Linux/macOS 上也能使用）  
- 支持 VTF 7.5  
- 支持 PBR  
- 支持 BSP v19-v21（BSP v21 部分支持，Portal 2 和 CS:GO 地图运行良好）  
- 支持 MDL v46-49  
- 移除了一些无用/不必要的依赖项  
- 成就系统无需 Steam 即可运行  
- 修复了许多 Bug

# 当前任务
- 为 OpenGL 渲染重写材质系统  
- 支持 dxvk-native  
- 移植到 Elbrus  
- 支持 Bink 音频（用于 video_bink）  
- 修复 GamepadUI 的 bug

# 警告
- **GamepadUI 不支持以下模块：** portal、cstrike、dod、hl1mp、hl1  
- **可以与以下模块一起工作：** hl2 和 episodic  

# 如何构建？
- [构建说明（英文）](https://github.com/nillerusr/source-engine/wiki/Source-Engine-(EN))
- [构建说明 (简体中文)](https://github.com/2376780283/source-engine-mod-gamepadui/wiki/Source‐Engine‐(CN))

 <img src="https://github.com/2376780283/source-engine-mod-gamepadui/blob/default/.github/workflows/BG_CS_Hyakkiyako_02_kr.jpg" width="500">

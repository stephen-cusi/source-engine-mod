# HL2SB 玩家人体动画（走路 / 奔跑 / 蹲下 / 朝向）根因调查报告

日期：2026-09-14 ｜ 引擎仓库 `D:\project\source-engine` 分支 `anim_smooth_experiment`
本文只写**有代码/日志证据**的结论，每条都给了文件:行号。凡是推测都显式标注「待验证」。

---

## 0. 一句话结论

**客户端的 `m_flCycle` 被「插值器」每帧覆写回旧值，所以屏幕上的人体永远停在序列的第 0 帧附近
（日志实测恒定 `cycle=0.011`），而后端服务端自己算得好好的（`cycle=0.730 → 0.258 → …`）。
换弹动作走的是 overlay 手势层（另一条渲染路径），所以它一直正常。**

修的是三个位置（都在 `game/client/c_baseanimating.cpp`）：

| # | 位置 | 改动 |
|---|---|---|
| A | `C_BaseAnimating` 构造函数 | `m_bClientSideAnimation = false;` **提前**到 `AddBaseAnimatingInterpolatedVars()` 之前（原来在它之后，等于读未初始化内存来决定插值方式） |
| B | `RemoveBaseAnimatingInterpolatedVars()` | HL2MP 那个「predictable 别删 m_flCycle」的 HACK 增加 `\|\| m_bClientSideAnimation` 条件，客户端自己驱动动画时**必须**把它移出插值表 |
| C | `PostDataUpdate()` | 删掉我上一版**无效**的 `m_flAnimTime` 还原（见 §3），改成显式跑 `UpdateRelevantInterpolatedVars()` |

---

## 1. 现象与实测数据

`D:\srceng\hl2sb\ds_debug.log`（2026-09-14 07:56 那次运行，即上一版 DLL）里的 `animdbg` 行：

```
[animdbg/cl] (null) seq=109 'idle_revolver' act='ACT_HL2MP_IDLE_REVOLVER' cycle=0.011 rate=1.00 speed=0.0 | move_x idx=1 val=+0.50 | move_y idx=0 val=+0.50
[animdbg/cl] (null) seq=109 'idle_revolver' ... cycle=0.011 ...
[animdbg/sv] models/player/group02/male_04.mdl seq=109 'idle_revolver' cycle=0.730 rate=1.00 ...
[animdbg/sv] models/player/group02/male_04.mdl seq=109 'idle_revolver' cycle=0.258 rate=1.00 ...
```

* 客户端 `cycle` **恒定 0.011**（连续 40 条采样、跨 20 秒），服务端同样的 seq/act 却在正常绕圈。
* `seq` 两边一致 ⇒ 序列是被正确网络同步的，问题**不在选序列**。
* `rate=1.00` 两边一致 ⇒ 问题**不在播放速率**。
* `move_x/move_y = +0.50` = 归一化后的正中间 ⇒ 速度 0 时混合参数是对的（服务端那行 0.00 是另一个 bug，见 §6.3）。

⇒ 唯一坏掉的就是「客户端自己的 cycle 推进」。

---

## 2. 机制：这套引擎里「客户端自己驱动动画」是怎么运作的

这一节是后面所有结论的基础，全部来自本树源码。

### 2.1 服务端会把 cycle 藏起来

`game/server/baseanimating.cpp:257`：

```cpp
SendPropDataTable( "serveranimdata", 0, &REFERENCE_SEND_TABLE( DT_ServerAnimationData ), SendProxy_ClientSideAnimation ),
```

而 `DT_ServerAnimationData` 里**只有** `m_flCycle`（`baseanimating.cpp:221-224`）。同一个 proxy 也挂在
`DT_AnimTimeMustBeFirst`（`m_flAnimTime`）上（`game/server/baseentity.cpp:269`），实现是：

```cpp
// game/server/baseentity.cpp:153-162
void* SendProxy_ClientSideAnimation( const SendProp *pProp, const void *pStruct, const void *pVarData, ... )
{
	CBaseAnimating *pAnimating = pEntity->GetBaseAnimating();
	if ( pAnimating && !pAnimating->IsUsingClientSideAnimation() )
		return (void*)pVarData;   // 服务端驱动 -> 发
	else
		return NULL;              // 客户端驱动 -> 这两张表整个不发
}
```

**⇒ 对 `UseClientSideAnimation()` 的玩家，客户端永远收不到 `m_flCycle`，也收不到 `m_flAnimTime`。**

`DT_BaseAnimating` 里仍然会发的只有：`m_nSequence`、`m_flPlaybackRate`、`m_flPoseParameter`、
`m_bClientSideAnimation`、各种 parity（`baseanimating.cpp:229-264`）。

### 2.2 客户端唯一的推进点是 `FrameAdvance(0)`

* `C_BaseAnimating::StudioFrameAdvance()`（客户端）第 5137 行开头就 `if ( m_bClientSideAnimation ) return;`。
* 客户端真正的入口是 `UpdateClientSideAnimations()`（`c_baseanimating.cpp:6162`，由
  `game/client/cdll_client_int.cpp:2448` 每帧调用），它遍历「客户端动画实体表」，只处理带
  `FCLIENTANIM_SEQUENCE_CYCLE` 标志的（`c_baseanimating.cpp:6170`），然后
  `UpdateClientSideAnimation()` → `FrameAdvance( 0.0f )`（`c_baseanimating.cpp:4953-4971`）。
* 实体进表的条件只有一个：`PostDataUpdate()` 里 `if ( m_bClientSideAnimation ) AddToClientSideAnimationList()`；
  标志由 `ComputeClientSideAnimationFlags()` 给出，基类返回 `FCLIENTANIM_SEQUENCE_CYCLE`
  （`c_baseanimating.cpp:4948-4951`，本树里**没有任何玩家类覆写它**，已全仓库 grep 确认）。
* `FrameAdvance()` 的推进量（`c_baseanimating.cpp:5308-5341`）：
  `addcycle = flInterval * GetSequenceCycleRate(hdr, GetSequence()) * m_flPlaybackRate`，
  其中 `flInterval = gpGlobals->curtime - m_flAnimTime`（第 5294-5306 行），然后
  `SetCycle( GetCycle() + addcycle )`。

⇒ 也就是说，**这条链路本身是通的**：实体在表里、标志正确、interval 也有值
（`m_flAnimTime` 只被 `FrameAdvance` 自己写过，所以 interval 就是帧间隔）。
既然链路通，`cycle` 还是恒定 ⇒ **一定有人在 `FrameAdvance` 之后把 `m_flCycle` 改回去**。

### 2.3 谁在改回去：插值器 `m_iv_flCycle`

`C_BaseEntity::Interp_Interpolate()`（`game/client/c_baseentity.cpp:857-889`）每帧对
`m_VarMap` 里 `m_nInterpolatedEntries` 个变量调用 `watcher->Interpolate(currentTime)`——
它会把「网络值/历史值」插值后**写回该变量**。`m_flCycle` 就是其中之一：

```cpp
// c_baseanimating.cpp:871-881（客户端）
void C_BaseAnimating::AddBaseAnimatingInterpolatedVars()
{
	AddVar( m_flEncodedController, &m_iv_flEncodedController, LATCH_ANIMATION_VAR, true );
	AddVar( m_flPoseParameter, &m_iv_flPoseParameter, LATCH_ANIMATION_VAR, true );

	int flags = LATCH_ANIMATION_VAR;
	if ( m_bClientSideAnimation )
		flags |= EXCLUDE_AUTO_INTERPOLATE;      // ← 关键开关

	AddVar( &m_flCycle, &m_iv_flCycle, flags, true );
}
```

`EXCLUDE_AUTO_INTERPOLATE` 的语义（`interpolatedvar.h:34`）：**不参与自动插值**，`AddVar` 时也不计入
`m_nInterpolatedEntries`（`c_baseentity.cpp:6417-6425`）。这正是为「客户端自己驱动 cycle」准备的开关。

**但本树有两处让它失效：**

1. **构造函数里的顺序是反的**（`c_baseanimating.cpp:688` vs `:711`，修前）：

   ```cpp
   AddBaseAnimatingInterpolatedVars();   // 688 行：此时 m_bClientSideAnimation 还没赋值
   ...
   m_bClientSideAnimation = false;       // 711 行：太晚
   ```

   `AddBaseAnimatingInterpolatedVars()` 在第 877 行读的就是这个**尚未初始化**的成员。
   新建实体内存一般是干净的 0，于是 `flags` **没有** `EXCLUDE_AUTO_INTERPOLATE`
   ⇒ `m_flCycle` 被登记成「自动插值」变量，而且是 `AddToHead` + `++m_nInterpolatedEntries`
   （`c_baseentity.cpp:6421-6425`）。

2. **HL2MP 的 HACK 让它永远改不回来**（`c_baseanimating.cpp:883-898`）：

   ```cpp
   void C_BaseAnimating::RemoveBaseAnimatingInterpolatedVars()
   {
       RemoveVar( m_flEncodedController, false );
       RemoveVar( m_flPoseParameter, false );

   #ifdef HL2MP
       // HACK: Don't want to remove interpolation for predictables in hl2dm ...
       if ( !GetPredictable() )
   #endif
       {
           RemoveVar( &m_flCycle, false );
       }
   }
   ```

   `HL2MP` **确实在编译宏里**（`game/client/client_hl2mp.vpc:19`：
   `$PreprocessorDefinitions "$BASE;HL2MP;HL2_CLIENT_DLL;HL2SB;ARGG;LUA_SDK"`）。
   而本地玩家 `GetPredictable()` 为真 ⇒ 走 else ⇒ **`m_flCycle` 不被移除**。

   更致命的是：唯一会「用正确 flags 重登记」的入口是 `UpdateRelevantInterpolatedVars()`
   （`c_baseanimating.cpp:856-868`），它在本树里长这样：

   ```cpp
   if ( !GetPredictable() && !IsClientCreated() && GetModelPtr() && ... )
       AddBaseAnimatingInterpolatedVars();       // 只有这条路会带上 EXCLUDE
   else
       RemoveBaseAnimatingInterpolatedVars();    // 本地玩家走这条 -> 又被 HACK 留下
   ```

   ⇒ 本地玩家**永远**走不到 `AddBaseAnimatingInterpolatedVars()`（因为 `GetPredictable()` 为真），
   于是 `m_flCycle` 从出生起就一直带着「错的 flags」被插值。

3. 结果：每帧 `m_iv_flCycle.Interpolate(curtime)` 把 `m_flCycle` 写回**插值历史**。
   而插值历史的数据来源是网络 sneder——**服务端对这个实体根本不发 `m_flCycle`**（§2.1），
   历史里只有出生时的那一个值（≈0.011）⇒ `m_flCycle` 被死死钉在 0.011。
   `FrameAdvance` 每帧算 `0.011 + 一帧推进量`，下一帧又被插值器写回 0.011 ——
   **屏幕上永远只渲染 0.011 那一帧**，正好是日志里的现象。

**这条链条完整解释了所有现象：**

| 现象 | 解释 |
|---|---|
| 走路/奔跑/蹲下「没有」 | 主序列 cycle 被钉死，只渲染第 0 帧 |
| 换弹动作正常 | 手势/overlay 层是 `C_AnimationLayer`（`c_baseanimatingoverlay.cpp`），不经过 `m_flCycle` |
| 服务端一切正常 | 服务端走 `CBaseAnimating::StudioFrameAdvance()`（`server/baseanimating.cpp:484-519`），没有这层插值 |
| 「修了和没修一样」 | 见 §3 |
| 「又没了」 | 关键：**重启游戏后** `PostDataUpdate` 里 `m_bClientSideAnimation` 相关的分支会重跑一遍列表逻辑，而 HACK 每次都把 `m_flCycle` 留下；上一版我删掉的「每帧 ResetSequenceInfo」只是让情况从「抖动」变回「静止」 |

---

## 3. 我上一版为什么完全无效（自我纠错）

上一版我在 `C_BaseAnimating::PostDataUpdate` 里加了：

```cpp
if ( m_flOldAnimTime != 0.0f )
    m_flAnimTime = m_flOldAnimTime;   // 想给客户端一个自己的动画时钟
```

**这是死代码。** 因为 §2.1 已经证明：客户端自带动画的实体，服务端**根本不发**
`DT_AnimTimeMustBeFirst`（`SendProxy_ClientSideAnimation` 返回 NULL）。客户端收到的
`m_flAnimTime` 一直是 0，`m_flOldAnimTime` 也是 0，那句 `if` 永远不成立。

我当时把现象归因成「listen server 里客户端时钟 = 服务端时钟 ⇒ interval ≤ 0.001 ⇒ FrameAdvance 返回」，
**方向错了**：本树里 `m_flAnimTime` 只被 `FrameAdvance` 自己写，interval 一直有效；
真正把 cycle 钉死的是插值器。教训：**先用「这个值到底有没有被网络发过来」验证前提，再谈时钟。**

同一次我还加过「只在序列 activity 相同的情况下不重选序列」的逻辑（`hl2mp_player_shared.cpp:583-636`）
——那部分是**对的**，但它只解决「服务端每帧重掷序列 → parity 抖动」，和客户端的 cycle 冻结是两件事。

---

## 4. 本次改动（已编译并部署）

全部在 `game/client/c_baseanimating.cpp`：

**A. 构造函数顺序（`C_BaseAnimating::C_BaseAnimating`）**

```cpp
	// HL2SB: the flag must be defined BEFORE the interpolated vars are registered.
	// ... (EXCLUDE_AUTO_INTERPOLATE 决定 m_flCycle 能否被自动插值)
	m_bClientSideAnimation = false;

	AddBaseAnimatingInterpolatedVars();
```

**B. HL2MP HACK 加条件（`RemoveBaseAnimatingInterpolatedVars`）**

```cpp
	if ( !GetPredictable() || m_bClientSideAnimation )
	{
		RemoveVar( &m_flCycle, false );
	}
```

这样：
* 客户端自带动画的玩家 → `m_flCycle` **彻底移出插值表**（不再需要 flags 对不对，直接不插值）；
* HL2MP 原本的语义（服务端驱动 cycle 的本地玩家保留插值，第三视角下半身更顺）**不受影响**。

**C. `PostDataUpdate()` 里显式刷新一次，并删掉无效代码**

```cpp
	if ( m_bClientSideAnimation )
	{
		SetCycle( m_flOldCycle );
		AddToClientSideAnimationList();
		UpdateRelevantInterpolatedVars();   // ← 保证上面的移除一定会发生
	}
```

为什么需要 C：`AddToClientSideAnimationList()` 在「已经在表里」时会提前 return
（`c_baseanimating.cpp:6116`），而 `m_bClientSideAnimation` 是网络值、可能晚于入表到达，
所以这里的刷新不能只依赖它内部那一次调用。

**部署记录**：`waf.bat build --targets=client,server`（37s，client.dll 重编；server.dll 未变），
备份 `D:\srceng\hl2sb\bin\client.dll.bak_20260914_080359`，
新 `client.dll` sha256 `AFA184065EFA042A13DFCE51E837BDCB41ABF09D271F1283CC75CCA68CDB7D36`，
与 `build\game\client\client.dll` 一致。**未提交**（按你的要求）。

---

## 5. 与 GMod / Valve 官方实现的对照

GMod 的玩家动画**不是** GMod 自己写的状态机，而是 **引擎的 `CMultiPlayerAnimState`**（一本树里就有：
`game/shared/Multiplayer/multiplayer_animstate.cpp`）+ 一层 Lua 钩子：

| 层 | GMod 侧 | 本树 HL2SB 现状 |
|---|---|---|
| 状态机 | `CMultiPlayerAnimState : CBasePlayerAnimState` | HL2MP 自己手搓的 `CPlayerAnimState`（`game/shared/hl2mp/hl2mp_player_shared.cpp`），**第三套实现** |
| 活动翻译表 | `acttable_t`（[acttable_t - VDC](https://developer.valvesoftware.com/wiki/Acttable_t)） | 已补 204 个 GMod 活动名（提交 `23d7e7d2`），但**没有 acttable**，靠 Lua HoldType + 活动名回退 |
| 混合序列 | 8/9-way 靠 `move_x/move_y`（`$poseparameter`）＋ 状态机的 `SetupPoseParameters` | 我在 `ComputePoseParam_BodyYaw` 里手写 |
| 钩子 | `GM:CalcMainActivity` / `GM:TranslateActivity` / `GM:UpdateAnimation` / `GM:DoAnimationEvent`（[GM:CalcMainActivity - GMod Wiki](https://wiki.facepunch.com/gmod/GM:CalcMainActivity)，源码 `gamemodes/base/gamemode/animations.lua`） | `CalcMainActivity()` / `TranslateActivity()` 已按同样的处理顺序实现（noclip→driving→vault→jump→swim→duck→move） |
| 手势层 | 4 个槽 `GESTURE_SLOT_ATTACK_AND_RELOAD/JUMP/CUSTOM/VCD`，`AnimRestartGesture` / `AnimSetGestureWeight` / `AnimRestartMainSequence` | 只有 HL2MP 老的 `RestartGesture( activity, ... )` 单层；**没有** `AnimRestartMainSequence` |
| 客户端动画归属 | 引擎用 `FCLIENTANIM_*` + `SetServerIntendedCycle()` 做「客户端算、服务端校正」握手 | 本树只有 CS 玩家实现了 `SetServerIntendedCycle`（`game/client/cstrike/c_cs_player.cpp:694`），HL2MP 的 `GetServerIntendedCycle()` 恒返回 -1 ⇒ 校正从不发生 |

VDC 原话（[m_PlayerAnimState](https://developer.valvesoftware.com/wiki/M_PlayerAnimState)）：
> Missing or incomplete translation tables are the number one cause of unanimated models.

也就是「模型不动」在 Valve 的设计里首先是**表/序列层面**的问题；但**前提是引擎那条
`FCLIENTANIM_SEQUENCE_CYCLE` 链路是完整的**——本树恰恰是这条链路被 HL2MP 的历史 HACK 破坏了（§2.3）。
这是在 GMod/VDC 上都查不到的、本 fork 特有的坑，所以之前的网络检索没能定位它。

---

## 6. 还没解决 / 待你复测的问题

### 6.1 朝向（「模型方向错误 / 第三人称向后人物还是向前的」）——待复测

上一版我按 9-way 的逻辑改过：当 `m_bUseMoveXYBlend` 为真时，把
`m_flCurrentFeetYaw = m_flGoalFeetYaw = GetAnimEyeAngles().y`、`m_flCurrentTorsoYaw = 0`
（`hl2mp_player_shared.cpp:990` 附近），让 `GetRenderAngles()` 直接等于眼睛朝向，
而方向由 `move_x/move_y` 表达。

**注意**：`m_flCycle` 被钉死时，模型只渲染 9-way 的第一个动画（SW 方向），
看起来就极像「朝向错了」。所以**这次修完请先只看腿有没有动、朝向对不对**，
如果朝向仍不对，我再按下面的办法查（有明确判据）：

1. `C_HL2MP_Player::GetRenderAngles()` 返回 `m_PlayerAnimState.GetRenderAngles()`
   （`game/client/hl2mp/c_hl2mp_player.cpp:515-523`），而 `GetRenderAngles()` 是真正参与
   `AngleMatrix()` 的变换（`c_baseanimating.cpp:6189`、`clientleafsystem.cpp:376`）⇒ 它一定是「渲染朝向」；
2. 9-way 模型上，`m_angRender` 应该等于**眼睛朝向**（GMod 行为），方向交给 `move_x/move_y`；
   现在需要确认 `m_bUseMoveXYBlend` 在 `ComputePoseParam_BodyLookYaw()` 里**确实为真**
   （它在 `ComputePoseParam_BodyYaw()` 里赋值，调用顺序是 BodyYaw→BodyPitch→BodyLookYaw，应该没问题）；
3. 若仍不对，退一步只做一件事：把 `m_angRender.y` 直接设成 `GetLocalAngles().y` 对比测试，
   能一次性区分「是 render 角错」还是「是混合参数把方向吃掉了」。

### 6.2 蹲下的「延迟」（已改，待复测）

GMod 的 `HandlePlayerDucking` 判的是 `FL_ANIMDUCKING`（下蹲**开始**就置位），
而 `FL_DUCKING` 要等过渡结束。我已改成 `FL_ANIMDUCKING`（`hl2mp_player_shared.cpp` `HandlePlayerDucking`）。

### 6.3 服务端 `move_x/move_y` 写成了 0.00（新发现，未修）

日志里 `[animdbg/sv] ... move_x val=+0.00 | move_y val=+0.00`，而客户端是 `+0.50`（=中间）。
`+0.00` 是归一化后的 **-1 端**，不是「没写」。⇒ 服务端的
`ComputePoseParam_BodyYaw()` 在速度 0 时算出了边界值。客户端反正会覆盖，暂时看不到视觉问题；
但如果以后要「服务端驱动 + 客户端混合」或者做 server 端 IK/命中盒对齐，这里必须修。

### 6.4 结构性问题（建议按序做，不要一次全上）

1. **把手势层补齐**：`GESTURE_SLOT_*` + `AnimRestartGesture` + `AnimSetGestureWeight`，
   再把 `AnimRestartMainSequence()` 接到跳跃/落地（GMod `HandlePlayerJumping` 里就是靠它）。
2. **`HandlePlayerDriving` 对齐 GMod 官方顺序**：`drive_jeep` → `drive_airboat` → `drive_pd` → `sit_rollercoaster`，
   只有「车内可用武器」时才用 `sit_<holdtype>`。（现在只做了 `sit_<holdtype>`/`sit`/`sit_rollercoaster`。）
3. **考虑把 `game/shared/Multiplayer/multiplayer_animstate.cpp` 编进 hl2sb**
   （目前只在 portal 的 vpc 里），再派生一个 `CHL2MPPlayerAnimState`——
   这才是 GMod 的架构，能一次性拿到 8/9-way、acttable、手势槽、`SetServerIntendedCycle`。
   代价：要动 `client_hl2mp.vpc` / `server_hl2mp.vpc`，并让 Lua HoldType 表继续生效。**建议在这轮验证通过后再评估。**

---

## 7. 验收方法（不用我再加日志）

1. 完全重启游戏（DLL 只在进程启动时加载）。
2. 进图，切第三人称，**站着不动 → 走路 → 奔跑 → 蹲下**，看腿部主序列是否连续：
   走路/奔跑应该是持续的迈步循环，而不是定在某一个姿势。
3. 允许我复跑一次（可选，验证用，不是「靠日志装」）：
   `lua_dofile_cl lua/hl2sb_anim_probe.lua` 后执行 `HL2SB_AnimClientWatch(5)`，
   看客户端 `cycle` 是否随服务端一起变化（修好前恒定 0.011，修好后应连续增长/绕圈）。
4. 朝向：第三人称绕圈走，人体应始终面向镜头方向（GMod 行为），侧移/后退时方向由
   9-way 混合表达（人不应「倒着走」）。

---

## 8. 附：本次调查用到的关键源码坐标

| 事实 | 位置 |
|---|---|
| cycle/animtime 对客户端动画实体**不发** | `game/server/baseentity.cpp:153-162,269`；`game/server/baseanimating.cpp:221-224,257` |
| 客户端每帧推进 cycle 的唯一入口 | `game/client/cdll_client_int.cpp:2448` → `c_baseanimating.cpp:6162,4953-4971,5280-5341` |
| `EXCLUDE_AUTO_INTERPOLATE` 的决定点 | `game/client/c_baseanimating.cpp:871-881`（构造函数调用点 `:688`，赋值点 `:711`） |
| HL2MP HACK 留下 `m_flCycle` | `game/client/c_baseanimating.cpp:883-898` |
| 只有 `Add...` 分支会带正确 flags | `game/client/c_baseanimating.cpp:856-868` |
| 插值器写回变量 | `game/client/c_baseentity.cpp:857-889`；`AddVar` 的 flags 变更处理 `:6384-6433` |
| 服务端 cycle 正常推进 | `game/server/baseanimating.cpp:484-519` |
| `HL2MP` 宏确实定义 | `game/client/client_hl2mp.vpc:19` |
| 手势层（换弹独立于主序列） | `game/client/c_baseanimatingoverlay.cpp`（`C_AnimationLayer`） |

---

# 附录 A（2026-09-14 第二轮）：GMod 实现的直接取证

## A.1 从 GMod 的 DLL 里抽字符串（RTTI 类名）

对 `D:\games\garrysmod\bin\client.dll`（7.7 MB）与 `server.dll`（12 MB）做 ASCII 串抽取，
再和我们的 `build\game\client\client.dll` 对比（脚本产物在 `D:\project\_dsh_gmod_strings\`）：

| RTTI 类名 | GMod client.dll | 我们的 client.dll |
|---|---|---|
| `CHL2MPPlayerAnimState` | ✅ **有** | ❌ 无 |
| `CMultiPlayerAnimState` | ✅ **有** | ❌ 无 |
| `C_TEPlayerAnimEvent` / `CTEPlayerAnimEvent` / `DT_TEPlayerAnimEvent` | ✅ 有 | ❌ 无 |
| `CBasePlayerAnimState` / `IPlayerAnimState` | ✅ 有 | ✅ 有 |
| `PLAYERANIMEVENT_*`（JUMP/SWIM/RELOAD/ATTACK_*/CUSTOM*/FLINCH_*/SNAP_YAW/DIE…） | ✅ 有 | ❌ 无 |
| 姿态参数名 `move_yaw` / `move_x` / `move_y` / `aim_yaw` / `aim_pitch` / `head_yaw` | ✅ 有 | ✅ 有 |
| `cl_showanimstate`、`UpdateClientSideAnimations`、`UseClientSideAnimation` | ✅ 有 | ✅ 有 |

**结论（硬证据）**：GMod 的玩家人体动画用的是 **Valve 的多人状态机 `CHL2MPPlayerAnimState`（派生自
`CMultiPlayerAnimState`，后者派生自 `CBasePlayerAnimState`）**，还带 `TE_PLAYERANIM`
（`C_TEPlayerAnimEvent`）与整套 `PLAYERANIMEVENT_*`。
**我们的构建里这两个类根本不存在**（RTTI 都没有），取而代之的是 HL2MP 当年手搓的
`CPlayerAnimState`（`game/shared/hl2mp/hl2mp_player_shared.cpp`，没有虚函数所以没有 RTTI）。
也就是说：**我们从一开始就用错了实现，而不是「哪里写错了一行」**。

## A.2 GMod 的 Lua 契约（`gamemodes/base/gamemode/animations.lua`，405 行全文已读）

GMod 的 Lua 只做「决定活动 + 设置姿态参数/速率 + 播放手势」，序列与 cycle 全部由引擎状态机负责：

| 钩子 | 引擎何时调 | 干什么 |
|---|---|---|
| `GM:CalcMainActivity(ply, velocity)` | 每帧 | 返回 `(活动, 序列覆盖)`；`ACT_MP_STAND_IDLE` 为默认 |
| `GM:HandlePlayerLanding` | 在 CalcMainActivity **最开头**、无条件调用 | 落地瞬间 `AnimRestartGesture(GESTURE_SLOT_JUMP, ACT_LAND, true)` |
| `GM:HandlePlayerDucking` | 判定顺序**最后**（noclip→driving→vault→jump→swim→duck→move） | **`FL_ANIMDUCKING`**；速度>0.5 → `ACT_MP_CROUCHWALK`，否则 `ACT_MP_CROUCH_IDLE` |
| `GM:HandlePlayerJumping` | 同上 | 起跳首帧/落地/入水各调 **`ply:AnimRestartMainSequence()`** |
| `GM:HandlePlayerDriving` | 同上 | `drive_jeep`→`drive_airboat`→`drive_pd`→`sit_rollercoaster`，再 `sit_<holdtype>` |
| `GM:UpdateAnimation(ply, velocity, maxseqgroundspeed)` | 每帧 | `SetPlaybackRate(min(len/maxseqgroundspeed,2))`；水下 ≥0.5；下坠 0.1；车内 `SetPoseParameter("vehicle_steer"/"vertical_velocity"/"aim_yaw")` |
| `GM:DoAnimationEvent(ply, event, data)` | `PLAYERANIMEVENT_*` 时 | `AnimRestartGesture(GESTURE_SLOT_ATTACK_AND_RELOAD, …)`；`PLAYERANIMEVENT_JUMP` 里再调 `AnimRestartMainSequence()` |

**活动映射是「算术」的**（animations.lua:332-345）：

```lua
local IdleActivity = ACT_HL2MP_IDLE
[ACT_MP_STAND_IDLE] = IdleActivity + 0
[ACT_MP_WALK]       = IdleActivity + 1
[ACT_MP_RUN]        = IdleActivity + 2
[ACT_MP_CROUCH_IDLE]= IdleActivity + 3
[ACT_MP_CROUCHWALK] = IdleActivity + 4
[ACT_MP_ATTACK_STAND_PRIMARYFIRE] = IdleActivity + 5
[ACT_MP_RELOAD_STAND]= IdleActivity + 6
[ACT_MP_JUMP]       = ACT_HL2MP_JUMP_SLAM
[ACT_MP_SWIM]       = IdleActivity + 9
```
（`ACT_MP_*` = 引擎状态机的通用活动，`ACT_HL2MP_*` = HL2MP 活动；我们再翻译成武器 holdtype 版本。）

## A.3 我们缺的 Lua/引擎 API（GMod 插件与 gamemode 直接依赖）

| GMod API | 用途 | 我们的状态 |
|---|---|---|
| `Player:AnimRestartMainSequence()` | 跳跃/落地重启主序列 | ❌ 缺（这也是 GMod 跳跃动画的**唯一**驱动） |
| `Player:AnimRestartGesture(slot, act, autokill)` | 4 个手势槽 | ❌ 缺（只有 HL2MP 老的 `RestartGesture(act,…)` 单槽） |
| `Player:AnimResetGestureSlot(slot)` / `AnimSetGestureWeight(slot, w)` | 清槽 / 权重（说话抓耳等） | ❌ 缺 |
| `GESTURE_SLOT_ATTACK_AND_RELOAD / JUMP / CUSTOM / VCD` | 槽常量 | ❌ 缺 |
| `Player:GetTable()` | GMod 用玩家表存 `m_bJumping` 等动画状态 | ❌ 缺（需要在 Lua 侧搭） |
| `PLAYERANIMEVENT_*` / `TE_PLAYERANIM` | 攻击/换弹/跳跃事件 | ❌ 缺（我们的换弹靠武器表硬走 `RestartGesture`） |

## A.4 本轮改动（修「移动冻僵」的机制层）

之前那版只删了「插值器覆写 `m_flCycle`」这一条，但 **`Interp_Interpolate()` 跑在
`SimulateEntities()` 之前**，所以只删插值还不够稳。本轮把 cycle 的所有权收敛成**唯一一处**：

1. `game/client/c_baseanimating.cpp` `RemoveBaseAnimatingInterpolatedVars()`：
   `if ( !GetPredictable() || m_bClientSideAnimation || IsPlayer() )` → **玩家永不参与 cycle 插值**
   （HL2MP 那个 HACK 是给「服务端驱动 cycle」的时代写的，我们不是那个时代）。
2. `game/client/hl2mp/c_hl2mp_player.{h,cpp}`：
   - 新增覆写 `ComputeClientSideAnimationFlags()` → 返回 **0**，把玩家移出引擎的
     `UpdateClientSideAnimations()` 列表（避免两处同时推进、速度翻倍）；
   - `AddEntity()` 里在 `m_PlayerAnimState.Update()` 之前调 **`FrameAdvance( 0.0f )`** ——
     这是引擎自己的推进函数（含 `m_flAnimTime` 记账与 `GetServerIntendedCycle` 校正），
     而 `AddEntity` 每帧必被调用（与 `FCLIENTANIM_*` 标志、与列表成员资格都无关）。
3. 构造函数里 `m_bClientSideAnimation = false;` 前移（原先是读未初始化成员决定插值方式）。

代价与风险：这是「让引擎的 cycle 由我们在 AddEntity 里推进」，与 GMod 的正统做法
（用 `CMultiPlayerAnimState`）仍有差距；若这次腿部能动，下一步就按 A.5 做正统移植。

## A.5 正统做法的移植清单（建议下一步做，不要一次全上）

1. 把 `game/shared/Multiplayer/multiplayer_animstate.{h,cpp}` 加进
   `client_hl2mp.vpc` / `server_hl2mp.vpc`（目前只在 portal 的 vpc 里），
   本树里它依赖的 `CBasePlayerAnimState`（`game/shared/base_playeranimstate.cpp`）**已经在编**（RTTI 有）。
2. 派生 `CHL2MPPlayerAnimState : CMultiPlayerAnimState`，把
   `CalcMainActivity/TranslateActivity/Update` 接到 Lua 钩子（GMod 的名字与签名见 A.2）。
3. 让 `CHL2MP_Player`（服务端 + 客户端）持有它：`CreateHL2MPPlayerAnimState(this)`，
   并在 `C_HL2MP_Player::AddEntity`（客户端）与 `CHL2MP_Player::PostThink`（服务端）调 `Update()`。
4. 补 Lua API：`AnimRestartMainSequence` / `AnimRestartGesture` / `AnimResetGestureSlot` /
   `AnimSetGestureWeight` / 手势槽常量 / `PLAYERANIMEVENT_*`。
5. 到这一步，CS 那种「客户端算、服务端用 `SetServerIntendedCycle` 校正」的握手才算完整
   （本树只有 CS 玩家实现了它，HL2MP 的 `GetServerIntendedCycle()` 恒返回 -1）。
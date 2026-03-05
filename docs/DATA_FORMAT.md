# Endless Sky 数据格式指南

Endless Sky 使用一种自定义的、基于缩进的文本数据格式（类似于 Python）。本指南详细说明了主要的根节点（Root Nodes）、它们常见的子节点（Sub-nodes）以及源代码中负责处理它们的类。

## 加载机制概述

所有数据加载的入口点是 `UniverseObjects::LoadFile` (位于 `source/UniverseObjects.cpp`)。它读取 `.txt` 文件，并根据每一行的第一个标记（Token）将数据路由到相应的对象处理器。

---

## 主要根节点参考

### 1. `ship` (舰船)
定义一艘舰船的属性、外观和装备槽位。
- **处理类**: `Ship` (`source/Ship.cpp`)
- **常见子节点**:
  - `sprite`: 引用的图像资源路径。
  - `attributes`: 基础属性（质量、转向、推力等）。
  - `outfits`: 初始装备。
  - `engine`, `reverse engine`, `steering engine`: 引擎火焰位置。
  - `gun`, `turret`: 武器硬点位置和属性。
  - `bay`: 载机库（战机或无人机）。
  - `description`: 舰船描述 (显示文本)。

### 2. `outfit` (装备)
定义可以安装在舰船上的任何物品。
- **处理类**: `Outfit` (`source/Outfit.cpp`)
- **常见子节点**:
  - `display name`: 装备的显示名称 (显示文本)。
  - `category`: 装备类别（如 "Guns", "Engines"）(显示文本)。
  - `cost`: 购买价格。
  - `thumbnail`: 商店中显示的缩略图 (显示文本)。
  - `attributes`: 提供的属性增益。
  - `weapon`: 如果是武器，定义弹药、伤害等。
  - `description`: 装备描述 (显示文本)。

### 3. `system` (星系)
定义恒星系统及其在地图上的位置。
- **处理类**: `System` (`source/System.cpp`)
- **常见子节点**:
  - `display name`: 星系的显示名称 (显示文本)。
  - `pos <x> <y>`: 地图坐标。
  - `government`: 所属政府。
  - `habitable`: 宜居度。
  - `object <planet_name>`: 包含的行星或空间站。
  - `link <system_name>`: 超空间连接路径。
  - `asteroid <sprite> <count>`: 存在的环境陨石。

### 4. `planet` (行星/空间站)
定义可以着陆的对象。
- **处理类**: `Planet` (`source/Planet.cpp`)
- **常见子节点**:
  - `attributes`: 环境属性。
  - `description`: 描述文本 (显示文本)。
  - `spaceport`: 港口对话或新闻 (显示文本)。
  - `port`: 港口设施，更精确地定义可用服务。
  - `shipyard`: 出售的舰船列表。
  - `outfitter`: 出售的装备列表。
  - `bribe`: 贿赂所需的条件和成本。

### 5. `mission` (任务)
定义任务的逻辑、对话和奖励。
- **处理类**: `Mission` (`source/Mission.cpp`)
- **常见子节点**:
  - `name`: 任务的名称 (显示文本)。
  - `description`: 任务的描述 (显示文本)。
  - `landing`: 着陆时触发。
  - `deadline`: 截止日期。
  - `source`: 起始星系或行星。
  - `destination`: 目标星系或行星。
  - `on accept`, `on complete`, `on fail`: 不同状态下的动作。
  - `dialog`: 任务对话内容 (显示文本)。

### 6. `government` (政府/派系)
定义派系属性和外交关系。
- **处理类**: `Government` (`source/Government.cpp`)
- **常见子节点**:
  - `display name`: 派系的显示名称 (显示文本)。
  - `color`: 在地图上显示的颜色。
  - `attitude`: 对不同派系的基础态度。
  - `reputation`: 玩家的初始声望。
  - `raid`: 袭击逻辑（触发条件和舰队）。
  - `atrocities`: 定义拥有特定非法物品（如偷来的装备或舰船）的惩罚。

---

## 辅助与特殊根节点

| 关键字 | 说明 | 处理类 / 逻辑位置 |
| :--- | :--- | :--- |
| `conversation` | 复杂的交互式对话树 | `Conversation` |
| `phrase` | 随机生成的文本短语 | `Phrase` |
| `event` | 改变宇宙状态的脚本事件 | `GameEvent` |
| `fleet` | 预定义的舰船编队 | `Fleet` |
| `interface` | UI 布局定义（标签、按钮、进度条） | `Interface` |
| `galaxy` | 包含多个星系的星系团（用于背景渲染） | `Galaxy` |
| `hazard` | 空间环境伤害（如辐射、酸性云） | `Hazard` |
| `substitutions` | 动态文本替换逻辑 | `TextReplacements` |
| `gamerules` / `gamerules preset` | 游戏全局规则设置 | `Gamerules` |
| `color` | 定义全局颜色常量 | `Color` |
| `swizzle` | 定义颜色转换规则 | `Swizzle` |
| `effect` | 定义视觉特效（爆炸、尾迹等） | `Effect` |
| `formation` | 定义舰队阵型模式 | `FormationPattern` |
| `minable` | 定义可开采资源 | `Minable` |
| `outfitter` | 定义装备商店的库存 | `Shop<Outfit>` |
| `shipyard` | 定义船厂的库存 | `Shop<Ship>` |
| `person` | 定义特定的 NPC | `Person` |
| `start` | 定义玩家的初始状态和可选剧本 | `StartConditions` |
| `trade` | 定义商品交易的供需关系 | `Trade` |
| `news` | 定义新闻条目 | `News` |
| `wormhole` | 定义虫洞 | `Wormhole` |
| `message` / `message category` | 定义游戏内消息系统 | `Message` / `Message::Category` |
| `category` | 定义舰船和装备的分类 | `CategoryList` |
| `landing message` | 定义着陆在特定对象上时显示的消息 | `UniverseObjects::LoadFile` (内部逻辑) |
| `star` | 定义恒星的属性（太阳能、太阳风） | `UniverseObjects::LoadFile` (内部逻辑) |
| `rating` | 定义评级文本 | `UniverseObjects::LoadFile` (内部逻辑) |
| `tip` / `help` | 定义工具提示和帮助信息 | `UniverseObjects::LoadFile` (内部逻辑) |
| `disable` | 禁用某个游戏对象（任务、事件等） | `UniverseObjects::LoadFile` (内部逻辑) |
| `test` / `test-data` | 用于自动化测试的数据 | `Test` / `TestData` |


## 特殊标记

- `overwrite`: 放置在任何根节点之前，指示加载器清除该对象之前的定义，以便完全替换。
- `#`: 行注释起始符。
- `add`: 用于在现有对象属性基础上增加值，而不是覆盖（如 `add attributes`）。
- `remove`: 用于从现有对象的列表中移除项目（如 `remove fleet "..."`）。

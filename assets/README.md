# 角色与卡牌资源

characters.json 配置角色资料及立绘。路径相对本目录，禁止绝对路径和 ..；缺失图片使用文字占位。
原图不修改，卡面裁切使用纹理坐标；规则核心不持有 GPU 纹理。

## cards.json

schema_version 为 1，卡表 1–128 张定义，ID 唯一。当前起始牌组要求每位玩家恰好 15 种适用定义，各两张。

| 字段 | 含义 |
|---|---|
| id / name | 稳定 ID / 显示名称 |
| kind | skill 或 character，默认 skill |
| character_id | 作品角色归属，省略为通用卡；非空须引用角色清单 |
| cost | 0–10 的整数灵力费用 |
| effect / amount | 简单技能：damage / heal / shield；1–100 的整数效果值 |
| effects | 多段技能使用 1–4 项数组，不能与顶层 effect / amount 同时填写 |
| attack / health | 角色牌必填：整数攻击 / 最大生命 |
| guard / haste | 角色牌可选布尔值：守护 / 疾奏 |

每人使用所有通用卡和自己的专属卡。增删卡牌后须保持每人 15 种适用定义。
作品资料与游戏设定分开，详见 [角色配置](../docs/characters.md) 和 [素材来源](SOURCES.md)。
新增中文文本后运行 tools/update_glyphs.ps1，再构建同步资源。

多段技能示例：

```json
{
  "id": "example",
  "name": "回响",
  "cost": 2,
  "effects": [
    {"effect": "damage", "amount": 2, "timing": "on_resolve", "recipient": "selected"},
    {"effect": "heal", "amount": 1, "timing": "end_of_turn", "recipient": "controller"}
  ]
}
```

timing 必填：on_resolve / end_of_turn。recipient 默认 selected（已选择的目标），也可为 controller（施放者玩家）或 opponent（对方玩家）。
至少保留一项 selected，同一技能的 selected 段不能同时要求友方和敌方。角色牌暂不支持 effects。
回合末效果在原技能结算成功时登记，结束阶段入栈并开放响应；已经处于结束阶段时登记的效果顺延至下一回合末。

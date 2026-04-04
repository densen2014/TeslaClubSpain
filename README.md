转载自 tesla-open-can-mod 以及 icutcher/tesla-open-can-mod-esp-32

> 更新
请勿更新至 2026.2.9.x 和 2026.8.6 版本，FSD 在 HW4 上无法正常工作。

一款适用于特斯拉车辆的开源通用CAN总线修改工具。虽然最初的目标是实现FSD功能，但最终目标是作为一个完全开源的项目，开放并控制所有可通过CAN总线访问的功能。
有些卖家对类似的解决方案要价高达 500 欧元。硬件成本大约 20 欧元，即使算上人工费，合理的价格也不应该超过 50 欧元。这个项目的存在就是为了让大家不必支付过高的价格。

> 禁止
任何试图绕过完全自动驾驶 (FSD) 的购买或订阅要求的行为都将导致永久禁止使用特斯拉服务。
FSD 是一项高级功能，必须购买或订阅才能使用。


> 警告
本项目仅用于测试和教育目的。向车辆发送错误的 CAN 总线消息可能会导致意外行为、禁用安全关键系统或永久损坏电子元件。CAN 总线控制着从制动和转向到安全气囊的所有系统——格式错误的消息可能会造成严重后果。如果您不完全了解自己在做什么，请勿将此程序安装到您的车辆上。


> 警告
使用本项目需自行承担风险。本项目仅供测试用途，且仅限在私人场所使用。修改车辆上的 CAN 总线消息可能会导致意外或危险行为。
作者对因使用本软件而导致的任何车辆损坏、人身伤害或法律后果概不负责。本项目可能会使您的车辆保修失效，并且可能不符合您所在地区的道路安全法规。
除私人测试外，任何其他用途均须遵守所有适用的当地法律法规。驾驶时，双手始终放在方向盘上，并保持注意力集中。

先决条件
您的车辆必须已激活FSD套件——无论是购买的还是订阅的。此电路板可在CAN总线层启用FSD功能，但车辆仍需获得特斯拉颁发的有效FSD授权。

#### 本测试代码只摘取了HW4部分，并转为Arduino代码

原始来源 icutcher/tesla-open-can-mod-esp-32

#### HW4

| CAN ID | Name | R/W | Mux | Bit | Value | Signal | Description |
|---|---|---|---|---|---|---|---|
| 921 | DAS_status | R+W | — | 13 | 1 | DAS_suppressSpeedWarning | suppress chime |
| 921 | DAS_status | R+W | — | 56–63 | (checksum) | DAS_statusChecksum | update checksum |
| 1016 | UI_driverAssistControl | R | — | 45–47 | (0–7) | UI_accFollowDistanceSetting | read distance |
| 1021 | UI_autopilotControl | R+W | 0 | 38 | (0/1) | UI_fsdStopsControlEnabled | read FSD |
| 1021 | UI_autopilotControl | R+W | 0 | 46 | 1 | | enable FSD |
| 1021 | UI_autopilotControl | R+W | 0 | 59 | 1 | | enable detection |
| 1021 | UI_autopilotControl | R+W | 0 | 60 | 1 | | enable V14 |
| 1021 | UI_autopilotControl | R+W | 1 | 19 | 0 | UI_applyEceR79 | suppress nag |
| 1021 | UI_autopilotControl | R+W | 1 | 47 | 1 | UI_hardCoreSummon | enable summon |
| 1021 | UI_autopilotControl | R+W | 2 | 60–62 | (0–4) | | inject profile |

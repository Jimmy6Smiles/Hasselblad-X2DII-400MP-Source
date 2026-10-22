# Firmware 1.3.18.1 / Pixel Shift 1.2.5-rc3

2026-10-22：维护者安装测试包后反馈“可以拍”。这是单台开发机的拍摄反馈，
不代表所有镜头、输出格式、存储组合或异常恢复均已验收。

## 本次源码同步

- `src/merge/auto_prepare.c` 与 `src/coordinator/auto_prepare.c` 同步接受
  RAW Software 标签 `1.3.16.2` 和 `1.3.18.1`，未知固件仍拒绝。
- 原机型、RAW 尺寸、位深、压缩布局和其他数据校验保持不变。
- 对照 rc3 构建源码快照，公开的 `src/`、`shared/` 中仅上述两个文件需要变更。
  公共运行时、安装器及传输实验不属于本次公开同步范围。

## 构建边界

新固件候选不是只修改 RAW 标签就能部署：其构建使用针对 1.3.18.1 核对过的
原厂调用/链接依赖、配套公共运行时及版本化组件路径。
候选组件根为 `/system/camera-mods/pixel-shift-400mp/1.2.5-rc3`；构建重新生成
版本相关路径与登记信息，并对设备清单使用 LF 行尾。公共 ABI 为 1，运行时范围
为 `>=1.2.0,<2.0.0`。不要直接将旧包改名或放宽安装器固件检查。

此库仍是源码发布，不包含原厂二进制依赖、公共宿主、完整候选构建环境或安装器，
不宣称仅克隆此库即可逐字节重建整包。部分成像工具沿用已验收二进制，历史
`1.2.2/binary-baseline.json` 不改写为新固件基线。

用于核对本次测试版本的身份（不是新发布的 Release）：

- 包：`pixel-shift-400mp-1.2.5-rc3-FW-1.3.18.1-Test.x2d2ext`
- 外层 SHA256：`c1171fb65936135a0f4179aca1886c0f874c0fc42d4fffe262b136ba4fb239c9`
- 构建描述 SHA256：`5c014d7fab65982116de0abf8926dbdddda43577d187cb01863d4a6756b0aede`
- 功能载荷标识：`952145379823d9860f7f3eae51a4c7fc6f3fc9508b52ff00d3d2e28b8881fb56`

English: this update exports the two RAW-reader firmware-allowlist changes from
the rc3 source snapshot. The maintainer reported successful capture on 1.3.18.1;
this is not comprehensive device acceptance or a reproducible full-package build.
Firmware-specific linking, shared-runtime integration and versioned package paths
remain separate requirements. No installer or experimental transfer components
are published here.

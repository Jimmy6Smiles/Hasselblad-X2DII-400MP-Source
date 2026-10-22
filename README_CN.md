# 哈苏 X2D II 四亿像素位移源码

[English](README.md) | 简体中文

维护者：**Jimmy6Smiles**。适用于 **X2D II 100C**，非哈苏官方项目。
原适配固件为 **1.3.16.2**；现同步 **1.3.18.1 / 1.2.5-rc3** 适配，维护者于
2026-10-22 确认可以拍摄。具体源码、构建及验收边界见[新固件适配说明](docs/FIRMWARE_1_3_18_1.md)。

公开六帧采集、四亿重建、RAW/JPEG 封装、原厂显影适配、回放放大、菜单、快门接管和异常恢复源码。支持 RAW、JPEG、RAW+JPEG，不支持 HEIF。

`firmware/pixel-shift/1.2.2` 是历史目录名，包含后来兼容性、恢复及 1.2.4-rc4 登记式接入修改，并不表示目录内所有文件都属于原始 1.2.2。

本库是源码快照，**不是完整安装器，也不宣称可以独立重建 rc4 全部二进制**。不包含安装器源码、公共运行时宿主、原厂库/配置、固件、照片、日志或密钥。构建入口与缺失依赖见 [BUILD.md](docs/BUILD.md)。历史安装包仍在[原发布库](https://github.com/Jimmy6Smiles/Hasselblad-X2DII-Extensions/releases)，旧独占包不能与新登记式安装机制混用。

2026-10-21 用户确认新版登记式安装器/MOD 自行安装实测目前正常；这与本次源码导出是否能完整复现二进制是不同的验收。

后续遵循[公共接口开发规则](docs/DEVELOPMENT_RULES_CN.md)：公共运行时和启动配置仅由安装器管理，MOD 只登记自己的组件，不覆盖其他模块。

开发来源及公开版本时间见 [PROVENANCE.md](PROVENANCE.md)。保留上游贡献和许可，不倒填提交时间，不把公开记录解释为对所有其他项目的首创证明。源码可见不等于对所有文件另行授予开源许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

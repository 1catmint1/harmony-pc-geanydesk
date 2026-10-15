# GeanyDesk — Geany 社区 Qt 保存完整性适配层

基线为 [ohos_geany c0db47b78d0e38d320cdd26782995d7945b81e63](https://gitcode.com/OpenHarmonyPCDeveloper/ohos_geany/tree/c0db47b78d0e38d320cdd26782995d7945b81e63)。社区既有 Qt Widgets 编辑器、界面和功能实现归属原作者。本仓库提供新增 TextSaveGuard、生产分支修复、8 项应用内验收、26 项主机核心控制及可复现集成补丁；采用 GPL-2.0-or-later，不宣称原 GTK Geany 二进制或插件 ABI 已移植。

## 修复

原鸿蒙保存分支直接截断目标并统一写 UTF8，忽略所选编码和行尾。本层严格编码/解码，保留 UTF8 BOM、UTF16LE/BE BOM 与统一 CRLF/LF；非法源字节、不可表示字符、未知原始摘要、外部覆盖、删除/重建、符号链接和超过 64 MiB 的文件明确拒绝。摘要来自实际读取字节；QSaveFile 禁止直接写回退，在 commit 前再次核对，提交成功后才清理编辑器的修改状态和执行文本规范化。

真机运行发现替换文本期间括号高亮可能用旧光标位置索引缩短后的 QString；补齐两侧索引上下界。R2 日志把退出定位在 setPlainText 内，R3 的边界修复使同一编辑步骤和后续控制全部完成。未取得底层崩溃栈，不扩大为所有 Qt 退出问题的结论。

只使用应用自有目录，不请求剪贴板、桌面、文档、下载目录或持久文件访问权限。乐观摘要检查与 commit 之间仍有竞争窗口，不宣称跨进程强锁、混合行尾逐字节不变或完整 ACL/xattr 保留。

## 验证

Windows QtCore 5.15.19+kde 主机核心 26/26，跳过 0。目标 Qt 5.15.12 的签名 HAP，在当前 USB 鸿蒙 PC 同一次启动、boot/PID/UID 绑定下，真实生产 saveDocument 完成 8/8：三种 BOM/CRLF 未改保存、Unicode UTF16LE 编辑保存、外部冲突保留编辑器、非法 UTF8 拒绝、Latin1 不可表示拒绝、成功提交后才规范化。实际 GUI 保留外部冲突的编辑器副本，显示 UTF16LE-BOM/CRLF 中文和 emoji，点击 Save 后出现 Saved 回执。

R1/R2 应用退出的失败记录保留，不计作完整通过；更早 raw ARM64 shell 尝试仅返回 126，没有执行控制且已清理。本次没有重跑上游原有 41 项自测，不冒充这些功能的新实现或验证。

R3 HAP SHA256 `ac788a69cf6d4a9934902cef13e03c2efa91019e1844778f408e839c204c1ac6` 只用于证据绑定，设备 debug 包不公开。签名 HAP 内 libentry 与当前源码编译所得的可加载部分一致。社区提交待审与 AppGallery 正式上架独立记账；本仓库不包含公开发布二进制，也不声称应用市场已批准。

## 复现与许可

取得固定上游后，在根目录运行 `git apply --unidiff-zero integration/geany.patch`，并将 modules/TextSaveGuard.h 放到 harmony_pc/entry/src/main/cpp。Windows DevEco 的 ARM64/API22 独立投影使用自己包名和签名，提供匹配目标 Qt 5.15.12；指定 QT_PREFIX 和 Windows 可执行 QT_HOST_MOC，避免调用 Mac 工具。目标启动 Want 参数 query=`--geany-save-guard-test` 才运行应用内 8 项控制，并在自有目录生成独立夹具和报告。仅在测试安装中启用该参数。

主机有 Qt5 Core 开发环境时，使用 `cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt前缀>`、`cmake --build build --parallel 2`，然后运行 `geany-save-controls <已有绝对临时目录>`。它会在该目录建立自己的 QTemporaryDir 并清理，只用于控制数据。Windows 本次验证使用官方 MSYS2 UCRT Qt/GCC，包与摘要见 evidence。

GPL 原文在 LICENSE。上游 Qt 编辑器及其依赖适用原许可；1.0.1 完整社区前端在 harmony/；Qt 对应源码及端口补丁在 third_party/。本仓库发布修复源码与补丁，不复制 Qt/平台插件/运行时二进制、签名配置、私钥、证书、Profile 或设备标识。1.0.1 的正式发行签名、对应源码和许可材料已另行准备；平台提审及最终批准另记。


## 1.0.1 PC 发行版本

发行工程为 harmony/，保留社区界面及原作者归属，关闭自动验收启动入口，新增隐私与许可页。PC ARM64、SDK 6.0.2.130/API22、版本1.0.1/1000001，不申请权限。R4 对调试签名的发行模式包做了编辑、保存和许可页 GUI 验证；输入通道把 emoji 变为问号，本轮不声称完成 emoji 输入。R3 的8/8生产保存与26/26主机控制是历史结果，并非本轮重跑。正式APP/HAP已官方验签，23项代码和资源载荷与R4一致；正式发行签名本身未直接安装到USB。

Qt 源码、端口来源和通知见 QT-SOURCE.md、THIRD_PARTY_NOTICES.txt、third_party/SOURCE-MANIFEST.json。Qt库和五个插件与官方Alpha_v9 SDK逐字节匹配；未自行重建该SDK，不声称位级可重现。libc++来自官方API22 SDK，其告知包含于LLVM-NOTICE.txt。无需用任何私有签名材料复现源码或修改动态库；安装到自己的设备时使用自己的有效签名。

在 harmony/entry/build-profile.json5 的 buildOption.externalNativeOptions.arguments 中设置 -DQT_PREFIX=<匹配目标Qt安装目录> -DQT_HOST_MOC=<本机可执行moc>。使用DevEco Studio、官方SDK22及自己的私有签名配置；依赖安装 ohpm install --all --no-link --max_concurrent 1，构建 hvigorw assembleApp --mode project -p product=default -p buildMode=release --no-daemon --no-parallel --max-workers 2。原生编译池2、链接池1。harmony/ 不含密钥、Profile、运行库二进制或设备包。

# Qt 对应源码与重建说明

GeanyDesk 1.0.1 动态链接Qt5.15.12，官方端口仓库 https://atomgit.com/openharmony-sig/qt ，Alpha_v9固定提交044e71e63b15cf03067f61022d6e4b27e9cb8c21。对应Qtbase官方标签v5.15.12-lts-lgpl固定提交e4391422574aa9aa89fece74f16c07c609cbbae2，来源 https://github.com/qt/qtbase 。源码镜像随本仓库提供，摘要见third_party/SOURCE-MANIFEST.json。

解开third_party/qtbase-5.15.12-source.tar.gz、qt-openharmony-alpha-v9-source.tar.gz。Git archive遵循上游export-ignore，Qtbase根.gitignore被省略；将third_party/qtbase-patch-prerequisite.gitignore复制为Qtbase根.gitignore，再运行git apply --check和git apply应用qtbase-alpha-v9.patch。本次对固定源码的补丁检查已通过。对应原库和五个插件与官方Alpha_v9的macos-14-output-qt5.15.12-arm64-v8a-18-release.zip完全相同，v8则不匹配。官方完整SDK可从该端口仓库Alpha_v9发布页下载。

端口源归档包含qtohextras、qtharmonystyle、工具脚本及配置。构建应在匹配的SDK18/ARM64开发环境按固定提交script/configure.json的5.15.12设置进行：先构建带补丁Qtbase，再构建qtohextras及qtharmonystyle，并提供匹配Qt平台和图像插件。逐条审阅脚本并把编译并行度限制为2；原脚本含重置与清理，不能对已有工作目录盲目运行。GeanyDesk应用自身使用API22构建。Qt来源SDK未由本项目重新编译，构建说明不是位级重现声明。

保留原GNU GPL2/GPL3/LGPL3全文及第三方通知；具体适用许可以各源文件与模块为准。新增TextSaveGuard为GPL-2.0-or-later；不将整个上游改称MIT。不额外禁止为调试修改所需的逆向工程。可修改并重建源码、替换动态库；用户部署需自己有效的签名，不提供开发者私钥。Qt的编译器、平台及可选依赖通知作为完整材料提供，并不表示所有可选功能均已链接或验收。

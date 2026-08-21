# fw_patches — 已废弃

> 2026-08-21 撤销。**这个目录不再存放代码副本。**

本目录一度放了 `sal_pan_interface.c` / `panu_service.c` 的整文件快照，理由是
「`frameworks/connectivity/bluetooth` 不在任何仓的 git 管理内」。

**那个判断是错的。** 依据是 `frameworks/connectivity/.gitignore` 里的 `/*/`——它确实
忽略了所有一级子目录，但 `frameworks/connectivity/bluetooth` **本身就是一个独立的
repo project**（`openvela.xml:152`，name=`frameworks_bluetooth`，remote
`https://github.com/open-vela/frameworks_bluetooth`），有自己的 `.git`，父仓忽略它
正是因为它由 repo 单独 checkout。

正确做法是直接在那个仓里提交，已完成：

```
frameworks/connectivity/bluetooth  branch bletest
  ec9ad5c5 fix(pan): size the BNEP TX pool for real and auto-connect from the bond list
```

按大赛规则（团队仓 README 第五节第 3 条），公共仓改动**不放在团队仓**，而是 fork
对应公共仓、以 PR 提交到 `dev-ai-contest-2026` 分支。交付路径见
`../22_pan_engineering_guide.md` 第 6 节。

保留本文件仅为说明这段返工，避免后来人重复同样的误判。`app_patches/` 不同——那是
真正在本工作区之外的独立 Android 工程，快照是唯一形式。

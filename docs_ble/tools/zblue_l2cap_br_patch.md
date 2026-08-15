# zblue l2cap_br.c 本地补丁（2026-08-15）

文件: external/zblue/zblue/subsys/bluetooth/host/classic/l2cap_br.c
（该文件被 zblue 仓库 .gitignore 忽略，无法 git 提交；此文档记录改动，重装/同步时需重新应用）

## 修改：l2cap_br_conf() 总是发送 MTU option

原因: Android BNEP (NAP) 拒绝空配置 (CONF_RSP result=0x0001 UNACCEPTABLE_PARAMS)，
观察于 Redmi Note 12 Turbo 蓝牙网络共享。

原代码:
```c
	if (BR_CHAN(chan)->rx.mtu != L2CAP_BR_DEFAULT_MTU) {
		l2cap_br_conf_add_mtu(buf, BR_CHAN(chan)->rx.mtu);
	}
```

改为:
```c
	{
		uint16_t mtu = BR_CHAN(chan)->rx.mtu;
		if (mtu == 0) {
			mtu = L2CAP_BR_DEFAULT_MTU;
		}
		l2cap_br_conf_add_mtu(buf, mtu);
	}
```

验证: ninja 编译通过（SRAM 90.38%），l2cap_br.c.o 已重新生成。
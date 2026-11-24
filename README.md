# 汽车电子泵控制 ECU 软件模块实践

[![build-and-test](https://github.com/zzdxkk/pump-ecu-software/actions/workflows/ci.yml/badge.svg)](https://github.com/zzdxkk/pump-ecu-software/actions/workflows/ci.yml)

三个可以独立编译、独立测试的 C 模块，覆盖汽车嵌入式软件开发中最常被问到的一类工作：
**模块设计 → 单元测试 → 集成测试 → 边界条件验证**。

不是演示工程，每个模块都有对应的故障场景和边界用例；`./run_all.sh` 一条命令跑完 316 项断言，详细结果见 [docs/test-report.md](docs/test-report.md)。

## 模块

| 模块 | 内容 | 断言数 |
|---|---|---|
| `01-can-codec` | CAN 报文信号编解码：Intel / Motorola 两种位序、缩放与偏移、范围保护、有符号数、信号数据库 | 184 |
| `02-uds-diagnostics` | ISO-TP 分帧重组 + UDS 服务处理（0x10 / 0x11 / 0x22 / 0x27 / 0x2E / 0x3E），含 NRC 返回码 | 84 |
| `03-pump-fsm` | 泵电机控制状态机：预充、运行、降额、故障锁存与恢复门控；含随机故障注入与 CAN 集成测试 | 48 |

模块间有真实的依赖：`03` 的集成测试直接调用 `01` 的编解码，模拟“收到 CAN 指令 → 状态机动作 → 回发状态帧”这条链路。

## 快速开始

```bash
./run_all.sh          # 三个模块全部编译 + 测试
cd 03-pump-fsm && make test   # 单独跑某一个
```

依赖仅为 C99 编译器与 make，无第三方库。已验证：Apple clang 21 / GCC，`-Wall -Wextra -Werror` 零告警。

## 为什么是这三个模块

泵类 ECU 的软件工作，拆开看就是三件事：

1. **和整车说话**——CAN 信号怎么打包、字节序怎么定、超范围的值怎么处理；
2. **和诊断仪说话**——UDS 会话、安全访问、DID 读写、长报文怎么分帧；
3. **和执行器说话**——什么条件下允许转、过流过热怎么保护、故障之后怎么恢复。

这三块各自独立，但都要求同一件事：**每个分支都有测试**，尤其是不正常的那些分支。

## 测试覆盖思路

不是"跑通主流程"就算完，重点在异常路径：

- 边界值：`0`、上限、上限+1、负值、有符号数的负半轴；
- 非法输入：长度不足、空指针、超出信号位宽、传感器开路（温度 250 ℃）；
- 时序：去抖窗口前 40 ms 不跳、第 60 ms 才跳；预充必须满 300 ms；
- 状态机：故障锁存后不点 `clear` 不恢复；`clear` 但输入仍异常时拒绝恢复；
- 协议：ISO-TP 序号错乱要中止传输、单帧长度超过 DLC 要拒绝；
- 随机注入：4000 步随机传感器序列，断言"故障态下 PWM 恒为 0、PWM 不超过指令、错误码不越界"。

## 设计取舍

- **不用动态内存**：所有缓冲区定长，`isotp_rx_t` / `pump_ctx_t` 由调用方分配，便于在 MCU 上静态布局。
- **错误用返回值不用全局变量**：`can_status_t` / `isotp_status_t`，方便在单元测试里直接断言。
- **状态机与 IO 分离**：`pump_step()` 只吃传感器数值，输出放在 `pump_ctx_t` 里，测试不需要模拟硬件。
- **保护优先级高于控制**：先判故障再走状态迁移，故障态下 PWM 与功率级同时切断。

## 边界与未做的事

- 没有真实硬件在环（HIL），时序是软件仿真步进，不能替代台架测试；
- CAN 只实现了应用层编解码，没有实现控制器驱动与错误帧处理；
- UDS 只覆盖常用服务和 NRC，未实现 0x2F、0x31、0x19 等；
- 未做 MISRA-C 静态检查与 ASIL 等级论证，这份代码是"可测试的模块范例"，不是可认证的安全件。

## 目录

```
01-can-codec/{include,src,tests}   can_codec.h / can_codec.c / test_can_codec.c
02-uds-diagnostics/{include,src,tests}
03-pump-fsm/{include,src,tests}
docs/test-report.md                测试报告（分组、风险等级、残余风险）
.github/workflows/ci.yml           gcc + clang 双编译器 CI
run_all.sh
```

# MHC 통신 초기 로딩 안정화 리팩토링 보고서

## 1. 개요

본 보고서는 `MHC_InterfaceCyclic.c`의 MHC 통신 초기화 및 주기 처리 로직을 수정한 내용을 정리한 것이다.

기존 코드에서는 MHC 통신 초기화 과정에서 간헐적으로 로딩이 완료되지 않는 현상이 발생할 수 있었다. 원인은 단일 함수 내부에서 송신, 수신, startup 처리, 설정값 변경 처리가 flag 조건에 따라 분기되면서 통신 상태가 명확하게 관리되지 않았기 때문이다.

이에 따라 MHC 통신 초기화 과정을 상태머신 기반으로 리팩토링하고, 수신 처리를 매 사이클 보장하도록 변경하였다. 또한 startup 응답 확인, timeout, retry, 통신 복구 절차를 명시적으로 추가하여 간헐적인 초기 로딩 실패 가능성을 줄였다.

---

## 2. 문제 현상

MHC 통신 활성화 후 외부 온도 컨트롤러 또는 핫러너 관련 데이터가 간헐적으로 로딩되지 않는 현상이 있었다.

주요 증상은 다음과 같다.

| 구분    | 내용                                                  |
| ----- | --------------------------------------------------- |
| 현상    | MHC 통신 초기 로딩이 간헐적으로 완료되지 않음                         |
| 발생 시점 | 전원 투입 후 또는 Zone 설정 변경 후                             |
| 영향    | PV, 출력률, 전류값 등 MHC 관련 데이터 갱신 지연 또는 미표시              |
| 추정 원인 | startup 응답 수신 타이밍 누락, startup 처리 중 RX 미수행, 복구 루틴 부재 |

---

## 3. 기존 구조의 문제점

### 3.1 수신 처리 실행 조건이 제한적이었음

기존 `_CYCLIC` 구조에서는 다음 조건일 때만 송신/수신 루틴이 실행되었다.

```c
if(startupfuntion_flag == 0 && setdata_change_flag == 0){
    hotrunertxthread();
    hotrunerrxthread();
}
```

즉, `startupfuntion_flag`가 켜져 있거나 `setdata_change_flag`가 켜져 있는 동안에는 `hotrunerrxthread()`가 매 사이클 보장되지 않았다.

이 구조에서는 startup 설정 처리나 설정값 변경 처리 중 들어오는 수신 데이터를 놓칠 수 있고, 특히 초기 handshake 응답이 짧은 타이밍으로 들어올 경우 간헐적인 로딩 실패로 이어질 수 있다.

---

### 3.2 startup 송신 로직이 TX thread 내부에 섞여 있었음

기존에는 `hotrunertxthread()`의 case 0에서 `hot_startupflag`가 꺼져 있을 경우 startup code를 직접 송신하였다.

이 방식은 runtime polling 로직과 startup initialization 로직이 하나의 TX state 안에 섞이는 구조였다. 따라서 현재 코드가 startup 대기 상태인지, startup 응답 대기 상태인지, 정상 운전 상태인지 명확하게 분리되지 않았다.

---

### 3.3 startup 응답 처리 흐름이 분산되어 있었음

기존 RX 로직에서는 startup 응답을 받으면 `startupfuntion_tamp_flag`를 세우고, 이후 `FRMR_FINISH` 단계에서 다시 `startupfuntion_flag`를 세우는 방식이었다.

즉, startup 응답 수신과 startup 설정 진입이 한 지점에서 처리되지 않고, RX 내부 flag와 Receiver step 진행 상태에 의존하였다.

이 구조는 응답이 정상적으로 들어왔더라도 Receiver step 처리 타이밍에 따라 startup 진입이 지연되거나 꼬일 가능성이 있었다.

---

### 3.4 timeout / retry / recovery 기준이 없었음

기존 코드에는 startup 요청 후 응답이 없을 때 다음을 판단하는 기준이 명확하지 않았다.

| 항목                       | 기존 상태 |
| ------------------------ | ----- |
| startup 응답 timeout       | 없음    |
| startup 재시도 횟수           | 없음    |
| 일정 횟수 실패 후 재초기화          | 없음    |
| runtime 통신 끊김 감지         | 없음    |
| buffer 및 receiver 상태 초기화 | 제한적   |

따라서 통신 초기화 과정에서 한 번 꼬이면 자동 복구 없이 그대로 로딩 실패 상태에 머물 가능성이 있었다.

---

## 4. 리팩토링 주요 내용

## 4.1 MHC 통신 상태머신 추가

MHC 통신 상태를 명확하게 관리하기 위해 `MHC_STATE_TYP` enum을 추가하였다.

추가된 상태는 다음과 같다.

| 상태                           | 역할                        |
| ---------------------------- | ------------------------- |
| `MHC_STATE_POWERON_WAIT`     | 전원 투입 후 장치 응답 준비 대기       |
| `MHC_STATE_SEND_STARTUP`     | startup code 송신           |
| `MHC_STATE_WAIT_STARTUP_ACK` | startup 응답 대기             |
| `MHC_STATE_STARTUP_SETTING`  | 기존 startup 설정 sequence 수행 |
| `MHC_STATE_RUN`              | 정상 polling 및 설정값 변경 처리    |
| `MHC_STATE_COMM_RECOVERY`    | 통신 이상 시 복구 처리             |

이를 통해 기존 flag 기반 흐름을 명시적인 상태 기반 흐름으로 변경하였다.

---

## 4.2 `_CYCLIC` 구조 단순화

기존에는 flag 조건에 따라 TX/RX/startup/setdata 처리가 분기되었다.

변경 후에는 `_CYCLIC` 흐름을 다음 순서로 정리하였다.

```c
mhc_rx_ok_this_cycle = 0;
hotrunerrxthread();

MHC_StateManager();

if(REC_DATA.MHC_CTRL.EnableCommunication){
    MHC_Alarm_Process();
}
```

핵심은 `hotrunerrxthread()`가 매 사이클 가장 먼저 실행된다는 점이다.

이를 통해 startup 처리 중이든, 설정값 변경 처리 중이든, runtime polling 중이든 RX 처리가 계속 수행된다.

---

## 4.3 startup handshake / timeout / retry 추가

startup code 송신 후 바로 정상 운전으로 넘어가지 않고, startup 응답을 명시적으로 기다리도록 수정하였다.

처리 흐름은 다음과 같다.

```text
POWERON_WAIT
 → SEND_STARTUP
 → WAIT_STARTUP_ACK
 → STARTUP_SETTING
 → RUN
```

startup 응답이 들어오면 `mhc_startup_ack_received`를 세우고 startup 설정 단계로 진입한다.

응답이 들어오지 않으면 `MHC_STARTUP_ACK_TIMEOUT` 기준으로 timeout을 판단하고, `MHC_STARTUP_RETRY_MAX` 범위 안에서 startup 송신을 재시도한다.

이를 통해 장치가 늦게 응답하거나 초기 통신 타이밍이 어긋나는 경우에도 재시도 가능하도록 변경하였다.

---

## 4.4 runtime watchdog 복구 추가

정상 운전 상태에서도 수신 성공 여부를 `mhc_rx_ok_this_cycle`로 감시하도록 변경하였다.

정상 패킷을 수신하면 통신 끊김 카운터를 초기화한다.

```c
if(mhc_rx_ok_this_cycle)
{
    mhc_comm_lost_count = 0;
}
```

반대로 일정 횟수 이상 응답이 없으면 통신 이상으로 판단하고 복구 상태로 전환한다.

```c
if(mhc_comm_lost_count >= MHC_COMM_LOST_LIMIT)
{
    hot_startupflag = 0;
    MHC_ResetRxBuffer();
    mhc_state = MHC_STATE_COMM_RECOVERY;
}
```

이를 통해 초기 로딩뿐 아니라 운전 중 통신 응답이 끊겼을 때도 자동으로 재초기화할 수 있게 되었다.

---

## 4.5 startup code 송신 함수 분리

기존에는 startup code 생성 및 송신 로직이 `hotrunertxthread()` 내부 case 0에 포함되어 있었다.

변경 후에는 이를 `MHC_SendStartupCode()` 함수로 분리하였다.

분리 후 역할은 다음과 같다.

| 함수                      | 역할                               |
| ----------------------- | -------------------------------- |
| `MHC_StateManager()`    | 언제 startup을 보낼지 결정               |
| `MHC_SendStartupCode()` | startup packet 구성 및 송신           |
| `hotrunertxthread()`    | 정상 운전 중 PV/MK/Current polling 처리 |

이 변경으로 startup 초기화 로직과 runtime polling 로직의 책임이 분리되었다.

---

## 4.6 RX buffer / Receiver 상태 초기화 함수 추가

통신 복구 시 RX buffer와 Receiver 상태를 일괄 초기화하기 위해 `MHC_ResetRxBuffer()`를 추가하였다.

초기화 대상은 다음과 같다.

```c
memset(Rx_buffer,0,sizeof(Rx_buffer));
memset(Rx_tmp_buffer,0,sizeof(Rx_tmp_buffer));
rxthbuf_count = 0;
Receiver.step = FRMR_READ;
Receiver.buffer = 0;
Receiver.buffer_length = 0;
Receiver.status = 0;
```

이를 통해 LRC 오류, timeout, 통신 복구 상황에서 이전 수신 잔여 데이터가 다음 packet 처리에 영향을 주지 않도록 하였다.

---

## 4.7 RX packet 처리 개선

기존에는 startup 응답을 수신하면 `startupfuntion_tamp_flag`를 세웠고, 이후 `FRMR_FINISH`에서 `startupfuntion_flag`를 세우는 구조였다.

변경 후에는 startup 응답 수신 시 `mhc_startup_ack_received`를 직접 세운다.

```c
if(data[2]==0x12 && data[4]==0x03){
    mhc_startup_ack_received = 1;
    startupfuntion_tamp_flag = 0;
    rxthbuf_count = 0;
}
```

또한 정상 runtime packet을 분석한 경우 `mhc_rx_ok_this_cycle`을 세워 watchdog 판단에 사용하도록 했다.

```c
rx_data_analyze(Rx_buffer,rxthbuf_count);
mhc_rx_ok_this_cycle = 1;
```

추가로 RX buffer 검사 조건도 안전하게 정리하였다.

기존에는 `Rx_buffer[rxthbuf_count -1]`를 먼저 참조한 뒤 `rxthbuf_count > 1`을 검사했다. 변경 후에는 `rxthbuf_count > 1`을 먼저 확인한 뒤 buffer index를 참조하도록 순서를 변경하였다.

```c
if((rxthbuf_count>1)&&(Rx_buffer[rxthbuf_count -1]==0x03))
```

이는 `rxthbuf_count`가 충분하지 않은 상태에서 잘못된 index 접근이 발생할 가능성을 줄이는 안정성 개선이다.

---

## 4.8 `hotrunertxthread()`의 startup 책임 제거

기존 `hotrunertxthread()` case 0은 `hot_startupflag`가 꺼져 있으면 startup code를 직접 송신했다.

변경 후 case 0은 `hot_startupflag`가 켜진 경우에만 polling step으로 진입하고, startup 요청은 `MHC_StateManager()`에서만 처리한다.

```c
if(hot_startupflag == 1)
{
    hotrunertxthread_step = 1;
    hotrunertxthread_delay=0;
}
else
{
    /* Startup request is handled only by MHC_StateManager. */
    hotrunertxthread_delay = 0;
}
```

이로써 startup 송신 루틴이 중복되거나 runtime polling 흐름과 섞이는 문제를 제거하였다.

---

## 5. 변경 전후 흐름 비교

### 변경 전

```text
_CYCLIC
 ├─ startupfuntion_flag == 0 && setdata_change_flag == 0
 │   ├─ hotrunertxthread()
 │   └─ hotrunerrxthread()
 │
 ├─ startupfuntion_flag == 1
 │   └─ startupfuntion()
 │
 └─ setdata_change_flag == 1 && startupfuntion_flag == 0
     └─ setdata_change()
```

문제점은 startup 또는 setdata 처리 중 RX가 보장되지 않는다는 점이다.

---

### 변경 후

```text
_CYCLIC
 ├─ hotrunerrxthread()
 ├─ MHC_StateManager()
 └─ MHC_Alarm_Process()
```

상태머신 내부 흐름은 다음과 같다.

```text
POWERON_WAIT
 → SEND_STARTUP
 → WAIT_STARTUP_ACK
 → STARTUP_SETTING
 → RUN
 → COMM_RECOVERY
```

변경 후에는 RX 처리와 상태 관리가 분리되고, startup 실패 및 runtime 통신 이상에 대한 복구 흐름이 명확해졌다.

---

## 6. 기대 효과

이번 수정으로 기대되는 효과는 다음과 같다.

| 항목        | 개선 내용                                      |
| --------- | ------------------------------------------ |
| 초기 로딩 안정성 | startup 응답을 명시적으로 대기하고 timeout/retry 처리    |
| 수신 누락 방지  | RX thread를 매 사이클 우선 실행                     |
| 통신 복구성    | 응답 없음 누적 시 COMM_RECOVERY로 자동 재진입           |
| 코드 가독성    | startup, run, recovery 상태가 명확하게 분리         |
| 유지보수성     | startup 송신, buffer reset, 상태 관리를 함수 단위로 분리 |
| 안전성       | RX buffer index 검사 순서 개선                   |

---

## 7. 결론

본 수정은 MHC 통신 초기 로딩 실패를 해결하기 위해 기존 flag 중심 처리 구조를 상태머신 기반 통신 관리 구조로 리팩토링한 것이다.

핵심 변경점은 다음과 같다.

1. RX 처리를 매 사이클 보장하였다.
2. startup 송신과 startup 응답 대기를 명확히 분리하였다.
3. startup timeout 및 retry를 추가하였다.
4. runtime 통신 watchdog과 recovery 상태를 추가하였다.
5. startup 송신 로직을 `hotrunertxthread()`에서 분리하였다.
6. RX buffer와 Receiver 상태를 복구 시점에 명확히 초기화하도록 하였다.

따라서 기존처럼 startup 응답 타이밍이 어긋나거나 통신 상태가 중간에 꼬였을 때 그대로 로딩 실패 상태에 머무는 문제를 줄이고, 일정 조건에서 자동으로 재시도 및 복구할 수 있는 구조로 개선하였다.

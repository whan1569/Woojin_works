# Woojin_works

산업용 장비 제어 소프트웨어를 개발·유지보수하며 남긴 **공개용 기술 기록 아카이브**입니다.

이 저장소의 목적은 완성된 제품 소스 전체를 공개하는 것이 아니라, 실제 장비에서 발생한 문제를  
**소프트웨어 상태 → I/O → 통신/전장 → 실제 장치 동작**의 흐름으로 추적하고 해결한 과정을 보여주는 것입니다.

> **Public Portfolio Version**
>
> - 고객사명과 식별 가능한 현장 정보는 `고객사 A/B/C` 형태로 익명화했습니다.
> - 회사/고객의 비공개 소스, 상세 설비 식별정보, 보안 설정값은 포함하지 않습니다.
> - 과거 실명 정보가 Git history에 남지 않도록 공개본에는 원본 `.git` 이력을 포함하지 않았습니다.
> - 코드와 보고서는 문제 해결 방식과 설계 판단을 설명하기 위한 공개 가능한 범위의 자료입니다.

---

## What this repository demonstrates

### 1. Physical System Debugging
코드 내부 값만 확인하지 않고 실제 장비의 흐름을 따라 문제를 좁힙니다.

```text
Input / Sensor
    ↓
PLC State / Control Logic
    ↓
I/O Mapping / Communication
    ↓
Relay / Drive / Output
    ↓
Physical Device
```

- 실제 입력 신호와 프로그램 상태 비교
- 최종 출력과 내부 제어 변수의 연결 추적
- 전기/하드웨어 상태와 SW 타이밍 경계 분석
- TRACE 및 실장비 반복 시험을 통한 검증

### 2. State Machine & Recovery
비동기적으로 회복되는 장비 상태를 단일 이벤트에 의존하지 않고 상태 기반으로 관리합니다.

- 비상정지 해제 후 Motor Enable 복구
- 통신 초기화 / ACK / Timeout / Retry / Recovery 상태 관리
- 다중 PLC Scan에 걸친 기능 블록의 데이터 수명주기 분석
- 안전 인터록과 사용자 재기동 절차 유지

### 3. Configuration & Product Maintenance
기종과 하드웨어 구성이 달라져도 기준 프로젝트의 정합성을 유지하기 위한 작업을 기록합니다.

- B&R Automation Studio 프로젝트 구조
- Physical Configuration / Data Object 관리
- PPC2100 / PPC2200 구성 분리
- 4.3 → 4.12 버전 전환 및 유지보수 기준
- SVN 기반 파생 코드 추적성 개선 제안

### 4. Field Communication
고객의 공정 요구를 그대로 코드에 넣기보다, 실제 출력 구조와 안전 조건을 확인한 뒤 SW 구조에 맞게 번역합니다.

- 고객 요구사항 확인
- 장비 경험자 / HW 담당자와 원인 가설 공유
- 코드 및 I/O 흐름 추적
- 실제 장비 적용 및 반복 검증
- 결과와 재발 방지 기준 문서화

---

## Repository Map

| Directory | Contents |
|---|---|
| `코드 리펙토링/` | Before/After 코드와 문제 분석 보고서 |
| `외근보고서/` | 익명화된 현장·원격지원 사례 |
| `PPC2200모니터 관련/` | 하드웨어 구성 변경 및 실제 장비 테스트 결과 |
| `SVN관련/` | 버전·파생 프로젝트 관리 기준 |
| `업무 회고록/` | 업무 방식과 시스템 분석 관점 정리 |
| `제안/` | 유지보수성·추적성 개선 제안 |
| `커밋 기록/` | 실제 제품 코드 유지보수 작업 이력 예시 |

---

## Representative Cases

### Motor Enable Recovery
비상정지 해제 이벤트 시점과 실제 하드웨어 복구 시점의 불일치로 인해 Motor Enable이 OFF 상태에 고착되는 문제를 분석했습니다.

핵심 판단은 **비상정지 해제 순간**이 아니라 **실제 하드웨어가 Enable 가능한 상태로 회복되었는지**를 기준으로 복구 경로를 구성하는 것이었습니다.

`외근보고서/고객사_A/`

### Temperature Card Sensor-Type Write
다중 PLC Scan에 걸쳐 실행되는 비동기 기능 블록에 함수 지역변수의 주소를 전달하면서 포인터 유효기간이 끝나는 문제를 분석했습니다.

단순 카드 설정 문제가 아니라 **변수 lifetime과 비동기 실행 시점**의 문제로 보고 데이터 보존 구조를 수정했습니다.

`외근보고서/고객사_B/`

### Core Pull / Cooling Water Sequence
고객 공정 요구를 반영할 때 최종 DO를 직접 강제하지 않고, 해당 출력을 소유하는 내부 변수와 조건 구조를 역추적해 수정했습니다.

`외근보고서/고객사_C/`

### MHC Communication Recovery
통신 초기화 과정을 `POWERON_WAIT → SEND_STARTUP → WAIT_ACK → RUN → RECOVERY` 형태의 명시적 상태로 분리하고 Timeout / Retry / Watchdog을 관리했습니다.

`코드 리펙토링/MHC_InterfaceCyclic관련/`

---

## Main Technologies

- ANSI C
- PLC Cyclic Control
- B&R Automation Studio
- I/O Mapping
- State Machine
- Safety Interlock
- Industrial Communication
- TRACE / Field Debugging
- SVN / Configuration Management

---

## Notes

이 저장소는 **실행 가능한 회사 제품 소스 전체를 제공하기 위한 저장소가 아닙니다.**

실무 과정에서 직접 다룬 문제 중 공개 가능한 범위를 다시 정리한 자료이며,  
장비명·고객명·식별정보·내부 설정은 필요에 따라 익명화 또는 축약되어 있습니다.

따라서 이 저장소에서 가장 중요하게 보는 부분은 특정 변수명이나 제품 자체가 아니라,

**문제를 어떻게 재현하고 → 어느 경계까지 추적하고 → 어떤 기준으로 수정하며 → 실제 장비에서 어떻게 검증했는가**

입니다.

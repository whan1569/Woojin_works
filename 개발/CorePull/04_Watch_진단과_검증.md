# 04. Watch 진단과 검증

## 1. Watch 목록

| 변수 | 확인 |
|---|---|
| CorePullConfig | Enable 및 시간·속도·압력 입력 |
| gCorePullTest | TEST 누름/해제 신호 |
| gCorePullObservation | 운전 모드, 기본 조건, 형폐·노즐·사출, axisQuiet, paramsWithinLimits |
| gCorePullPreflight | 누름·해제 횟수, 사출 상승 횟수, 사전 검사 통과 후보 및 blockMask |
| gCorePullInput | 엔진에 전달된 입력 |
| gCorePullOutput | 상태·요청·결과 |
| gCorePullOwner | 현재 미연동, ready=0이 정상 |

TEST가 정상 연결되면 누름/해제 신호와 testPressCount/testReleaseCount 변화가 관측돼야 한다. 단, 버튼이 실제 hold 방식으로 동작하는지는 확인하여야 한다. 첫 샘플은 에지로 집계하지 않는다. 자동 검증은 기존 승인된 운전 절차 내에서 형폐·노즐터치·사출 순서와 injectionRiseCount, lastInjectionRiseMask, qualifiedInjectionCount를 관측한다. 이 값은 제어권 승인이 아니라 사전 검사 후보 수다.

## 2. 사전 검사 비트마스크

| HEX | DEC | 의미 |
|---|---:|---|
| 0x0001 | 1 | 제어권 미연동 (항상 포함) |
| 0x0002 | 2 | Enable OFF |
| 0x0004 | 4 | 운전 모드 조건 불일치 |
| 0x0008 | 8 | 기본 허가·모터·안전 모듈 조건 |
| 0x0010 | 16 | 설정값 범위 오류 |
| 0x0020 | 32 | CO1 동작 또는 요청 관측 |
| 0x0040 | 64 | 수동/자동 시퀀서 상태 |
| 0x0080 | 128 | 데이터 복사/잠금 조건 |
| 0x0100 | 256 | Jog 활성 |
| 0x0200 | 512 | 형폐 미완료 |
| 0x0400 | 1024 | 노즐터치 미완료 |
| 0x0800 | 2048 | 일반 CO1 레시피 충돌 후보 |
| 0x1000 | 4096 | 사출과 코어 펌프 동일 |
| 0x2000 | 8192 | 노즐 전진 전 사출 옵션 |
| 0x4000 | 16384 | 기존 사출 병렬 코어 설정 |

co1TableMask는 6종 코어 테이블(CUPos, CUNeg, IPNeg, Cool, EjAdv, EjRet)을 비트 0~5로 기록한다. 이 검사는 보수적 선별이며 펌프 동일 사용 등의 조건이 곧 모든 기종에서 불가능하다는 의미는 아니다. Watch에서 Mask를 16진수로 확인한다. **0x0001만 남아도 실제 구동 가능 상태는 아니다.**

## 3. 호스트 재현 및 결과 (2026-11-20)

이 디렉터리에서 실행:

```sh
gcc -std=c90 -pedantic -Wall -Wextra -Werror -DCOREPULL_HOST_TEST -Isrc tests/test_corepull.c src/corepull_controller.c -o test_corepull
./test_corepull
```

최신 C/H와 기존 단위 테스트를 조합하여 재컴파일 및 실행했다.

| 검증 항목 | 결과 |
|---|---|
| 기동 시 유지된 요청 차단 | PASS |
| 정상 자동 요청 및 사출 래치와 1회 시작 | PASS |
| 인터록 상실·정지 확인·명시적 재설정 | PASS |
| 승인 전 취소 및 오래된 응답 거절 | PASS |
| 시작 응답 타임아웃 | PASS |
| Organizer 재대기 응답 정지 처리 | PASS |
| 너무 이른 사출 또는 이미 HIGH인 출력 차단 | PASS |
| 수동 요청 해제 정지 | PASS |
| 설정 오류 및 축 사용 중 요청 차단 | PASS |
| 구동 watchdog | PASS |

**10/10 PASS**, 순수 엔진 테스트다. 가상 소유자 응답을 사용하며 AS 전용 관측부는 호스트 빌드에서 제외한다. 이는 장비 안전 검증이나 실제 축 구동 성공을 뜻하지 않는다.

## 4. 실장비 적용 현황과 인수자 잔여 작업

| 항목 | 상태 |
|---|---|
| 이전 단계 HMI/Task 등록·변수 입력 | 사용자 연결 정상 확인 |
| 최신 C/H 및 PC 테스트 | 작성, 호스트 컴파일 및 10건 통과 |
| 최신 AS4.12 컴파일·Transfer | 별도 확인 필요 |
| 실제 Watch 및 TEST/사출 에지 | 검증 대기 |
| 실제 축 제어권 및 요청 소비 | 미연동 |
| 정지 완료·후속 공정 제한 | 계약 정의, 실제 연결 미완료 |
| 실장비 동작 및 안전 검증 | 미실시 |

향후 기존 수동·자동 시퀀서에서 CO1 제어권을 확보하고 충돌을 해소하는 인터페이스를 구성해야 한다. 요청/승인/종료 ID의 정합성, 인터록 해제 후 의도치 않은 재시작 방지, HMI 통신 단절, 펌프 공유, 레시피 의존성, 형개 등 후속 동작 제한 및 실제 출력 매핑을 검증하기 전에는 실구동을 활성화하지 않는다.

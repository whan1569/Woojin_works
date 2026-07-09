# Woojin_works

PLC/C 기반 산업장비 제어, I/O 매핑, 상태 로직, 알람 처리, 현장 디버깅 과정을 정리한 기술 기록 아카이브입니다.

이 저장소는 하나의 완성된 애플리케이션이나 배포용 라이브러리가 아니라, 실제 장비 유지보수 및 개선 과정에서 확인한 문제, 원인 분석, 수정 방향, 설정 변경 이력을 정리하기 위한 개인 기술 기록 저장소입니다.

## Purpose

현장 장비에서 발생한 문제를 단순히 임시 조치로 끝내지 않고,  
문제 발생 조건, 확인 과정, 수정 방향, 재발 방지 포인트를 기록하기 위해 만들었습니다.

특히 다음과 같은 내용을 중심으로 정리합니다.

- PLC/C 기반 제어 로직 분석
- 장비 상태 전이 및 안전 조건 정리
- I/O 매핑 및 하드웨어 설정 기록
- 알람 발생 조건과 리셋 흐름 분석
- Automation Studio 프로젝트 구성 및 버전 관리
- 현장 디버깅 과정에서 확인한 문제와 조치 내용

## Repository Type

이 저장소의 성격은 다음과 같습니다.

| Type | Description |
|---|---|
| Field Archive | 실제 현장 장비 대응 과정에서 남긴 기록 |
| Debugging Notes | 문제 원인 분석 및 확인 절차 정리 |
| PLC/C Study Record | PLC 제어 구조와 C 기반 로직 분석 기록 |
| Configuration Log | 하드웨어 구성, I/O, 버전 차이 등 설정 기록 |

## Main Topics

- PLC control logic
- C-based machine control
- I/O mapping
- Alarm handling
- State transition logic
- Safety reset flow
- Automation Studio
- Hardware configuration
- Field troubleshooting
- Version migration

## Notes

일부 내용은 실제 장비 및 현장 업무와 관련되어 있으므로,  
민감한 정보, 세부 수치, 장비 식별 정보, 회사 내부 설정값은 제거하거나 축약하여 정리했습니다.

따라서 이 저장소는 그대로 실행하기 위한 코드 저장소라기보다,  
문제 해결 방식과 제어 로직 분석 과정을 보여주기 위한 기록 저장소에 가깝습니다.

## What I Focused On

이 저장소에서 중점적으로 정리한 부분은 다음과 같습니다.

1. 문제가 발생한 조건을 먼저 분리한다.
2. 물리 배선, 입력, 출력, 로직 순서로 원인을 좁힌다.
3. 임시 조치와 근본 수정 방향을 구분한다.
4. 수정 후 같은 문제가 반복되지 않도록 구조와 문서로 남긴다.

## Status

현재도 실무 기록 및 기술 정리 용도로 사용 중입니다.

# 실험용 AD103 Windows UEFI 포트

[English](README.md) | [저장소 개요](../../README.ko.md)

검토된 **MSI RTX 4070 Ti SUPER(AD103)** 프로필용 Windows 부팅 전 포트입니다.
읽기 전용 수집, 검증 조건을 둔 GPU 초기화, 무입력 진입, ROM 캡처 보존,
해제 가능한 부팅 예약을 제공합니다. 기반은
[Latence.app의 AD104 기여](../ad104-msi/README.md)입니다.
GPU 모델이나 PCI ID만으로 호환성을 판단할 수 없습니다. 캡처한 펌웨어, 소유 관계,
USB 연결 구조, PCI 자원과 Windows 부팅 이미지까지 일치해야 합니다.

## 관찰한 결과

0.3.0 실행 한 번에서 드라이버 해제, GPU 트랜잭션, PCIe Function Level Reset(FLR)
한 번, DMA 정리와 Windows 복귀를 확인했습니다. 전후 각각 독립 프로세스 3회로
측정한 cuBLAS 4096×4096 GEMM 중앙값은 다음과 같습니다.

| 연산 | 적용 전 TFLOP/s | 적용 후 TFLOP/s | 변화 |
|---|---:|---:|---:|
| FP32 | 30.34 | 30.37 | +0.1% |
| FP16 입력, FP32 누산 | 87.03 | 145.67 | +67.4% |
| BF16 입력, FP32 누산 | 86.63 | 143.97 | +66.2% |

수치 검사가 통과했고 전체 출력 해시 18개가 모두 같았습니다. 전후 GPU, 드라이버,
VBIOS, 라이브러리, 스크립트와 전력 제한을 동일하게 유지했습니다. 백그라운드 작업과
열 상태는 완전히 통제하지 못했습니다. 이 결과는 해당 GEMM 작업의 성능입니다.
이미지 생성, 로컬 언어 모델, 혼합 작업, 게임과 장기 안정성은 측정하지 않았습니다.
[측정 요약](../../docs/evidence/ad103-windows/summary.json)을 참고하십시오.

이후 매 부팅 설정을 저장하고 펌웨어 변수를 다시 읽어 확인했습니다. 이 변경을 위해
추가로 부팅하지는 않았습니다. 반복 냉간·온간 실행, 자식 장치가 하나인 실제 구성,
물리적 복구는 미검증입니다.

## 실행 조건과 실패 처리

진입 프로필은 GPU·서브시스템 ID, ROM·펌웨어 해시, 초기 레지스터, 정확한 드라이버
코드와 소유 관계를 검사합니다. GPU 자식 경로를 열거해 교차 검사하며, 알 수 없거나
중복·소유자 없는 경로와 도중에 바뀌는 구성을 거부합니다. 로그 실패 시 PCI decode
복원을 중단합니다.

EFI는 **10초 대기 후** 키 입력 없이 한 번 시도합니다. **S/Esc**는 검증된 Windows
이미지로 건너뛰고 **U**는 즉시 시작합니다. GPU 변경 전 실패는 Windows 이미지 검증과
로그 종료에 성공한 경우에만 Windows로 복귀하며, 그 외에는 펌웨어로 돌아갑니다.
변경 후 실패하면 정지합니다. 한 번의 부팅 안에서 자동 재시작·재시도는 하지 않습니다.
안전한 해제를 확인하지 못한 DMA 매핑은 전원이 꺼질 때까지 유지합니다. 성공 경로도
마지막 Windows 인계 검사를 통과해야 합니다.

VBIOS EEPROM 플래시가 아니라 GPU의 임시 상태를 바꿉니다. 설정된 전압·클럭·전력
제한은 변경하지 않습니다. 이것만으로 물리적 안전이나 실패 후 복구를 보증할 수는 없습니다.

## 빌드와 검증

아래 명령은 `windows/ad103-msi`에서 실행합니다. Windows x64, 실제 Python 3
인터프리터, `ar`·`nm`을 포함한 GCC/MinGW-w64와 MSVC x64 링커 도구가 필요합니다.
기존 스크립트에 컴파일러·인터프리터 경로를 직접 지정할 수 있습니다.

```powershell
$Gcc = (Get-Command gcc.exe).Source
$Python = Get-Command python.exe -All | Where-Object Source -NotMatch '\\WindowsApps\\' |
    Select-Object -First 1 -ExpandProperty Source
.\uefi\ad103\Build-Experimental.ps1 -Gcc $Gcc -Python $Python
```

별도로 검토한 `local/profile.json`, 비공개 ESP 헤더와 정확한 Windows 부팅 이미지가
필요합니다. 빌드는 해당 이미지를 읽고 로컬 산출물을 만듭니다. EFI 배치, 부팅 변수
변경이나 GPU 실행은 하지 않습니다. 다른 기기의 설정을 복사하거나 검사 조건을
지우지 말고 새 구성은 별도로 수집·검토해야 합니다.

[읽기 전용 진단](uefi/diagnostic/README.md)은 `tools/Configure.ps1`,
`uefi/diagnostic/Build.ps1`, `tools/Probe.ps1`을 사용합니다. `Configure.ps1`은
로컬 프로필을 만들며 기존 파일이 있으면 거부합니다. 진단 관리자의
`stage`·`arm`·`collect`·`cancel`은 쓰기 기능이 있는 언락 관리자와 구분됩니다.

고정된 NVIDIA 입력은 Linux/WSL에서 이 저장소의 `make assets` 절차로 생성합니다.
필요한 정확한 파일과 라이선스 고지를 검토한 로컬 에셋 폴더에 둡니다. 언락 관리자는
소스·프로필·EFI·검증 보고서의 해시 연결과 검증된 ROM 보존본을 모두 요구합니다.
빌드만으로 유효한 `validation-manifest.json`이 만들어지거나 배치가 허용되지 않습니다.
기존 EFI 모의 시나리오 334개는 정확한 소스와 비공개 입력에 연결돼 있으며,
Windows 관리자 모의 검사는 65개입니다. 새 경로의 빌드와 관리자 검증 결과는
[validation.json](../../docs/evidence/ad103-windows/validation.json)에 있습니다.

## 검증된 설치의 부팅 설정

검증된 빌드·에셋·비공개 프로필·저장된 부팅 상태가 있는 원래 설치 폴더를 사용합니다.
새 소스 체크아웃은 설치 완료 상태가 아닙니다. 서로 다른 상태 파일을 가진 두 폴더에서
하나의 설치를 관리하지 마십시오. 대상 PC의 관리자 PowerShell에서 실행합니다.

| 명령 | 동작 |
|---|---|
| `.\tools\Unlock.ps1 status` | 배치·부팅 상태 조회. `every_boot_enabled`로 매 부팅 설정 확인. |
| `.\tools\Unlock.ps1 stage` | 연결된 ESP에 검증된 EFI·에셋 배치. 부팅 예약은 하지 않음. |
| `.\tools\Unlock.ps1 arm` | `BootNext`로 다음 부팅 한 번 예약. |
| `.\tools\Unlock.ps1 enable-auto` | 저장된 Windows 부팅 순서 앞에 소유한 `Boot8F80`~`Boot8FFF` 항목 추가. |
| `.\tools\Unlock.ps1 disable-auto` | 저장된 원래 순서를 복원한 뒤 소유 항목 제거. |
| `.\tools\Unlock.ps1 cancel` | 일회성 예약 또는 매 부팅 설정 해제. |
| `.\tools\Unlock.ps1 collect` | 실행 로그 보존. 기존 같은 이름의 다른 바이트를 덮어쓰지 않음. |

이 명령들은 Windows를 재시작하지 않습니다. 매 부팅 설정은 원래 Windows 항목과
파일을 보존하며 `BootNext`를 사용하지 않습니다. 해제·취소는 무관한 항목을 보존하고
외부에서 바뀐 소유 항목·순서의 덮어쓰기를 거부합니다. Windows 로더가 업데이트돼도
같은 Windows/ESP 식별 정보가 맞으면 해제할 수 있습니다. 해제 명령이 현재 Windows
세션의 GPU 상태를 초기화하지는 않습니다.

한 번 건너뛰려면 대기 중 S/Esc 또는 펌웨어 부팅 메뉴의 **Windows Boot Manager**를
선택합니다. 4분 안에 Windows가 돌아오지 않으면 완전히 전원을 끄고 10초 기다린 뒤,
다음 부팅에서 Windows Boot Manager를 선택하고 `disable-auto`를 실행합니다.
**매 부팅 설정에서는 건너뛰지 않고 전원만 껐다 켜면 다시 시도합니다.**
재설치, ESP 변경, 펌웨어 초기화, Secure Boot 변경으로 설정이 무효화될 수 있습니다.
Windows 이미지가 달라지면 GPU 변경 전에 거부합니다. 고정 해시를 맞추려고 예전
Windows 이미지를 덮어쓰지 마십시오. 로그를 보존하며 공간·번호가 소진되면 자동 삭제
대신 실행을 거부합니다.

## ROM 보존과 복구

```powershell
.\tools\BackupFirmware.ps1 create
.\tools\BackupFirmware.ps1 verify
```

이 명령은 이전에 검토한 완전한 읽기 전용 캡처 파일을 다룹니다. 캡처 원본, PROM과
캐시된 EFI ROM을 해시와 함께 보존합니다. GPU를 새로 읽거나 펌웨어를 플래시·복원하지
않습니다. 플래시 칩 전체 범위 및 프로그래머·플래시 도구와의 호환성은 미검증입니다.

비공개 프로필, 원본 캡처, 부팅 상태 사본, 로그와 적절한 Windows 복구 자료를 포함한
기기별 복구 패키지를 대상 PC 밖에도 보관하십시오.
`tools/recovery/Verify-Package.ps1`은 GPU나 펌웨어에 접근하지 않고 기존 패키지의
manifest와 파일 해시를 검사합니다. 부팅 가능한 복구 미디어를 만들거나 물리적 복구
성공을 입증하는 도구는 아닙니다.

## 기여와 라이선스

이 포크에서 AD103 이식과 자동 부팅 관리 기능을 유지합니다. Windows UEFI 기반과
소유 관계 처리는 **Latence.app** 및
[impacifist/Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK)의
작업에서 파생됐습니다. Ada 통합은
[JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada),
[Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock),
[TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker)에 기반합니다.

언락 코드는 GPL-2.0-only이며 진단과 포함된 의존성은 각각의 라이선스를 유지합니다.
[출처](provenance.json), [고지](uefi/ad103/THIRD_PARTY_NOTICES.md),
[진단 고지](uefi/diagnostic/THIRD_PARTY.md)와 포함된 라이선스 파일을 참고하십시오.

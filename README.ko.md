# GPU unlock 1.1.1 — RTX 4090, RTX 4070 Ti SUPER, RTX 4060 Ti 16GB

[English](README.md) | [한국어](README.ko.md)

Linux에서는 [명령어 중심 빠른 시작](QUICKSTART.md)부터 읽으십시오.
AD103 Windows 포트는 프로필에 연결된 도구를 사용하기 전에
[영문](windows/ad103-msi/README.md) 또는 [한국어](windows/ad103-msi/README.ko.md)
가이드를 확인하십시오.

이 Linux 커널 모듈은 기본 NVIDIA 드라이버가 시작되기 전에 SM speed-select
오버라이드를 적용합니다. RTX 4090(`10de:2684`, AD102)에서 **FP32 누산을 사용하는
FP16·BF16 텐서 처리량이 약 2배**로 증가한 결과를 확인했습니다.

| 연산 | 적용 전 | 적용 후 |
|---|---:|---:|
| FP16 → FP32 누산 | 165.6 TF/s | 331.6 TF/s |
| BF16 → FP32 누산 | 165.5 TF/s | 330.6 TF/s |

오버라이드는 PCIe Function Level Reset(FLR), 기본 NVIDIA `615.78.08` 드라이버
초기화와 CUDA 벤치마크 이후에도 유지됐습니다. INT8, `dp4a`, 스칼라 FP32는 기준값에
가까웠습니다. 최초 트랜잭션은 저장소 정리 전에 실행했습니다.
[기술 보고서](rtx4090-register-report.md)와 [원래 증거](docs/evidence/rtx4090/README.md)를
참고하십시오.

별도의 사용자가 리비전 `8855e7b`를 실행해 Linux `7.2.11-arch1-1`에서 RTX 4090
트랜잭션과 NVIDIA 드라이버 인계에 성공했습니다. NVIDIA `615.78.08`과 동일한 450W
제한을 사용한 전후 비교에서 FP16/FP32 명령 처리량은 **160.4 → 320.4 TF/s**,
BF16/FP32는 **159.7 → 320.2 TF/s**였습니다. 수행한 수치 검사는 모두 통과했습니다.
실제 cuBLAS GEMM 중앙값은 행렬 크기에 따라 달랐습니다. FP16은 12~34%, BF16은
두 크기에서 16~20% 개선됐고 4096³에서는 사실상 같았습니다. 데스크톱이 실행 중인
상태에서 측정한 전후 한 쌍의 결과이며 전체 애플리케이션 향상이나 장기 안정성
시험은 아닙니다. [비교 증거와 한계](docs/evidence/rtx4090-pr1-matched/README.md),
[검증 절차](docs/testing.md)를 참고하십시오.

RTX 4070 Ti SUPER 한 장(`10de:2705`)과 NVIDIA `580.178.04`로 수행한 별도의 Ada
냉간 시험에서는 동일한 240W 제한 아래 FP16/FP32 텐서 명령 발행률이
**95.0 → 189.9 TF/s**, BF16/FP32가 **95.0 → 189.7 TF/s**로 측정됐습니다.
INT8은 379.5 TOP/s로 유지됐습니다. 기본 드라이버가 로드됐고 NVIDIA RM 조회는
FMLA32 전체 처리율을 보고했습니다. 결정적인 FP16/BF16 GEMM 출력이 정확히 일치했고,
사용자의 채굴 작업도 새 유효 share를 제출하며 복구됐습니다.
두 번째 조건을 맞춘 명령 벤치마크에서는 **TF32/FP32 47.49 → 94.51 TF/s**(1.99배)를
확인했고, 대조군 FP16/FP32도 약 2배였습니다. 부호 없는 INT4/INT32는 전후 모두
약 **754.8 TOP/s**였습니다. 이는 레지스터 안에서 실행한 명령 발행률이며,
애플리케이션 GEMM이나 추론 성능 향상을 뜻하지 않습니다.
같은 카드의 조건을 맞추고 수치 검증한 E4M3 FP8/FP32 CUTLASS GEMM은
**기본 59.326 TF/s → 적용 후 59.495 TF/s**, 약 0.3% 차이였습니다.
이 단일 GEMM에서는 유의미한 FP8 향상을 보지 못했고 다른 FP8 커널은 미검증입니다.
카드 한 장에서 냉간 부팅 두 번과 일반 온간 재부팅 두 번 성공했으며, 마지막은
영구 EFI 기본 항목으로 시작했습니다. 처리량은 첫 냉간 부팅에서 측정했습니다.
[2705 시험 보고서](docs/ada-2705-report.md)와
[민감정보를 제거한 원시 벤치마크 출력](docs/evidence/ada2705/README.md)을 참고하십시오.

AD102 페이로드와 디버그 부터 분석은
**[TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker)**에서
가져왔습니다. 이 포크는 공개된 해당 페이로드를
**[Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock)**의 Linux 모듈,
펌웨어 생성과 부팅 패키징에 통합합니다. 페이로드는 이 결과의 핵심 요소입니다.

## 실험용 Windows 기여 — Latence.app

별도의 [MSI RTX 4070 Ti AD104 Windows UEFI 참조 포트](windows/ad104-msi/README.md)는
NVIDIA 616.56에서 수동 부팅 한 번에 성공한 기록과, 조건을 맞춘 cuBLAS FP16/FP32
**+80.1%**, BF16/FP32 **+76.4%** 측정을 담고 있습니다.
캡처한 MSI Z490 프로필 하나에 연결돼 있으며 Linux 모듈에 AD104 허용 대상을
추가하지는 않습니다. [보고서와 한계](docs/ad104-windows-latence-app.md)를 참고하십시오.
이 AD104 기여에는 펌웨어·Windows·ROM 바이너리나 자동 부팅 설치 도구를 배포하지
않습니다. **Latence.app**의 기여이며 원래 상류 기여자와 라이선스 고지를 유지합니다.

## 실험용 Windows AD103 포트

이 포크는 [검증 조건을 둔 RTX 4070 Ti SUPER AD103 Windows UEFI 포트](windows/ad103-msi/README.ko.md)를
추가합니다. 읽기 전용 진단, ROM 캡처 보존, 무입력 진입과 해제 가능한 일회성·매 부팅
예약을 제공합니다. 검토된 플랫폼 프로필이 필요하며 모델명이나 PCI ID의 일치만으로
호환성이 입증되지는 않습니다.

0.3.0 실행 한 번에서 드라이버 해제, GPU 트랜잭션, FLR 한 번, DMA 정리와 Windows
인계를 완료했습니다. 조건을 맞춘 독립 프로세스 3회씩의 cuBLAS 4096×4096 GEMM
중앙값은 다음과 같습니다.

| 연산 | 적용 전 TFLOP/s | 적용 후 TFLOP/s | 변화 |
|---|---:|---:|---:|
| FP32 | 30.34 | 30.37 | +0.1% |
| FP16 입력·FP32 누산 | 87.03 | 145.67 | +67.4% |
| BF16 입력·FP32 누산 | 86.63 | 143.97 | +66.2% |

수치 검사와 전체 출력 해시 18개가 모두 일치했습니다. 짧은 GEMM 측정 결과이며
이미지 생성, 로컬 언어 모델, 혼합 작업, 게임과 장기 안정성은 측정하지 않았습니다.
[측정 요약](docs/evidence/ad103-windows/summary.json)과
[빌드·검증 기록](docs/evidence/ad103-windows/validation.json)을 참고하십시오.

검증된 설치에서 `enable-auto`는 매 부팅 Windows 전에 EFI를 선택합니다.
10초 대기 뒤 키 입력 없이 한 번 시도하고 S/Esc는 해당 부팅을 건너뜁니다.
`disable-auto`는 저장된 부팅 순서를 복원합니다. GPU 변경 후 실패하면 정지하며,
매 부팅 설정 상태에서 전원만 다시 켜면 다시 시도하게 됩니다.
코드는 VBIOS EEPROM이 아니라 GPU의 임시 상태를 바꿉니다. 매 부팅 설정은 저장 후
다시 읽어 확인했지만 반복 냉간·온간 실행과 물리적 복구는 미검증입니다.
[포트 가이드](windows/ad103-msi/README.ko.md)에 사전 조건·건너뛰기·해제·복구 방법이
있습니다. Linux 허용 대상과 기존 AD104 포트는 변경하지 않았습니다.

## Linux 대상

RTX 4090 `10de:2684`에는 AD102 펌웨어와 페이로드를 자동 선택합니다.
기존 GeForce/CMP GA102·GA104 장치 목록과 GA102 페이로드를 유지하고, 추가된
GA103·GA106 목록도 포함합니다. 상류는 RTX 3090 성공을 보고했지만 이 브랜치가
모든 기존 보드를 다시 검증한 것은 아닙니다.

RTX 4090 D(`10de:2685`)는 실험용 명시 옵션 `gpu_unlock.rtx4090d_experimental=1`로
AD102 경로를 사용합니다. 대상 카드는 `gpu_unlock.targets=`로 선택합니다.

Ada 포트는 RTX 4070 Ti SUPER `10de:2705`를 **카드 한 장에서 검증한 실험 대상
AD103**으로 인식합니다. `gpu_unlock.ad103_experimental=1`과 해당 카드의 정확한
단일 PCI 주소를 지정한 `gpu_unlock.targets=`가 모두 필요합니다.
`targets=all`로는 선택하지 않습니다. 시험한 2705에서 냉간 부팅 두 번과 이후 온간
재부팅을 통해 드라이버 580.178.04 인계와 전체 처리율 읽기 값을 확인했고,
기본 부팅과 적용 부팅 사이의 조건을 맞춰 처리량을 측정했습니다. 다른 보드는 미검증입니다.

RTX 4060 Ti 16GB `10de:2805`는 **카드 한 장에서 검증한 실험 대상 AD106**입니다.
같은 AD102 계열 페이로드를 사용하며 `gpu_unlock.ad106_experimental=1`과 정확한
단일 PCI 주소가 모두 필요합니다. `targets=all`로는 선택하지 않습니다.
시험한 카드의 냉간 부팅 한 번에서 모든 완료 표식, 권한, 선택 값과 FLR 후 검사를
통과했고 NVIDIA 595.99.02 드라이버가 로드됐습니다. RM 조회에서 FMLA32·IMLA2·IMLA4가
절반에서 전체 처리율로 바뀌었습니다. 같은 카드의 기본 처리량 측정은 아직 없습니다.
[2805 보고서](docs/ada-2805-report.md)를 참고하십시오.
다른 Ada ID, 전문가용 RTX A 시리즈와 데이터센터 제품은 허용하지 않습니다.
Ada 칩끼리 펌웨어를 공유한다는 사실만으로 미검증 카드 지원이 성립하지는 않습니다.

RTX 4090 경로는 서명된 펌웨어를 실행하고 페이로드 완료 표식과 필요한 권한을
확인한 뒤 SS0 `0x88888888`, SS1 `0x8`을 쓰고, 수정하지 않은 NVIDIA 드라이버가
연결되기 전에 FLR을 수행합니다. FLR 전후의 실효 speed-select 읽기 값이 예상값과
맞아야 합니다. 패치한 NVIDIA 드라이버나 UEFI 애플리케이션은 필요하지 않습니다.
페이로드는 공개된 복호화 디버그 부터 분석에서 도출한 명령 주소를 사용하며,
제품용 부터는 GPU 내부에서 인증·복호화합니다.

## Linux 빌드와 설치

GPL-2.0-only 소스를 복제하고 패키지를 로컬에서 빌드하면 고정된 NVIDIA 펌웨어를
자신의 컴퓨터에서 다운로드·생성합니다.

```sh
git clone https://github.com/snowyegret23/gpu-unlock-ada.git
cd gpu-unlock-ada
make install
```

`make install`은 지원하는 배포판 계열을 감지합니다. 명시적인 대상은 다음과 같습니다.

```sh
make install-arch   # makepkg -sif
make install-deb    # apt-get build-dep, dpkg-buildpackage, apt-get install
make install-rpm    # dnf builddep, rpmbuild, dnf install
```

`make arch`, `make deb`, `make rpm`은 설치 없이 빌드합니다.
패키지는 커밋된 `HEAD`에서 빌드하며 Debian·RPM 출력은 `build/deb`, `build/rpm`에
생깁니다. 커널 헤더와 정상 동작하는 DKMS 도구 체인이 필요합니다.

빌드는 고정된 NVIDIA `610.43.03` 소스와 바이너리 입력을 HTTPS로 가져오고 SHA-256을
검사합니다. `make inputs`는 캐시를 준비하고, `make firmware`는 캐시 입력에서
에셋을 생성하며, `make assets`는 둘 다 수행합니다. 기본 캐시는
`$HOME/.cache/gpu-unlock`이고 `CACHE=`, `INPUTS=`, `ASSETS=`로 바꿀 수 있습니다.
깨끗한 chroot에서 Debian을 빌드하려면 `GPU_UNLOCK_INPUTS`로 입력 캐시를 지정해야
합니다. `dpkg-buildpackage` 자체는 입력을 다운로드하지 않습니다.

`make dist`의 소스 아카이브, Debian 소스 패키지와 `.nosrc.rpm`에는 NVIDIA
아카이브나 생성된 펌웨어가 포함되지 않습니다. 입력·에셋 구조는
[firmware/README.md](firmware/README.md)를 참고하십시오.
이 저장소는 미리 빌드한 펌웨어 패키지를 배포하지 않습니다.

## 부팅 시 활성화 — Linux

패키지는 모듈과 펌웨어를 initramfs에 설치해 NVIDIA가 카드에 연결되기 전에
언락을 실행합니다. 커널 명령줄에서 명시적으로 선택하기 전에는 비활성 상태입니다.

```text
gpu_unlock.targets=0000:03:00.0
gpu_unlock.targets=0000:03:00.0,0000:04:00.0
gpu_unlock.targets=all
gpu_unlock.targets=0000:01:00.0 gpu_unlock.ad103_experimental=1
gpu_unlock.targets=0000:01:00.0 gpu_unlock.ad106_experimental=1
```

위 PCI 주소는 예시입니다. `lspci -D`가 출력하는 전체 PCI 이름을 사용하십시오.
`all`은 기존 GA102·GA104 대상을 포함해 허용된 미연결 카드 전부를 선택합니다.
RTX 4090 한 장을 선택할 때는 주소 명시가 유용합니다. `all`과 주소를 섞지 마십시오.
RTX 4070 Ti SUPER `10de:2705`와 RTX 4060 Ti 16GB `10de:2805`는 각각 해당 실험
옵션과 정확한 단일 PCI 주소가 필요하며 `all`로는 선택하지 않습니다.
잘못된 항목은 로그에 기록하고 무시합니다.

패키지는 다음 initramfs 생성기를 지원합니다.

- **mkinitcpio:** `/etc/mkinitcpio.conf.d/zz-gpu-unlock.conf`가 `MODULES`에서
  `gpu_unlock`을 `nvidia` 바로 앞에 넣고, NVIDIA가 없으면 마지막에 넣습니다.
  설치 시 이미지를 다시 만듭니다. `-c`나 preset의 `_config`로 선택한 사용자 설정은
  drop-in을 읽지 않으므로 해당 설정의 `MODULES`에 직접 `nvidia` 앞에 추가해야 합니다.
- **initramfs-tools:** `/usr/share/initramfs-tools/modules.d/gpu-unlock.conf`.
- **dracut:** `/usr/lib/dracut/dracut.conf.d/gpu-unlock.conf`.

kmod는 일치하는 첫 번째 soft dependency 줄만 사용해 다른 패키지와 충돌할 수 있으므로
`softdep nvidia pre:` 규칙은 사용하지 않습니다.

**첫 시도는 컴퓨터를 완전히 끄고 다시 켜서 진행하십시오.** 모듈은 NVIDIA가
로드되기 전, 펌웨어 상태가 새로 초기화된 카드를 필요로 합니다. 온간 재부팅이
항상 이 상태를 제공하지는 않습니다. 시험한 RTX 4070 Ti SUPER에서는 펌웨어 실행 전
WPR2 읽기 값이 0으로 돌아와 AC 전원을 분리하지 않아도 다시 성공했습니다.
다른 보드는 WPR2를 유지할 수 있으며, 이 경우 모듈이 펌웨어 시퀀스 재실행을
거부합니다. 이미 연결됐거나 전원이 내려간 카드도 건너뜁니다. 보드 펌웨어가
bus mastering을 켜둔 경우 모듈이 끄고 꺼진 상태가 유지되는지 검사하며,
이후 WPR2 검사가 이미 초기화된 카드를 계속 거부합니다.
NVIDIA가 카드에 연결된 뒤 모듈을 수동으로 로드해도 언락할 수 없습니다.

정확한 해당 카드에서 냉간 부팅과 일회성 온간 재부팅을 검증한 뒤에는 영구 부트로더
항목으로 같은 초기 initramfs와 정확한 `gpu_unlock.*` 옵션을 매 부팅 선택할 수
있습니다. 일회성 항목만으로는 유지되지 않습니다. 수정하지 않은 기본 부팅 항목을
남겨 두고 매 부팅 `gpu_unlock: unlocked`, 처리율 읽기 값과 GPU 상태를 확인하십시오.
시험한 대상은 커널 버전에 종속된 이미지를 사용합니다. 업그레이드 때 고정된 커널을
지우기 전에 해당 이미지를 다시 만들고 검증해야 합니다.

수동으로 초기 상태의 카드를 시험하기 전에는 `nvidia-smi`, CUDA, NVML,
`nvidia-modprobe`를 사용하지 마십시오. 이름을 지정해 NVIDIA를 로드할 수 있습니다.
일반 modprobe 블랙리스트는 이름을 직접 지정한 로드를 막지 않습니다.
패키지의 initramfs 흐름을 쓰면 수동 펌웨어 배치와 연구용 매개변수가 필요하지 않습니다.

기존 디스플레이 카드 인계 방식은 유지합니다. FLR 직전에 펌웨어 프레임버퍼를
제거하고 `nvidia-drm`이 인계할 때까지 화면이 검게 유지됩니다. 디스플레이 인계와
다중 카드 동작은 RTX 4090 검증 범위에 포함되지 않았습니다.

## 확인과 해제 — Linux

부팅 후 커널 로그를 확인합니다.

```sh
journalctl -k -b | grep gpu_unlock
lspci -nnk -s 0000:03:00.0  # replace the example address
```

모듈이 로드됐거나 `modprobe`가 성공했다는 사실만으로 언락이 입증되지는 않습니다.
선택한 카드의 `gpu_unlock: unlocked` 메시지, 페이로드, 선택 값과 FLR 후 검사를
확인하십시오. 보고서는 독립적인 레지스터·성능 확인 방법을 설명합니다.
선택적인 읽기 전용 샘플러는 `diagnostics/peek`, CUDA 명령 벤치마크는
`diagnostics/bench`에 있습니다. `gpu_unlock`이 펌웨어 트랜잭션을 수행하는 동안
두 번째 BAR0 샘플러를 로드하지 마십시오.

펌웨어가 멈추지 않아 DMA 버퍼를 유지한다고 보고하면 재시도 전에 전원을 끕니다.
펌웨어가 아직 사용할 수 있는 버퍼를 의도적으로 유지하는 것이므로, 이 상태에서
모듈을 강제로 언로드하지 마십시오.

커널 명령줄에서 `gpu_unlock.targets=`를 제거하면 이후 시도를 비활성화합니다.
부트로더 편집기에서 한 번의 부팅에만 제거할 수도 있습니다. 패키지를 제거하면
모듈과 initramfs 통합이 제거됩니다. 새 하드웨어 상태로 돌아가려면 전원을 끕니다.

## 개발과 기여

`make test`는 GPU에 접근하지 않는 호스트 검사를 실행합니다. 생성된 펌웨어가 없으면
전체 트랜잭션 검사를 명시적으로 건너뛰었다고 보고합니다. `make assets` 후
`make test-full`을 사용하면 고정 소스와 생성된 에셋 모두를 요구하는 엄격한 검사를
수행합니다. 다른 곳에서 준비한 입력은 `ASSETS=`, `NVIDIA_SOURCE=`로 지정합니다.
이 검사는 펌웨어 실행과 장치 동작을 모의 실행하므로 실제 하드웨어 재검증을 대신하지
않습니다. 검증 단계, 조건을 맞춘 벤치마크 명령과 기록된 하드웨어 증거는
[검증과 결과 해석](docs/testing.md)을 참고하십시오.

커널은 카드를 점유하거나 펌웨어 DMA를 시작하기 전에 각 에셋을 고정 SHA-256과
대조합니다. initramfs에 배치된 파일도 검사합니다. 커널의 SHA-256 라이브러리
`CONFIG_CRYPTO_LIB_SHA256`이 필요합니다. `make -C kernel/gpu_unlock`은 실행 중인
커널 헤더로 빌드하며 해당 헤더가 Clang 빌드를 지원하면 `LLVM=1`을 사용할 수 있습니다.

Linux 모듈과 패키징은 [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock)에서
파생됐습니다. AD102 four-PLM 페이로드는 공개 페이로드 생성기와 디버그 부터 분석을
포함한 [TOTORONG/AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker)에
기반합니다. GA102 V67 체인은
[WildFlash1st/cmp90hx-unlock](https://github.com/WildFlash1st/cmp90hx-unlock)에서
파생됐습니다. 초기 CMP 언락 작업, NVIDIA가 공개한 로더 코드, Nouveau와
[envytools](https://github.com/envytools)도 로더와 조사 기반을 이룹니다.

커널 모듈, 패키징과 페이로드 통합은 GPL-2.0-only입니다. NVIDIA 펌웨어에는
별도 조건이 적용됩니다. 출처와 보존된 라이선스 고지는
[firmware/NOTICE.md](firmware/NOTICE.md), [licenses/](licenses/)를 참고하십시오.

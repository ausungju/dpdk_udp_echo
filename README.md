# DPDK UDP Echo NIC Performance Test

## 의존성

필요한 빌드 도구 및 라이브러리:

```bash
sudo apt update
sudo apt install -y build-essential make pkg-config libnuma-dev libpcap-dev linux-headers-$(uname -r)
sudo apt install -y libdpdk-dev dpdk
```

## 디렉토리 구조

```text
./
    Makefile
    README.md
    build/
    include/
        client.h
        common.h
        server.h
    src/
        client/
            main.c
            cli.c
        server/
            main.c
            cli.c
        common/
            cli.c
            runtime.c
            stats.c
```

- `src/` : 서버/클라이언트 처리 루프와 CLI 구현.
- `src/common/` : 공통 CLI 파서, DPDK 런타임/포트 초기화, 통계 출력.
- `include/` : 공통 헤더와 CLI 옵션 정의.

## 빌드

```sh
make
```

## 공통 옵션

```text
-p PORTMASK
    사용할 DPDK port mask. 여러 bit가 켜져 있으면 가장 낮은 port 하나만 사용한다.

--port PORT
    사용할 DPDK port id를 직접 지정한다.

-P
    promiscuous mode 활성화.

-T PERIOD
    성능 통계 출력 주기, 초 단위. 기본값은 1. 0이면 주기 출력 비활성화.
```

## 멀티코어 동작

활성화된 EAL lcore 수만큼 RX/TX queue pair를 만들고, 각 lcore가 자신의 queue를 처리한다.
예를 들어 `-l 1-4` 또는 `-l 1,2,3,4`로 실행하면 4개 코어와 4개 queue pair를 사용한다.
멀티코어 실행에서는 worker가 동시에 CSV를 쓰지 않도록 주기 통계 출력은 최종 합산 출력만 남긴다.

## 서버 실행

```sh
sudo ./build/udpecho_server -l 1,2,3,4 -a 0000:01:00.0 -- \
    --port 0 --burst 64 -T 1
```

서버 추가 옵션:

```text
--burst N
    RX/TX burst 크기. 1~64, 기본값 64.
```

## 클라이언트 실행

기본 성능 테스트 예시:

```sh
sudo ./build/udpecho_client -l 5,6,7,8 -a 0000:01:00.0 -- \
    --port 0 --dst-mac 00:02:ca:44:20:74 \
    --burst 64 --payload-len 1400 --duration 10 -T 1
```

서버 MAC을 알고 있으면 broadcast 대신 직접 지정하는 편이 좋다.

```sh
--dst-mac 00:02:ca:44:20:74
```

클라이언트 추가 옵션:

```text
--burst N
    TX/RX burst 크기. 1~64, 기본값 64.

--payload-len N
    자동 생성할 UDP payload 길이. 0~1400, 기본값 64.

--duration SEC
    송신 지속 시간. 0이면 계속 송신한다.
```

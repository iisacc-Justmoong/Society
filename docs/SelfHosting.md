# Society 자체 서버와 NAS 호스팅

Society는 로컬 네트워크를 포함하는 계정 기반 기기 동기화 시스템이다. 같은 계정의 Society 앱과 SocietyDaemon이 컨테이너를 교환한다. NAS, 데스크톱, 별도 서버는 동일한 호스트 역할을 수행할 수 있다. Dreamscapes 등 소비 앱은 각 기기의 동기화된 로컬 컨테이너를 사용한다.

| 배치 | 연결 방법 | 컨테이너 제공자 |
| --- | --- | --- |
| 같은 LAN의 기기 | Bonjour/NSD 자동 페어링과 직접 TLS | Society 데스크톱 |
| NAS에 자체 호스팅 | NAS의 WebSocket 서버에 같은 계정으로 접속 | NAS의 SocietyDaemon |
| 인터넷의 자체 웹서버 | TLS reverse proxy와 WebSocket 중계 | 데스크톱 또는 별도 SocietyDaemon |
| 로컬·원격 혼합 | 인증된 서버 목록의 로컬 TLS 주소를 우선 시도하고 원격 중계로 전환 | 동일한 기본 호스트 |

NAS의 SMB/NFS 폴더만 지정하는 것이 이 프로토콜의 서버를 만드는 것은 아니다. 지원 OS와 CPU용 SocietyDaemon 및 SDK 런타임을 NAS에 배치하여 컨테이너 소유 프로세스를 실행한다. 동일 컨테이너를 여러 호스트 프로세스가 공유 마운트해 동시에 소유하지 않는다. 일반 웹사이트도 Society WebSocket 프로토콜 구현 없이는 연결 대상이 아니다.

## 앱에서 연결

1. 같은 iisacc 계정으로 호스트와 클라이언트에 로그인한다.
2. Devices → **Your Society server**에 `wss://nas.example.com/society`를 입력한다.
3. 원본 컨테이너를 제공하는 데스크톱은 **Host this container**, 다른 기기는 **Connect to server**를 선택한다.
4. 최초 전체 미러가 완료되면 변경·삭제를 양방향으로 교환한다. **Disconnect**는 자동 재접속도 중지하고, **Resume automatic sync**로 재개한다. **Use nearby discovery**는 서버 설정을 해제한다.

서버 주소와 호스트 모드는 기존 암호화 그룹 저장소의 세션 바인딩에 포함된다. 같은 로그인 세션을 복원한 GUI와 데몬이 설정을 공유한다. 로그아웃·계정 교체 시 연결을 정리하고 설정도 제거한다. 주소만 바꾸는 기존 C++ `setRelayUrl()`는 자동으로 자격증명을 전송하지 않으며, 앱의 명시적 `configureServer()`가 저장과 연결을 수행한다.

## 서버 배치

`iiServerHost`의 `ii-server-relay`는 계정 검증과 호스트 목록·WebSocket 중계를 담당한다. SocietyDaemon은 컨테이너 및 실제 동기화를 담당한다. 두 프로세스를 같은 NAS에서 실행하거나 분리할 수 있다. 기존 Qt WebSockets와 SDK를 사용하며 추가 유료 서비스나 신규 패키지 의존성은 없다.

```sh
ii-server-relay --address 127.0.0.1 --port 9443 \
  --session-url https://iisacc.com/Account/Session
```

외부 웹서버의 `/society`를 이 loopback 포트에 연결한다. [nginx WebSocket 공식 설정](https://nginx.org/en/docs/http/websocket.html)에 따라 기존 TLS 가상 서버에 다음 location을 추가한다.

```nginx
location /society {
    proxy_pass http://127.0.0.1:9443;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_read_timeout 120s;
}
```

reverse proxy를 사용하지 않으면 `ii-server-relay --address 0.0.0.0 --certificate /path/fullchain.pem --key /path/privkey.pem`으로 직접 TLS를 제공한다. 외부 접속 가능한 DNS·TLS·방화벽 경로는 운영자가 제공한다. 사설 NAS가 인터넷에서 직접 도달하지 않으면 접근 가능한 자체 서버를 중계로 배치하고 NAS와 클라이언트가 그 서버로 외향 연결한다. LAN 검색이 라우터나 NAT를 넘어 서버 주소를 자동으로 알아내지는 않는다.

## NAS에서 화면 없이 실행

NAS의 로컬 파일시스템에 컨테이너를 준비한다. 다음은 설치 경로를 PATH에 추가한 환경의 예이다.

```sh
mkdir -p /srv/society/container
iiSocietyContainerDriveTool create /srv/society/container
SocietyDaemon --sync --container /srv/society/container \
  --directory /var/lib/society/helper \
  --server wss://nas.example.com/society --host \
  --login-file /etc/society/login.json \
  --status-file /var/lib/society/status.json
```

`login.json`은 `email`, `password` 문자열을 가진 JSON이며 실행 계정만 읽고 쓸 수 있는 모드 `0600`으로 둔다. 프로그램은 심볼릭 링크·그룹/다른 사용자 권한·16 KiB 초과 파일을 거부한다. 비밀번호를 명령행 인자나 로그에 넣지 않는다. 이 명시적 headless 경로는 파일을 읽어 매 실행마다 로그인하고 인증 정보를 메모리에만 유지한다. OS 보안 저장소에 로그인된 데스크톱에서는 `--login-file`을 생략하여 기존 세션을 복원한다. 메일 코드가 요구되는 계정은 상태 파일의 `codeRequired`로 구분하며 로그인된 앱의 세션을 먼저 준비해야 한다.

`--host`를 생략하면 서버에 연결하는 클라이언트 데몬이다. `--container`는 기존 컨테이너를 요구하며 앱이 마지막으로 선택한 컨테이너를 쓸 때는 생략한다. `--directory`는 로컬 Helper·프로세스 소유권 기록 위치이며 데이터 컨테이너와 구분한다. 프로세스 감시는 운영체제의 기존 서비스 관리자를 사용한다. 예제의 전용 실행 계정, 설치 경로, 컨테이너·상태 디렉터리 권한을 실제 배치에 맞춘다. 예제 systemd 단위 파일은 [deploy/self-hosted](../deploy/self-hosted/)에 있다.

호스트에서 `SOCIETY_HOST_CERTIFICATE`와 `SOCIETY_HOST_KEY`를 함께 지정하면 서버 경로에서도 직접 로컬 TLS 리스너를 제공한다. 클라이언트는 인증된 서버 목록의 인증서 지문과 대조하며 연결 실패 시 원격 중계를 사용한다. 이 설정이 없으면 원격 중계만 사용한다. [Qt WebSocket 문서](https://doc.qt.io/qt-6.8/qwebsocket.html)를 따른다.

## 인증과 동기화 경계

계정 이름을 입력하는 것만으로 권한을 얻지 않는다. 설정한 서버가 신뢰하는 계정 authority에 현재 세션을 검증하여 얻은 subject로 호스트 목록과 요청을 격리한다. 세션 쿠키는 설정한 신뢰 서버로 전달되므로 사용자가 운영하거나 신뢰하는 서버를 지정해야 한다. 서버 운영자는 전달되는 파일을 볼 수 있다. 이는 TLS 연결이며 종단 간 암호화 저장소는 아니다.

외부 연결에는 `wss://`만 허용하고 숫자형 loopback의 `ws://`만 개발·내부 proxy용으로 허용한다. URL 내 사용자 정보·쿼리·fragment·0번 포트는 거부하며 인증서 오류를 무시하지 않는다. 서버에서 인증된 account subject와 현재 계정이 다르면 동기화와 컨테이너 요청을 거부한다. 서버용 scope는 계정 origin·subject에서 LAN 권한 발급기와 동일하게 계산하므로 LAN 증명의 만료나 로컬 네트워크 권한 거부가 원격 동기화를 막지 않는다.

컨테이너 UUID, 최초 미러 전 원본 보존, 해시·매니페스트 우선 교환, 부분 전송 재개, 양방향 변경·삭제는 기존 iiSocietySync 계약을 유지한다. 서버 호스트도 기본 호스트 ID를 기록하므로 LAN 방식으로 전환할 때 기존 호스트 선택을 유지한다. 이미 미러링한 클라이언트는 저장된 기본 호스트만 따른다. 최초 연결에 여러 독립 호스트가 있으면 임의로 미러하지 않고 하나의 기본 호스트가 남을 때까지 기다린다. 모바일은 모든 연결 방식에서 클라이언트이며 OS가 허용한 백그라운드 시간만 사용한다.

## 검증

`Society.Account`는 합성 계정 HTTP authority와 실제 WebSocket 중계로 LAN 탐색·LAN 증명 없이 700,000바이트 모델 복제, UUID 일치, 클라이언트 업로드, 호스트 수정, 삭제, 중계 재시작 후 재연결, 로그아웃 정리를 검사한다. 저장된 서버 설정 복원과 안전하지 않은 URL 거부도 확인한다. `Society.ClientOnlyNetwork`는 모바일 서버 입력과 호스트 제어 차단을, `Society.Daemon`은 headless 인자 검증을 포함한다. `Society.Account`는 별도 SocietyDaemon 프로세스의 파일 기반 로그인, 서버 호스팅, 모델 복제와 상태 파일도 검사한다.

실제 NAS OS/CPU 패키지, 공인 도메인의 TLS, 서로 다른 인터넷 회선의 물리 기기 전송은 별도 배포 검증이다. 로컬 중계 테스트 성공을 해당 배포 완료로 간주하지 않는다.

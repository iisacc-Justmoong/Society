# Society 베타 앱 아이콘

베타 아이콘은 정식 아이콘을 대체하지 않는 별도 빌드 입력이다. 원본 시안은
`resources/Appicon/Beta/Society-beta-concept.png`, 플랫폼별 결과는
`resources/Appicon/Beta/generated/`에 둔다. 시안은 기존 아이콘을 바탕으로
이미지 생성 도구가 다시 그린 것이므로 원본 벡터와 선·비율이 완전히 같지는 않다.

`tools/generate_app_icons.py`는 `--source`와 `--output`으로 정식과 베타
리소스를 각각 생성한다. 매니페스트는 실제 입력 파일의 이름과 SHA-256을 기록한다.
베타 빌드는 다음과 같이 선택한다.

```sh
python3 tools/generate_app_icons.py --source resources/Appicon/Beta/Society-beta-concept.png --output resources/Appicon/Beta/generated
cmake -S . -B build -DSOCIETY_APP_ICON_DIRECTORY="$PWD/resources/Appicon/Beta/generated"
cmake --build build --target Society
ctest --test-dir build -R 'Society.(AppIcons|BetaAppIcons)' --output-on-failure
```

`SOCIETY_APP_ICON_DIRECTORY`를 지정하지 않은 새 구성은
`resources/Appicon/generated/`를 사용한다. 기존 캐시에서 정식 아이콘으로
돌아가려면 구성할 때 `-U SOCIETY_APP_ICON_DIRECTORY`를 사용한다. 선택한
세트는 macOS 번들·런타임 브랜딩과 Windows 실행 파일 리소스에 적용된다.
패키지 서명, Apple 공증, Microsoft Store 제출은 아이콘 선택과 별도로 검증한다.

function(society_add_android_qr target)
    get_target_property(package ${target} QT_ANDROID_PACKAGE_SOURCE_DIR)
    file(READ "${package}/AndroidManifest.xml" manifest)
    string(REPLACE "org.qtproject.qt.android.bindings.QtActivity" "com.iisacc.society.SocietyActivity" manifest "${manifest}")
    string(REPLACE "</manifest>" "<uses-permission android:name=\"android.permission.CAMERA\" /><uses-feature android:name=\"android.hardware.camera\" android:required=\"false\" /></manifest>" manifest "${manifest}")
    file(WRITE "${package}/AndroidManifest.xml" "${manifest}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/platform/android/src/com/iisacc/society/SocietyActivity.java"
        "${package}/src/com/iisacc/society/SocietyActivity.java" COPYONLY)
    get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    file(READ "${qt_prefix}/src/android/templates/build.gradle" gradle)
    string(REPLACE "implementation fileTree" "implementation 'com.journeyapps:zxing-android-embedded:4.3.0'\n    implementation fileTree" gradle "${gradle}")
    file(WRITE "${package}/build.gradle" "${gradle}")
    target_sources(${target} PRIVATE App/Network/AndroidQrScanner.cpp)
    qt_add_resources(${target} android_qr_licenses PREFIX "/licenses/zxing"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/zxing"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/zxing/ZXing-LICENSE.txt"
              "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/zxing/ZXing-Android-Embedded-LICENSE.txt")
endfunction()

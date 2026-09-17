function(society_add_android_qr target)
    get_target_property(package ${target} QT_ANDROID_PACKAGE_SOURCE_DIR)
    file(READ "${package}/AndroidManifest.xml" manifest)
    string(REPLACE "</manifest>" "<uses-permission android:name=\"android.permission.BLUETOOTH\" android:maxSdkVersion=\"30\" /><uses-permission android:name=\"android.permission.BLUETOOTH_ADMIN\" android:maxSdkVersion=\"30\" /><uses-permission android:name=\"android.permission.ACCESS_FINE_LOCATION\" android:maxSdkVersion=\"30\" /><uses-permission android:name=\"android.permission.BLUETOOTH_SCAN\" android:usesPermissionFlags=\"neverForLocation\" /><uses-permission android:name=\"android.permission.BLUETOOTH_CONNECT\" /><uses-permission android:name=\"android.permission.BLUETOOTH_ADVERTISE\" /><uses-feature android:name=\"android.hardware.bluetooth_le\" android:required=\"false\" /></manifest>" manifest "${manifest}")
    string(REPLACE "org.qtproject.qt.android.bindings.QtActivity" "com.iisacc.society.SocietyActivity" manifest "${manifest}")
    string(REPLACE "</manifest>" "<uses-permission android:name=\"android.permission.CAMERA\" /><uses-feature android:name=\"android.hardware.camera\" android:required=\"false\" /></manifest>" manifest "${manifest}")
    string(REPLACE "</manifest>" "<uses-permission android:name=\"android.permission.READ_MEDIA_IMAGES\" /><uses-permission android:name=\"android.permission.READ_MEDIA_VIDEO\" /><uses-permission android:name=\"android.permission.READ_MEDIA_VISUAL_USER_SELECTED\" /><uses-permission android:name=\"android.permission.ACCESS_MEDIA_LOCATION\" /><uses-permission android:name=\"android.permission.READ_EXTERNAL_STORAGE\" android:maxSdkVersion=\"32\" /><uses-permission android:name=\"android.permission.WRITE_EXTERNAL_STORAGE\" android:maxSdkVersion=\"28\" /></manifest>" manifest "${manifest}")
    string(REPLACE "</application>" "<service android:name=\"com.iisacc.society.SocietySyncService\" android:exported=\"false\" android:stopWithTask=\"false\" android:foregroundServiceType=\"dataSync\" /></application>" manifest "${manifest}")
    string(REPLACE "</activity>" "<meta-data android:name=\"android.app.background_running\" android:value=\"true\" /></activity>" manifest "${manifest}")
    string(REPLACE "</manifest>" "<uses-permission android:name=\"android.permission.FOREGROUND_SERVICE\" /><uses-permission android:name=\"android.permission.FOREGROUND_SERVICE_DATA_SYNC\" /><uses-permission android:name=\"android.permission.WAKE_LOCK\" /></manifest>" manifest "${manifest}")
    file(WRITE "${package}/AndroidManifest.xml" "${manifest}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/platform/android/src/com/iisacc/society/SocietyActivity.java"
        "${package}/src/com/iisacc/society/SocietyActivity.java" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/platform/android/src/com/iisacc/society/SocietyPhotoLibrary.java"
        "${package}/src/com/iisacc/society/SocietyPhotoLibrary.java" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/platform/android/src/com/iisacc/society/SocietyDiscovery.java"
        "${package}/src/com/iisacc/society/SocietyDiscovery.java" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/platform/android/src/com/iisacc/society/SocietySyncService.java"
        "${package}/src/com/iisacc/society/SocietySyncService.java" COPYONLY)
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

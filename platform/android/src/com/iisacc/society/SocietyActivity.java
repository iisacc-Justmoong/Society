package com.iisacc.society;

import android.Manifest;
import android.app.Dialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.provider.Settings;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import com.google.zxing.BarcodeFormat;
import com.journeyapps.barcodescanner.BarcodeCallback;
import com.journeyapps.barcodescanner.BarcodeResult;
import com.journeyapps.barcodescanner.CameraPreview;
import com.journeyapps.barcodescanner.DecoratedBarcodeView;
import com.journeyapps.barcodescanner.DefaultDecoderFactory;
import java.util.Collections;
import org.qtproject.qt.android.bindings.QtActivity;

public final class SocietyActivity extends QtActivity {
    private static final int CAMERA_PERMISSION = 9075;
    private long qrRequest;
    private Dialog qrDialog;
    private DecoratedBarcodeView qrCamera;
    private static native void qrFinished(long request, String code, String error, boolean denied);

    public void startQrScan(long request) {
        runOnUiThread(() -> {
            if (qrRequest != 0) finishQr("", "", false);
            qrRequest = request;
            if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED)
                requestPermissions(new String[] {Manifest.permission.CAMERA}, CAMERA_PERMISSION);
            else showQrCamera();
        });
    }

    private void showQrCamera() {
        if (qrRequest == 0 || isFinishing()) return;
        qrDialog = new Dialog(this, android.R.style.Theme_Material_NoActionBar_Fullscreen);
        FrameLayout content = new FrameLayout(this);
        qrCamera = new DecoratedBarcodeView(this);
        qrCamera.getBarcodeView().setDecoderFactory(new DefaultDecoderFactory(Collections.singletonList(BarcodeFormat.QR_CODE)));
        qrCamera.setStatusText("Scan the QR code displayed in desktop Society. Use the same Wi-Fi or LAN.");
        content.addView(qrCamera, new FrameLayout.LayoutParams(-1, -1));
        Button close = new Button(this); close.setText("Close");
        FrameLayout.LayoutParams closeLayout = new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.END);
        closeLayout.topMargin = 48; closeLayout.rightMargin = 16;
        content.addView(close, closeLayout);
        close.setOnClickListener(view -> finishQr("", "", false));
        qrDialog.setContentView(content);
        qrDialog.setOnCancelListener(dialog -> finishQr("", "", false));
        qrDialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        qrCamera.getBarcodeView().addStateListener(new CameraPreview.StateListener() {
            public void previewSized() {}
            public void previewStarted() {}
            public void previewStopped() {}
            public void cameraClosed() {}
            public void cameraError(Exception error) {
                finishQr("", "The camera is unavailable. Close other camera apps and try again.", false);
            }
        });
        qrCamera.decodeSingle(new BarcodeCallback() {
            public void barcodeResult(BarcodeResult result) { finishQr(result.getText(), "", false); }
        });
        qrDialog.show(); qrCamera.resume();
    }

    private void finishQr(String code, String error, boolean denied) {
        long request = qrRequest; qrRequest = 0;
        if (qrCamera != null) { qrCamera.pause(); qrCamera = null; }
        if (qrDialog != null) { qrDialog.dismiss(); qrDialog = null; }
        if (request != 0) qrFinished(request, code, error, denied);
    }

    public void stopQrScan(long request) {
        runOnUiThread(() -> { if (request == qrRequest) finishQr("", "", false); });
    }

    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request != CAMERA_PERMISSION || qrRequest == 0) return;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) showQrCamera();
        else finishQr("", "Allow camera access in Settings to scan the desktop QR code.", true);
    }

    @Override protected void onStop() { finishQr("", "", false); super.onStop(); }

    public void openCameraSettings() {
        runOnUiThread(() -> startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
            Uri.parse("package:" + getPackageName()))));
    }
}

package com.iisacc.society;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

/** Keeps the existing Society client process alive for its current transfer batch. */
public final class SocietySyncService extends Service {
    private static final String CHANNEL = "society-sync";
    private static final long MAX_BATCH_MS = 15 * 60 * 1000L;
    private final Handler timer = new Handler(Looper.getMainLooper());
    private PowerManager.WakeLock wakeLock;
    private long token;
    private static native void syncExpired(long token);

    public static boolean start(Context context, long token) {
        try {
            Intent intent = new Intent(context, SocietySyncService.class).putExtra("token", token);
            return context.startForegroundService(intent) != null;
        } catch (RuntimeException unavailable) { return false; }
    }
    public static void stop(Context context) { context.stopService(new Intent(context, SocietySyncService.class)); }
    @Override public IBinder onBind(Intent intent) { return null; }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        token = intent == null ? 0 : intent.getLongExtra("token", 0);
        if (token == 0) { stopSelf(); return START_NOT_STICKY; }
        NotificationManager notifications = getSystemService(NotificationManager.class);
        notifications.createNotificationChannel(new NotificationChannel(CHANNEL, "Society synchronization", NotificationManager.IMPORTANCE_LOW));
        Intent launch = new Intent(this, SocietyActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        Notification notification = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync).setContentTitle("Society is syncing your devices")
            .setContentText("Your current file transfer continues in the background.")
            .setContentIntent(PendingIntent.getActivity(this, 0, launch, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT))
            .setOngoing(true).setOnlyAlertOnce(true).build();
        try {
            if (Build.VERSION.SDK_INT >= 29) startForeground(9076, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            else startForeground(9076, notification);
            if (wakeLock == null) wakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Society:sync");
            if (!wakeLock.isHeld()) wakeLock.acquire(MAX_BATCH_MS);
            timer.removeCallbacksAndMessages(null);
            timer.postDelayed(this::expire, MAX_BATCH_MS);
        } catch (RuntimeException unavailable) { expire(); }
        return START_NOT_STICKY;
    }
    private void expire() {
        if (token != 0) { long previous = token; token = 0; syncExpired(previous); }
        stopForeground(STOP_FOREGROUND_REMOVE); stopSelf();
    }
    // Android 15+ dataSync budget exhaustion. Also compile with older SDKs.
    public void onTimeout(int startId, int foregroundServiceType) { expire(); }
    @Override public void onDestroy() {
        timer.removeCallbacksAndMessages(null);
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        if (token != 0) { long previous = token; token = 0; syncExpired(previous); }
        super.onDestroy();
    }
}

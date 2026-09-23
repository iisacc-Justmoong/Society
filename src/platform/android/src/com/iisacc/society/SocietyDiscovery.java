package com.iisacc.society;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Handler;
import android.os.Looper;
import android.os.Build;
import java.net.InetAddress;
import java.net.Inet4Address;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.HashMap;
import org.json.JSONObject;

/** OS-managed DNS-SD; this class neither authenticates accounts nor serves files. */
public final class SocietyDiscovery {
    private static final String TYPE = "_society-pair._udp.";
    private final NsdManager manager;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final long request;
    private final HashMap<String, NsdServiceInfo> peers = new HashMap<>();
    private final ArrayDeque<String> queue = new ArrayDeque<>();
    private boolean running, resolving;
    private NsdManager.DiscoveryListener browser;
    private NsdManager.RegistrationListener registration;
    private static native void event(long request, String message);

    public SocietyDiscovery(Context context, long request) {
        manager = (NsdManager) context.getApplicationContext().getSystemService(Context.NSD_SERVICE);
        this.request = request;
    }
    private void report(String op, String key, JSONObject record, String address, int port) {
        if (!running) return;
        try { event(request, new JSONObject().put("op", op).put("key", key).put("record", record)
                .put("address", address).put("port", port).toString()); } catch (Exception ignored) { }
    }
    private void error(int code) {
        if (!running) return;
        try { event(request, new JSONObject().put("op", "error")
                .put("message", "Local device discovery is unavailable (" + code + "). Retrying…").toString()); }
        catch (Exception ignored) { }
    }
    public void start(String recordText, int port) {
        handler.post(() -> {
            if (running) return;
            running = true;
            try {
                JSONObject record = new JSONObject(recordText);
                NsdServiceInfo info = new NsdServiceInfo();
                info.setServiceName("Society-" + record.getString("nonce").substring(0, 12));
                info.setServiceType(TYPE); info.setPort(port);
                for (String key : new String[]{"v", "scope", "id", "name", "kind", "host", "nonce", "epoch", "proof", "autoHost", "primary"})
                    if (record.has(key)) info.setAttribute(key, record.getString(key));
                registration = new NsdManager.RegistrationListener() {
                    public void onServiceRegistered(NsdServiceInfo service) { }
                    public void onRegistrationFailed(NsdServiceInfo service, int code) { handler.post(() -> error(code)); }
                    public void onServiceUnregistered(NsdServiceInfo service) { }
                    public void onUnregistrationFailed(NsdServiceInfo service, int code) { }
                };
                browser = new NsdManager.DiscoveryListener() {
                    public void onDiscoveryStarted(String type) { }
                    public void onDiscoveryStopped(String type) { }
                    public void onStartDiscoveryFailed(String type, int code) { handler.post(() -> error(code)); }
                    public void onStopDiscoveryFailed(String type, int code) { }
                    public void onServiceFound(NsdServiceInfo service) { handler.post(() -> {
                        if (!running || peers.size() >= 128 || !(service.getServiceType().equals(TYPE)
                                || service.getServiceType().equals("_society-pair._udp"))) return;
                        String key = service.getServiceName(); peers.put(key, service);
                        if (!queue.contains(key)) queue.add(key); resolveNext();
                    }); }
                    public void onServiceLost(NsdServiceInfo service) { handler.post(() -> {
                        String key = service.getServiceName(); peers.remove(key); queue.remove(key);
                        report("lost", key, null, "", 0);
                    }); }
                };
                manager.registerService(info, NsdManager.PROTOCOL_DNS_SD, registration);
                manager.discoverServices(TYPE, NsdManager.PROTOCOL_DNS_SD, browser);
                handler.postDelayed(refresh, 6000);
            } catch (Exception exception) { error(-1); }
        });
    }
    private final Runnable refresh = new Runnable() {
        public void run() {
            if (!running) return;
            for (String key : peers.keySet()) if (!queue.contains(key)) queue.add(key);
            resolveNext(); handler.postDelayed(this, 6000);
        }
    };
    @SuppressWarnings("deprecation") // API 28-compatible, one resolution at a time.
    private void resolveNext() {
        if (!running || resolving || queue.isEmpty()) return;
        String key = queue.remove(); NsdServiceInfo service = peers.get(key);
        if (service == null) { resolveNext(); return; }
        resolving = true;
        try { manager.resolveService(service, new NsdManager.ResolveListener() {
            public void onResolveFailed(NsdServiceInfo info, int code) { handler.post(() -> { resolving = false; resolveNext(); }); }
            public void onServiceResolved(NsdServiceInfo info) { handler.post(() -> {
                resolving = false;
                if (running && peers.containsKey(key)) {
                    try {
                        JSONObject record = new JSONObject();
                        for (String field : new String[]{"v", "scope", "id", "name", "kind", "host", "nonce", "epoch", "proof", "autoHost", "primary"}) {
                            byte[] value = info.getAttributes().get(field);
                            if (value != null && value.length <= 255) record.put(field, new String(value, StandardCharsets.UTF_8));
                        }
                        if (Build.VERSION.SDK_INT >= 34) {
                            for (InetAddress address : info.getHostAddresses()) if (address instanceof Inet4Address)
                                report("found", key, record, address.getHostAddress(), info.getPort());
                        } else if (info.getHost() instanceof Inet4Address) {
                            report("found", key, record, info.getHost().getHostAddress(), info.getPort());
                        }
                    } catch (Exception ignored) { }
                }
                resolveNext();
            }); }
        }); } catch (Exception exception) { resolving = false; handler.postDelayed(this::resolveNext, 1000); }
    }
    public void stop() { handler.post(() -> {
        running = false; handler.removeCallbacks(refresh); peers.clear(); queue.clear();
        if (browser != null) { try { manager.stopServiceDiscovery(browser); } catch (Exception ignored) { } browser = null; }
        if (registration != null) { try { manager.unregisterService(registration); } catch (Exception ignored) { } registration = null; }
    }); }
}

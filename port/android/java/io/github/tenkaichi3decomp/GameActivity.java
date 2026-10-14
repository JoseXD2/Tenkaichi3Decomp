package io.github.tenkaichi3decomp;

import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.Window;

import java.io.File;
import java.io.FileInputStream;

/**
 * The game's activity: SDL's, plus Discord Rich Presence when an application id is known.
 *
 * The id is the one built in (DISCORD_ID) or the digits in discord.txt in the data folder (DataDir.java), which
 * wins; "0" there, or the launcher's Settings > Game > Discord status, switches the status off. With an id, Discord's
 * Social SDK (libdiscord_partner_sdk.so) and the discord-rpc layer over it (libbt3discord.so) are loaded here, where
 * Java can run their JNI_OnLoad, the SDK is given this activity, and BT3_DISCORD_ID tells the loader (loader.c) to
 * start the status thread. Anything failing here only leaves the status off.
 */
public class GameActivity extends org.libsdl.app.SDLActivity {
    boolean mSustained;
    static final String DISCORD_ID = "1542174536970010685"; // the Discord application of the port (Developer Portal), "" = none

    @Override
    protected void onCreate(Bundle b) {
        try { // the screen's own shape, for the picture's "Whole screen" (plat_stub.c): the whole display, landscape
            android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
            getWindowManager().getDefaultDisplay().getRealMetrics(dm);
            int lo = Math.min(dm.widthPixels, dm.heightPixels), hi = Math.max(dm.widthPixels, dm.heightPixels);
            if (lo > 0) android.system.Os.setenv("BT3_SCREEN_MILLI", Integer.toString(hi * 1000 / lo), true);
        } catch (Throwable t) {
            Log.w("bt3", "screen shape: " + t);
        }
        if (android.os.Build.VERSION.SDK_INT >= 28) { // over the camera cut-out too (no grey strip beside the picture)
            getWindow().getAttributes().layoutInDisplayCutoutMode = android.os.Build.VERSION.SDK_INT >= 30 ? 3 /* ALWAYS */ : 1 /* SHORT_EDGES */;
        }
        try { // the data folder (DataDir.java), for the loader (native/loader.c)
            android.system.Os.setenv("BT3_APP_DIR", DataDir.current(this).getPath(), true);
            android.system.Os.setenv("BT3_DATA", DataDir.gameData(this).getPath(), true);
        } catch (Throwable t) {
            Log.w("bt3", "data folder: " + t);
        }
        // what the launcher set (Launcher.java): the switched-on mods, the compatibility mode, sustained performance
        KV launch = new KV(new File(DataDir.current(this), "launcher.txt"));
        try {
            String mods = new Mods(DataDir.current(this)).env();
            if (mods.length() > 0) {
                android.system.Os.setenv("BT3_MODS", mods, true);
                Log.i("bt3", "mods: " + mods.replace('\n', ';'));
            }
            if (launch.on("gpu_safe", false)) { android.system.Os.setenv("BT3_GPU_SAFE", "1", true); }
            if (launch.on("fast_depth", false)) { android.system.Os.setenv("BT3_NZ", "2", true); } // (gs_draw.c pipeline_get)
        } catch (Throwable t) {
            Log.w("bt3", "launcher settings: " + t);
        }
        mSustained = launch.on("sustained", false);
        try {
            String id = launch.on("discord", true) ? discordId() : "";
            if (id.length() > 0 && !id.equals("0")) {
                System.loadLibrary("discord_partner_sdk");
                System.loadLibrary("bt3discord");
                // (by name: a build without Discord's library, the GitHub workflow's without its files, still compiles)
                Class.forName("com.discord.socialsdk.DiscordSocialSdkInit").getMethod("setEngineActivity", android.app.Activity.class).invoke(null, this);
                android.system.Os.setenv("BT3_DISCORD_ID", id, true);
                Class.forName("com.hxdiscord.rpc.sdk.DiscordApplicationMetadata").getMethod("fetch", String.class).invoke(null, id); // the app's name and images
                Log.i("bt3", "discord: status on, application " + id);
            }
        } catch (Throwable t) {
            Log.w("bt3", "discord: status off: " + t);
        }
        super.onCreate(b);
    }

    // Online play: Android's Wi-Fi power saving holds incoming packets back and lets them through in bursts, which
    // showed as lag spikes in a match. While the game is in front the Wi-Fi is asked for low latency instead.
    android.net.wifi.WifiManager.WifiLock mWifiLock;

    @Override
    protected void onResume() {
        super.onResume();
        if (mSustained && android.os.Build.VERSION.SDK_INT >= 24) { // a steady speed rather than the highest one (Launcher: Performance)
            try {
                android.os.PowerManager pm = (android.os.PowerManager) getSystemService(POWER_SERVICE);
                if (pm.isSustainedPerformanceModeSupported()) { getWindow().setSustainedPerformanceMode(true); }
            } catch (Throwable t) {
                Log.w("bt3", "sustained performance: " + t);
            }
        }
        try {
            if (mWifiLock == null) {
                android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
                int mode = android.os.Build.VERSION.SDK_INT >= 29 ? 4 /* WIFI_MODE_FULL_LOW_LATENCY */ : 3 /* WIFI_MODE_FULL_HIGH_PERF */;
                mWifiLock = wm.createWifiLock(mode, "bt3:online");
                mWifiLock.setReferenceCounted(false);
            }
            mWifiLock.acquire();
        } catch (Throwable t) {
            Log.w("bt3", "wifi lock: " + t);
        }
        hideBarsSoon();
        try {
            getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
                public void onSystemUiVisibilityChange(int vis) {
                    if ((vis & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                        View v = getWindow().getDecorView();
                        v.removeCallbacks(mHide);
                        v.postDelayed(mHide, 2500); // (bars shown by a swipe: away again after a moment)
                    }
                }
            });
        } catch (Throwable t) { /* (only a convenience) */ }
    }

    // The system's bars (back, home, recent apps; the status bar) hidden for good: they come back for a moment with
    // a swipe from the edge and go again. Done here rather than by SDL's full screen, which changed the window's
    // size against the touch controls; SDL's window (not "full screen") shows the bars when it is made, so this is
    // done again whenever the window gets the focus and whenever the bars come back.
    void hideBars() {
        Window w = getWindow();
        if (w == null) return;
        w.clearFlags(android.view.WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
        w.addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            android.view.WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(android.view.WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
        w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    final Runnable mHide = new Runnable() { public void run() { hideBars(); } };

    void hideBarsSoon() {
        View v = getWindow() != null ? getWindow().getDecorView() : null;
        if (v != null) {
            v.removeCallbacks(mHide);
            v.postDelayed(mHide, 300); // (after SDL's own window style, which is set on the same thread)
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideBarsSoon();
    }

    @Override
    protected void onPause() {
        try {
            if (mWifiLock != null && mWifiLock.isHeld()) mWifiLock.release();
        } catch (Throwable t) { /* (nothing to undo) */ }
        super.onPause();
    }

    /** Online play's matchmaking (port/src/gs/net_match.c): POSTs JSON, returns the answer's text, or null. Called
        from the game's matchmaking thread, never from the UI thread. */
    public static String httpPost(String url, String body, String headers) {
        java.net.HttpURLConnection c = null;
        try {
            c = (java.net.HttpURLConnection) new java.net.URL(url).openConnection();
            c.setConnectTimeout(8000);
            c.setReadTimeout(12000);
            c.setRequestProperty("User-Agent", "DragonRage-Android"); // (GitHub answers nothing without a name)
            for (String h : headers != null ? headers.split("\n") : new String[0]) { // the build key's signature (net_sign.c)
                int k = h.indexOf(':');
                if (k > 0) c.setRequestProperty(h.substring(0, k).trim(), h.substring(k + 1).trim());
            }
            if (body != null) {
                c.setRequestMethod("POST");
                c.setDoOutput(true);
                c.setRequestProperty("Content-Type", "application/json");
                byte[] b = body.getBytes("UTF-8");
                c.setFixedLengthStreamingMode(b.length);
                java.io.OutputStream os = c.getOutputStream();
                os.write(b);
                os.close();
            } else { // a GET: the update check (net_match.c)
                c.setRequestMethod("GET");
                c.setInstanceFollowRedirects(true);
            }
            int status = c.getResponseCode();
            Log.i("bt3", "matchmaking: " + url + " -> HTTP " + status);
            java.io.InputStream in = status >= 400 ? c.getErrorStream() : c.getInputStream();
            if (in == null) return "ERR HTTP " + status + " with no body";
            java.io.ByteArrayOutputStream all = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) all.write(buf, 0, n);
            in.close();
            return all.toString("UTF-8");
        } catch (Exception e) {
            Log.w("bt3", "matchmaking: " + url + ": " + e);
            return "ERR " + e;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    String discordId() {
        String id = DISCORD_ID;
        try {
            File f = new File(DataDir.current(this), "discord.txt");
            if (f.isFile()) {
                byte[] buf = new byte[64];
                FileInputStream in = new FileInputStream(f);
                int n = in.read(buf);
                in.close();
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < n; i++) {
                    if (buf[i] >= '0' && buf[i] <= '9') sb.append((char) buf[i]);
                }
                id = sb.toString();
            }
        } catch (Exception e) {
            Log.w("bt3", "discord.txt: " + e);
        }
        return id;
    }
}

/*
 * Updates from the repository's releases, as the PC build's "Check for updates" does: the launcher asks GitHub for
 * the newest published release (one GET, nothing else is sent), and when its tag is a newer version than this app
 * and it has an .apk attached, offers it. On the player's yes the APK is downloaded to the app's cache and handed to
 * Android's package installer, which asks the player to confirm (and, the first time, to allow installs from this
 * app). An update installs over this app only when it was signed with the same key (the official releases are).
 * Switched off in the launcher: Settings > Game > Check for updates (launcher.txt update_check=0).
 */
package io.github.tenkaichi3decomp;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInstaller;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

final class Updater {
    /* the repository whose releases are the app's updates (a fork that publishes its own changes this) */
    static final String REPO = "JoseXD2/Tenkaichi3Decomp";
    static final String ACTION = "io.github.tenkaichi3decomp.INSTALL_STATUS";

    /* a newer release: its tag, version code, notes and APK */
    static final class Release {
        String tag, notes, url;
        long code, size;
    }

    interface Progress { void at(long done, long total); }

    private Updater() {}

    /* "v1.2.5" -> 125: the releases' version codes (major * 100 + minor * 10 + patch, .github/workflows/android.yml) */
    static long code(String tag) {
        String[] p = tag.replaceFirst("^[vV]", "").split("[-+]")[0].split("\\.");
        long c = 0, mul = 100;
        for (int i = 0; i < 3; i++, mul /= 10) {
            long n = 0;
            if (i < p.length) {
                try { n = Long.parseLong(p[i]); } catch (NumberFormatException e) { return -1; }
            }
            c += n * mul;
        }
        return c;
    }

    static HttpURLConnection open(String url) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(10000);
        c.setReadTimeout(20000);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", "DragonRage-Android"); // (GitHub answers nothing without a name)
        c.setRequestProperty("Accept", "application/vnd.github+json");
        return c;
    }

    /* The newest release if it is newer than `mine` and has an APK; null otherwise (or without a connection). */
    static Release check(long mine) {
        HttpURLConnection c = null;
        try {
            c = open("https://api.github.com/repos/" + REPO + "/releases/latest");
            if (c.getResponseCode() != 200) { return null; }
            ByteArrayOutputStream all = new ByteArrayOutputStream();
            try (InputStream in = c.getInputStream()) {
                byte[] b = new byte[8192];
                int n;
                while ((n = in.read(b)) > 0) { all.write(b, 0, n); }
            }
            JSONObject j = new JSONObject(all.toString("UTF-8"));
            Release r = new Release();
            r.tag = j.optString("tag_name", "");
            r.code = code(r.tag);
            r.notes = j.optString("body", "");
            if (r.code <= mine) { return null; }
            JSONArray as = j.optJSONArray("assets");
            for (int i = 0; as != null && i < as.length(); i++) {
                JSONObject a = as.getJSONObject(i);
                if (a.optString("name", "").toLowerCase(java.util.Locale.ROOT).endsWith(".apk")) {
                    r.url = a.optString("browser_download_url", null);
                    r.size = a.optLong("size", 0);
                    break;
                }
            }
            return r.url != null ? r : null;
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) { c.disconnect(); }
        }
    }

    static File download(Context ctx, Release r, Progress p) throws IOException {
        File dir = ctx.getExternalCacheDir() != null ? ctx.getExternalCacheDir() : ctx.getCacheDir();
        File f = new File(dir, "update.apk"), part = new File(dir, "update.apk.part");
        HttpURLConnection c = open(r.url);
        c.setRequestProperty("Accept", "application/octet-stream");
        try {
            if (c.getResponseCode() != 200) { throw new IOException("HTTP " + c.getResponseCode()); }
            long total = c.getContentLengthLong() > 0 ? c.getContentLengthLong() : r.size, done = 0;
            try (InputStream in = c.getInputStream(); OutputStream o = new FileOutputStream(part)) {
                byte[] b = new byte[1 << 16];
                int n;
                while ((n = in.read(b)) > 0) {
                    o.write(b, 0, n);
                    done += n;
                    p.at(done, total);
                }
            }
            if (total > 0 && done != total) { throw new IOException("download cut short"); }
        } finally {
            c.disconnect();
        }
        f.delete();
        if (!part.renameTo(f)) { throw new IOException("rename"); }
        return f;
    }

    /* Hands the APK to Android's installer. `done` hears the outcome: null when Android asks the player (the
       confirmation is shown), otherwise the failure's message. On success the app is replaced and closed. */
    interface Outcome { void at(String failure); }

    static void install(final Context ctx, File apk, final Outcome done) throws IOException {
        PackageInstaller pi = ctx.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams sp = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        sp.setAppPackageName(ctx.getPackageName());
        int id = pi.createSession(sp);
        try (PackageInstaller.Session s = pi.openSession(id)) {
            try (OutputStream o = s.openWrite("base.apk", 0, apk.length()); InputStream in = new FileInputStream(apk)) {
                byte[] b = new byte[1 << 16];
                int n;
                while ((n = in.read(b)) > 0) { o.write(b, 0, n); }
                s.fsync(o);
            }
            BroadcastReceiver br = new BroadcastReceiver() {
                @Override
                public void onReceive(Context c, Intent i) {
                    int st = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
                    if (st == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                        Intent confirm = i.getParcelableExtra(Intent.EXTRA_INTENT);
                        if (confirm != null) {
                            confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                            c.startActivity(confirm);
                        }
                        done.at(null);
                        return;
                    }
                    try { c.unregisterReceiver(this); } catch (Exception e) { /* already */ }
                    if (st != PackageInstaller.STATUS_SUCCESS) {
                        String m = i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
                        done.at(m != null ? m : "status " + st);
                    }
                }
            };
            IntentFilter f = new IntentFilter(ACTION);
            if (Build.VERSION.SDK_INT >= 33) {
                ctx.registerReceiver(br, f, Context.RECEIVER_NOT_EXPORTED);
            } else {
                ctx.registerReceiver(br, f);
            }
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            PendingIntent pend = PendingIntent.getBroadcast(ctx, id, new Intent(ACTION).setPackage(ctx.getPackageName()), flags);
            s.commit(pend.getIntentSender());
        }
    }
}

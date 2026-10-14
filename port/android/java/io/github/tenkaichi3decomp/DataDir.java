/*
 * Where the app keeps the game's data (gamedata/, textures/, saves, settings, logs): the "DragonRage" folder at
 * the root of the phone's storage, where the player can reach it with any file manager (to drop texture packs in,
 * say). That needs Android's "all files access" (11+) or the storage permission (before); without it the app's
 * own folder (Android/data/io.github.tenkaichi3decomp/files) is used, as up to 1.1.3.
 *
 * Both processes (the launcher and ":game") ask here, so they always agree; the game's loader gets the folder in
 * BT3_APP_DIR and the game's data in BT3_DATA (GameActivity.java, native/loader.c).
 */
package io.github.tenkaichi3decomp;

import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Environment;

import java.io.File;

final class DataDir {
    static final String NAME = "DragonRage";

    private DataDir() {}

    /* /storage/emulated/0/DragonRage */
    static File root() { return new File(Environment.getExternalStorageDirectory(), NAME); }

    /* the app's own folder (the one up to 1.1.3) */
    static File app(Context c) {
        File d = c.getExternalFilesDir(null);
        return d != null ? d : c.getFilesDir();
    }

    static boolean canUseRoot(Context c) {
        if (Build.VERSION.SDK_INT >= 30) { return Environment.isExternalStorageManager(); }
        return c.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    /* The game's data (gamedata/, ~3 GB in ~68 000 files) stays in the app's own folder: Android writes there
       directly, while every file made in the shared storage goes through its media service (minutes longer to
       unpack). Nobody needs to touch it; the folder at the root keeps what a player does touch (textures, saves,
       settings, logs). A gamedata made at the root by 1.1.4 is still used. */
    static boolean hasGame(File gd) { return new File(gd, "disc/SLUS_216.78").isFile(); }

    static File gameData(Context c) {
        File own = new File(app(c), "gamedata");
        if (hasGame(own)) { return own; }
        File r = new File(root(), "gamedata");
        if (canUseRoot(c) && hasGame(r)) { return r; }
        return own;
    }

    /* something in the app's own folder still to move over (an install cut short and the game's data do not count) */
    static boolean hasContent(File d) {
        String[] k = d.list();
        if (k == null) { return false; }
        for (String n : k) { if (!n.equals("gamedata.tmp") && !n.equals("gamedata")) { return true; } }
        return false;
    }

    /* the folder in use now: the root one once the launcher has moved the old data there (Launcher.migrate) */
    static File current(Context c) {
        if (canUseRoot(c) && !hasContent(app(c))) {
            File r = root();
            r.mkdirs();
            if (r.isDirectory() && r.canWrite()) {
                File nm = new File(r, ".nomedia"); // the textures and screenshots kept out of the gallery
                try { if (!nm.exists()) { nm.createNewFile(); } } catch (Exception e) { /* only cosmetic */ }
                return r;
            }
        }
        File d = app(c);
        d.mkdirs();
        return d;
    }
}

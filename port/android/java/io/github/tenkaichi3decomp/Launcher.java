/*
 * The Android app's launcher, in the colours and shapes of the game's own settings window (port/src/gs/ui.cpp,
 * Dear ImGui's dark style with the orange accent): a side bar (Play, Settings, Mods, About) and a page beside it.
 * In the phone's language (Str.java). First, what the phone needs before the game can start: the data folder at
 * the root of the storage (DataDir.java) and the game's data unpacked from the player's own disc image.
 *
 * Settings are the game's own (bt3_settings.txt, port/src/plat_settings.c), changed here before it starts; a few
 * belong to the launcher (launcher.txt: Discord status, compatibility mode, sustained performance, language), and
 * the mods to mods.txt (Mods.java). GameActivity reads them when the game starts.
 *
 * The game runs in a process of its own (":game" in the manifest): its image is mapped at a fixed address
 * (port/android/native/loader.c), which a second start in the same process could not do again. A game left open is
 * brought back as it was, unless something was changed here: then it is ended and started again.
 * Built in code: the APK is made without a resource compiler (port/android/make_apk.py).
 */
package io.github.tenkaichi3decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.util.ArrayList;
import java.util.List;

public class Launcher extends Activity {
    // the colours of the game's settings window (ui.cpp style()) and of the PC setup window (setup.cpp)
    static final int BG = 0xFF0C0D12, WINDOW = 0xF5121419, FRAME = 0xFF262B3A, FRAME_HI = 0xFF323A4E, BUTTON = 0xFF2B3345,
            ACCENT = 0xFFF58C21, ACCENT_DIM = 0x66F58C21, TEXT = 0xFFE8E8EA, DIM = 0xFF8F95A3, OK = 0xFF7FD28A, BAD = 0xFFE87A6A,
            SEP = 0xFF22273A;
    static final int REQ_ISO = 1, REQ_STORAGE = 2, REQ_MOD = 3;
    static final int HOME = 0, SETTINGS = 1, MODS = 2, ABOUT = 3;
    static final String GAME = "Dragon Ball Z: Budokai Tenkaichi 3";

    File mDir;                 // the data folder in use (DataDir.current)
    KV mSet, mLaunch;          // bt3_settings.txt, launcher.txt
    Mods mMods;
    FrameLayout mRoot;
    LinearLayout mWin;         // the setup pages' window, or the page beside the side bar
    LinearLayout mSide;
    int mSection = HOME, mTab;
    boolean mDirty;            // something changed since the game was started: a game left open is started again
    String mGpu;
    Updater.Release mUpdate;   // a newer release found on GitHub (Updater.java)
    boolean mUpdChecked;       // asked once per opening of the launcher
    String mUpdState;          // while downloading / installing: what is shown in the card

    int dp(float x) { return (int) (x * getResources().getDisplayMetrics().density + 0.5f); }

    boolean hasData() { return DataDir.hasGame(DataDir.gameData(this)); }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);
        mRoot = new FrameLayout(this);
        mRoot.setBackgroundColor(BG);
        setContentView(mRoot);
        start();
    }

    @Override
    protected void onResume() {
        super.onResume();
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        if (mSide != null) { section(mSection); } // (a game running or not changes Play / Continue)
    }

    @Override
    public void onBackPressed() {
        if (mSide != null && mSection != HOME) { section(HOME); } else { super.onBackPressed(); }
    }

    /* what the launcher does on opening, and again after each setup step */
    void start() {
        mLaunch = new KV(new File(DataDir.current(this), "launcher.txt"));
        Str.use(mLaunch.str("lang", "auto"));
        // the data folder at the root of the storage (DataDir.java): asked for once, until it is given or refused
        if (!DataDir.canUseRoot(this) && !new File(getFilesDir(), "root_refused").exists()) {
            pageStorage();
            return;
        }
        File old = DataDir.app(this);
        if (DataDir.canUseRoot(this) && DataDir.hasContent(old)) { // the files of 1.1.3 and before: moved over first
            File to = DataDir.root();
            to.mkdirs();
            migrate(old, to);
            return;
        }
        mDir = DataDir.current(this);
        mLaunch = new KV(new File(mDir, "launcher.txt"));
        Str.use(mLaunch.str("lang", "auto"));
        mSet = new KV(new File(mDir, "bt3_settings.txt"));
        mMods = new Mods(mDir);
        mMods.dir.mkdirs();
        new File(mDir, "textures").mkdirs();
        if (!hasData()) {
            pagePick(null);
            return;
        }
        gpu();
        shell();
        if (mLaunch.on("update_check", true) && !mUpdChecked) { updateCheck(false); }
    }

    /* the graphics chip, once (a throwaway OpenGL ES context, Gpu.java); on the first start its profile is applied */
    void gpu() {
        mGpu = mLaunch.str("gpu", null);
        if (mGpu != null) { return; }
        new Thread(() -> {
            final String g = Gpu.query();
            runOnUiThread(() -> {
                mGpu = g;
                mLaunch.setStr("gpu", g);
                if (!mSet.has("scale")) { preset(Gpu.tier(g)); }
                if (Gpu.wantsSafe(g) && !mLaunch.has("gpu_safe")) { mLaunch.set("gpu_safe", true); }
                if (mSide != null) { section(mSection); }
            });
        }).start();
    }

    // ---- the shapes of ui.cpp's window
    static GradientDrawable rounded(int color, float r) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(r);
        return g;
    }

    static GradientDrawable outlined(int color, int line, int width, float r) {
        GradientDrawable g = rounded(color, r);
        g.setStroke(width, line);
        return g;
    }

    TextView text(String s, float sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setLineSpacing(0, 1.15f);
        return t;
    }

    android.graphics.Bitmap mBanner;

    android.graphics.Bitmap banner() {
        if (mBanner == null) {
            try (InputStream in = getAssets().open("banner.png")) { // the Dragon Rage banner (assets/banner.png)
                mBanner = android.graphics.BitmapFactory.decodeStream(in);
            } catch (IOException e) { /* the title as text */ }
        }
        return mBanner;
    }

    /* the setup pages: one window in the middle, as the PC setup */
    void centered() {
        mRoot.removeAllViews();
        mSide = null;
        ScrollView sv = new ScrollView(this);
        sv.setFillViewport(true);
        FrameLayout holder = new FrameLayout(this);
        mWin = new LinearLayout(this);
        mWin.setOrientation(LinearLayout.VERTICAL);
        mWin.setPadding(dp(26), dp(22), dp(26), dp(20));
        mWin.setBackground(rounded(WINDOW, dp(14)));
        int w = Math.min(dp(620), getResources().getDisplayMetrics().widthPixels - dp(32));
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, -2, Gravity.CENTER);
        lp.topMargin = lp.bottomMargin = dp(12);
        holder.addView(mWin, lp);
        sv.addView(holder, new FrameLayout.LayoutParams(-1, -2));
        mRoot.addView(sv, new FrameLayout.LayoutParams(-1, -1));
    }

    void heading(String title, String sub) {
        if (mSide == null) { centered(); } else { mWin.removeAllViews(); }
        if (banner() != null && mSide == null) {
            android.widget.ImageView iv = new android.widget.ImageView(this);
            iv.setImageBitmap(banner());
            iv.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
            iv.setContentDescription(title);
            mWin.addView(iv, new LinearLayout.LayoutParams(-1, dp(110)));
        } else {
            TextView h = text(title, 24, TEXT);
            h.setTypeface(Typeface.DEFAULT_BOLD);
            mWin.addView(h);
        }
        if (sub != null) {
            TextView s = text(sub, 14, DIM);
            s.setPadding(0, dp(4), 0, dp(12));
            mWin.addView(s);
        }
    }

    TextView bigButton(String label, boolean primary) {
        TextView t = text(label, 15, primary ? 0xFF140F08 : TEXT);
        t.setGravity(Gravity.CENTER);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setBackground(rounded(primary ? 0xE6F58C21 : BUTTON, dp(6)));
        t.setPadding(dp(14), 0, dp(14), 0);
        t.setClickable(true);
        t.setFocusable(true);
        t.setOnFocusChangeListener((v, f) -> v.setAlpha(f ? 0.8f : 1f));
        return t;
    }

    /* buttons on one line at the bottom right */
    void bottomRow(TextView... buttons) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.END);
        r.setPadding(0, dp(18), 0, 0);
        for (TextView b : buttons) {
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, dp(42));
            p.leftMargin = dp(10);
            b.setMinWidth(dp(130));
            r.addView(b, p);
        }
        mWin.addView(r);
        if (buttons.length > 0) { buttons[buttons.length - 1].requestFocus(); }
    }

    void step(String label, int state) { // 0 waiting, 1 running, 2 done, 3 failed
        TextView t = text((state == 2 ? "✓  " : state == 3 ? "✗  " : state == 1 ? "›  " : "·  ") + label, 14,
                state == 2 ? OK : state == 3 ? BAD : state == 1 ? ACCENT : DIM);
        t.setPadding(dp(4), dp(3), 0, dp(3));
        mWin.addView(t);
    }

    ProgressBar bar() {
        final ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        GradientDrawable track = rounded(FRAME, dp(4)), fill = rounded(ACCENT, dp(4));
        bar.setProgressDrawable(new LayerDrawable(new android.graphics.drawable.Drawable[] {track, new ClipDrawable(fill, Gravity.START, ClipDrawable.HORIZONTAL)}));
        ((LayerDrawable) bar.getProgressDrawable()).setId(0, android.R.id.background);
        ((LayerDrawable) bar.getProgressDrawable()).setId(1, android.R.id.progress);
        mWin.addView(bar, new LinearLayout.LayoutParams(-1, dp(10)));
        return bar;
    }

    // ---- the launcher: side bar and page
    void shell() {
        mRoot.removeAllViews();
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(dp(12), dp(10), dp(12), dp(10));
        mSide = new LinearLayout(this);
        mSide.setOrientation(LinearLayout.VERTICAL);
        row.addView(mSide, new LinearLayout.LayoutParams(dp(200), -1));
        ScrollView sv = new ScrollView(this);
        sv.setBackground(rounded(WINDOW, dp(14)));
        sv.setFillViewport(true);
        mWin = new LinearLayout(this);
        mWin.setOrientation(LinearLayout.VERTICAL);
        mWin.setPadding(dp(22), dp(16), dp(22), dp(18));
        sv.addView(mWin, new FrameLayout.LayoutParams(-1, -2));
        LinearLayout.LayoutParams cp = new LinearLayout.LayoutParams(0, -1, 1f);
        cp.leftMargin = dp(12);
        row.addView(sv, cp);
        mRoot.addView(row, new FrameLayout.LayoutParams(-1, -1));
        section(mSection);
    }

    boolean gameOpen() {
        android.app.ActivityManager am = (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
        for (android.app.ActivityManager.AppTask t : am.getAppTasks()) {
            try {
                android.app.ActivityManager.RecentTaskInfo ti = t.getTaskInfo();
                if (ti.baseActivity != null && GameActivity.class.getName().equals(ti.baseActivity.getClassName())) { return true; }
            } catch (RuntimeException e) { /* gone meanwhile */ }
        }
        return false;
    }

    void side() {
        mSide.removeAllViews();
        if (banner() != null) {
            android.widget.ImageView iv = new android.widget.ImageView(this);
            iv.setImageBitmap(banner());
            iv.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
            mSide.addView(iv, new LinearLayout.LayoutParams(-1, dp(64)));
        }
        TextView play = bigButton("▶  " + Str.t(gameOpen() && !mDirty ? "continue" : "play"), true);
        play.setTextSize(17);
        play.setOnClickListener(v -> play());
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(-1, dp(50));
        pp.topMargin = dp(8);
        pp.bottomMargin = dp(10);
        mSide.addView(play, pp);
        nav("⌂  " + Str.t("home") + (mUpdate != null ? "  ⬆" : ""), HOME);
        nav("⚙  " + Str.t("settings"), SETTINGS);
        nav("✚  " + Str.t("mods") + (mMods.on.isEmpty() ? "" : "  (" + mMods.on.size() + ")"), MODS);
        nav("ⓘ  " + Str.t("about"), ABOUT);
        View gap = new View(this);
        mSide.addView(gap, new LinearLayout.LayoutParams(-1, 0, 1f));
        TextView foot = text("Dragon Rage " + version() + "\n" + (mGpu == null ? Str.t("st_gpu_wait") : Gpu.label(mGpu)), 11, DIM);
        foot.setPadding(dp(6), 0, 0, 0);
        mSide.addView(foot);
    }

    void nav(String label, final int s) {
        TextView t = text(label, 15, s == mSection ? TEXT : DIM);
        t.setGravity(Gravity.CENTER_VERTICAL);
        t.setPadding(dp(14), 0, dp(10), 0);
        t.setBackground(s == mSection ? outlined(FRAME_HI, ACCENT_DIM, dp(1), dp(6)) : rounded(0, dp(6)));
        if (s == mSection) { t.setTypeface(Typeface.DEFAULT_BOLD); }
        t.setClickable(true);
        t.setFocusable(true);
        t.setOnClickListener(v -> section(s));
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, dp(42));
        p.bottomMargin = dp(4);
        mSide.addView(t, p);
    }

    /* this build has the Discord status (its library is in the APK) */
    boolean hasDiscord() {
        return new File(getApplicationInfo().nativeLibraryDir, "libbt3discord.so").exists();
    }

    String version() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception e) { return ""; }
    }

    void section(int s) {
        mSection = s;
        side();
        mWin.removeAllViews();
        if (s == HOME) { pageHome(); } else if (s == SETTINGS) { pageSettings(); } else if (s == MODS) { pageMods(); } else { pageAbout(); }
    }

    void title(String t, String sub) {
        TextView h = text(t, 22, TEXT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        mWin.addView(h);
        if (sub != null) {
            TextView s = text(sub, 13, DIM);
            s.setPadding(0, dp(2), 0, dp(8));
            mWin.addView(s);
        }
    }

    /* ImGui's SeparatorText: a label and a line */
    void separator(String label) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(0, dp(14), 0, dp(4));
        TextView t = text(label, 13, ACCENT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        r.addView(t);
        View line = new View(this);
        line.setBackgroundColor(SEP);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(1), 1f);
        lp.leftMargin = dp(10);
        r.addView(line, lp);
        mWin.addView(r);
    }

    void info(String k, String v, int color) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setPadding(0, dp(5), 0, dp(5));
        TextView a = text(k, 14, DIM);
        r.addView(a, new LinearLayout.LayoutParams(dp(170), -2));
        TextView b = text(v, 14, color);
        r.addView(b, new LinearLayout.LayoutParams(0, -2, 1f));
        mWin.addView(r);
    }

    // ---- pages
    void pageHome() {
        title(Str.t("ready"), Str.t("ready_sub"));
        if (mUpdate != null) { updateCard(); }
        separator(GAME);
        info(Str.t("st_data"), "✓  " + Str.t("st_data_ok"), OK);
        info(Str.t("st_gpu"), mGpu == null ? Str.t("st_gpu_wait") : Gpu.label(mGpu), TEXT);
        if (mGpu != null) { info(Str.t("st_profile"), presetName(Gpu.tier(mGpu)), ACCENT); }
        info(Str.t("st_folder"), mDir.getPath(), TEXT);
        info(Str.t("st_mods"), mMods.on.isEmpty() ? Str.t("st_none") : TextUtils.join(", ", mMods.on), TEXT);
        TextView hint = text(Str.t("hint_ingame"), 13, DIM);
        hint.setPadding(0, dp(12), 0, 0);
        mWin.addView(hint);
        TextView note = text(Str.t("restart_note"), 12, DIM);
        note.setPadding(0, dp(6), 0, 0);
        mWin.addView(note);
        TextView reinstall = bigButton(Str.t("reinstall"), false);
        reinstall.setOnClickListener(v -> pagePick(null));
        TextView go = bigButton(Str.t(gameOpen() && !mDirty ? "continue" : "play"), true);
        go.setOnClickListener(v -> play());
        bottomRow(reinstall, go);
    }

    // ---- settings: rows as ui.cpp draws them
    interface IntSet { void to(int v); }

    void changed() {
        mDirty = true;
        side();
    }

    LinearLayout rowBase(String label, String tip) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.VERTICAL);
        r.setPadding(0, dp(7), 0, dp(7));
        TextView t = text(label, 15, TEXT);
        r.addView(t);
        if (tip != null) {
            TextView d = text(tip, 12, DIM);
            d.setPadding(0, dp(1), 0, dp(4));
            r.addView(d);
        }
        mWin.addView(r);
        return r;
    }

    /* an ImGui checkbox: a frame with the accent's tick, the label beside it */
    void check(String label, String tip, boolean value, final IntSet set) {
        final LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(0, dp(7), 0, dp(7));
        final TextView box = text(value ? "✓" : "", 16, ACCENT);
        box.setGravity(Gravity.CENTER);
        box.setTypeface(Typeface.DEFAULT_BOLD);
        box.setBackground(rounded(FRAME, dp(4)));
        r.addView(box, new LinearLayout.LayoutParams(dp(26), dp(26)));
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(12), 0, 0, 0);
        col.addView(text(label, 15, TEXT));
        if (tip != null) { col.addView(text(tip, 12, DIM)); }
        r.addView(col, new LinearLayout.LayoutParams(0, -2, 1f));
        final boolean[] on = {value};
        r.setClickable(true);
        r.setFocusable(true);
        r.setOnClickListener(v -> {
            on[0] = !on[0];
            box.setText(on[0] ? "✓" : "");
            set.to(on[0] ? 1 : 0);
            changed();
        });
        mWin.addView(r);
    }

    /* one choice of a few: a row of chips (a combo's list, laid out) */
    void choice(String label, String tip, final String[] names, int sel, final IntSet set) {
        LinearLayout r = rowBase(label, tip);
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        final LinearLayout chips = new LinearLayout(this);
        chips.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < names.length; i++) {
            final int k = i;
            TextView c = text(names[i], 14, i == sel ? 0xFF140F08 : TEXT);
            c.setGravity(Gravity.CENTER);
            c.setPadding(dp(14), 0, dp(14), 0);
            c.setBackground(i == sel ? rounded(0xE6F58C21, dp(5)) : rounded(FRAME, dp(5)));
            if (i == sel) { c.setTypeface(Typeface.DEFAULT_BOLD); }
            c.setClickable(true);
            c.setFocusable(true);
            c.setOnClickListener(v -> {
                for (int j = 0; j < chips.getChildCount(); j++) {
                    TextView o = (TextView) chips.getChildAt(j);
                    o.setTextColor(j == k ? 0xFF140F08 : TEXT);
                    o.setTypeface(j == k ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
                    o.setBackground(j == k ? rounded(0xE6F58C21, dp(5)) : rounded(FRAME, dp(5)));
                }
                set.to(k);
                changed();
            });
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, dp(36));
            p.rightMargin = dp(6);
            chips.addView(c, p);
        }
        hs.addView(chips);
        r.addView(hs);
    }

    /* an ImGui slider: the frame filled to the value, the value written on it */
    void slider(String label, final int min, final int max, int value, final String unit, final IntSet set) {
        LinearLayout r = rowBase(label, null);
        LinearLayout line = new LinearLayout(this);
        line.setOrientation(LinearLayout.HORIZONTAL);
        line.setGravity(Gravity.CENTER_VERTICAL);
        SeekBar sb = new SeekBar(this);
        sb.setMax(max - min);
        sb.setProgress(Math.max(0, Math.min(max - min, value - min)));
        GradientDrawable track = rounded(FRAME, dp(4)), fill = rounded(ACCENT_DIM, dp(4));
        LayerDrawable ld = new LayerDrawable(new android.graphics.drawable.Drawable[] {track, new ClipDrawable(fill, Gravity.START, ClipDrawable.HORIZONTAL)});
        ld.setId(0, android.R.id.background);
        ld.setId(1, android.R.id.progress);
        sb.setProgressDrawable(ld);
        GradientDrawable thumb = rounded(ACCENT, dp(3));
        thumb.setSize(dp(12), dp(26));
        sb.setThumb(thumb);
        sb.setSplitTrack(false);
        sb.setPadding(dp(8), 0, dp(8), 0);
        final TextView val = text(value + unit, 14, TEXT);
        val.setGravity(Gravity.END);
        line.addView(sb, new LinearLayout.LayoutParams(0, dp(30), 1f));
        line.addView(val, new LinearLayout.LayoutParams(dp(64), -2));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) { val.setText((p + min) + unit); }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {
                set.to(s.getProgress() + min);
                changed();
            }
        });
        r.addView(line);
    }

    void tabs(String[] names) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        for (int i = 0; i < names.length; i++) {
            final int k = i;
            TextView t = text(names[i], 14, i == mTab ? TEXT : DIM);
            t.setGravity(Gravity.CENTER);
            t.setPadding(dp(16), 0, dp(16), 0);
            t.setBackground(i == mTab ? outlined(FRAME_HI, ACCENT, dp(1), dp(6)) : rounded(BUTTON, dp(6)));
            if (i == mTab) { t.setTypeface(Typeface.DEFAULT_BOLD); }
            t.setClickable(true);
            t.setFocusable(true);
            t.setOnClickListener(v -> { mTab = k; section(SETTINGS); });
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, dp(38));
            p.rightMargin = dp(6);
            r.addView(t, p);
        }
        hs.addView(r);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = dp(6);
        mWin.addView(hs, lp);
    }

    // the profiles: resolution and smooth motion at once (Performance tab; the first start: the chip's, Gpu.java)
    static final int[][] PRESET = {{1, 0}, {2, 0}, {3, 1}}; // scale, interp

    String presetName(int p) { return Str.t(p == 0 ? "p_battery" : p == 1 ? "p_balanced" : "p_quality"); }

    void preset(int p) {
        mSet.set("scale", PRESET[p][0]);
        mSet.set("interp", PRESET[p][1]);
        mSet.set("frame_skip", 1);
    }

    int presetNow() {
        for (int p = 0; p < PRESET.length; p++) {
            if (mSet.get("scale", 2) == PRESET[p][0] && mSet.get("interp", 0) == PRESET[p][1]) { return p; }
        }
        return 3;
    }

    void pageSettings() {
        title(Str.t("settings"), Str.t("restart_note"));
        tabs(new String[] {Str.t("tab_gfx"), Str.t("tab_perf"), Str.t("tab_ctl"), Str.t("tab_audio"), Str.t("tab_game")});
        final KV s = mSet;
        if (mTab == 0) {
            String[] res = new String[8];
            for (int i = 0; i < 8; i++) { res[i] = (i + 1) + "x"; }
            choice(Str.t("res"), Str.t("res_tip") + (mGpu != null ? "  " + Str.f("preset_tip", presetName(Gpu.tier(mGpu))) : ""), res,
                    Math.max(0, Math.min(7, s.get("scale", 2) - 1)), v -> s.set("scale", v + 1));
            final int[] milli = {0, 1333, 1600, 1778, 2389};
            int a = 0;
            if (!s.on("aspect_screen", true)) {
                int m = s.get("aspect_milli", 1333);
                a = 1;
                for (int i = 2; i < milli.length; i++) { if (Math.abs(milli[i] - m) < Math.abs(milli[a] - m)) { a = i; } }
            }
            choice(Str.t("aspect"), Str.t("aspect_tip"), new String[] {Str.t("whole"), "4:3", "16:10", "16:9", "21:9"}, a, v -> {
                s.set("aspect_screen", v == 0);
                if (v > 0) { s.set("aspect_milli", milli[v]); }
            });
            choice(Str.t("f2d"), Str.t("f2d_tip"), new String[] {Str.t("sharp"), Str.t("smooth")}, s.on("smooth_2d", true) ? 1 : 0,
                    v -> s.set("smooth_2d", v));
            check(Str.t("texpack"), Str.f("texpack_tip", new File(mDir, "textures").getPath()), s.on("texture_pack", true),
                    v -> s.set("texture_pack", v));
            choice(Str.t("hud"), null, new String[] {Str.t("hud0"), Str.t("hud1"), Str.t("hud2")}, Math.max(0, Math.min(2, s.get("hud_layout", 0))),
                    v -> s.set("hud_layout", v));
            check(Str.t("menus43"), Str.t("menus43_tip"), s.on("menus_43", false), v -> s.set("menus_43", v));
            separator(Str.t("effects"));
            for (int i = 0; i < 6; i++) {
                final int bit = 1 << i;
                check(Str.t("fx" + i), null, (s.get("fx_off", 0) & bit) == 0, v -> {
                    int off = s.get("fx_off", 0);
                    s.set("fx_off", v != 0 ? off & ~bit : off | bit);
                });
            }
            slider(Str.t("glow"), 0, 100, s.get("glow", 60), "%", v -> s.set("glow", v));
        } else if (mTab == 1) {
            String rec = mGpu != null ? presetName(Gpu.tier(mGpu)) : "?";
            choice(Str.t("preset"), Str.f("preset_tip", rec),
                    new String[] {Str.t("p_battery"), Str.t("p_balanced"), Str.t("p_quality"), Str.t("p_custom")}, presetNow(), v -> {
                        if (v < 3) { preset(v); }
                        section(SETTINGS);
                    });
            choice(Str.t("fps"), Str.t("fps_tip"), new String[] {Str.t("fps0"), "60", "120", "240"},
                    Math.max(0, Math.min(3, s.get("interp", 0))), v -> s.set("interp", v));
            check(Str.t("skip"), Str.t("skip_tip"), s.on("frame_skip", true), v -> s.set("frame_skip", v));
            check(Str.t("vsync"), Str.t("vsync_tip"), s.on("vsync", false), v -> s.set("vsync", v));
            check(Str.t("meter"), null, s.on("meter", true), v -> s.set("meter", v));
            check(Str.t("fastz"), Str.t("fastz_tip"), mLaunch.on("fast_depth", false), v -> mLaunch.set("fast_depth", v));
            check(Str.t("sustained"), Str.t("sustained_tip"), mLaunch.on("sustained", false), v -> mLaunch.set("sustained", v));
            check(Str.t("safe"), Str.t("safe_tip"), mLaunch.on("gpu_safe", false), v -> mLaunch.set("gpu_safe", v));
        } else if (mTab == 2) {
            choice(Str.t("touch"), null, new String[] {Str.t("touch0"), Str.t("touch1"), Str.t("touch2")},
                    Math.max(0, Math.min(2, s.get("touch", 1))), v -> s.set("touch", v));
            slider(Str.t("touch_size"), 50, 200, s.get("touch_size", 100), "%", v -> s.set("touch_size", v));
            slider(Str.t("touch_alpha"), 15, 100, s.get("touch_alpha", 70), "%", v -> s.set("touch_alpha", v));
            TextView h = text(Str.t("touch_edit"), 12, DIM);
            h.setPadding(0, dp(8), 0, 0);
            mWin.addView(h);
        } else if (mTab == 3) {
            slider(Str.t("music"), 0, 200, s.get("music", 100), "%", v -> s.set("music", v));
            int fx = s.get("effects", 100);
            slider(Str.t("sfx"), 0, 200, fx, "%", v -> s.set("effects", v));
            slider(Str.t("voices"), 0, 200, s.get("voices", fx), "%", v -> s.set("voices", v));
            slider(Str.t("stage"), 0, 200, s.get("stage_sounds", fx), "%", v -> s.set("stage_sounds", v));
            check(Str.t("follows"), null, s.on("music_follows_speed", false), v -> s.set("music_follows_speed", v));
        } else {
            check(Str.t("skip_logos"), null, s.on("skip_logos", false), v -> s.set("skip_logos", v));
            check(Str.t("skip_intro"), null, s.on("skip_intro", false), v -> s.set("skip_intro", v));
            check(Str.t("skip_copy"), null, s.on("skip_copyright", false), v -> s.set("skip_copyright", v));
            check(Str.t("upd_check"), Str.t("upd_check_tip"), mLaunch.on("update_check", true), v -> mLaunch.set("update_check", v));
            if (hasDiscord()) { // (only in builds made with the Discord status: port/android/build_apk.sh, HXDISCORD_DIR)
                check(Str.t("discord"), Str.t("discord_tip"), mLaunch.on("discord", true), v -> mLaunch.set("discord", v));
            }
            final String[] codes = {"auto", "en", "pt", "es"};
            String cur = mLaunch.str("lang", "auto");
            int sel = 0;
            for (int i = 0; i < codes.length; i++) { if (codes[i].equals(cur)) { sel = i; } }
            choice(Str.t("lang"), null, new String[] {Str.t("lang_auto"), "English", "Português", "Español"}, sel, v -> {
                mLaunch.setStr("lang", codes[v]);
                Str.use(codes[v]);
                section(SETTINGS);
            });
        }
    }

    // ---- mods
    void pageMods() {
        title(Str.t("mods"), Str.f("mods_sub", mMods.dir.getPath()));
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        TextView imp = bigButton("＋  " + Str.t("mods_import"), true);
        imp.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
            startActivityForResult(i, REQ_MOD);
        });
        TextView ref = bigButton("⟳  " + Str.t("mods_refresh"), false);
        ref.setOnClickListener(v -> { mMods = new Mods(mDir); section(MODS); });
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-2, dp(40));
        p.rightMargin = dp(8);
        bar.addView(imp, p);
        bar.addView(ref, new LinearLayout.LayoutParams(-2, dp(40)));
        bar.setPadding(0, dp(4), 0, dp(6));
        mWin.addView(bar);
        TextView warn = text(Str.t("mods_online"), 12, DIM);
        mWin.addView(warn);
        separator(Str.t("mods"));
        List<String> all = mMods.all();
        if (all.isEmpty()) {
            TextView e = text(Str.t("mods_empty"), 14, DIM);
            e.setPadding(0, dp(8), 0, 0);
            mWin.addView(e);
        }
        for (final String name : all) { modRow(name); }
    }

    void modRow(final String name) {
        final boolean on = mMods.on.contains(name);
        final int at = mMods.on.indexOf(name);
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(10), dp(8), dp(8), dp(8));
        r.setBackground(rounded(on ? FRAME : 0x55262B3A, dp(8)));
        TextView box = text(on ? "✓" : "", 16, ACCENT);
        box.setGravity(Gravity.CENTER);
        box.setTypeface(Typeface.DEFAULT_BOLD);
        box.setBackground(rounded(on ? FRAME_HI : FRAME, dp(4)));
        box.setClickable(true);
        box.setOnClickListener(v -> { mMods.set(name, !on); changed(); section(MODS); });
        r.addView(box, new LinearLayout.LayoutParams(dp(28), dp(28)));
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(12), 0, dp(6), 0);
        TextView n = text((on ? (at + 1) + ".  " : "") + name, 15, on ? TEXT : DIM);
        n.setTypeface(on ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        col.addView(n);
        final TextView meta = text("…", 12, DIM);
        col.addView(meta);
        new Thread(() -> {
            final long[] c = Mods.count(new File(mMods.dir, name));
            runOnUiThread(() -> meta.setText(Str.f("mods_files", c[0], Mods.size(c[1]))));
        }).start();
        r.addView(col, new LinearLayout.LayoutParams(0, -2, 1f));
        if (on) {
            r.addView(smallButton("▲", at > 0, v -> { mMods.move(name, -1); changed(); section(MODS); }));
            r.addView(smallButton("▼", at < mMods.on.size() - 1, v -> { mMods.move(name, 1); changed(); section(MODS); }));
        }
        r.addView(smallButton("✕", true, v -> new AlertDialog.Builder(this)
                .setMessage(Str.f("mods_delete", name))
                .setPositiveButton(Str.t("yes"), (d, w) -> { mMods.delete(name); changed(); section(MODS); })
                .setNegativeButton(Str.t("no"), null)
                .show()));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(6);
        mWin.addView(r, lp);
    }

    TextView smallButton(String label, boolean enabled, View.OnClickListener l) {
        TextView t = text(label, 15, enabled ? TEXT : 0x44E8E8EA);
        t.setGravity(Gravity.CENTER);
        t.setBackground(rounded(BUTTON, dp(5)));
        if (enabled) {
            t.setClickable(true);
            t.setOnClickListener(l);
        }
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(dp(38), dp(34));
        p.leftMargin = dp(6);
        t.setLayoutParams(p);
        return t;
    }

    void importMod(final Uri uri) {
        String name = "mod";
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) { name = c.getString(0); }
        } catch (Exception e) { /* "mod" */ }
        final String fname = name;
        mWin.removeAllViews();
        title(Str.t("mods"), null);
        final TextView what = text(Str.f("mods_importing", fname), 14, DIM);
        mWin.addView(what);
        final ProgressBar pb = new ProgressBar(this);
        mWin.addView(pb, new LinearLayout.LayoutParams(dp(40), dp(40)));
        new Thread(() -> {
            String err = null;
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                mMods.importZip(in, fname);
            } catch (Exception e) {
                err = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String fe = err;
            runOnUiThread(() -> {
                changed();
                section(MODS);
                if (fe != null) {
                    TextView t = text(fe, 14, BAD);
                    t.setPadding(0, dp(8), 0, 0);
                    mWin.addView(t, 2);
                }
            });
        }).start();
    }

    // ---- about
    void pageAbout() {
        title("Dragon Rage", Str.t("about_sub"));
        info(Str.t("version"), version(), TEXT);
        TextView chk = text("⟳  " + Str.t("upd_check_now"), 14, ACCENT);
        chk.setPadding(0, dp(4), 0, dp(4));
        chk.setClickable(true);
        chk.setOnClickListener(v -> { chk.setText(Str.t("upd_checking")); updateCheck(true); });
        mWin.addView(chk);
        info(Str.t("device"), android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL + " · Android " + android.os.Build.VERSION.RELEASE, TEXT);
        info(Str.t("st_gpu"), mGpu == null ? Str.t("st_gpu_wait") : Gpu.label(mGpu), TEXT);
        info(Str.t("st_folder"), mDir.getPath(), TEXT);
        info(Str.t("st_data"), DataDir.gameData(this).getPath(), DIM);
        if (!DataDir.canUseRoot(this)) { // refused before: can still be turned on from here
            TextView use = text(Str.t("use_root"), 14, ACCENT);
            use.setPadding(0, dp(8), 0, 0);
            use.setClickable(true);
            use.setOnClickListener(v -> { new File(getFilesDir(), "root_refused").delete(); askStorage(); });
            mWin.addView(use);
        }
        separator("⚠");
        mWin.addView(text(Str.t("about_legal"), 13, DIM));
        TextView c = text("Tenkaichi3Decomp · github.com/z3xox/Tenkaichi3Decomp\nSDL3 · Dear ImGui · Discord Social SDK", 12, DIM);
        c.setPadding(0, dp(12), 0, 0);
        mWin.addView(c);
    }

    // ---- updates (Updater.java)
    long versionCode() {
        try {
            android.content.pm.PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            return android.os.Build.VERSION.SDK_INT >= 28 ? pi.getLongVersionCode() : pi.versionCode;
        } catch (Exception e) {
            return Long.MAX_VALUE;
        }
    }

    /* asks GitHub in the background; `told`: the player asked (About), so "nothing newer" is said too */
    void updateCheck(final boolean told) {
        mUpdChecked = true;
        final long mine = versionCode();
        new Thread(() -> {
            final Updater.Release r = Updater.check(mine);
            runOnUiThread(() -> {
                if (r != null && (told || !r.tag.equals(mLaunch.str("update_skip", "")))) {
                    mUpdate = r;
                    if (mSide != null) { section(told ? HOME : mSection); }
                } else if (told && mSide != null) {
                    section(ABOUT);
                    TextView t = text(Str.t("upd_none"), 14, OK);
                    mWin.addView(t, 2);
                }
            });
        }).start();
    }

    void updateCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(12), dp(16), dp(12));
        card.setBackground(outlined(FRAME, ACCENT, dp(1), dp(10)));
        TextView h = text("⬆  " + Str.f("upd_title", mUpdate.tag), 16, ACCENT);
        h.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(h);
        String notes = mUpdate.notes.replaceAll("(?m)^#+\\s*", "").replaceAll("[*`>]", "").trim();
        if (notes.length() > 280) { notes = notes.substring(0, 280).trim() + "…"; }
        if (notes.length() > 0) {
            TextView n = text(notes, 12, DIM);
            n.setPadding(0, dp(4), 0, dp(6));
            card.addView(n);
        }
        if (mUpdState != null) { // downloading or installing
            TextView st = text(mUpdState, 13, TEXT);
            card.addView(st);
            mUpdStatus = st;
        } else {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setPadding(0, dp(6), 0, 0);
            TextView go = bigButton(Str.t("upd_now"), true);
            go.setOnClickListener(v -> updateRun());
            TextView later = bigButton(Str.t("upd_later"), false);
            later.setOnClickListener(v -> { mUpdate = null; section(HOME); });
            TextView skip = bigButton(Str.t("upd_skip"), false);
            skip.setOnClickListener(v -> { mLaunch.setStr("update_skip", mUpdate.tag); mUpdate = null; section(HOME); });
            for (TextView b : new TextView[] {go, later, skip}) {
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, dp(38));
                lp.rightMargin = dp(8);
                row.addView(b, lp);
            }
            card.addView(row);
        }
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = dp(6);
        lp.bottomMargin = dp(4);
        mWin.addView(card, lp);
    }

    TextView mUpdStatus;

    void updateStatus(String s) {
        mUpdState = s;
        if (mUpdStatus != null && mUpdStatus.isAttachedToWindow()) { mUpdStatus.setText(s); } else if (mSection == HOME && mSide != null) { section(HOME); }
    }

    void updateRun() {
        final Updater.Release r = mUpdate;
        updateStatus(Str.f("upd_downloading", r.tag, 0));
        section(HOME);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(() -> {
            try {
                final int[] last = {-1};
                File apk = Updater.download(this, r, (done, total) -> {
                    int pct = total > 0 ? (int) (done * 100 / total) : 0;
                    if (pct != last[0]) {
                        last[0] = pct;
                        runOnUiThread(() -> updateStatus(Str.f("upd_downloading", r.tag, pct)));
                    }
                });
                runOnUiThread(() -> updateStatus(Str.t("upd_installing")));
                Updater.install(this, apk, failure -> runOnUiThread(() -> {
                    if (failure != null) { updateFailed(failure); }
                }));
            } catch (Exception e) {
                final String m = e.getMessage() != null ? e.getMessage() : e.toString();
                runOnUiThread(() -> updateFailed(m));
            }
        }).start();
    }

    void updateFailed(String m) {
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        mUpdState = null;
        if (mSide != null) { section(HOME); }
        TextView t = text(Str.f("upd_fail", m), 13, BAD);
        t.setPadding(0, dp(4), 0, 0);
        mWin.addView(t, Math.min(3, mWin.getChildCount()));
    }

    // ---- start the game
    void play() {
        android.app.ActivityManager am = (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
        String game = GameActivity.class.getName();
        if (!mDirty) { // the game still open in the background, nothing changed here: back to it as it was
            for (android.app.ActivityManager.AppTask t : am.getAppTasks()) {
                try {
                    android.app.ActivityManager.RecentTaskInfo ti = t.getTaskInfo();
                    if (ti.baseActivity != null && game.equals(ti.baseActivity.getClassName())) {
                        t.moveToFront();
                        return;
                    }
                } catch (RuntimeException e) { /* a task that went away meanwhile */ }
            }
        } else { // changed here: a game left open is closed, and starts again with the changes
            for (android.app.ActivityManager.AppTask t : am.getAppTasks()) {
                try {
                    android.app.ActivityManager.RecentTaskInfo ti = t.getTaskInfo();
                    if (ti.baseActivity != null && game.equals(ti.baseActivity.getClassName())) { t.finishAndRemoveTask(); }
                } catch (RuntimeException e) { /* gone */ }
            }
        }
        // the game's process may still be alive (Android keeps closed apps cached): the game can only start once per
        // process (loader.c), so that one is ended first and the game starts in a fresh one
        boolean killed = false;
        List<android.app.ActivityManager.RunningAppProcessInfo> procs = am.getRunningAppProcesses();
        if (procs != null) {
            for (android.app.ActivityManager.RunningAppProcessInfo p : procs) {
                if (p.processName != null && p.processName.endsWith(":game") && p.pid != android.os.Process.myPid()) {
                    android.os.Process.killProcess(p.pid);
                    killed = true;
                }
            }
        }
        mDirty = false;
        Runnable go = () -> startActivity(new Intent(this, GameActivity.class));
        if (killed) {
            new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(go, 400); // let Android see it end
        } else {
            go.run();
        }
    }

    // ---- setup: the data folder at the root of the storage
    void pageStorage() {
        heading(Str.t("folder_title"), Str.t("folder_sub"));
        mWin.addView(text(Str.f("folder_text", DataDir.NAME), 14, TEXT));
        TextView later = bigButton(Str.t("not_now"), false);
        later.setOnClickListener(v -> {
            try { new File(getFilesDir(), "root_refused").createNewFile(); } catch (IOException e) { /* asked again next time */ }
            start();
        });
        TextView allow = bigButton(Str.t("allow"), true);
        allow.setOnClickListener(v -> askStorage());
        bottomRow(later, allow);
    }

    void askStorage() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            try {
                startActivityForResult(new Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())), REQ_STORAGE);
            } catch (Exception e) { // a phone without the per-app screen: the list of all apps
                startActivityForResult(new Intent(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), REQ_STORAGE);
            }
        } else {
            requestPermissions(new String[] {android.Manifest.permission.WRITE_EXTERNAL_STORAGE,
                    android.Manifest.permission.READ_EXTERNAL_STORAGE}, REQ_STORAGE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
        super.onRequestPermissionsResult(req, perms, res);
        if (req == REQ_STORAGE) { afterStorage(); }
    }

    void afterStorage() {
        if (!DataDir.canUseRoot(this)) { // not given: the app's own folder, without asking again
            try { new File(getFilesDir(), "root_refused").createNewFile(); } catch (IOException e) { /* asked again */ }
        }
        mSide = null;
        start();
    }

    /* everything in the old folder into the new one (the game's data stays: DataDir.gameData) */
    void migrate(final File from, final File to) {
        heading(Str.t("moving"), Str.f("moving_sub", DataDir.NAME));
        final TextView what = text("", 13, DIM);
        what.setPadding(0, dp(10), 0, dp(6));
        mWin.addView(what);
        final ProgressBar bar = bar();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(() -> {
            String err = null;
            try {
                final long total = size(from) - size(new File(from, "gamedata"));
                final long[] done = {0};
                Mover m = (name, n) -> {
                    done[0] += n;
                    runOnUiThread(() -> {
                        what.setText(name);
                        bar.setProgress(total > 0 ? (int) (done[0] * 1000 / total) : 0);
                    });
                };
                File[] k = from.listFiles();
                if (k != null) {
                    for (File f : k) {
                        if (!f.getName().equals("gamedata")) { move(f, new File(to, f.getName()), m); }
                    }
                }
            } catch (Exception e) {
                err = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String fe = err;
            runOnUiThread(() -> {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                if (fe == null) {
                    start();
                } else {
                    heading(Str.t("moving"), Str.t("moving_fail"));
                    mWin.addView(text(fe, 14, BAD));
                    TextView again = bigButton(Str.t("again"), true);
                    again.setOnClickListener(v -> start());
                    bottomRow(again);
                }
            });
        }).start();
    }

    // ---- setup: the game's data from the player's disc image
    void pagePick(String err) {
        mSide = null;
        heading(GAME, Str.t("install_sub"));
        mWin.addView(text(Str.t("install_text"), 14, TEXT));
        if (err != null) {
            TextView e = text(err, 14, BAD);
            e.setPadding(0, dp(10), 0, 0);
            mWin.addView(e);
        }
        TextView pick = bigButton(Str.t("choose_iso"), true);
        pick.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_ISO);
        });
        TextView close = bigButton(Str.t(hasData() ? "back" : "close"), false);
        close.setOnClickListener(v -> { if (hasData()) { start(); } else { finish(); } });
        bottomRow(close, pick);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_STORAGE) { afterStorage(); return; }
        if (req == REQ_ISO && res == RESULT_OK && data != null && data.getData() != null) { extract(data.getData()); }
        if (req == REQ_MOD && res == RESULT_OK && data != null && data.getData() != null && mSide != null) { importMod(data.getData()); }
    }

    void extract(final Uri uri) {
        mSide = null;
        heading(Str.t("installing"), Str.t("installing_sub"));
        step(Str.t("reading"), 1);
        final TextView what = text("", 13, DIM);
        what.setPadding(0, dp(10), 0, dp(6));
        mWin.addView(what);
        final ProgressBar bar = bar();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(() -> {
            String err = null;
            try (ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                 FileInputStream in = new FileInputStream(pfd.getFileDescriptor())) {
                IsoExtract x = new IsoExtract(in.getChannel(), pfd.getStatSize(), DataDir.app(this)); // (DataDir.gameData)
                x.progress = (done, total, s) -> runOnUiThread(() -> {
                    what.setText(s);
                    bar.setProgress(total > 0 ? (int) (done * 1000 / total) : 0);
                });
                x.run();
                File old = new File(DataDir.root(), "gamedata"); // a copy at the root made by 1.1.4: replaced by this one
                if (old.isDirectory() && DataDir.hasGame(new File(DataDir.app(this), "gamedata"))) { IsoExtract.delete(old); }
            } catch (Exception e) {
                err = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String fe = err;
            runOnUiThread(() -> {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                if (fe == null) { mDirty = true; start(); } else { pagePick(fe); }
            });
        }).start();
    }

    interface Mover { void at(String name, long bytes); }

    static long size(File f) {
        if (f.isFile()) { return f.length(); }
        long n = 0;
        File[] k = f.listFiles();
        if (k != null) { for (File c : k) { n += size(c); } }
        return n;
    }

    static void move(File from, File to, Mover m) throws IOException {
        if (from.getName().equals("gamedata.tmp")) { IsoExtract.delete(from); return; } // an install cut short
        if (from.isDirectory()) {
            if (!to.exists() && from.renameTo(to)) { m.at(from.getName(), size(to)); return; }
            to.mkdirs();
            File[] k = from.listFiles();
            if (k != null) { for (File c : k) { move(c, new File(to, c.getName()), m); } }
            from.delete();
            return;
        }
        long n = from.length();
        if (to.exists()) { // already there (a move cut short): the new folder's copy is kept if it is whole
            if (to.length() == n) { from.delete(); m.at(from.getName(), n); return; }
            to.delete();
        }
        if (!from.renameTo(to)) {
            File tmp = new File(to.getPath() + ".part");
            try (FileInputStream i = new FileInputStream(from); FileOutputStream o = new FileOutputStream(tmp)) {
                FileChannel ic = i.getChannel(), oc = o.getChannel();
                long w = 0;
                while (w < n) {
                    long k = ic.transferTo(w, Math.min(n - w, 8L << 20), oc);
                    if (k <= 0) { throw new IOException("Error copying " + from.getName()); }
                    w += k;
                }
            }
            if (!tmp.renameTo(to)) { throw new IOException("Error writing " + to.getPath()); }
            from.delete();
        }
        m.at(from.getName(), n);
    }

    /* ISO 9660 reader and AFS splitter (the same result as port/tools/extract_disc.py and native/iso_extract.c) */
    static final class IsoExtract {
        interface Progress { void at(long done, long total, String what); }
        final FileChannel ch;
        final long size;
        final File dir;
        Progress progress;
        long done;

        IsoExtract(FileChannel c, long s, File d) { ch = c; size = s; dir = d; }

        ByteBuffer read(long off, int n) throws IOException {
            ByteBuffer b = ByteBuffer.allocate(n).order(ByteOrder.LITTLE_ENDIAN);
            while (b.hasRemaining()) {
                if (ch.read(b, off + b.position()) < 0) { throw new IOException("The image ended earlier than expected."); }
            }
            b.flip();
            return b;
        }

        static final class Entry { String name; long lba, size; boolean dir; }

        List<Entry> list(long lba, long len) throws IOException {
            ByteBuffer b = read(lba * 2048, (int) len);
            List<Entry> out = new ArrayList<>();
            int pos = 0;
            while (pos < len) {
                int l = b.get(pos) & 0xFF;
                if (l == 0) { pos = (pos / 2048 + 1) * 2048; continue; }
                if (l < 34 || pos + l > len) { break; }
                int nl = b.get(pos + 32) & 0xFF;
                byte first = b.get(pos + 33);
                if (!(nl == 1 && (first == 0 || first == 1))) {
                    Entry e = new Entry();
                    e.lba = b.getInt(pos + 2) & 0xFFFFFFFFL;
                    e.size = b.getInt(pos + 10) & 0xFFFFFFFFL;
                    e.dir = (b.get(pos + 25) & 2) != 0;
                    byte[] nm = new byte[nl];
                    for (int i = 0; i < nl; i++) { nm[i] = b.get(pos + 33 + i); }
                    String s = new String(nm, "ISO-8859-1");
                    int semi = s.indexOf(';');
                    e.name = semi >= 0 ? s.substring(0, semi) : s;
                    out.add(e);
                }
                pos += l;
            }
            return out;
        }

        void copy(Entry e, File to) throws IOException {
            to.getParentFile().mkdirs();
            try (FileOutputStream o = new FileOutputStream(to); FileChannel oc = o.getChannel()) {
                long off = e.lba * 2048, left = e.size, w = 0;
                while (left > 0) { // (transferFrom does not move the target's position: the write offset is kept here)
                    long n = oc.transferFrom(ch.position(off), w, Math.min(left, 8L << 20));
                    if (n <= 0) { throw new IOException("Error copying " + e.name); }
                    off += n; left -= n; done += n; w += n;
                }
            }
        }

        /* An AFS archive split into its files. Writes are spread over a few threads: most files are small, so the
           time goes into making them, which the storage does faster several at a time. */
        void afs(Entry e, File to) throws IOException {
            final long base = e.lba * 2048;
            ByteBuffer h = read(base, 8);
            if (h.getInt(0) != 0x00534641) { throw new IOException(e.name + " is not an AFS archive."); }
            final int count = h.getInt(4);
            ByteBuffer t = read(base + 8, count * 8);
            to.mkdirs();
            final String name = e.name;
            final java.util.concurrent.atomic.AtomicLong dn = new java.util.concurrent.atomic.AtomicLong(done);
            final java.util.concurrent.atomic.AtomicInteger made = new java.util.concurrent.atomic.AtomicInteger();
            java.util.concurrent.ExecutorService pool = java.util.concurrent.Executors.newFixedThreadPool(4);
            List<java.util.concurrent.Future<?>> jobs = new ArrayList<>(count);
            try {
                for (int i = 0; i < count; i++) {
                    final long at = t.getInt(i * 8) & 0xFFFFFFFFL, n = t.getInt(i * 8 + 4) & 0xFFFFFFFFL;
                    final File f = new File(to, String.format(java.util.Locale.ROOT, "%05d.bin", i));
                    jobs.add(pool.submit(() -> {
                        write(f, base + at, n, name);
                        long d = dn.addAndGet(n);
                        int c = made.incrementAndGet();
                        if (progress != null && (c & 63) == 0) { progress.at(d, size, "Unpacking " + name + "  (" + c + " / " + count + ")"); }
                        return null;
                    }));
                }
                for (java.util.concurrent.Future<?> j : jobs) { j.get(); }
            } catch (java.util.concurrent.ExecutionException x) {
                Throwable c = x.getCause();
                throw c instanceof IOException ? (IOException) c : new IOException(String.valueOf(c));
            } catch (InterruptedException x) {
                throw new IOException("Interrupted");
            } finally {
                pool.shutdownNow();
            }
            done = dn.get();
        }

        /* `n` bytes of the image from `off` into the file `f` (transferTo reads at a given place without moving the
           image's own position: safe from several threads) */
        void write(File f, long off, long n, String what) throws IOException {
            try (FileOutputStream o = new FileOutputStream(f); FileChannel oc = o.getChannel()) {
                while (n > 0) {
                    long r = ch.transferTo(off, Math.min(n, 8L << 20), oc);
                    if (r <= 0) { throw new IOException("Error unpacking " + what); }
                    off += r;
                    n -= r;
                }
            }
        }

        void tree(Entry d, File to, File out, boolean top) throws IOException {
            for (Entry e : list(d.lba, d.size)) {
                if (e.dir) { tree(e, new File(to, e.name), out, false); continue; }
                if (top && e.name.toUpperCase().endsWith(".AFS")) {
                    afs(e, new File(out, e.name.substring(0, e.name.length() - 4).toLowerCase()));
                } else {
                    if (progress != null) { progress.at(done, size, "Copying " + e.name); }
                    copy(e, new File(to, e.name));
                }
            }
        }

        void run() throws IOException {
            ByteBuffer pvd = read(16 * 2048, 2048);
            if (pvd.get(0) != 1 || pvd.get(1) != 'C' || pvd.get(2) != 'D' || pvd.get(3) != '0') { throw new IOException("This file is not a disc image (ISO)."); }
            Entry root = new Entry();
            root.lba = pvd.getInt(156 + 2) & 0xFFFFFFFFL;
            root.size = pvd.getInt(156 + 10) & 0xFFFFFFFFL;
            List<Entry> top = list(root.lba, root.size);
            boolean slus = false;
            for (Entry e : top) { if (e.name.equals("SLUS_216.78")) { slus = true; } }
            if (!slus) { throw new IOException("This ISO is not the US version of Budokai Tenkaichi 3 (SLUS-21678)."); }
            File tmp = new File(dir, "gamedata.tmp"), fin = new File(dir, "gamedata"), disc = new File(tmp, "disc");
            disc.mkdirs();
            for (Entry e : top) {
                if (!e.dir && e.name.equals("SLUS_216.78")) { copy(e, new File(disc, e.name)); }
                else if (e.dir && (e.name.equals("DATA") || e.name.equals("BIN"))) { tree(e, new File(disc, e.name), tmp, e.name.equals("DATA")); }
            }
            if (fin.exists()) { delete(fin); }
            if (!tmp.renameTo(fin)) { throw new IOException("Could not finish the gamedata folder."); }
            if (progress != null) { progress.at(size, size, "Done"); }
        }

        static void delete(File f) {
            File[] k = f.listFiles();
            if (k != null) { for (File c : k) { delete(c); } }
            f.delete();
        }
    }
}

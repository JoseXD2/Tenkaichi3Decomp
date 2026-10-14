/*
 * Which graphics chip the phone has (asked from OpenGL ES once, in a throwaway context: the game itself draws with
 * Vulkan), and the settings profile that suits it. Mali (MediaTek, Exynos, Tensor), Adreno (Snapdragon), Xclipse
 * (newer Exynos), Immortalis and PowerVR are told apart by the renderer's name.
 */
package io.github.tenkaichi3decomp;

import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.os.Build;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class Gpu {
    static final int BATTERY = 0, BALANCED = 1, QUALITY = 2;

    private Gpu() {}

    /* "Mali-G720", "Adreno (TM) 740", ... or "" */
    static String query() {
        EGLDisplay d = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        if (d == EGL14.EGL_NO_DISPLAY) { return ""; }
        int[] v = new int[2];
        if (!EGL14.eglInitialize(d, v, 0, v, 1)) { return ""; }
        String r = "";
        EGLContext c = EGL14.EGL_NO_CONTEXT;
        EGLSurface s = EGL14.EGL_NO_SURFACE;
        try {
            int[] attr = {EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT, EGL14.EGL_NONE};
            EGLConfig[] cfg = new EGLConfig[1];
            int[] n = new int[1];
            if (EGL14.eglChooseConfig(d, attr, 0, cfg, 0, 1, n, 0) && n[0] > 0) {
                c = EGL14.eglCreateContext(d, cfg[0], EGL14.EGL_NO_CONTEXT, new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
                s = EGL14.eglCreatePbufferSurface(d, cfg[0], new int[] {EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0);
                if (c != EGL14.EGL_NO_CONTEXT && s != EGL14.EGL_NO_SURFACE && EGL14.eglMakeCurrent(d, s, s, c)) {
                    String g = GLES20.glGetString(GLES20.GL_RENDERER);
                    r = g != null ? g.trim() : "";
                }
            }
        } catch (Throwable t) {
            r = "";
        } finally {
            EGL14.eglMakeCurrent(d, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            if (s != EGL14.EGL_NO_SURFACE) { EGL14.eglDestroySurface(d, s); }
            if (c != EGL14.EGL_NO_CONTEXT) { EGL14.eglDestroyContext(d, c); }
            EGL14.eglTerminate(d);
        }
        return r;
    }

    /* the chip with the system-on-chip's name where Android tells it ("Mali-G720 · MT6989") */
    static String label(String gpu) {
        String soc = "";
        if (Build.VERSION.SDK_INT >= 31) { soc = Build.SOC_MODEL; }
        if (soc == null || soc.isEmpty() || soc.equalsIgnoreCase("unknown")) { soc = Build.HARDWARE; }
        String g = gpu == null || gpu.isEmpty() ? "?" : gpu.replace("(TM)", "").replace("  ", " ").trim();
        return soc == null || soc.isEmpty() ? g : g + " · " + soc;
    }

    static int num(String s, String re) {
        Matcher m = Pattern.compile(re, Pattern.CASE_INSENSITIVE).matcher(s);
        return m.find() ? Integer.parseInt(m.group(1)) : -1;
    }

    /* the profile for a chip: rough tiers from the families' generations */
    static int tier(String g) {
        if (g == null) { return BALANCED; }
        String s = g.toLowerCase(java.util.Locale.ROOT);
        if (s.contains("xclipse") || s.contains("immortalis")) { return QUALITY; }
        if (s.contains("adreno")) {
            int n = num(s, "adreno[^0-9]*([0-9]{3})");
            if (n >= 730) { return QUALITY; }  // 730, 740, 750, 830...
            if (n >= 640) { return BALANCED; }
            if (n >= 610 || (n >= 530 && n < 600)) { return BALANCED; }
            return n > 0 ? BATTERY : BALANCED;
        }
        if (s.contains("mali")) {
            int n = num(s, "mali-g([0-9]+)");
            if (n >= 100) { return BALANCED; } // G610 ... G720, G925 (Mali: less memory bandwidth than an Adreno of its year)
            if (n >= 76) { return BALANCED; }  // G76, G77, G78
            if (n >= 0) { return n >= 57 && n < 70 ? BALANCED : BATTERY; } // G57, G68 / G52, G71, G72
            return BATTERY;                     // Mali-T
        }
        if (s.contains("powervr")) { return s.contains("dxt") || s.contains("bxm") ? BALANCED : BATTERY; }
        return BALANCED;
    }

    /* PowerVR's and old Mali-T drivers: the plainest graphics path from the start */
    static boolean wantsSafe(String g) {
        if (g == null) { return false; }
        String s = g.toLowerCase(java.util.Locale.ROOT);
        return s.contains("powervr") || s.contains("mali-t");
    }
}

/*
 * Mods: each one a folder in <data folder>/mods with files laid out as in the game's data (pzs3us1/01234.bin,
 * disc/DATA/...). mods.txt lists the switched-on ones, the first first; the game gets their folders in BT3_MODS
 * (GameActivity.java) and takes a file from the first that has it (port/src/plat_file.c, Port_ModFind).
 */
package io.github.tenkaichi3decomp;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class Mods {
    static final String[] TOP = {"pzs3us0", "pzs3us1", "pzs3us2", "disc"}; // what a mod's folder holds

    final File dir, list;
    final List<String> on = new ArrayList<>();  // switched on, in order

    Mods(File data) {
        dir = new File(data, "mods");
        list = new File(data, "mods.txt");
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(list), StandardCharsets.UTF_8))) {
            String l;
            while ((l = r.readLine()) != null) {
                l = l.trim();
                if (l.length() > 0 && !on.contains(l) && new File(dir, l).isDirectory()) { on.add(l); }
            }
        } catch (IOException e) { /* none on */ }
    }

    /* every mod: the switched-on ones in their order, then the others by name */
    List<String> all() {
        List<String> out = new ArrayList<>(on);
        String[] k = dir.list();
        if (k != null) {
            Arrays.sort(k, String.CASE_INSENSITIVE_ORDER);
            for (String n : k) {
                if (!out.contains(n) && !n.endsWith(".tmp") && new File(dir, n).isDirectory()) { out.add(n); }
            }
        }
        return out;
    }

    void save() {
        try (Writer w = new OutputStreamWriter(new FileOutputStream(list), StandardCharsets.UTF_8)) {
            for (String n : on) { w.write(n + "\n"); }
        } catch (IOException e) { /* kept as it was */ }
    }

    void set(String name, boolean enable) {
        on.remove(name);
        if (enable) { on.add(name); }
        save();
    }

    void move(String name, int by) {
        int i = on.indexOf(name), j = i + by;
        if (i < 0 || j < 0 || j >= on.size()) { return; }
        on.remove(i);
        on.add(j, name);
        save();
    }

    /* for the game: the switched-on mods' folders, one per line */
    String env() {
        StringBuilder sb = new StringBuilder();
        for (String n : on) {
            File d = new File(dir, n);
            if (d.isDirectory()) {
                if (sb.length() > 0) { sb.append('\n'); }
                sb.append(d.getPath());
            }
        }
        return sb.toString();
    }

    static long[] count(File f) { // {files, bytes}
        if (f.isFile()) { return new long[] {1, f.length()}; }
        long[] t = {0, 0};
        File[] k = f.listFiles();
        if (k != null) { for (File c : k) { long[] x = count(c); t[0] += x[0]; t[1] += x[1]; } }
        return t;
    }

    static String size(long b) {
        if (b >= 1L << 30) { return String.format(java.util.Locale.ROOT, "%.1f GB", b / (double) (1L << 30)); }
        if (b >= 1L << 20) { return String.format(java.util.Locale.ROOT, "%.1f MB", b / (double) (1L << 20)); }
        return (b + 1023) / 1024 + " KB";
    }

    /* A .zip into mods/<name>. The game's folders may sit deeper in the zip (a folder named after the mod,
       gamedata/, mods/...): the part of each path before the first of TOP is dropped. Returns the mod's name. */
    String importZip(InputStream in, String name) throws IOException {
        name = name.replaceAll("(?i)\\.zip$", "").replaceAll("[\\\\/:*?\"<>|\\n]", "_").trim();
        if (name.isEmpty()) { name = "mod"; }
        String base = name;
        for (int i = 2; new File(dir, name).exists(); i++) { name = base + " (" + i + ")"; }
        File tmp = new File(dir, name + ".tmp");
        Launcher.IsoExtract.delete(tmp);
        tmp.mkdirs();
        int files = 0;
        byte[] buf = new byte[1 << 16];
        try (ZipInputStream z = new ZipInputStream(in)) {
            ZipEntry e;
            while ((e = z.getNextEntry()) != null) {
                if (e.isDirectory()) { continue; }
                String p = e.getName().replace('\\', '/');
                String rel = null;
                String[] parts = p.split("/");
                for (int i = 0; i < parts.length - 1 && rel == null; i++) {
                    for (String t : TOP) {
                        if (parts[i].equalsIgnoreCase(t)) {
                            StringBuilder sb = new StringBuilder(t);
                            for (int j = i + 1; j < parts.length; j++) { sb.append('/').append(parts[j]); }
                            rel = sb.toString();
                            break;
                        }
                    }
                }
                if (rel == null || rel.contains("..")) { continue; }
                File out = new File(tmp, rel);
                out.getParentFile().mkdirs();
                try (FileOutputStream o = new FileOutputStream(out)) {
                    int n;
                    while ((n = z.read(buf)) > 0) { o.write(buf, 0, n); }
                }
                files++;
            }
        }
        if (files == 0) {
            Launcher.IsoExtract.delete(tmp);
            throw new IOException(Str.t("mods_bad"));
        }
        File fin = new File(dir, name);
        if (!tmp.renameTo(fin)) { throw new IOException("rename " + fin); }
        set(name, true);
        return name;
    }

    void delete(String name) {
        Launcher.IsoExtract.delete(new File(dir, name));
        on.remove(name);
        save();
    }
}

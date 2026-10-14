/*
 * A file of name=value lines: the game's bt3_settings.txt (port/src/plat_settings.c reads it at start: numbers,
 * and a few name="text" lines, kept as they are) and the launcher's own launcher.txt. Written whole on each change.
 */
package io.github.tenkaichi3decomp;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

final class KV {
    final File file;
    final LinkedHashMap<String, String> map = new LinkedHashMap<>();

    KV(File f) {
        file = f;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) {
                int eq = line.indexOf('=');
                if (eq > 0) { map.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim()); }
            }
        } catch (Exception e) { /* no file yet: the defaults */ }
    }

    boolean has(String k) { return map.containsKey(k); }

    int get(String k, int def) {
        String v = map.get(k);
        if (v == null) { return def; }
        try { return Integer.parseInt(v); } catch (NumberFormatException e) { return def; }
    }

    boolean on(String k, boolean def) { return get(k, def ? 1 : 0) != 0; }

    String str(String k, String def) {
        String v = map.get(k);
        return v != null ? v : def;
    }

    void set(String k, int v) { map.put(k, Integer.toString(v)); save(); }

    void set(String k, boolean v) { set(k, v ? 1 : 0); }

    void setStr(String k, String v) { map.put(k, v); save(); }

    void save() {
        file.getParentFile().mkdirs();
        File tmp = new File(file.getPath() + ".new");
        try (Writer w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
            for (Map.Entry<String, String> e : map.entrySet()) {
                w.write(e.getKey() + "=" + e.getValue() + "\n");
            }
        } catch (Exception e) {
            return;
        }
        if (!tmp.renameTo(file)) { file.delete(); tmp.renameTo(file); }
    }
}

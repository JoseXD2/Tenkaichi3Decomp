#!/usr/bin/env python3
"""Packs the Android app without Gradle or aapt2: binary AndroidManifest.xml, classes.dex, the native libraries and
the game's lists as assets, signed with apksigner.

    port/android/make_apk.py --dex classes.dex --libs <folder with the .so files> --assets <folder> \
        --apksigner apksigner.jar [--keystore debug.keystore] -o Tenkaichi3Decomp-android.apk

The manifest is written straight in Android's binary XML (what aapt2 would make of port/android/app/src/main/
AndroidManifest.xml), with the framework's own theme and no resources of the app's own, so no resource compiler is
needed. classes.dex is SDL's Java side (SDL3/android-project/app/src/main/java), compiled with javac and d8."""
import argparse, os, pathlib, struct, subprocess, sys, zipfile

PACKAGE = "io.github.tenkaichi3decomp"
LABEL = "Dragon Rage"
ANDROID_NS = "http://schemas.android.com/apk/res/android"

# android.R.attr (from the platform's android.jar)
ATTR = {"process": 0x01010011, "isGame": 0x010102F4, "theme": 0x01010000, "label": 0x01010001, "icon": 0x01010002, "name": 0x01010003, "hasCode": 0x0101000C,
        "exported": 0x01010010, "launchMode": 0x0101001D, "screenOrientation": 0x0101001E, "configChanges": 0x0101001F,
        "alwaysRetainTaskState": 0x01010203, "minSdkVersion": 0x0101020C, "versionCode": 0x0101021B,
        "versionName": 0x0101021C, "targetSdkVersion": 0x01010270, "allowBackup": 0x01010280, "required": 0x0101028E,
        "installLocation": 0x010102B7, "hardwareAccelerated": 0x010102D3, "extractNativeLibs": 0x010104EA,
        "maxSdkVersion": 0x01010271, "requestLegacyExternalStorage": 0x01010603}
THEME_FULLSCREEN = 0x01030007  # @android:style/Theme.NoTitleBar.Fullscreen

T_REF, T_STRING, T_DEC, T_HEX, T_BOOL = 0x01, 0x03, 0x10, 0x11, 0x12
CONFIG_CHANGES = (0x0004 | 0x0010 | 0x0020 | 0x0040 | 0x0080 | 0x0100 | 0x0200 | 0x0400 | 0x0800 | 0x2000 | 0x8000 |
                  0x10000000 | 0x40000000)  # locale keyboard keyboardHidden navigation orientation screenLayout uiMode
#   screenSize smallestScreenSize layoutDirection grammaticalGender fontWeightAdjustment fontScale


def manifest_tree(min_sdk, target_sdk, version_code, version_name):
    def e(tag, attrs=(), kids=()):
        return (tag, list(attrs), list(kids))
    a = lambda name, typ, val: (name, typ, val)  # android: attribute
    feature = lambda n: e("uses-feature", [a("name", T_STRING, n), a("required", T_BOOL, False)])
    return e("manifest", [("package", T_STRING, PACKAGE, None), a("versionCode", T_DEC, version_code),
                          a("versionName", T_STRING, version_name), a("installLocation", T_DEC, 0)], [
        e("uses-sdk", [a("minSdkVersion", T_DEC, min_sdk), a("targetSdkVersion", T_DEC, target_sdk)]),
        feature("android.hardware.touchscreen"), feature("android.hardware.bluetooth"), feature("android.hardware.gamepad"),
        feature("android.hardware.usb.host"),
        e("uses-permission", [a("name", T_STRING, "android.permission.VIBRATE")]),
        e("uses-permission", [a("name", T_STRING, "android.permission.INTERNET")]),
        e("uses-permission", [a("name", T_STRING, "android.permission.WAKE_LOCK")]),  # (the Wi-Fi lock, GameActivity.java)
        e("uses-permission", [a("name", T_STRING, "android.permission.ACCESS_WIFI_STATE")]),
        # the data folder at the root of the storage (/sdcard/DragonRage, Launcher.java): "all files access" on
        # Android 11+, the old storage permissions before
        e("uses-permission", [a("name", T_STRING, "android.permission.MANAGE_EXTERNAL_STORAGE")]),
        e("uses-permission", [a("name", T_STRING, "android.permission.READ_EXTERNAL_STORAGE")]),
        e("uses-permission", [a("name", T_STRING, "android.permission.WRITE_EXTERNAL_STORAGE"), a("maxSdkVersion", T_DEC, 29)]),
        # Discord's app, which the status (GameActivity.java) talks to through its RPC service (Android 11+ hides
        # other apps unless they are named here)
        e("queries", [], [e("package", [a("name", T_STRING, "com.discord")]),
                          e("intent", [], [e("action", [a("name", T_STRING, "com.discord.socialsdk.rpc.IDiscordRpcService")])])]),
        e("application", [a("label", T_STRING, LABEL), a("icon", T_REF, ICON_ID), a("theme", T_REF, THEME_FULLSCREEN), a("hasCode", T_BOOL, True),
                          a("allowBackup", T_BOOL, True), a("hardwareAccelerated", T_BOOL, True), a("isGame", T_BOOL, True),
                          a("extractNativeLibs", T_BOOL, True), a("requestLegacyExternalStorage", T_BOOL, True)], [
            # the front screen (port/android/java/.../Launcher.java)
            e("activity", [a("name", T_STRING, "io.github.tenkaichi3decomp.Launcher"), a("label", T_STRING, LABEL),
                           a("screenOrientation", T_DEC, 6), a("configChanges", T_HEX, CONFIG_CHANGES), a("exported", T_BOOL, True)], [
                e("intent-filter", [], [
                    e("action", [a("name", T_STRING, "android.intent.action.MAIN")]),
                    e("category", [a("name", T_STRING, "android.intent.category.LAUNCHER")])])]),
            # the game, in a process of its own (its image can be mapped only once per process)
            e("activity", [a("name", T_STRING, "io.github.tenkaichi3decomp.GameActivity"), a("label", T_STRING, LABEL), a("process", T_STRING, ":game"),
                           a("alwaysRetainTaskState", T_BOOL, True), a("launchMode", T_DEC, 3),
                           a("screenOrientation", T_DEC, 6), a("configChanges", T_HEX, CONFIG_CHANGES),
                           a("exported", T_BOOL, False)], [
                e("intent-filter", [], [
                    e("action", [a("name", T_STRING, "android.hardware.usb.action.USB_DEVICE_ATTACHED")])])])])])

def axml(tree):
    """Android binary XML of `tree` (tag, attributes, children); an attribute is (name, type, value) in the android
    namespace, or (name, type, value, None) without one."""
    # the string pool: the android attribute names first (in the order of the resource map), then everything else
    names = []
    def walk(n):
        for at in n[1]:
            if len(at) == 3 and at[0] not in names:
                names.append(at[0])
        for k in n[2]:
            walk(k)
    walk(tree)
    names.sort(key=lambda s: ATTR[s])
    strings = list(names)
    def idx(s):
        if s not in strings:
            strings.append(s)
        return strings.index(s)
    idx("android"); idx(ANDROID_NS)

    body = []
    line = [1]
    def chunk(typ, header, payload):
        return struct.pack("<HHI", typ, len(header) + 8, len(header) + 8 + len(payload)) + header + payload
    def node_header():
        line[0] += 1
        return struct.pack("<II", line[0], 0xFFFFFFFF)
    def value(typ, val):
        if typ == T_STRING:
            i = idx(val)
            return i, struct.pack("<HBBI", 8, 0, T_STRING, i)
        if typ == T_BOOL:
            return 0xFFFFFFFF, struct.pack("<HBBI", 8, 0, T_BOOL, 0xFFFFFFFF if val else 0)
        return 0xFFFFFFFF, struct.pack("<HBBI", 8, 0, typ, val & 0xFFFFFFFF)
    def emit(n):
        tag, attrs, kids = n
        ns_uri = idx(ANDROID_NS)
        enc = []
        for at in attrs:
            if len(at) == 3:
                raw, tv = value(at[1], at[2])
                enc.append((ATTR[at[0]], struct.pack("<III", ns_uri, idx(at[0]), raw) + tv))
            else:
                raw, tv = value(at[1], at[2])
                enc.append((0, struct.pack("<III", 0xFFFFFFFF, idx(at[0]), raw) + tv))
        enc.sort(key=lambda x: (x[0] == 0, x[0]))  # by resource id, the plain attributes after them (as aapt does)
        enc.sort(key=lambda x: x[0] if x[0] else -1)  # (aapt puts attributes without an id first)
        ext = struct.pack("<IIHHHHHH", 0xFFFFFFFF, idx(tag), 20, 20, len(enc), 0, 0, 0)
        body.append(chunk(0x0102, node_header(), ext + b"".join(x[1] for x in enc)))
        for k in kids:
            emit(k)
        body.append(chunk(0x0103, node_header(), struct.pack("<II", 0xFFFFFFFF, idx(tag))))
    emit(tree)
    ns = struct.pack("<II", idx("android"), idx(ANDROID_NS))
    start_ns = chunk(0x0100, struct.pack("<II", 1, 0xFFFFFFFF), ns)
    end_ns = chunk(0x0101, struct.pack("<II", line[0] + 1, 0xFFFFFFFF), ns)

    # string pool, UTF-16
    data, offsets = b"", []
    for s in strings:
        offsets.append(len(data))
        u = s.encode("utf-16-le")
        data += struct.pack("<H", len(s)) + u + b"\0\0"
    data += b"\0" * (-len(data) % 4)
    hdr_size = 28
    strings_start = hdr_size + 4 * len(strings)
    pool = struct.pack("<HHIIIIII", 0x0001, hdr_size, strings_start + len(data), len(strings), 0, 0, strings_start, 0)
    pool += b"".join(struct.pack("<I", o) for o in offsets) + data
    resmap = struct.pack("<HHI", 0x0180, 8, 8 + 4 * len(names)) + b"".join(struct.pack("<I", ATTR[s]) for s in names)
    xml = pool + resmap + start_ns + b"".join(body) + end_ns
    return struct.pack("<HHI", 0x0003, 8, 8 + len(xml)) + xml


def string_pool(strings):
    data, offsets = b"", []
    for s in strings:
        offsets.append(len(data))
        data += struct.pack("<H", len(s)) + s.encode("utf-16-le") + b"\0\0"
    data += b"\0" * (-len(data) % 4)
    start = 28 + 4 * len(strings)
    return struct.pack("<HHIIIIII", 0x0001, 28, start + len(data), len(strings), 0, 0, start, 0) + \
        b"".join(struct.pack("<I", o) for o in offsets) + data


ICON_ID = 0x7F010000  # package 0x7f, type 1 (drawable), entry 0 (icon)
ICON_PATH = "res/drawable/icon.png"


def resources_arsc():
    """A resource table with one entry, the icon: drawable/icon -> res/drawable/icon.png (ICON_ID)."""
    glob = string_pool([ICON_PATH])
    types = string_pool(["drawable"])
    keys = string_pool(["icon"])
    spec = struct.pack("<HHIBBHI", 0x0202, 16, 20, 1, 0, 0, 1) + struct.pack("<I", 0)
    config = struct.pack("<I", 64) + b"\0" * 60
    head = 8 + 4 + 4 + 4 + len(config)
    entries = struct.pack("<HHI", 8, 0, 0) + struct.pack("<HBBI", 8, 0, T_STRING, 0)
    tchunk = struct.pack("<HHIBBHII", 0x0201, head, head + 4 + len(entries), 1, 0, 0, 1, head + 4) + config + \
        struct.pack("<I", 0) + entries
    name = PACKAGE.encode("utf-16-le").ljust(256, b"\0")
    phead = 288
    body = types + keys + spec + tchunk
    pkg = struct.pack("<HHII", 0x0200, phead, phead + len(body), 0x7F) + name + \
        struct.pack("<IIIII", phead, 1, phead + len(types), 1, 0) + body
    content = glob + pkg
    return struct.pack("<HHII", 0x0002, 12, 12 + len(content), 1) + content


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dex", required=True)
    ap.add_argument("--libs", required=True, help="folder with the arm64-v8a .so files")
    ap.add_argument("--assets", required=True)
    ap.add_argument("--apksigner", required=True, help="apksigner.jar of the Android build tools")
    ap.add_argument("--keystore", default=os.path.expanduser("~/.android/debug.keystore"))
    ap.add_argument("--min-sdk", type=int, default=26)
    ap.add_argument("--target-sdk", type=int, default=34)
    ap.add_argument("--version-code", type=int, default=1)
    ap.add_argument("--version-name", default="0.1-android")
    ap.add_argument("--icon", help="the app icon (PNG)")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    unsigned = out.with_suffix(".unsigned.apk")
    with zipfile.ZipFile(unsigned, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("AndroidManifest.xml", axml(manifest_tree(a.min_sdk, a.target_sdk, a.version_code, a.version_name)))
        z.write(a.dex, "classes.dex")
        if a.icon:
            # stored and 4-byte aligned, as Android 11 and later require of resources.arsc: the local header is
            # 30 bytes + the name + the extra field, which pads the data to a multiple of 4
            zi = zipfile.ZipInfo("resources.arsc")
            zi.compress_type = zipfile.ZIP_STORED
            pad = -(z.fp.tell() + 30 + len(zi.filename)) % 4
            if pad:  # an extra field of the zipalign kind (id 0xD935): 4 bytes of head + pad bytes
                zi.extra = struct.pack("<HH", 0xD935, pad) + b"\0" * pad
            z.writestr(zi, resources_arsc())
            z.write(a.icon, ICON_PATH)
        for f in sorted(pathlib.Path(a.libs).glob("*.so")):
            z.write(f, f"lib/arm64-v8a/{f.name}")
        for f in sorted(pathlib.Path(a.assets).iterdir()):
            z.write(f, f"assets/{f.name}")
    ks = pathlib.Path(a.keystore)
    if not ks.exists():
        ks.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["keytool", "-genkeypair", "-keystore", str(ks), "-storepass", "android", "-alias", "androiddebugkey",
                        "-keypass", "android", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
                        "-dname", "CN=Android Debug,O=Android,C=US"], check=True, capture_output=True)
    r = subprocess.run(["java", "-jar", a.apksigner, "sign", "--ks", str(ks), "--ks-pass", "pass:android",
                        "--ks-key-alias", "androiddebugkey", "--key-pass", "pass:android", "--min-sdk-version", str(a.min_sdk),
                        "--out", str(out), str(unsigned)], capture_output=True, text=True)
    if r.returncode:
        sys.exit("apksigner: " + r.stderr[-800:])
    unsigned.unlink()
    print(f"{out} ({out.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()

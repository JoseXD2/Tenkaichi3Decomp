#!/bin/bash
# Builds the Android app (Dragon Rage, arm64-v8a) the way its releases are made, without Gradle:
#
#   bash port/android/build_apk.sh [version name] [version code]      -> port/build/DragonRage-<version>.apk
#
# On Ubuntu 24.04 (or the GitHub workflow .github/workflows/android.yml) with: clang-20 llvm-20 lld-20 libclang-20-dev
# python3-clang-20 glslang-tools cmake ninja-build, a JDK 17 or newer, and an Android SDK in ANDROID_HOME with an NDK,
# a build-tools and a platform. Optional, in the environment:
#   HXDISCORD_DIR   the folder hxdiscord_rpc-android-support-master: with it the app shows the Discord status
#                   (libbt3discord.so over Discord's Social SDK); without it the app is built without the status
#   KEYSTORE        the keystore the app is signed with (alias androiddebugkey, passwords "android"). An update
#                   installs over an earlier version only if both were signed with the same one. Without it,
#                   ~/.android/debug.keystore (made if missing).
#   BT3_TOOLS       where SDL3 is downloaded and built for Android (default ~/bt3-tools), kept between runs
#
# What goes in (see port/android/native/loader.c for why the game is not an ordinary Android library):
#   libbt3game.so     the game, BT3_CC=android64 (port/tools/toolchain.py): linked at 0x20000000, moved by the loader
#   libmain.so        the loader: SDL's main, maps libbt3game.so below 4 GB and runs it
#   libSDL3.so, libc++_shared.so, and with HXDISCORD_DIR libbt3discord.so and libdiscord_partner_sdk.so
#   classes.dex       SDL's Java side, port/android/java (launcher, game activity), Discord's jars if any
#   assets            bt3.dat / bt3.mem (the game's data tables, filled from the player's disc at start), banner.png
# No game data is in the APK: the player's own disc image is unpacked by the app.
set -eo pipefail
VER=${1:-1.0}
CODE=${2:-1}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
cd "$SRC"
T=${BT3_TOOLS:-$HOME/bt3-tools}
SDL_TAG=release-3.4.12
step() { echo; echo "== $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

[ -n "$ANDROID_HOME" ] || die "ANDROID_HOME is not set (the Android SDK)"
NDK=${ANDROID_NDK_HOME:-$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1)}
N=$NDK/toolchains/llvm/prebuilt/linux-x86_64
BT=$(ls -d "$ANDROID_HOME"/build-tools/* 2>/dev/null | sort -V | tail -1)
JAR=$(ls "$ANDROID_HOME"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)
[ -x "$N/bin/clang" ] || die "no NDK in $ANDROID_HOME/ndk"
[ -f "$BT/lib/d8.jar" ] || die "no build-tools in $ANDROID_HOME/build-tools"
[ -f "$JAR" ] || die "no platform in $ANDROID_HOME/platforms"
echo "NDK $NDK, build-tools $BT, $JAR"
OUT=port/build/apk
rm -rf "$OUT"
mkdir -p "$OUT/lib" "$OUT/assets" "$OUT/cls" "$OUT/dex" "$T"

step "SDL3 for Android ($SDL_TAG)"
[ -d "$T/SDL3" ] || git clone -q --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$T/SDL3"
SDLA=$T/sdl3-android
if [ ! -f "$SDLA/lib/libSDL3.so" ]; then
    cmake -S "$T/SDL3" -B "$T/sdl3-android-build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDLA" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF > /dev/null
    cmake --build "$T/sdl3-android-build" > /dev/null
    cmake --install "$T/sdl3-android-build" > /dev/null
fi

step "the game (several minutes)"
mkdir -p port/build
export BT3_CC=android64 BT3_SKELETON=1 BT3_LLVM_SUFFIX=${BT3_LLVM_SUFFIX:--20} BT3_SDL3_ARM=$SDLA BT3_ANDROID_CXX=$N/bin/clang++ \
    ANDROID_SYSROOT=$N/sysroot
python3 port/tools/undefined.py > port/build/compile_android64.txt 2>&1 || { tail -30 port/build/compile_android64.txt; die "compiling"; }
if grep -q "^FAILED" port/build/compile_android64.txt; then grep "^FAILED" port/build/compile_android64.txt | head -40; die "compiling"; fi
tail -3 port/build/compile_android64.txt
python3 port/tools/link.py
"$N/bin/llvm-readelf" -lW port/build/libbt3game.so | grep -q " TLS " && die "the game image has TLS variables (the loader does not support them)"
"$N/bin/llvm-strip" --strip-debug -o "$OUT/lib/libbt3game.so" port/build/libbt3game.so
cp port/build/libbt3game.so.dat "$OUT/assets/bt3.dat"
cp port/build/libbt3game.so.mem "$OUT/assets/bt3.mem"
cp port/android/art/banner.png "$OUT/assets/"

step "the loader, SDL3 and the C++ library"
"$N/bin/clang" --target=aarch64-linux-android26 --sysroot="$N/sysroot" -shared -fPIC -O2 -Wall -Wno-unused-function \
    -I"$SDLA/include" port/android/native/loader.c port/android/native/iso_extract.c -L"$SDLA/lib" -lSDL3 -ldl -llog \
    -o "$OUT/lib/libmain.so"
"$N/bin/llvm-strip" --strip-unneeded -o "$OUT/lib/libSDL3.so" "$SDLA/lib/libSDL3.so"
"$N/bin/llvm-strip" --strip-unneeded -o "$OUT/lib/libc++_shared.so" "$N/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"

CP=$JAR
JAVA=$(find "$T/SDL3/android-project/app/src/main/java" port/android/java -name "*.java")
JARS=""
if [ -n "$HXDISCORD_DIR" ]; then
    step "the Discord status ($HXDISCORD_DIR)"
    D=$HXDISCORD_DIR/project/discord-rpc/android
    bash port/android/build_discord.sh "$HXDISCORD_DIR" "$N" "$OUT/lib"
    CP=$CP:$D/libs/discord_partner_sdk.jar
    JAVA="$JAVA $D/src/main/java/com/hxdiscord/rpc/sdk/DiscordApplicationMetadata.java"
    JARS="$D/libs/discord_partner_sdk.jar $D/libs/libwebrtc.jar"
else
    echo "(no HXDISCORD_DIR: built without the Discord status)"
fi

step "Java"
javac -nowarn -encoding UTF-8 --release 8 -cp "$CP" -d "$OUT/cls" $JAVA 2>&1 | grep -v "^Note\|warning: \[options\]\|^1 warning" || true
[ -f "$OUT/cls/io/github/tenkaichi3decomp/Launcher.class" ] || die "javac"
java -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 --release --min-api 26 --lib "$JAR" --output "$OUT/dex" \
    $(find "$OUT/cls" -name "*.class") $JARS

step "the APK"
python3 port/android/make_apk.py --dex "$OUT/dex/classes.dex" --libs "$OUT/lib" --assets "$OUT/assets" \
    --apksigner "$BT/lib/apksigner.jar" --keystore "${KEYSTORE:-$HOME/.android/debug.keystore}" \
    --version-code "$CODE" --version-name "$VER" --icon port/android/art/icon.png -o "port/build/DragonRage-$VER.apk"

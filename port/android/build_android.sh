#!/bin/bash
# Builds the Android app (arm64-v8a): Tenkaichi3Decomp-android.apk
#
#   bash port/android/build_android.sh [disc image]      (Ubuntu, or Ubuntu under WSL on Windows)
#
# Downloads on its first run: the Android command-line tools, SDK platform, NDK and Gradle (about 3 GB), into
# ~/bt3-tools. With a disc image, also unpacks the game's data (gamedata/) to copy to the phone.
#
# What it builds (see port/android/native/loader.c for why the game is not an ordinary Android library):
#   libSDL3.so        SDL3 for Android, from the same SDL release as the PC build
#   libbt3game.so     the game, BT3_CC=android64 (port/tools/toolchain.py): linked at 0x20000000, not position independent
#   libmain.so        the loader: SDL's main, maps libbt3game.so below 4 GB and runs it
#   libc++_shared.so  the NDK's C++ library (the settings window)
# and the APK from SDL's android-project with port/android/app laid over it.
set -o pipefail
SRC=$(cd "$(dirname "$0")/../.." && pwd)
T=${BT3_TOOLS:-$HOME/bt3-tools}
B=${BT3_BUILD_COPY:-$HOME/bt3-android-src}   # the build runs in a copy inside Linux (Windows folders are slow and get locked)
ISO=$1
NDK_VERSION=28.2.13676358
SDL_TAG=release-3.4.12
mkdir -p "$T"
exec > >(tee "$SRC/build_android_log.txt") 2>&1
step() { echo; echo "== $*"; }
die() { echo; echo "ERRO: $*"; echo "(log completo: build_android_log.txt)"; exit 1; }

step "1/8 ferramentas do Ubuntu"
sudo apt-get update -qq
sudo apt-get install -y -qq clang-20 llvm-20 lld-20 libclang-20-dev python3-clang-20 unzip zip curl cmake ninja-build rsync \
    glslang-tools git p7zip-full >/dev/null || die "apt-get falhou"
if ! command -v java >/dev/null; then
    sudo apt-get install -y -qq openjdk-17-jdk-headless >/dev/null 2>&1 || sudo apt-get install -y -qq openjdk-21-jdk-headless >/dev/null || die "Java (JDK) nao instalou"
fi

step "2/8 Android SDK e NDK"
# An SDK for Linux already there (ANDROID_HOME, or ~/Android/Sdk) is used; otherwise one is downloaded (~3 GB).
# (An SDK installed on Windows by Android Studio cannot be used from here: its NDK is made of Windows programs.)
[ -z "$ANDROID_HOME" ] && [ -d "$HOME/Android/Sdk" ] && ANDROID_HOME=$HOME/Android/Sdk
if [ -n "$ANDROID_HOME" ] && ls -d "$ANDROID_HOME"/ndk/*/toolchains/llvm/prebuilt/linux-x86_64 >/dev/null 2>&1; then
    echo "usando o SDK em $ANDROID_HOME"
else
    ANDROID_HOME=$T/android-sdk
fi
export ANDROID_HOME ANDROID_SDK_ROOT=$ANDROID_HOME
SDKM=$(ls "$ANDROID_HOME"/cmdline-tools/*/bin/sdkmanager 2>/dev/null | head -1)
if [ -z "$SDKM" ]; then
    mkdir -p "$ANDROID_HOME/cmdline-tools"
    curl -fL --retry 3 -o "$T/cmdtools.zip" https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip || die "download das ferramentas do Android"
    rm -rf "$ANDROID_HOME/cmdline-tools/latest" "$ANDROID_HOME/cmdline-tools/cmdline-tools"
    unzip -q "$T/cmdtools.zip" -d "$ANDROID_HOME/cmdline-tools" && mv "$ANDROID_HOME/cmdline-tools/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest" || die "unzip"
    SDKM=$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager
fi
yes | "$SDKM" --licenses >/dev/null 2>&1
NEED="platforms;android-35 build-tools;35.0.0 platform-tools"
ls -d "$ANDROID_HOME"/ndk/*/toolchains/llvm/prebuilt/linux-x86_64 >/dev/null 2>&1 || NEED="$NEED ndk;$NDK_VERSION"
"$SDKM" $NEED | grep -v '^\[' | tail -3 || die "sdkmanager"
NDK=$(ls -d "$ANDROID_HOME"/ndk/*/toolchains/llvm/prebuilt/linux-x86_64 | sort -V | tail -1)
NDK=${NDK%/toolchains/llvm/prebuilt/linux-x86_64}
echo "NDK: $NDK"
NDKBIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
export ANDROID_SYSROOT=$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot
[ -x "$NDKBIN/clang" ] || die "NDK nao encontrado em $NDK"

step "3/8 copiando o projeto para dentro do Linux"
mkdir -p "$B"
rsync -a --delete --exclude /port/build --exclude /gamedata --exclude '*.rej' --exclude '*.apk' --exclude '*_log.txt' "$SRC/" "$B/" || die "rsync"
mkdir -p "$B/port/build"
cd "$B" || die "cd"
[ -d "$T/SDL3" ] || git clone -q --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$T/SDL3" || die "download do SDL3"

step "4/8 SDL3 para Android"
SDLA=$T/sdl3-android
if [ ! -f "$SDLA/lib/libSDL3.so" ]; then
    cmake -S "$T/SDL3" -B "$T/sdl3-android-build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDLA" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF >/dev/null &&
        cmake --build "$T/sdl3-android-build" >/dev/null && cmake --install "$T/sdl3-android-build" >/dev/null || die "SDL3 para Android"
fi

step "5/8 compilando o jogo para Android (demora varios minutos)"
export BT3_CC=android64 BT3_SKELETON=1 BT3_LLVM_SUFFIX=-20 BT3_SDL3_ARM=$SDLA BT3_ANDROID_CXX=$NDKBIN/clang++
python3 port/tools/undefined.py > port/build/compile_android64.txt 2>&1 || { tail -20 port/build/compile_android64.txt; die "compilacao"; }
tail -4 port/build/compile_android64.txt
if grep -q "^FAILED" port/build/compile_android64.txt; then grep "^FAILED" port/build/compile_android64.txt | head -40; die "compilacao"; fi
python3 port/tools/link.py || die "link"
"$NDKBIN/llvm-readelf" -lW port/build/libbt3game.so | grep -q " TLS " && die "a imagem do jogo tem variaveis TLS (o loader nao suporta)"

step "6/8 loader e projeto Android"
APP=$T/apk-project
rm -rf "$APP"
cp -r "$T/SDL3/android-project" "$APP" && rm -rf "$APP/app/jni" || die "copia do android-project"
cp -r port/android/app/. "$APP/app/"
LIBS=$APP/app/libs/arm64-v8a
mkdir -p "$LIBS" "$APP/app/assets"
"$NDKBIN/clang" --target=aarch64-linux-android26 -shared -fPIC -O2 -Wall -Wno-unused-function -I"$SDLA/include" \
    port/android/native/loader.c -L"$SDLA/lib" -lSDL3 -ldl -llog -o "$LIBS/libmain.so" || die "loader"
cp "$SDLA/lib/libSDL3.so" "$LIBS/" || die "libSDL3.so"
cp "$ANDROID_SYSROOT/usr/lib/aarch64-linux-android/libc++_shared.so" "$LIBS/" || die "libc++_shared.so"
"$NDKBIN/llvm-strip" --strip-debug -o "$LIBS/libbt3game.so" port/build/libbt3game.so || die "strip"
cp port/build/libbt3game.so.dat "$APP/app/assets/bt3.dat" && cp port/build/libbt3game.so.mem "$APP/app/assets/bt3.mem" || die "listas do jogo (.dat/.mem)"

step "7/8 gerando o APK (Gradle; baixa o que precisa na primeira vez)"
cd "$APP" && chmod +x gradlew && ./gradlew --no-daemon -q assembleDebug || die "gradle"
cp app/build/outputs/apk/debug/app-debug.apk "$SRC/Tenkaichi3Decomp-android.apk" || die "APK nao encontrado"
echo "APK: Tenkaichi3Decomp-android.apk ($(du -h "$SRC/Tenkaichi3Decomp-android.apk" | cut -f1))"

step "8/8 dados do jogo"
if [ -n "$ISO" ]; then
    if [ -f "$SRC/gamedata/disc/SLUS_216.78" ]; then
        echo "gamedata ja existe em $SRC/gamedata"
    else
        python3 "$B/port/tools/extract_disc.py" "$ISO" "$SRC/gamedata" | tail -3 || die "extracao da ISO"
    fi
else
    echo "(sem ISO: rode de novo com o caminho da ISO para gerar a pasta gamedata)"
fi
echo
echo "PRONTO"

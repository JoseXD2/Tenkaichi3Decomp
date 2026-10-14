#!/bin/bash
# Builds libbt3discord.so (the discord-rpc API over Discord's Social SDK, for the status in GameActivity.java) from
# hxdiscord_rpc's android sources, and copies it with libdiscord_partner_sdk.so into the APK's library folder.
#   bash port/android/build_discord.sh <hxdiscord_rpc-android-support-master folder> <NDK llvm prebuilt folder> <libs out>
set -e
R=$1/project; D=$R/discord-rpc; N=$2; OUT=$3
"$N/bin/clang++" --target=aarch64-linux-android26 --sysroot="$N/sysroot" -shared -fPIC -O2 -std=c++17 -DDISCORD_ANDROID \
    -DDISCORD_DYNAMIC_LIB -DDISCORD_BUILDING_SDK -I"$D/include" -I"$R/rapidjson/include" \
    "$D/src/discord_rpc.cpp" "$D/src/discord_register_android.cpp" "$D/src/connection_android.cpp" \
    -L"$D/android/src/main/jniLibs/arm64-v8a" -ldiscord_partner_sdk -llog -static-libstdc++ -Wl,-soname,libbt3discord.so \
    -o "$OUT/libbt3discord.so"
"$N/bin/llvm-strip" --strip-unneeded "$OUT/libbt3discord.so"
cp "$D/android/src/main/jniLibs/arm64-v8a/libdiscord_partner_sdk.so" "$OUT/"
# Java: the dex also takes $D/android/libs/discord_partner_sdk.jar, libwebrtc.jar and
# $D/android/src/main/java/com/hxdiscord/rpc/sdk/DiscordApplicationMetadata.java

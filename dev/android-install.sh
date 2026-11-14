#!/usr/bin/env bash
# Build a game for Android and install it on a connected device, with its files beside it.
#
#   dev/android-install.sh <title> [adb serial]        e.g. dev/android-install.sh PCSE00397
#   APK_ONLY=1 dev/android-install.sh <title>          build and install the package, push no files
#
# There is no Gradle here: the package is a manifest, one dex (SDL's Java classes and the activity in
# android/) and three .so files, assembled with the SDK's own aapt2, d8 and apksigner. The game's code
# is libmain.so -- this project's runtime with the recompiled game linked into it, built by
# `TARGET=android vitarecomp build` -- and the game's DATA is pushed to the app's external files
# directory, never into the package. Nothing distributable here carries any of the game.
#
# Needs an Android SDK (ANDROID_HOME, default ~/Android/Sdk) with build-tools and a platform, an NDK,
# and the SDL3 that runtime/platform/android/setup-toolchain.sh builds.
set -euo pipefail
TITLE=${1:?usage: android-install.sh <title> [adb serial]}
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK=${ANDROID_HOME:-$HOME/Android/Sdk}
PKG=com.crownparkcomputing.nativ
ADB=(adb); [[ ${2:-} ]] && ADB=(adb -s "$2")
PY=$R/.venv/bin/python; [[ -x $PY ]] || PY=python3

BT=$(ls -1d "$SDK"/build-tools/*/ | sort -V | tail -1)
PLAT=$(ls -1d "$SDK"/platforms/android-*/ | sort -V | tail -1)
AT=${ATIV_ANDROID_TOOLCHAIN:-$R/toolchain-android}
NDK=${ANDROID_NDK:-$(ls -1d "$SDK"/ndk/*/ | sort -V | tail -1)}
[[ -f $AT/SDL3/lib/libSDL3.so ]] || { echo "no Android toolchain at $AT: run runtime/platform/android/setup-toolchain.sh" >&2; exit 1; }
[[ -f $PLAT/android.jar ]] || { echo "no android.jar under $PLAT" >&2; exit 1; }

echo "== building $TITLE for arm64-v8a"
TARGET=android "$PY" "$R/pipeline/vitarecomp.py" build "$TITLE"
eval "$("$PY" "$R/dev/android_launch_info.py" "$TITLE")"
echo "   $GAMENAME: $IMAGE entry $ENTRY, modules: ${MODS:-none}"

# ---- the package: manifest -> aapt2, java -> d8, then the libraries zipped in and the whole thing signed
B=$R/build/android/$TITLE
rm -rf "$B"; mkdir -p "$B/classes" "$B/lib/arm64-v8a"
SDLJAVA=$AT/sdl-src/android-project/app/src/main/java/org/libsdl/app
echo "== compiling the activity"
# no -bootclasspath: a current JDK refuses it alongside -target, and android.jar on the classpath is
# what these sources actually need (d8 desugars the rest against --lib below)
javac -nowarn -source 17 -target 17 -classpath "$PLAT/android.jar" \
      -d "$B/classes" "$SDLJAVA"/*.java "$R"/android/java/com/crownparkcomputing/nativ/*.java
mapfile -t CLASSES < <(find "$B/classes" -name '*.class')
[[ ${#CLASSES[@]} -gt 0 ]] || { echo "no classes compiled" >&2; exit 1; }
"$BT/d8" --release --min-api 28 --lib "$PLAT/android.jar" --output "$B" "${CLASSES[@]}"
[[ -f $B/classes.dex ]] || { echo "d8 produced no classes.dex" >&2; exit 1; }

cp "$AT/SDL3/lib/libSDL3.so" "$B/lib/arm64-v8a/"
cp "$OUT/libmain.so" "$B/lib/arm64-v8a/"
cp "$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$B/lib/arm64-v8a/"

echo "== packaging"
"$BT/aapt2" link -I "$PLAT/android.jar" --manifest "$R/android/AndroidManifest.xml" \
    --min-sdk-version 28 --target-sdk-version 35 -o "$B/base.apk" >/dev/null
(cd "$B" && zip -q -r base.apk classes.dex lib)
"$BT/zipalign" -f 4 "$B/base.apk" "$B/aligned.apk"
"$BT/apksigner" sign --ks "$HOME/.android/debug.keystore" --ks-pass pass:android \
    --ks-key-alias androiddebugkey --key-pass pass:android --out "$B/nativ.apk" "$B/aligned.apk"
echo "   $(du -h "$B/nativ.apk" | cut -f1) $B/nativ.apk"

echo "== installing"
"${ADB[@]}" install -r "$B/nativ.apk"

FILES=/sdcard/Android/data/$PKG/files
GDIR=$FILES/$TITLE
"${ADB[@]}" shell mkdir -p "$GDIR/fs/savedata0" "$GDIR/shader_cache"
if [[ -z ${APK_ONLY:-} ]]; then
  echo "== pushing the game (this is the slow part over wireless adb)"
  for f in "$IMAGE" $MODS; do "${ADB[@]}" push -q "$OUT/$f" "$GDIR/$f" >/dev/null; done
  # fs/app0 is a link on the desktop; its target is what the device needs
  "${ADB[@]}" push "$(readlink -f "$OUT/fs/app0")" "$GDIR/fs/app0"
fi

# ---- launch.ini: the arguments a desktop passes on the command line (runtime/platform/android/android_main.c)
MODPATHS=$(for f in $MODS; do printf '%s,' "$GDIR/$f"; done); MODPATHS=${MODPATHS%,}
{
  echo "# written by dev/android-install.sh -- the launch runtime/platform/android/android_main.c reads"
  echo "image=$GDIR/$IMAGE"
  echo "entry=$ENTRY"
  echo "budget=100000000000"
  echo "RECOMP_FS=$GDIR/fs"
  [[ -n $MODPATHS ]] && echo "RECOMP_MODULES=$MODPATHS"
  echo "RECOMP_SETTINGS=$GDIR/settings.ini"
  echo "RECOMP_SHADER_CACHE=$GDIR/shader_cache"
  echo "RECOMP_TITLE=$TITLEID"
  echo "RECOMP_GAME_NAME=$GAMENAME"
  echo "RECOMP_HITCH_LOG=50"
  for kv in $RUNENV; do echo "$kv"; done
} > "$B/launch.ini"
"${ADB[@]}" push -q "$B/launch.ini" "$FILES/launch.ini" >/dev/null
echo "== ready: $GAMENAME on $PKG"
echo "   start it:  adb shell monkey -p $PKG 1"
echo "   watch it:  adb logcat -s nativ"

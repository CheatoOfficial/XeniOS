#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: tools/build_apple_release.sh [options]

Builds and packages Apple release artifacts:
- macOS arm64 DMG
- macOS x86_64 DMG
- iOS arm64 ad-hoc-signed IPA

Options:
  --out DIR            Output directory (default: scratch/artifacts)
  --config NAME        checked|debug|release|valgrind (default: release)
  --ios-min VERSION    iOS minimum version (default: 16.0)
  --macos-min VERSION  macOS minimum version (default: 15.0)
  --mac-sign IDENTITY  macOS codesign identity (default: ad-hoc '-')
  --skip-ios
  --skip-macos-arm64
  --skip-macos-x86_64
  -h, --help

Notes:
- iOS packaging creates an ad-hoc-signed .ipa suitable for re-signing.
- This script expects Xcode command line tools (xcodebuild, codesign, hdiutil).
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

need_bin() {
  command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"
}

cap_config() {
  # checked -> Checked, debug -> Debug, release -> Release, valgrind -> Valgrind
  local s
  s="$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')"
  if [ -z "$s" ]; then
    return 0
  fi
  local first rest
  first="$(printf '%s' "$s" | cut -c1 | tr '[:lower:]' '[:upper:]')"
  rest="$(printf '%s' "$s" | cut -c2-)"
  printf '%s%s' "$first" "$rest"
}

repo_root() {
  cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd
}

find_first_app() {
  local dir="$1"
  local app=""
  app="$(find "$dir" -maxdepth 1 -type d -name "*.app" | LC_ALL=C sort | head -n 1 || true)"
  [ -n "$app" ] || return 1
  printf '%s' "$app"
}

resolve_macdeployqt() {
  if [ -n "${MACDEPLOYQT:-}" ] && [ -x "${MACDEPLOYQT}" ]; then
    echo "${MACDEPLOYQT}"
    return 0
  fi
  if command -v macdeployqt >/dev/null 2>&1; then
    command -v macdeployqt
    return 0
  fi
  local candidates=(
    "/opt/homebrew/opt/qt/bin/macdeployqt"
    "/usr/local/opt/qt/bin/macdeployqt"
  )
  for c in "${candidates[@]}"; do
    if [ -x "$c" ]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

normalize_version() {
  local v="${1:-0}"
  awk -v v="$v" '
    BEGIN {
      n = split(v, a, ".")
      for (i = 1; i <= 3; ++i) {
        printf("%d", (i <= n ? a[i] + 0 : 0))
        if (i < 3) printf(".")
      }
    }'
}

version_eq() {
  [ "$(normalize_version "$1")" = "$(normalize_version "$2")" ]
}

version_le() {
  local a b
  a="$(normalize_version "$1")"
  b="$(normalize_version "$2")"
  awk -v a="$a" -v b="$b" '
    BEGIN {
      split(a, av, ".")
      split(b, bv, ".")
      for (i = 1; i <= 3; ++i) {
        ai = av[i] + 0
        bi = bv[i] + 0
        if (ai < bi) { exit 0 }
        if (ai > bi) { exit 1 }
      }
      exit 0
    }'
}

dylib_macos_deployment_target() {
  local dylib="$1"
  otool -l "$dylib" | awk '
    $1 == "cmd" && $2 == "LC_BUILD_VERSION" { mode = "build"; next }
    $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" { mode = "legacy"; next }
    $1 == "cmd" { mode = "" }
    mode == "build" && $1 == "minos" { print $2; exit }
    mode == "legacy" && $1 == "version" { print $2; exit }
  '
}

deploy_qt_if_missing() {
  local app_bundle="$1"
  local qt_core="$app_bundle/Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
  local qcocoa="$app_bundle/Contents/PlugIns/platforms/libqcocoa.dylib"

  if [ -f "$qt_core" ] && [ -f "$qcocoa" ]; then
    echo "Qt already bundled by post-build step; skipping extra macdeployqt pass."
    return 0
  fi

  local macdeployqt=""
  if macdeployqt="$(resolve_macdeployqt)"; then
    "$macdeployqt" "$app_bundle" -always-overwrite \
      -libpath=third_party/metal-shader-converter/lib \
      -libpath=third_party/DirectXShaderCompiler/build_dxilconv_macos/lib \
      -libpath=third_party/DirectXShaderCompiler/build_dxilconv_macos_x86_64/lib \
      -libpath=/opt/homebrew/opt/sdl2/lib \
      -libpath=/usr/local/opt/sdl2/lib
  else
    echo "warning: macdeployqt not found; app may be missing Qt frameworks"
  fi
}

require_sdl2_deployment_target() {
  local app_bundle="$1"
  local required_target="$2"
  local app_frameworks="$app_bundle/Contents/Frameworks"
  local sdl_dylib="$app_frameworks/libSDL2-2.0.0.dylib"
  [ -f "$sdl_dylib" ] || die "bundled SDL2 not found: $sdl_dylib"

  local actual_target
  actual_target="$(dylib_macos_deployment_target "$sdl_dylib")"
  [ -n "$actual_target" ] || die "unable to read deployment target from $sdl_dylib"

  if ! version_le "$actual_target" "$required_target"; then
    die "SDL2 deployment target too new: got $actual_target, app target is $required_target ($sdl_dylib)"
  fi

  if version_eq "$actual_target" "$required_target"; then
    echo "SDL2 deployment target OK: $actual_target ($sdl_dylib)"
  else
    echo "SDL2 deployment target compatible: $actual_target (app target: $required_target) ($sdl_dylib)"
  fi
}

sign_macos_app() {
  local app_bundle="$1"
  local entitlements="$2"
  local identity="${3:--}"

  local main_exe="$app_bundle/Contents/MacOS/xenia_edge"
  local frameworks_dir="$app_bundle/Contents/Frameworks"
  local plugins_dir="$app_bundle/Contents/PlugIns"

  if [ -f "$main_exe" ]; then
    codesign --remove-signature "$main_exe" >/dev/null 2>&1 || true
  fi
  codesign --remove-signature "$app_bundle" >/dev/null 2>&1 || true

  if [ -d "$frameworks_dir" ]; then
    find "$frameworks_dir" -maxdepth 2 -name "*.framework" -type d -print0 | \
      xargs -0 -I{} codesign --remove-signature "{}" >/dev/null 2>&1 || true
    find "$frameworks_dir" -name "*.dylib" -type f -print0 | \
      xargs -0 -I{} codesign --remove-signature "{}" >/dev/null 2>&1 || true

    find "$frameworks_dir" -maxdepth 2 -name "*.framework" -type d -print0 | \
      xargs -0 -I{} codesign --force --sign "$identity" --timestamp=none "{}"
    find "$frameworks_dir" -name "*.dylib" -type f -print0 | \
      xargs -0 -I{} codesign --force --sign "$identity" --timestamp=none "{}"
  fi

  if [ -d "$plugins_dir" ]; then
    find "$plugins_dir" -name "*.dylib" -type f -print0 | \
      xargs -0 -I{} codesign --remove-signature "{}" >/dev/null 2>&1 || true
    find "$plugins_dir" -name "*.dylib" -type f -print0 | \
      xargs -0 -I{} codesign --force --sign "$identity" --timestamp=none "{}"
  fi

  if [ -f "$main_exe" ]; then
    codesign --force --sign "$identity" --timestamp=none "$main_exe"
  fi

  codesign --force --deep --timestamp=none --sign "$identity" \
    --entitlements "$entitlements" "$app_bundle"

  # Some Homebrew dylibs can be copied with read-only mode and carry provenance
  # xattrs, making recursive xattr clearing fail with EPERM.
  if ! xattr -cr "$app_bundle" >/dev/null 2>&1; then
    chmod -R u+w "$app_bundle" >/dev/null 2>&1 || true
    if ! xattr -cr "$app_bundle" >/dev/null 2>&1; then
      echo "warning: failed to clear one or more extended attributes in $app_bundle"
    fi
  fi
}

package_macos_dmg() {
  local app_bundle="$1"
  local dmg_out="$2"
  local license_file="$3"

  rm -f "$dmg_out"
  local dmg_contents
  dmg_contents="$(mktemp -d "${TMPDIR:-/tmp}/xenia_dmg.XXXXXX")"
  mkdir -p "$dmg_contents"
  cp -R "$app_bundle" "$dmg_contents/"
  cp -f "$license_file" "$dmg_contents/" || true
  ln -s /Applications "$dmg_contents/Applications"
  hdiutil create -volname "Xenia-Edge" -srcfolder "$dmg_contents" -ov -format UDZO "$dmg_out"
  rm -rf "$dmg_contents"
}

package_ios_ipa() {
  local app_bundle="$1"
  local ipa_out="$2"

  # Make the output absolute because we `cd` into a temp directory.
  local ipa_dir ipa_base
  ipa_dir="$(cd "$(dirname "$ipa_out")" && pwd)"
  ipa_base="$(basename "$ipa_out")"
  ipa_out="$ipa_dir/$ipa_base"

  rm -f "$ipa_out"
  local tmp
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/xenia_ipa.XXXXXX")"
  mkdir -p "$tmp/Payload"
  # ditto preserves bundle metadata better than cp -R on macOS.
  ditto "$app_bundle" "$tmp/Payload/$(basename "$app_bundle")"
  (cd "$tmp" && ditto -c -k --sequesterRsrc --keepParent "Payload" "$ipa_out")
  rm -rf "$tmp"
}

build_ios=1
build_macos_arm64=1
build_macos_x86_64=1
out_dir="scratch/artifacts"
config="release"
ios_min="16.0"
macos_min="15.0"
mac_sign_identity="-"

while [ $# -gt 0 ]; do
  case "$1" in
    --out)
      out_dir="${2:-}"; shift 2;;
    --config)
      config="${2:-}"; shift 2;;
    --ios-min)
      ios_min="${2:-}"; shift 2;;
    --macos-min)
      macos_min="${2:-}"; shift 2;;
    --mac-sign)
      mac_sign_identity="${2:-}"; shift 2;;
    --skip-ios)
      build_ios=0; shift;;
    --skip-macos-arm64)
      build_macos_arm64=0; shift;;
    --skip-macos-x86_64)
      build_macos_x86_64=0; shift;;
    -h|--help)
      usage; exit 0;;
    *)
      die "unknown argument: $1";;
  esac
done

root="$(repo_root)"
cd "$root"

# Avoid stale iOS sentinel state leaking into normal macOS premake runs.
rm -f .ios_target

if [ "$(uname -s)" != "Darwin" ]; then
  die "this script must be run on macOS"
fi

need_bin xcodebuild
need_bin codesign
need_bin hdiutil
need_bin ditto
need_bin xattr
need_bin otool

mkdir -p "$out_dir"

buildcfg="$(cap_config "$config")"

echo "Config: $buildcfg"
echo "Output: $out_dir"
echo "macOS min: $macos_min"
echo "iOS min: $ios_min"
echo "macOS signing: $mac_sign_identity"

if [ "$build_macos_arm64" -eq 1 ]; then
  echo ""
  echo "== macOS arm64 =="
  ./xb build --config="$buildcfg" --arch=arm64 --target=xenia-app -- \
    CODE_SIGNING_ALLOWED=NO \
    MACOSX_DEPLOYMENT_TARGET="$macos_min"

  mac_dir="build/bin/Mac-ARM64/$buildcfg"
  app_bundle="$(find_first_app "$mac_dir")" || die "macOS arm64 app not found in $mac_dir"

  mkdir -p "$app_bundle/Contents/Resources"
  if [ -f assets/icon/xenia.icns ]; then
    cp -f assets/icon/xenia.icns "$app_bundle/Contents/Resources/"
  fi

  deploy_qt_if_missing "$app_bundle"

  if [ -f third_party/metal-shader-converter/lib/libmetalirconverter.dylib ]; then
    mkdir -p "$app_bundle/Contents/Frameworks"
    cp -f third_party/metal-shader-converter/lib/libmetalirconverter.dylib \
      "$app_bundle/Contents/Frameworks/" || true
  fi
  if [ -f third_party/DirectXShaderCompiler/build_dxilconv_macos/lib/libdxilconv.dylib ]; then
    mkdir -p "$app_bundle/Contents/Frameworks"
    cp -f third_party/DirectXShaderCompiler/build_dxilconv_macos/lib/libdxilconv.dylib \
      "$app_bundle/Contents/Frameworks/" || true
  fi

  require_sdl2_deployment_target "$app_bundle" "$macos_min"

  sign_macos_app "$app_bundle" "xenia.entitlements" "$mac_sign_identity"
  package_macos_dmg "$app_bundle" "$out_dir/xenia_edge_macos_arm64.dmg" "LICENSE"
fi

if [ "$build_macos_x86_64" -eq 1 ]; then
  echo ""
  echo "== macOS x86_64 =="
  ./xb build --config="$buildcfg" --arch=x86_64 --target=xenia-app -- \
    CODE_SIGNING_ALLOWED=NO \
    MACOSX_DEPLOYMENT_TARGET="$macos_min"

  mac_dir="build/bin/Mac-x86_64/$buildcfg"
  app_bundle="$(find_first_app "$mac_dir")" || die "macOS x86_64 app not found in $mac_dir"

  mkdir -p "$app_bundle/Contents/Resources"
  if [ -f assets/icon/xenia.icns ]; then
    cp -f assets/icon/xenia.icns "$app_bundle/Contents/Resources/"
  fi

  deploy_qt_if_missing "$app_bundle"

  if [ -f third_party/metal-shader-converter/lib/libmetalirconverter.dylib ]; then
    mkdir -p "$app_bundle/Contents/Frameworks"
    cp -f third_party/metal-shader-converter/lib/libmetalirconverter.dylib \
      "$app_bundle/Contents/Frameworks/" || true
  fi
  if [ -f third_party/DirectXShaderCompiler/build_dxilconv_macos_x86_64/lib/libdxilconv.dylib ]; then
    mkdir -p "$app_bundle/Contents/Frameworks"
    cp -f third_party/DirectXShaderCompiler/build_dxilconv_macos_x86_64/lib/libdxilconv.dylib \
      "$app_bundle/Contents/Frameworks/" || true
  fi

  require_sdl2_deployment_target "$app_bundle" "$macos_min"

  sign_macos_app "$app_bundle" "xenia.entitlements" "$mac_sign_identity"
  package_macos_dmg "$app_bundle" "$out_dir/xenia_edge_macos_x86_64.dmg" "LICENSE"
fi

if [ "$build_ios" -eq 1 ]; then
  echo ""
  echo "== iOS arm64 (ad-hoc-signed ipa) =="

  ./xb premake --target_os=ios
  ./xb build --config="$buildcfg" --target_os=ios --no_premake --target=xenia-app -- \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGN_STYLE=Manual \
    DEVELOPMENT_TEAM= \
    PROVISIONING_PROFILE_SPECIFIER= \
    CODE_SIGN_IDENTITY= \
    IPHONEOS_DEPLOYMENT_TARGET="$ios_min"

  ios_dir="build/bin/iOS-ARM64/$buildcfg"
  app_bundle="$(find_first_app "$ios_dir")" || die "iOS app not found in $ios_dir"

  # Ad-hoc sign to embed entitlements (increased-memory-limit).
  # Re-signing tools will preserve these when applying a real identity.
  codesign --force --sign - --entitlements "$root/xenia_ios.entitlements" "$app_bundle"

  package_ios_ipa "$app_bundle" "$out_dir/xenia_edge_ios_arm64_adhoc.ipa"
fi

echo ""
echo "Done. Artifacts:"
find "$out_dir" -maxdepth 1 -type f \( -name "*.dmg" -o -name "*.ipa" \) -print

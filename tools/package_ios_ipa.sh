#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: tools/package_ios_ipa.sh APP_BUNDLE REQUESTED_ENTITLEMENTS IPA_OUT [REQUESTED_OUT] [SIGNED_OUT]

Packages an iOS .app bundle as an IPA and emits entitlement helper files:
- REQUESTED_OUT: the entitlements plist the build asks codesign/Xcode to use.
- SIGNED_OUT: the entitlements extracted from the app's current code signature.

The IPA is still only installable on stock iOS after a real signing tool
re-signs it with a provisioning profile that permits the requested entitlements.
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ "$#" -lt 3 ] || [ "$#" -gt 5 ]; then
  usage >&2
  exit 1
fi

app_bundle="$1"
requested_entitlements="$2"
ipa_out="$3"
requested_out="${4:-}"
signed_out="${5:-}"

[ -d "$app_bundle" ] || die "app bundle not found: $app_bundle"
[ -f "$requested_entitlements" ] || die "requested entitlements not found: $requested_entitlements"
command -v ditto >/dev/null 2>&1 || die "missing required tool: ditto"
command -v codesign >/dev/null 2>&1 || die "missing required tool: codesign"

ipa_dir="$(cd "$(dirname "$ipa_out")" && pwd)"
ipa_base="$(basename "$ipa_out")"
ipa_out="$ipa_dir/$ipa_base"
mkdir -p "$ipa_dir"

if [ -n "$requested_out" ]; then
  requested_dir="$(dirname "$requested_out")"
  mkdir -p "$requested_dir"
  cp -f "$requested_entitlements" "$requested_out"
fi

if [ -n "$signed_out" ]; then
  signed_dir="$(dirname "$signed_out")"
  mkdir -p "$signed_dir"
  if ! codesign -d --entitlements :- "$app_bundle" >"$signed_out.tmp" 2>/dev/null; then
    rm -f "$signed_out.tmp"
    die "failed to extract signed entitlements from $app_bundle"
  fi
  mv -f "$signed_out.tmp" "$signed_out"
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/xenios_ipa.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/Payload"
ditto "$app_bundle" "$tmp/Payload/$(basename "$app_bundle")"
rm -f "$ipa_out"
(cd "$tmp" && ditto -c -k --sequesterRsrc --keepParent Payload "$ipa_out")

echo "IPA: $ipa_out"
if [ -n "$requested_out" ]; then
  echo "Requested entitlements: $requested_out"
fi
if [ -n "$signed_out" ]; then
  echo "Signed entitlements: $signed_out"
fi

#!/usr/bin/env bash
# Builds an (optionally signed) macOS .pkg installing VST3 + AU system-wide.
# Usage: build_pkg.sh <artefacts_dir> <version> <out_dir>
# Optional env: MAC_INSTALLER_SIGN_ID ("Developer ID Installer: ..."), notarization done by the workflow.
set -euo pipefail
ART="$1"; VERSION="$2"; OUT="$3"
COMPANY="TMFU audiovibe"
PRODUCT="CHROMA5YNTH"
BUNDLE="com.tmfuaudiovibe.chroma5ynth"
WORK="$(mktemp -d)"
mkdir -p "$WORK/vst3/Library/Audio/Plug-Ins/VST3" "$WORK/au/Library/Audio/Plug-Ins/Components" "$OUT"

cp -R "$ART/VST3/$PRODUCT.vst3" "$WORK/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/$PRODUCT.component" "$WORK/au/Library/Audio/Plug-Ins/Components/"

pkgbuild --root "$WORK/vst3" --identifier "$BUNDLE.vst3" --version "$VERSION" --install-location / "$WORK/vst3.pkg"
pkgbuild --root "$WORK/au"   --identifier "$BUNDLE.au"   --version "$VERSION" --install-location / "$WORK/au.pkg"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$PRODUCT $VERSION – $COMPANY</title>
  <organization>$BUNDLE</organization>
  <options customize="allow" require-scripts="false" hostArchitectures="x86_64,arm64"/>
  <os-version min="10.13"/>
  <choices-outline>
    <line choice="vst3"/>
    <line choice="au"/>
  </choices-outline>
  <choice id="vst3" title="$PRODUCT VST3" description="/Library/Audio/Plug-Ins/VST3"><pkg-ref id="$BUNDLE.vst3"/></choice>
  <choice id="au" title="$PRODUCT Audio Unit" description="/Library/Audio/Plug-Ins/Components"><pkg-ref id="$BUNDLE.au"/></choice>
  <pkg-ref id="$BUNDLE.vst3" version="$VERSION">vst3.pkg</pkg-ref>
  <pkg-ref id="$BUNDLE.au" version="$VERSION">au.pkg</pkg-ref>
</installer-gui-script>
XML

SIGN_ARGS=()
if [[ -n "${MAC_INSTALLER_SIGN_ID:-}" ]]; then
  echo "Building signed installer..."
  productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK" \
    --sign "$MAC_INSTALLER_SIGN_ID" \
    "$OUT/${PRODUCT}-${VERSION}-macOS.pkg"
else
  echo "Building unsigned installer..."
  productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK" \
    "$OUT/${PRODUCT}-${VERSION}-macOS.pkg"
fi

rm -rf "$WORK"
echo "Built $OUT/${PRODUCT}-${VERSION}-macOS.pkg"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "${SIGN_ARGS[@]}" \
  "$OUT/${PRODUCT}-${VERSION}-macOS.pkg"
rm -rf "$WORK"
echo "Built $OUT/${PRODUCT}-${VERSION}-macOS.pkg"

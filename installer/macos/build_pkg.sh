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

# VST3 and AU share the same CFBundleIdentifier (JUCE BUNDLE_ID): with relocatable bundles (pkgbuild default)
# Installer "relocates" the VST3 onto the existing .component and only the AU survives. Force fixed paths.
PB=/usr/libexec/PlistBuddy
for KIND in vst3 au; do
  pkgbuild --analyze --root "$WORK/$KIND" "$WORK/$KIND-components.plist"
  N=$($PB -c "Print" "$WORK/$KIND-components.plist" | grep -c "BundleIsRelocatable" || true)
  for ((i = 0; i < N; i++)); do $PB -c "Set :$i:BundleIsRelocatable false" "$WORK/$KIND-components.plist"; done
done

pkgbuild --root "$WORK/vst3" --component-plist "$WORK/vst3-components.plist" --identifier "$BUNDLE.vst3" --version "$VERSION" --install-location / "$WORK/vst3.pkg"
pkgbuild --root "$WORK/au"   --component-plist "$WORK/au-components.plist"   --identifier "$BUNDLE.au"   --version "$VERSION" --install-location / "$WORK/au.pkg"

for KIND in vst3 au; do
  rm -rf "$WORK/check-$KIND"; pkgutil --expand "$WORK/$KIND.pkg" "$WORK/check-$KIND"
  if grep -q "<relocate>" "$WORK/check-$KIND/PackageInfo"; then echo "ERROR: $KIND package is relocatable"; exit 1; fi
done

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

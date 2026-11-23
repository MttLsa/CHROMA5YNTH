; Inno Setup script – CHROMA5YNTH VST3 (Windows x64)
; Build: iscc /DAppVersion=0.1.0 /DSourceDir=<VST3 artefacts dir> /DOutDir=<out> chroma5ynth.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\CHROMA5YNTH_artefacts\Release\VST3"
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif

[Setup]
AppId={{E1008942-ADBE-4B38-A2CB-6438DAF4757F}
AppName=CHROMA5YNTH
AppVersion={#AppVersion}
AppVerName=CHROMA5YNTH {#AppVersion}
AppPublisher=TMFU audiovibe
VersionInfoCompany=TMFU audiovibe
VersionInfoProductName=CHROMA5YNTH
VersionInfoVersion={#AppVersion}
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutDir}
OutputBaseFilename=CHROMA5YNTH-{#AppVersion}-Windows-x64
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=CHROMA5YNTH (TMFU audiovibe)

[Files]
Source: "{#SourceDir}\CHROMA5YNTH.vst3\*"; DestDir: "{commoncf64}\VST3\CHROMA5YNTH.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\CHROMA5YNTH.vst3"

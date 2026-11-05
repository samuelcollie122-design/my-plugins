; One installer for SpaceTrack + SpaceMaster + MotionShaper + OptoComp (VST3). Built by build-installers.ps1
#define AppName "SpaceTrack, SpaceMaster, MotionShaper and OptoComp"
#define AppVersion "1.0.0"
#ifndef SpaceBuildDir
  #error Pass /DSpaceBuildDir=<path to the SpaceTrack cmake build folder>
#endif
#ifndef MotionBuildDir
  #error Pass /DMotionBuildDir=<path to the MotionShaper cmake build folder>
#endif
#ifndef OptoBuildDir
  #error Pass /DOptoBuildDir=<path to the OptoComp cmake build folder>
#endif

[Setup]
AppId={{E81B7FAA-512F-4F3C-B46F-CF442B0FFB3A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Personal
CreateAppDir=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=PluginSuite-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} (VST3)

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This installs your VST3 plugins into C:\Program Files\Common Files\VST3.%n%nPlease close FL Studio before continuing.
FinishedLabel=The plugins are installed.%n%nOpen FL Studio, go to Options > Manage plugins and click Find installed plugins.

[Types]
Name: "full"; Description: "Full installation (all plugins)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "space"; Description: "SpaceTrack + SpaceMaster (SpaceMaster needs SpaceTrack)"; Types: full custom
Name: "motion"; Description: "MotionShaper"; Types: full custom
Name: "opto"; Description: "OptoComp"; Types: full custom

[Files]
Source: "{#SpaceBuildDir}\SpaceTrack_artefacts\Release\VST3\SpaceTrack.vst3\*"; DestDir: "{commoncf64}\VST3\SpaceTrack.vst3"; Components: space; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SpaceBuildDir}\SpaceMaster_artefacts\Release\VST3\SpaceMaster.vst3\*"; DestDir: "{commoncf64}\VST3\SpaceMaster.vst3"; Components: space; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#MotionBuildDir}\MotionShaper_artefacts\Release\VST3\MotionShaper.vst3\*"; DestDir: "{commoncf64}\VST3\MotionShaper.vst3"; Components: motion; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#OptoBuildDir}\OptoComp_artefacts\Release\VST3\OptoComp.vst3\*"; DestDir: "{commoncf64}\VST3\OptoComp.vst3"; Components: opto; Flags: ignoreversion recursesubdirs createallsubdirs

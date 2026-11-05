BUILDING THE INSTALLER (Windows)

1. Unzip the whole PluginSuite folder to a short path with no spaces, e.g. C:\dev\PluginSuite
   (not OneDrive / Desktop / Downloads).
2. Needed once: Visual Studio with "Desktop development with C++" and "C++ CMake tools for Windows",
   and Git for Windows. (Inno Setup is installed automatically through winget if it is missing.)
3. Double-click Installer\build-installers.bat and wait. The first build takes several minutes.
4. The setup file appears in PluginSuite\Installers:
      PluginSuite-Setup.exe     installs SpaceTrack, SpaceMaster and MotionShaper
5. Close FL Studio, run the setup (it asks for administrator rights), then in FL Studio use
   Options > Manage plugins > Find installed plugins.

Uninstall: Windows Settings > Apps > "SpaceTrack, SpaceMaster and MotionShaper" > Uninstall.

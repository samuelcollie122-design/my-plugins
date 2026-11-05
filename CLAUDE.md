# PluginSuite: handoff notes for Claude Code

This project was started in a claude.ai chat. That chat could write code but could NOT compile JUCE or Windows code.
Claude Code has since built all three plugins, but much of this is still untested. Read "Status" before trusting anything.

## The user and the environment
- Windows PC, FL Studio. Makes VST3 plugins for personal use. Wants things explained step by step, plainly.
- Visual Studio Community 2026, Git for Windows, CMake. JUCE 8.0.4 is fetched by CMake (FetchContent), no manual install.
- Generator that worked for the user: `cmake -B build -G "Visual Studio 18 2026" -A x64`, then
  `cmake --build build --config Release`. Output: `build\<Name>_artefacts\Release\VST3\<Name>.vst3` (a folder).
- Install location: `C:\Program Files\Common Files\VST3` (needs admin). FL Studio: Options > Manage plugins > Find installed plugins.
  FL locks the plugin file while open, so close FL before overwriting.

## Layout
- `SpaceTrack/`   builds TWO plugins: SpaceTrack (per track) and SpaceMaster (control surface, passes audio through).
- `MotionShaper/` builds one plugin: a tempo-synced step sequencer with a drawable envelope per step.
- `Installer/`    Inno Setup script + PowerShell/batch script that builds everything into one `PluginSuite-Setup.exe`.
- `.github/workflows/build-installer.yml`  builds the same installer on GitHub's Windows runner (no local Visual Studio needed).

## Status (be honest about it)
- SpaceTrack / SpaceMaster: the user built and ran several versions, including the 3D mode (they reported that moving the
  sound up/down was hard to hear, so the height cue was strengthened afterwards; not yet re-tested).
  Claude Code built the current source on 2026-11-04 with VS 2026 (Release): no errors, no source changes needed.
- MotionShaper: rebuilt on 2026-11-05 with a new engine and window modelled on a reference image from the user. It builds, and
  `MotionShaperTest` passes (headless: presets stable, each effect, bypass, segments, swing, sidechain ducking, save/load, undo).
  Window pictures were compared with the image by eye. On 2026-11-05 the user loaded that version and reported "it works".
  Rebuilt later that day with 8 more effects, 16 more presets, Random, Save and the stopped-transport change: `MotionShaperTest` passes
  (stopped/playing with a pretend host, pitch accuracy, preset file round trip), but this version has NOT been loaded in FL Studio.
  Not exercised by Claude: mouse actions, menus, the native save dialog, how the new effects sound, FL's sidechain routing.
- The 2026-11-04 SpaceTrack and SpaceMaster builds produced `moduleinfo.json`. No build was copied to the VST3 folder; SpaceTrack and SpaceMaster UI and audio from those builds are unverified.
- SpaceTrack HRTF and shared room (2026-11-05): `SpaceTrackDspTest` passes at 44.1, 48 and 96 kHz, but NOBODY HAS HEARD IT. Whether height and front/back now sound convincing is unknown.
- All three plugins link the Visual C++ runtime statically (checked with dumpbin on 2026-11-05), so a new PC needs no Redistributable.
- Code lives in the private repo https://github.com/samuelcollie122-design/my-plugins (branch main). Every push to main runs the workflow, which publishes `PluginSuite-Setup.exe` to the Releases page under the tag `latest`.
- `Installer/build-installers.bat` has never been run. See the repo's Actions tab for the state of the GitHub workflow.
- No test has been done inside FL Studio for any of it beyond what the user reported.

## SpaceTrack + SpaceMaster: how it works
- Goal: move each mixer track's sound around a stereo/3D field, with one master window showing all tracks as dots.
- FL's mixer is stereo, so "3D" means binaural-style cues for headphones, not real surround or Atmos.
- Plugins talk through Windows shared memory (`Source/SharedRegistry.h/.cpp`, mapping name `Local\SpaceTrackRegistry_v4`).
  Up to 64 slots. Each SpaceTrack instance claims a slot (owner id, heartbeat), publishes x/y/z, mode, colour, name.
  The master writes commands (position, mode, colour, name) using sequence counters; the track applies them.
  RULE: if `SharedSlot` or `SharedRegistry` changes, bump the mapping name (v4 -> v5) so old and new DLLs never share memory.
- Track parameters (host-visible, automatable): `x` (left/right), `y` (back/front), `z` (height), all -1..1, and `mode3d` (bool).
  Name and colour are NOT parameters; they are properties on `apvts.state` (`trackName`, `trackColor` as ARGB int, 0 = automatic).
- Undo history: dragging in the master sets `dragging`/`dragBeat` in the slot so the track holds ONE parameter gesture for
  the whole drag. Keep that behaviour, the user did not want undo history flooded.
- DSP (`PluginProcessor.cpp`, `HrtfEngine.h`): a 2D path (equal-power pan, lowpass and level drop and narrower width when behind) and a 3D path, crossfaded by `mode3d`.
  3D path: mono point source through a measured HRTF (MIT KEMAR, 710 directions, `Assets/hrtf_kemar.bin`, credit in `Assets/README.txt`, rebuilt with `Tools/convert_hrtf.py`).
  Each direction is a minimum-phase, diffuse-field equalised response plus a separate interaural delay, blended from the 3 nearest directions. Other sample rates are windowed-sinc
  resampled; `Table::lead` keeps the pre-ringing and `Voice` delays its dry signal to match. Below -40 degrees (no measurements) it fades to a 2.5 kHz lowpass.
- Shared room (3D only): `EarlyRoom` (6 first reflections from image sources) plus a `juce::Reverb` tail, per track. Size and amount are set in SpaceMaster, published through `SharedRegistry`
  (`roomSize`, `roomAmount`, `roomBeat`), and each track keeps a copy in its state so it still works without SpaceMaster. Not parameters, so not automatable.
- Possible upgrades: object size, a choice of HRTF sets, better distance cues. FL's mixer is stereo, so real speaker layouts and real Atmos files stay out of reach. Do not use Dolby's names.
- Flat stereo or 3D: `mode3d` is per track. In 2D the height is ignored and it is a normal stereo pan, for speakers; positions are kept when switching. SpaceMaster's "All stereo" and
  "All 3D" buttons switch the SOUND of every track and also move the view. Its 2D/3D switch at the top only changes the VIEW. The switch under SELECTED changes one track.
  Test: `-DSPACETRACK_TESTS=ON`, target `SpaceMasterTest` (private shared memory via `SPACETRACK_MAPPING_NAME`, safe while FL is running). The user plans personal use only, so HRTF data licences allowing personal use are enough.
- UI: `SpatialUI.h` has the shared look, 2D field, 3D room projection, colour palette, 2D/3D switch. Titles "SpaceTrack" and
  "SpaceMaster" are centred at the top. SpaceMaster has a track list panel (select, double-click to rename, scroll),
  per-axis sliders with a "C" centre button each, Center selected, Center all, a 2D/3D view switch, and a Center camera button
  in 3D view (also: double-click empty space).
  Clicking inside a cluster keeps the SELECTED dot; Ctrl-click picks the nearest dot instead.
- Keyboard rule: the user complained that plugin text boxes kept capturing the keyboard. Name editing is double-click, Enter or
  clicking away saves, and `juce::Component::unfocusAllComponents()` releases focus. Slider text boxes are read-only.
  Keep it that way.

## MotionShaper: how it works
- Effect, stereo in/out plus an optional stereo sidechain input. Tempo sync: follows the host position and only moves while the host plays (the user asked for this;
  stopped = the shape holds its place and Noise layers fade out). It free-runs only if the host reports no position at all.
- Up to 6 layers in series (slot order), each one effect: Filter, Crush, Texture, Volume, Pan, Pitch, Drive, Distortion, Noise, Reverb, Width, Delay, Chorus
  (`Source/FxCore.h`, pure C++, except Reverb which is `juce::Reverb`). New effects go at the END of `FxType` and `fxNames()` so saved projects keep their effect. Three generic knobs
  `a`, `b`, `c` change meaning per effect (`knobCaption`, `knobText`; an empty caption hides the knob). The shape moves knob `b` by `depth` (-1..1, the small "S1" ring). Layer output and mix follow.
  Pitch delays the sound by up to its grain length (20-100 ms) and this is not reported to the host.
- Each layer: mode (shape or follow), grid (pattern length: 1/4 = 4 steps ... 1/32 = 32), rate, swing, retrigger (off, sidechain, 1 bar, 2 bars).
- Pattern (`Shape::Pattern`, `Source/ShapeCore.h`, pure C++): 32 steps grouped into segments. A segment has one shape stretched over its steps; steps can be muted (no effect).
  Shapes are baked into 512-sample lookup tables per segment. The audio thread only try-locks `rtLock` and holds the last value during a swap.
  `shapeRevision` tells the editor to reload after presets/paste/undo/project load.
- Sidechain (global): source External or Internal (default Internal, so it works without routing), filter, trigger peak/rms, sensitivity, 3 detection bands with solo and on, listen.
  It drives follow mode and sidechain retrigger. The low/mid/high section splits the DETECTION signal, not the audio.
- Patterns are saved under a `CURVES` child in the state and are not host parameters, so they cannot be automated. Layer settings and knobs ARE parameters
  (`l0_used`, `l0_on`, `l0_fx`, `l0_mode`, `l0_grid`, `l0_rate`, `l0_swing`, `l0_retrig`, `l0_a/b/c`, `l0_depth`, `l0_out`, `l0_mix`, global `mix`, `out`, `smooth`, `sc_*`, `x_low/high`, `b_on_*`, `b_solo_*`).
- Presets: 24 factory ones built in code (`loadFactoryPreset`; add new ones at the end). User presets are `.mspreset` XML files in `%APPDATA%\MotionShaper\Presets`, saved with the
  "save" button beside the preset name, and listed under "My presets" in the preset menu.
- The "random" button in the sequencer panel replaces the selected layer's whole pattern (undoable). Right-click Randomize changes one segment.
- Undo/redo: `proc.undo`. Pattern edits record one `PatternAction` per gesture; call `undo.beginNewTransaction()` BEFORE changing anything in a child's mouseDown,
  because the editor's own mouse listener runs after the child.
- UI (`Source/EditorWidgets.h`, `PluginEditor.*`): fixed 1107x740. Curve editor: double-click adds/removes a point, drag moves, right-drag or wheel bends, Shift toggles snap.
  Focus off shows the whole pattern. Lanes: click selects a segment, drag a bar's right end to merge/split, click a circle to mute, right-click for shape presets, randomize, copy, paste, apply to all.
- Test: configure with `-DMOTIONSHAPER_TESTS=ON`, build target `MotionShaperTest`, run it with a folder for window pictures. Use a short build path (Windows 260 character limit).
- Ideas the user may want next: per-layer phase offset, MIDI retrigger mode, resizable window.

## Installer
- `Installer/build-installers.bat` -> `build-installers.ps1`: finds CMake (also via vswhere), builds both projects, installs Inno Setup
  through winget if missing, runs `Installer/PluginSuite.iss`. Output: `Installers/PluginSuite-Setup.exe` with a component choice
  (SpaceTrack + SpaceMaster, MotionShaper), installs into `{commoncf64}\VST3`.
- The GitHub Actions runner has no "Visual Studio 18 2026", so the workflow lets CMake pick the default generator.
- The .exe is unsigned, so Windows may show a SmartScreen warning on a new PC. The user plans personal use only; sharing or selling would need a look at JUCE's licence terms.

## Suggested next steps
1. Ask the user to install the latest build and judge SpaceTrack's new 3D sound and MotionShaper's new effects by ear in FL Studio.
2. Confirm the installer from GitHub puts three .vst3 folders in the VST3 directory.

## Conventions
- Keep files that are used by Windows scripts (.bat, .ps1, .iss) with CRLF line endings.
- Never claim something works if it was not built or run. Say what was and was not verified.
- Small, focused edits. Do not rewrite the UI style without being asked.

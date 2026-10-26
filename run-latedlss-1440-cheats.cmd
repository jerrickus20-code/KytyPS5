@echo off
rem Late DLSS: the game renders its 3D at 1280x720, its TAA is skipped (the picture kept jittered), its bloom, depth of
rem field and the rest run at 720, then one DLSS super resolution pass anti-aliases and upscales that finished picture to
rem 2560x1440 before the HUD is laid over it (KYTY_LATE_DLSS, src/local/frame-gen.h); the game's output is 2560x1440 too
rem (loader/demonsSoulsHdOutput.h; "set KYTY_LATE_DLSS_OUTPUT=4k" in the console first: its 4K output; other switches
rem the same way, this file's -Set list taking -Set). Needs DLSS frame generation
rem (NVIDIA's Streamline runtime, -StreamlineDir or a "streamline" folder next to the emulator).
rem
rem In game: Shift+F6 late DLSS on/off (off: the game's own TAA and stretch, to compare), F4 the 30 fps cap (60 shown with
rem frame generation), F12 the FPS panel; the switches are remembered in _dlss\toggles.txt.
rem
rem Cheats (optional, in a "cheats" folder next to this file):
rem   PPSA01341_01.007.000.json        the etaHEN-style cheat file (-Patch)
rem   PPSA01341_01.007.000.keys.txt    F1-F3, F5, F11: switch cheats of that file on and off (KYTY_CHEAT_KEYS)
rem   PPSA01341_01.007.000.items.txt   the item list of the give items panel (KYTY_CHEAT_ITEMS)
rem   give.txt                         items to give at once from the panel (KYTY_CHEAT_GIVE_FILE)
rem F10 opens the give items panel (KYTY_CHEAT_GIVE_SLOT, for 01.007.000).
setlocal
set "CHEATS=%~dp0cheats"
set "PATCH="
if exist "%CHEATS%\PPSA01341_01.007.000.json" set PATCH=-Patch "%CHEATS%\PPSA01341_01.007.000.json"
rem (The -Set list quoted: a folder with a space in its path stays one argument.)
if not exist "%~dp0_dlss" mkdir "%~dp0_dlss"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-windows.ps1" -Prompt -Follow -Fullscreen -AspectFit -FrameGen 1 -RenderHeight 720 %PATCH% -Set "KYTY_LATE_DLSS=1440,KYTY_LATE_DLSS_AA=1,KYTY_DLSS_PRESET=M,KYTY_CHEAT_KEYS=%CHEATS%\PPSA01341_01.007.000.keys.txt,KYTY_CHEAT_GIVE_SLOT=1E78A70,KYTY_CHEAT_ITEMS=%CHEATS%\PPSA01341_01.007.000.items.txt,KYTY_CHEAT_GIVE_FILE=%CHEATS%\give.txt,KYTY_TOGGLE_FILE=%~dp0_dlss\toggles.txt" %*
if %errorlevel% neq 0 pause

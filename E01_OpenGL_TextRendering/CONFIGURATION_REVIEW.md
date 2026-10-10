# Konfigurationsbereinigung vom 10.10.2026

## Änderungen

- `E01_OpenGL_TextRendering.vcxproj`: NuGet-Import für GLFW 3.4.0, `EnsureNuGetPackageBuildImports` und den Eintrag für `packages.config` entfernt.
- `E01_OpenGL_TextRendering.vcxproj.filters`: Eintrag für `packages.config` entfernt. Die ausschließlich GLFW betreffende `packages.config` wurde gelöscht.
- vcpkg wird explizit über die vorhandenen MSBuild-Dateien eingebunden. Standardpfad ist `C:\Development\vcpkg\`; `VcpkgRoot` lässt sich extern überschreiben. Classic Mode und Auto-Linking sind aktiviert. Die benutzerspezifische automatische Integration wird nur für dieses Projekt deaktiviert, um doppelte Imports zu vermeiden. Globale Einstellungen bleiben unverändert.
- GLFW wird aus vcpkg verwendet (installiert: glfw3 3.5.1, x64-windows). Der Build kopiert die benötigte Debug-`glfw3.dll` neben die EXE.
- GLAD 0.1.36 wird über die vorhandene Bibliothek im vcpkg-Debug-Lib-Verzeichnis eingebunden. Das wirkungslose Makro `GLAD_GL_IMPLEMENTATION` wurde entfernt; eine eigene GLAD-Quelldatei ist nicht erforderlich. Der erfolgreiche Link bestätigt die Auflösung der verwendeten GLAD-Symbole.
- `main.cpp`: Initialisierung prüft jetzt GLFW, Fenstererzeugung und GLAD. Bei Fehlern wird mit Rückgabewert 1 beendet; nach erfolgreichem Laden werden `GL_VERSION`, `GL_VENDOR`, `GL_RENDERER` und das abgefragte Kontextprofil ausgegeben.
- C++20 und OpenGL 4.6 Core bleiben erhalten. Der Schleifenrumpf der Game Loop bleibt unverändert. Kein TextRendering und keine neuen Bibliotheken.

## Build-Test

Rebuild mit MSBuild 18.10.1, `/p:Configuration=Debug /p:Platform=x64`, Toolset v145 und `/std:c++20`.

Der erste Versuch scheiterte an fehlenden vcpkg-Include-Pfaden (C1083 für `glad/glad.h`). Explizite Projekt-Imports beheben dies. Ein doppelter vcpkg-Import wurde anschließend ebenfalls beseitigt.

Finaler Rebuild: erfolgreich, Exitcode 0, 0 Warnungen und 0 Fehler. Der Linker verwendet `installed\x64-windows\debug\lib`. Vollständiges Protokoll: `build-debug-x64.log`.

Der Build musste außerhalb der Sandbox ausgeführt werden, da der Linker-Lauf innerhalb der Sandbox nicht fertig wurde. Win32 und Release wurden nicht getestet; für Win32 sind keine passenden Pakete in der geprüften vcpkg-Installation nachgewiesen.

## Laufzeittest

Die Debug-x64-EXE wurde gestartet. Nach drei Sekunden lief der Prozess und besaß ein Fenster mit Titel `OpenGL 4.6` und einem gültigen Fensterhandle. Die Initialisierung meldete:

```text
GL_VERSION: 4.6.0 - Build 32.0.101.9034
GL_VENDOR: Intel
GL_RENDERER: Intel(R) Arc(TM) B580 Graphics
Context profile: Core (mask: 1)
```

Die Standardfehlerausgabe war leer. Das Fenster wurde über `CloseMainWindow` geschlossen; der Prozess beendete sich innerhalb der Wartefrist. Fenstererzeugung und Kontextinitialisierung wurden damit technisch bestätigt; eine visuelle Bildschirmkontrolle wurde nicht durchgeführt. Ausgabeprotokolle: `runtime-debug-x64.log` und `runtime-debug-x64.err.log`.

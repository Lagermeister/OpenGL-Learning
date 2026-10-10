# Einfaches 2D-TextRendering

## Dateien und Einbindung

- Neu: `TextRendering.h`, `TextRendering.cpp` (Klasse, Atlas, Shader und Zeichenfunktionen).
- Geändert: `main.cpp` (Initialisierung, drei Textbeispiele plus GUI-Rechteck mit Text, Shutdown vor Kontextzerstörung).
- Geändert: `.vcxproj` und `.vcxproj.filters` (neue Quell-/Headerdateien und vorhandener Font). Das Projekt setzt das Debugger-Arbeitsverzeichnis auf `$(ProjectDir)` und kopiert den Font beim Build nach `$(OutDir)fonts`.
- Vorhandener Font unverändert: `fonts/NotoSans-Regular.ttf`. `Init` verwendet diesen relativen Pfad. Beim Start außerhalb von Visual Studio muss das Arbeitsverzeichnis das Projektverzeichnis oder das EXE-Verzeichnis sein.
- FreeType 2.14.3 ist bereits für `x64-windows` installiert. Header `ft2build.h`, Debug-Bibliothek `freetyped.lib` und DLL kommen aus der bestehenden vcpkg-Installation; das vorhandene Auto-Linking reicht aus. Es wurden keine Bibliotheken installiert und keine globalen Einstellungen geändert.

## Verwendung

Nach Erstellung des OpenGL-Kontextes `Init("fonts/NotoSans-Regular.ttf", 32, 640, 480)` aufrufen und den bool-Rückgabewert prüfen. `DrawText(str, x, y)` zeichnet weißen Text in Originalgröße. Die zweite Überladung nimmt Skalierung und RGB-Farbe entgegen. `DrawRectangle` zeichnet ein gefülltes RGB-Rechteck mit denselben Shadern.

Koordinaten sind Pixelkoordinaten mit Ursprung links oben. `y` bezeichnet den oberen Rand der typografischen Zeile. Die Grundlinie ist `y + ascender * scale`; der sichtbare Buchstabenrand kann je nach Glyph-Metriken etwas unterhalb von `y` liegen. Der horizontale Vorschub nutzt die FreeType-Metrik. Kein Kerning, Shaping oder Zeilenumbruch; Steuerzeichen und Bytes außerhalb des ASCII-Bereichs werden übersprungen. Leerzeichen verschieben nur die Schreibposition.

Die orthografische Projektionsmatrix verwendet die bei `Init` angegebenen Abmessungen. Der Versuch ist auf 640 x 480 ausgelegt; eine dynamische Anpassung bei Fenstergrößenänderung ist nicht implementiert. Der Renderer verwendet den aktuellen Viewport, Framebuffer und gegebenenfalls die aktuelle Scissor-Begrenzung.

## Weg eines Zeichens

1. FreeType lädt den Font einmal und rastert die Zeichen 0 bis 127. Bitmap-Pitch, Größe, Bearing und Vorschub werden berücksichtigt. Danach werden FreeType-Face und -Library freigegeben.
2. Die Graustufen-Bitmaps werden mit transparentem Abstand in einen gemeinsamen `GL_R8`-Atlas kopiert. Lineare Filterung und Clamp-to-edge werden verwendet; keine Mipmaps.
3. `DrawText` berechnet aus Position, Skalierung und Glyph-Metriken ein Quad. Sechs Vertices bilden zwei Dreiecke; die UV-Koordinaten zeigen auf das Zeichen im Atlas.
4. Der Vertex-Shader transformiert Pixelkoordinaten orthografisch. Der Fragment-Shader liest den roten Atlas-Kanal als Alpha und kombiniert ihn mit der gewählten RGB-Farbe. Alpha-Blending mischt das Zeichen über Hintergrund oder GUI-Rechteck.

Beide Shader verwenden `#version 460 core`. VAO/VBO, Textur und Programm werden ohne veraltete OpenGL-Funktionen erstellt. Der einfache Renderer führt pro sichtbarem Zeichen einen Draw Call aus; große Skalierungen können wegen der Bitmap-Basis weich aussehen.

## OpenGL-Zustand und Ressourcen

`Init` verwendet Direct State Access. Für den Atlas-Upload setzt es temporär Pixel-Unpack-Alignment auf 1, Row-Length und Skip-Werte auf 0 und bindet den Pixel-Unpack-Buffer ab. Diese Werte und die Buffer-Bindung werden anschließend wiederhergestellt.

Während `DrawText` und `DrawRectangle` werden temporär geändert und danach wiederhergestellt:

- Shaderprogramm und VAO.
- Aktive Textureinheit sowie 2D-Textur- und Sampler-Bindung auf Einheit 0.
- Blending für Farbausgabe 0: aktiviert, Add-Gleichungen, RGB-Faktoren `SRC_ALPHA` / `ONE_MINUS_SRC_ALPHA`, Alpha-Faktoren `ONE` / `ONE_MINUS_SRC_ALPHA`.
- Farb-Schreibmaske für Ausgabe 0: alle Kanäle aktiviert.
- Depth-Test, Culling und Rasterizer-Discard: deaktiviert.
- Polygonmodus: Fill.

Viewport, Framebuffer, Scissor, Depth-Schreibmaske und Array-Buffer-Bindung bleiben unverändert. Andere Farbausgaben werden nicht durch globale Blend-/ColorMask-Aufrufe verändert. Der Renderer ist für den hier verwendeten gewöhnlichen 2D-Framebuffer vorgesehen; Spezialzustände wie Stencil-Tests oder Color-Logic-Op werden vom Aufrufer bestimmt.

`Shutdown` muss bei noch aktuellem Kontext aufgerufen werden. Es gibt Textur, VBO, VAO und Programm frei und darf mehrfach aufgerufen werden. Die Klasse ist nicht kopierbar und besitzt keinen automatisch GL aufrufenden Destruktor. `Init` räumt bei Shaderfehlern angelegte Ressourcen auf.

## Prüfung am 10.10.2026

- Debug-x64-Rebuild mit MSBuild 18.10.1 und Toolset v145: **0 Warnungen, 0 Fehler**, Exitcode 0. C++20 und OpenGL 4.6 Core bleiben erhalten. Protokoll: `build-text-debug-x64.log`.
- Die Anwendung wurde aus ihrem EXE-Verzeichnis gestartet. Font erfolgreich geladen: `Noto Sans Regular, 32 pixels`. GLFW-Fenster mit gültigem Handle und Titel `OpenGL 4.6` vorhanden. Reguläres Schließen, Exitcode 0; keine Standardfehlerausgabe. Protokolle: `runtime-text-debug-x64.log` und `.err.log`.
- Kontext: `4.6.0 - Build 32.0.101.9034`, Intel, `Intel(R) Arc(TM) B580 Graphics`, Core-Profil (Maske 1).
- Separater Prüfaufbau unter `x64/Debug/verification` verwendet dieselbe Initialisierung und Renderer-Quelldatei, zeichnet die vier Beispiele und liest den Framebuffer aus. Bild `text-preview.png` visuell geprüft: alle Texte korrekt orientiert, unterschiedliche Größen/Farben, GUI-Text über gefülltem Rechteck.
- Nach absichtlich geänderten Blend-Faktoren/-Gleichungen, Depth-Test, Culling, Rasterizer-Discard, Polygonmodus, Farbmaske und aktiver Textureinheit wurden Text und Rechteck gezeichnet. Die abgefragten Zustände waren anschließend identisch; `glGetError()` meldete `GL_NO_ERROR`. Auch doppeltes `Shutdown` wurde ausgeführt. Prüfaufbau: Exitcode 0.
- Release/Win32 und automatische Größenanpassung wurden nicht geprüft. Es wurde kein anderes Projekt verändert.

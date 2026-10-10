# Tutorial: Die vorhandene TextRendering-Implementierung

Dieses Tutorial erklärt `TextRendering.h` und `TextRendering.cpp` sowie ihre Verwendung in `main.cpp`. Es beschreibt den vorhandenen Code, ohne ihn zu erweitern. Der Renderer arbeitet mit C++20, FreeType und OpenGL 4.6 Core; er benötigt keine GUI-Bibliothek oder 3D-Kamera.

## 1. Gesamtüberblick über die Text-Rendering-Pipeline

Die Arbeit verteilt sich auf zwei Phasen. `Init()` lädt und rasterisiert die Schrift einmal auf der CPU. Es speichert Glyph-Metriken und überträgt die Bitmap-Daten in eine OpenGL-Textur. Außerdem erzeugt es Shaderprogramm, VAO und VBO. Danach werden die FreeType-Objekte bereits wieder freigegeben.

Während der Game Loop erzeugt `DrawText()` aus den gespeicherten Metriken für jedes sichtbare Zeichen ein Rechteck. Zwei Dreiecke bilden dieses Rechteck. Die GPU transformiert seine Position, interpoliert seine Texturkoordinaten und liest aus dem Font-Atlas die Abdeckung des Zeichens. Alpha-Blending mischt die gewählte Farbe über den vorhandenen Hintergrund.

```text
Font-Datei -> FreeType -> Glyph-Bitmap und Metriken -> gemeinsamer R8-Atlas
                                                       |
ASCII-Text -> Glyph-Tabelle -> Quad-Vertices und UVs -> Vertex-Shader
                                                       |
                                                Rasterisierung
                                                       |
Atlas-Abdeckung + RGB-Farbe -> Fragment-Shader -> Alpha-Blending -> Framebuffer
```

`main.cpp` initialisiert `TextRendering` nach dem OpenGL-Kontext. Nach dem Löschen des Farbpuffers zeichnet es drei Textbeispiele, ein gefülltes GUI-Rechteck und einen weiteren Text darüber. Die Zeichenreihenfolge bestimmt hier die Überlagerung, weil der Renderer den Tiefentest während seiner Zeichenoperationen deaktiviert.

## 2. Unterschied zwischen Font, Glyph und Bitmap

Ein **Font** ist eine Schriftdatei mit Informationen über die Gestalt und Anordnung von Schriftzeichen. Die vorhandene Datei heißt `fonts/NotoSans-Regular.ttf`. Ein **Glyph** ist eine konkrete zeichnerische Form aus diesem Font. Ein Zeichencode und ein Glyph sind unterschiedliche Begriffe: FreeType ordnet den Zeichencode über die Zeichentabelle des Fonts einem Glyph zu.

Eine **Bitmap** ist das rasterisierte Bild eines Glyphs bei einer bestimmten Pixelgröße. Sie enthält hier Graustufen, die die Abdeckung der einzelnen Pixel beschreiben. Weiß bedeutet volle, Schwarz keine Abdeckung; Zwischenwerte ermöglichen weiche Kanten. Die endgültige Textfarbe ist darin nicht gespeichert.

Die private Struktur `Glyph` speichert die zum Zeichnen benötigten Daten:

| Daten | Bedeutung |
|---|---|
| `width`, `height` | Ausdehnung der rasterisierten Bitmap in Pixeln |
| `bearingX` | Horizontaler Versatz der Bitmap zur Schreibposition |
| `bearingY` | Abstand von der Grundlinie nach oben zur Bitmap-Oberkante |
| `advance` | Horizontaler Vorschub zur Schreibposition des nächsten Zeichens |
| `u0`, `v0`, `u1`, `v1` | Normalisierte Grenzen des Bitmap-Ausschnitts im Atlas |

Bitmap-Breite und Vorschub sind nicht gleich. Ein Leerzeichen verschiebt die Schreibposition, besitzt aber normalerweise keine sichtbare Bitmap. Buchstaben können durch ihre Bearings links oder rechts über den erwarteten Bereich hinausragen.

## 3. FreeType: Laden und Rasterisieren eines Zeichens

`Init()` beginnt mit `Shutdown()`, damit eine erneute Initialisierung keine bisherigen GPU-Ressourcen zurücklässt. Es prüft die Parameter und erzeugt mit `FT_Init_FreeType()` eine Library. `FT_New_Face()` lädt den Font in ein `FT_Face`; `FT_Set_Pixel_Sizes()` legt die gewünschte Pixelhöhe fest. Der relative Font-Pfad wird vom aktuellen Arbeitsverzeichnis aus aufgelöst.

`FT_Load_Char(face, c, FT_LOAD_RENDER)` lädt das dem Zeichencode `c` zugeordnete Glyph und rasterisiert es. Das Ergebnis liegt in `face->glyph`, einem wiederverwendbaren Glyph-Slot. Der nächste Ladeaufruf überschreibt diesen Slot. Deshalb kopiert die Klasse die Metriken in `glyphs[c]` und später die Bitmap-Bytes in den Atlas.

FreeType liefert den Ascender und den horizontalen Vorschub hier als 26.6-Festkommawerte. Sechs Bits stehen für den Bruchteil; die Division durch `64.0f` ergibt Pixelwerte. `bitmap_left` und `bitmap_top` sind dagegen bereits ganzzahlige Pixelwerte.

Die erste Schleife lädt die Codes 0 bis 127, speichert Metriken und ermittelt die größte Bitmap. Die zweite Schleife rasterisiert diese Codes erneut, nachdem die endgültige Atlas-Größe feststeht. Der Code akzeptiert nichtleere Bitmaps nur im Graustufenformat `FT_PIXEL_MODE_GRAY`.

Beim Kopieren ist `bitmap.pitch` wichtig: Es beschreibt den Byte-Abstand zwischen Bitmap-Zeilen, der von der sichtbaren Breite abweichen kann. Das Vorzeichen kennzeichnet die Zeilenrichtung. Die Implementierung verwendet den Betrag für den Zeilenabstand und berücksichtigt die Richtung bei der Quellzeilenauswahl. Kopiert werden jeweils nur `g.width` Bytes.

Nach diesen Kopien geben `FT_Done_Face()` und `FT_Done_FreeType()` die FreeType-Objekte frei. `DrawText()` ruft FreeType nicht mehr auf.

## 4. Aufbau und Zweck des Font-Atlas

Der Atlas fasst alle 128 Glyph-Bitmaps in einer Textur zusammen. Dadurch muss beim Zeichnen nicht für jeden Buchstaben eine andere Textur gebunden werden. Die vorliegende Implementierung verwendet ein einfaches Raster mit 16 Spalten und 8 Zeilen.

Die Zellbreite ist die größte Bitmap-Breite plus zwei Pixel, die Zellhöhe entsprechend die größte Bitmap-Höhe plus zwei Pixel. Jede Bitmap beginnt einen Pixel innerhalb ihrer Zelle. `calloc()` setzt das gesamte CPU-Bild auf null; unbenutzte Flächen und Zellränder bleiben transparent.

Für Zeichencode `c` wird die Bitmap an folgender Stelle abgelegt:

```cpp
ax = (c % 16) * cellWidth + 1;
ay = (c / 16) * cellHeight + 1;
```

Die GPU-Textur verwendet `GL_R8`: Ein 8-Bit-Kanal genügt, weil nur Abdeckung gespeichert wird. `GL_LINEAR` interpoliert zwischen Texeln und erzeugt weiche Übergänge. Die transparenten Zellränder verringern dabei das Einmischen benachbarter Glyphen. `GL_CLAMP_TO_EDGE` betrifft die Außenkante des gesamten Atlas. Es gibt nur eine Texturstufe und keine Mipmaps.

Vor `glTextureSubImage2D()` sichert der Code die verwendeten Pixel-Unpack-Einstellungen und die Pixel-Unpack-Buffer-Bindung. Für das dicht gepackte CPU-Bild setzt er Alignment 1, keine Zeilen-/Pixelversätze und keinen gebundenen PBO. Andernfalls könnten bestehende Upload-Einstellungen die Daten anders interpretieren. Nach dem Upload restauriert er diese Zustände und gibt das CPU-Bild frei.

## 5. Erzeugung der Vertex-Daten und UV-Koordinaten

Der Parameter `y` bezeichnet den oberen **typografischen Zeilenrand**, nicht die Grundlinie und auch nicht zwingend die oberste sichtbare Pixelreihe. Die Grundlinie wird so berechnet:

```cpp
baseline = y + ascender * scale;
```

Ein Glyph-Quad erhält seine Grenzen aus Bearings und Bitmap-Größe:

```cpp
left   = x + g.bearingX * scale;
top    = baseline - g.bearingY * scale;
right  = left + g.width * scale;
bottom = top + g.height * scale;
```

Der Ascender schafft eine gemeinsame vertikale Referenz. Verschiedene Buchstaben bleiben damit auf derselben Grundlinie, auch wenn ihre Bitmaps unterschiedlich hoch sind. Nach jedem unterstützten Zeichen wird `x` um `g.advance * scale` erhöht. Ist seine Bitmap leer, entfällt nur das Zeichnen, nicht der Vorschub.

UV-Koordinaten beschreiben Positionen innerhalb einer Textur, üblicherweise im Bereich 0 bis 1. `Init()` berechnet beispielsweise `u0 = ax / atlasWidth` und `u1 = (ax + width) / atlasWidth`. Die V-Werte entstehen entsprechend aus der vertikalen Atlas-Position. Die erste kopierte Bitmap-Zeile wird der oberen Quad-Kante zugeordnet. Diese konsistente Zuordnung macht einen weiteren vertikalen Flip unnötig.

`vertices[6][4]` enthält pro Vertex vier Fließkommazahlen: `x, y, u, v`. Die sechs Einträge bilden zwei Dreiecke:

```text
Dreieck 1: links oben -> links unten -> rechts unten
Dreieck 2: links oben -> rechts unten -> rechts oben
```

Der **VBO** ist der GPU-Puffer für diese Daten. Er besitzt Platz für genau ein Quad und wird vor jedem sichtbaren Zeichen mit `glNamedBufferSubData()` aktualisiert. Das **VAO** beschreibt, wie der Vertex-Shader die Pufferdaten lesen soll: Attribut 0 enthält vier Floats, der Abstand zwischen Vertices beträgt vier Floats. Es speichert auch die Zuordnung zum VBO. Diese Implementierung verwendet einen Draw Call pro sichtbarem Zeichen.

## 6. Vertex- und Fragment-Shader

`CompileShader()` kompiliert eine einzelne GLSL-Stufe und prüft ihren Status. `Init()` verbindet Vertex- und Fragment-Shader anschließend durch Linken zu einem Programm. Bei Fehlern wird eine Diagnose ausgegeben. Nach dem Linkversuch werden die separaten Shader-Objekte gelöscht; ein erfolgreich gelinktes Programm bleibt verwendbar.

Beide Shader beginnen mit `#version 460 core`. Sie verwenden damit den bestehenden OpenGL-4.6-Core-Kontext ohne Immediate Mode oder feste Funktionspipeline.

Der Vertex-Shader erhält das Attribut `vertex`. Seine Komponenten `xy` sind Pixelkoordinaten; `zw` sind UV-Koordinaten. Eine orthografische Matrix bildet den logischen Zeichenbereich auf Clip-Koordinaten ab:

```text
(0, 0)                      -> (-1, +1)
(screenWidth, screenHeight)  -> (+1, -1)
```

Das negative Y-Vorzeichen erzeugt den Ursprung links oben. Die Matrix wird spaltenweise gespeichert und mit `GL_FALSE` ohne Transponieren übertragen. Es gibt keine perspektivische Verkleinerung. Die bei `Init()` gewählte Projektion passt sich späteren Fenstergrößenänderungen nicht automatisch an.

Die Rasterisierung interpoliert die UVs zwischen den Vertices. Im Fragment-Shader liefert `texture(atlas, uv).r` die Glyph-Abdeckung. `fragColor` kombiniert die gewünschte RGB-Farbe mit dieser Abdeckung als Alpha. Der Sampler-Uniformwert 0 verweist auf **Textureinheit 0**, nicht auf die Textur-ID.

Für `DrawRectangle()` setzt der gemeinsame Zeichenpfad `solid` auf 1. Der Shader verwendet dann Alpha 1 statt einer Atlas-Abfrage. So kann dasselbe Programm einen deckenden GUI-Hintergrund zeichnen.

Alpha-Blending erfolgt nach dem Fragment-Shader. Für RGB ist die verwendete Mischung:

```text
Ergebnis = Textfarbe * Abdeckung + Hintergrundfarbe * (1 - Abdeckung)
```

Dazu passen die Faktoren `GL_SRC_ALPHA` und `GL_ONE_MINUS_SRC_ALPHA`. Die RGB-Farbe ist nicht mit Alpha vormultipliziert. Für den Ziel-Alpha-Kanal verwendet der Code separat `GL_ONE` und `GL_ONE_MINUS_SRC_ALPHA`.

## 7. Ablauf eines Aufrufs von DrawText()

1. Die kurze Überladung ergänzt Skalierung 1 und Weiß. Die ausführliche Überladung zeichnet nur bei nichtnull `str` und positiver Skalierung. Der private Pfad `Draw()` kehrt ohne initialisiertes Programm sofort zurück.
2. `Draw()` sichert die OpenGL-Zustände, die es verändern wird: Programm, VAO, aktive Textureinheit, Einheit-0-Textur und -Sampler, Blend-Einstellungen und Farbmaske für Ausgabe 0 sowie Depth-Test, Culling, Rasterizer-Discard und Polygonmodus.
3. Der Renderer aktiviert Blending, erlaubt alle Farbkanäle und erzwingt gefüllte Dreiecke. Tiefentest, Culling und Rasterizer-Discard werden temporär deaktiviert. Er bindet sein Programm, VAO und den Atlas. Sampler 0 auf Einheit 0 lässt die Filterparameter der Textur gelten.
4. Die Uniforms erhalten Textfarbe und Textmodus. Aus dem Zeilenanfang entsteht die Grundlinie.
5. Die Zeichenfolge wird byteweise bis zum Nullterminator gelesen. Der Zugriff verwendet `unsigned char`, damit negative Werte nicht als Tabellenindizes auftreten. Steuerzeichen, DEL und Nicht-ASCII-Bytes werden übersprungen; insbesondere erzeugt ein Zeilenumbruch keine neue Zeile.
6. Für ein sichtbares Glyph werden Quad und UVs bestimmt, der VBO aktualisiert und sechs Vertices mit `GL_TRIANGLES` gezeichnet. Leerzeichen verändern nur die Schreibposition.
7. Danach stellt der Code die gesicherten Zustände wieder her. Die Bindungen auf Einheit 0 werden restauriert, bevor die zuvor aktive Textureinheit wieder ausgewählt wird.

Die Klasse übernimmt nicht sämtliche denkbaren OpenGL-Zustände. Viewport, Framebuffer und Scissor bleiben unverändert; ebenso werden etwa Stencil-Test und Color-Logic-Op nicht konfiguriert. Die Dokumentation der Zustandswiederherstellung bezieht sich auf die Zustände, die der vorhandene Zeichenpfad tatsächlich verändert. Die indexierten Blend- und Farbmasken-Aufrufe betreffen Ausgabe 0, ohne andere Farbausgaben umzustellen.

## 8. Verwaltung und Freigabe der OpenGL-Ressourcen

Das Objekt besitzt vier GPU-Ressourcen: Atlas-Textur, Shaderprogramm, VAO und VBO. Direct State Access erlaubt es, diese Objekte bei der Initialisierung weitgehend ohne Wechsel fremder Bindungen zu konfigurieren. CPU-Atlas-Speicher und FreeType-Objekte werden bereits innerhalb von `Init()` freigegeben.

Shaderfehler führen zur Freigabe der angelegten Ressourcen. `Init()` prüft außerdem Parameter, FreeType-Aufrufe, Bitmap-Format, Atlas-Größe und CPU-Speicher. Sein bool-Rückgabewert beschreibt diese implementierten Prüfungen; er ist keine allgemeine Überprüfung sämtlicher möglicher OpenGL-Fehler.

`Shutdown()` löscht vorhandene GPU-Objekte und setzt ihre IDs auf 0. Dadurch sind mehrfache Aufrufe möglich. Es muss bei noch aktuellem Kontext aufgerufen werden. `main.cpp` erledigt das nach der Game Loop und vor `glfwDestroyWindow()` beziehungsweise `glfwTerminate()`.

Die Klasse besitzt bewusst keinen automatisch OpenGL aufrufenden Destruktor. Der Zeitpunkt der Freigabe bleibt damit beim Aufrufer, der die Lebensdauer des Kontextes kennt. Kopierkonstruktor und Kopierzuweisung sind gesperrt: Eine Kopie der IDs würde zwei Objekte mit scheinbar denselben Ressourcen erzeugen und die Besitzverhältnisse unklar machen.

Nach `Shutdown()` sind die IDs null; Zeichenaufrufe bleiben ohne Ausgabe, bis wieder erfolgreich initialisiert wurde. Die fest angelegte Glyph-Tabelle braucht keine separate Speicherfreigabe.

### Vergleichsprüfung der Dokumentationsänderung

Der kommentierte Quelltext wurde mit dem vor der Bearbeitung in diesem Durchlauf gelesenen Quelltext verglichen. Nach Ausblenden von Kommentaren und Formatierungsabständen waren die Tokenfolgen identisch: 248 Tokens im Header und 2358 Tokens in der Implementierung. String-Literale einschließlich der GLSL-Shader wurden beim Vergleich erhalten. Es wurden keine Deklarationen, Ausdrücke oder Shader-Inhalte geändert.

Die Bearbeitung betrifft ausschließlich Kommentare in `TextRendering.h` und `TextRendering.cpp` sowie dieses neue Tutorial. `main.cpp`, Projektmappe, Projektdateien, Font und vcpkg-Einstellungen wurden nicht bearbeitet. Ein erneuter Build wurde für diese Dokumentationsänderung nicht ausgeführt; die vorherigen Funktionstests werden dadurch nicht als neue Tests ausgegeben.

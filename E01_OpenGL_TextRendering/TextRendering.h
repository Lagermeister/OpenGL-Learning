#pragma once

/**
 * @brief Einfacher ASCII-Renderer für Text und gefüllte 2D-Rechtecke in OpenGL 4.6 Core.
 *
 * Init() rasterisiert den Font einmal mit FreeType und erzeugt einen gemeinsamen
 * Graustufen-Atlas. DrawText() verwendet danach ausschließlich OpenGL-Ressourcen.
 * Pixelkoordinaten beginnen links oben; y bezeichnet den oberen typografischen
 * Zeilenrand, nicht die Grundlinie oder zwingend den sichtbaren Buchstabenrand.
 * Steuerzeichen und Nicht-ASCII-Bytes werden übersprungen; Kerning und Umbruch
 * werden nicht unterstützt. Der Aufrufer stellt einen aktuellen Kontext bereit
 * und ruft Shutdown() vor dessen Zerstörung auf. Es gibt keinen GL-Destruktor.
 */
class TextRendering
{
public:
    /**
     * @brief Lädt den Font und erzeugt Atlas, Shaderprogramm, VAO und VBO.
     * @param fontPath Pfad zur Font-Datei; relative Pfade beziehen sich auf das Arbeitsverzeichnis.
     * @param fontSize Gewünschte Pixelhöhe der Schrift, gültiger Bereich 1 bis 256.
     * @param screenWidth Positive Breite des logischen Zeichenbereichs in Pixeln.
     * @param screenHeight Positive Höhe des logischen Zeichenbereichs in Pixeln.
     * @return true bei erfolgreicher Initialisierung; false bei ungültigen Parametern,
     * FreeType-, Atlas- oder Shaderfehlern, die die Implementierung prüft.
     * @note Benötigt einen aktuellen OpenGL-Kontext. Eine erneute Initialisierung
     * gibt zuerst die bisherigen Ressourcen frei. Die Projektion bleibt danach fest.
     */
    bool Init(const char* fontPath, int fontSize, int screenWidth, int screenHeight);

    /**
     * @brief Zeichnet weißen Text mit Skalierung 1.
     * @param str Nullterminierte ASCII-Zeichenfolge; nullptr bewirkt keine Ausgabe.
     * @param x Horizontale Schreibposition in Pixeln ab dem linken Rand.
     * @param y Oberer typografischer Zeilenrand in Pixeln ab dem oberen Rand.
     * @note Verwendet dieselben Zustandsänderungen wie die ausführliche Überladung.
     */
    void DrawText(const char* str, float x, float y);

    /**
     * @brief Zeichnet ASCII-Text in einer frei gewählten Größe und RGB-Farbe.
     * @param str Nullterminierte ASCII-Zeichenfolge; nullptr bewirkt keine Ausgabe.
     * @param x Horizontale Schreibposition in Pixeln; Bearings können den sichtbaren Rand verschieben.
     * @param y Oberer typografischer Zeilenrand; die Grundlinie wird aus dem Ascender berechnet.
     * @param scale Positiver Faktor für Glyph-Größe, Bearings und Vorschub; sonst keine Ausgabe.
     * @param r Rotanteil, üblicherweise zwischen 0 und 1.
     * @param g Grünanteil, üblicherweise zwischen 0 und 1.
     * @param b Blauanteil, üblicherweise zwischen 0 und 1.
     * @note Nach erfolgreichem Init() bei aktuellem Kontext aufrufen. Temporär
     * geänderte Bindungen, Blend-Zustände, Farbmaske, Depth-Test, Culling,
     * Rasterizer-Discard und Polygonmodus werden wiederhergestellt.
     * Viewport, Framebuffer und Scissor werden vom Aufrufer vorgegeben.
     */
    void DrawText(const char* str, float x, float y, float scale, float r, float g, float b);

    /**
     * @brief Zeichnet ein deckendes Rechteck, beispielsweise als GUI-Hintergrund.
     * @param x Linker Rand in Pixeln.
     * @param y Oberer Rand in Pixeln.
     * @param width Positive Breite in Pixeln; sonst keine Ausgabe.
     * @param height Positive Höhe in Pixeln; sonst keine Ausgabe.
     * @param r Rotanteil, üblicherweise zwischen 0 und 1.
     * @param g Grünanteil, üblicherweise zwischen 0 und 1.
     * @param b Blauanteil, üblicherweise zwischen 0 und 1.
     * @note Nutzt denselben Zeichenpfad und dieselbe Zustandswiederherstellung
     * wie Text. Zuerst das Rechteck, danach den darüberliegenden Text zeichnen.
     */
    void DrawRectangle(float x, float y, float width, float height, float r, float g, float b);

    /**
     * @brief Gibt Textur, VBO, VAO und Shaderprogramm frei und setzt ihre IDs auf 0.
     * @note Bei noch aktuellem Kontext aufrufen. Mehrfache Aufrufe sind erlaubt;
     * danach zeichnen die Methoden bis zum nächsten erfolgreichen Init() nichts.
     */
    void Shutdown();

    /// @brief Erzeugt ein Objekt ohne angelegte OpenGL-Ressourcen.
    TextRendering() = default;
    /// @brief Kopieren ist verboten, damit OpenGL-Ressourcen keinen zweiten Besitzer erhalten.
    TextRendering(const TextRendering&) = delete;
    /// @brief Kopierzuweisung ist aus demselben Besitzgrund verboten.
    TextRendering& operator=(const TextRendering&) = delete;

private:
    /**
     * @brief Bitmap-Metriken und Atlas-Ausschnitt eines ASCII-Zeichens.
     * Die CPU benötigt diese Daten zum Aufbau des Quads; FreeType muss dazu
     * während der Game Loop nicht mehr aktiv sein.
     */
    struct Glyph
    {
        /** @brief Bitmap-Breite/-Höhe und Abstand der Bitmap von Schreibposition/Grundlinie.
         * bearingX verschiebt die linke Kante, bearingY misst von der Grundlinie
         * nach oben zur Bitmap-Oberkante; alle vier Werte sind unskalierte Pixel.
         */
        int width = 0, height = 0, bearingX = 0, bearingY = 0;
        /** @brief Vorschub in Pixeln sowie normalisierte Atlas-Grenzen.
         * advance ist unabhängig von der Bitmap-Breite; u0/v0 und u1/v1
         * begrenzen die obere linke bzw. untere rechte Ecke in der verwendeten Zuordnung.
         */
        float advance = 0, u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    };
    /// @brief Tabelle für Zeichencodes 0 bis 127; Steuerzeichen werden beim Zeichnen übersprungen.
    Glyph glyphs[128] = {};
    /// @brief IDs des R8-Atlas, Shaderprogramms, Vertex-Array und wiederverwendbaren Vertex-Puffers.
    unsigned int texture = 0, program = 0, vao = 0, vbo = 0;
    /// @brief Uniform-Adressen für RGB-Farbe und Umschaltung zwischen Text und deckendem Rechteck.
    int colorLocation = -1, solidLocation = -1;
    /// @brief Abstand vom typografischen Zeilenanfang zur Grundlinie in unskalierten Pixeln.
    float ascender = 0;
    /** @brief Gemeinsamer interner Zeichenpfad: str != nullptr zeichnet Text,
     * str == nullptr ein Rechteck mit width/height. Sichert und restauriert
     * die Zustände, die für diese Zeichenoperation temporär geändert werden.
     */
    void Draw(const char* str, float x, float y, float scale,
              float r, float g, float b, float width, float height);
};

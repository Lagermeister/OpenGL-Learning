#include "TextRendering.h"
#include <glad/glad.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Einzelne Shader werden separat kompiliert; das spätere Linken verbindet beide Stufen.
static GLuint CompileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Text shader: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool TextRendering::Init(const char* fontPath, int fontSize, int screenWidth, int screenHeight)
{
    // Ein erneutes Init beginnt ohne alte GPU-Ressourcen; der Kontext muss bereits aktuell sein.
    Shutdown();
    if (!fontPath || fontSize <= 0 || fontSize > 256 || screenWidth <= 0 || screenHeight <= 0)
        return false;
    // FreeType-Library verwaltet die Engine, Face die geladene Schrift. Die Pixelgröße bestimmt die Rasterisierung.
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    if (FT_Init_FreeType(&library)) return false;
    if (FT_New_Face(library, fontPath, 0, &face) || FT_Set_Pixel_Sizes(face, 0, fontSize)) {
        std::fprintf(stderr, "Cannot load font: %s\n", fontPath);
        if (face) FT_Done_Face(face);
        FT_Done_FreeType(library);
        return false;
    }
    std::printf("Font: %s %s, %d pixels\n", face->family_name, face->style_name, fontSize);
    // Ascender und Vorschub sind 26.6-Festkommawerte: Division durch 64 liefert Pixel. Der Ascender verankert die Grundlinie unter dem Zeilenanfang.
    ascender = face->size->metrics.ascender / 64.0f;
    // Feste Zellen mit transparentem Rand trennen die Glyphen bei linearer Filterung.
    // Erster Durchlauf: Metriken merken und die größte Bitmap bestimmen. Jede Zelle erhält rundherum einen transparenten Pixel Abstand.
    int cellWidth = 2, cellHeight = 2;
    bool ok = true;
    for (int c = 0; c < 128; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) { ok = false; break; }
        // Der Glyph-Slot wird bei jedem Laden überschrieben. Daher speichern wir die benötigten Metriken in unserer eigenen Tabelle.
        FT_GlyphSlot slot = face->glyph;
        Glyph& g = glyphs[c];
        g.width = (int)slot->bitmap.width; g.height = (int)slot->bitmap.rows;
        // Bearings beschreiben die Bitmap-Lage zur Schreibposition und Grundlinie; advance bestimmt dagegen die Position des nächsten Zeichens.
        g.bearingX = slot->bitmap_left; g.bearingY = slot->bitmap_top;
        g.advance = slot->advance.x / 64.0f;
        if (g.width + 2 > cellWidth) cellWidth = g.width + 2;
        if (g.height + 2 > cellHeight) cellHeight = g.height + 2;
    }
    // 16 Spalten mal 8 Zeilen ergeben 128 feste Zellen. Nullinitialisierte Bytes bedeuten transparente Atlas-Flächen.
    int atlasWidth = cellWidth * 16, atlasHeight = cellHeight * 8;
    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    if (atlasWidth > maxTexture || atlasHeight > maxTexture) ok = false;
    unsigned char* pixels = ok ? (unsigned char*)std::calloc((size_t)atlasWidth * atlasHeight, 1) : nullptr;
    if (!pixels) ok = false;
    // Zweiter Durchlauf: erneut rasterisieren und die Bitmaps in die jetzt bekannte Atlas-Geometrie kopieren.
    for (int c = 0; ok && c < 128; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) { ok = false; break; }
        // Unterstützt werden Graustufen-Bitmaps. Ein Byte beschreibt hier die Abdeckung eines Pixels, nicht seine RGB-Farbe.
        const FT_Bitmap& bitmap = face->glyph->bitmap;
        if (bitmap.width && bitmap.rows && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) { ok = false; break; }
        Glyph& g = glyphs[c];
        // Zeichencode bestimmt die Zelle; +1 lässt den transparenten Rand frei.
        int ax = (c % 16) * cellWidth + 1, ay = (c / 16) * cellHeight + 1;
        // pitch ist der Byte-Abstand zwischen Zeilen und kann Padding enthalten oder negativ sein. Die Kopie berücksichtigt Richtung und tatsächliche Bitmap-Breite.
        for (int row = 0; row < g.height; ++row) {
            const unsigned char* source = bitmap.buffer +
                (bitmap.pitch >= 0 ? row : g.height - 1 - row) * std::abs(bitmap.pitch);
            std::memcpy(pixels + (size_t)(ay + row) * atlasWidth + ax, source, g.width);
        }
        // UV-Koordinaten normieren die Bitmap-Grenzen auf den gesamten Atlas. Die erste kopierte Bitmap-Zeile wird später der oberen Quad-Kante zugeordnet.
        g.u0 = (float)ax / atlasWidth; g.v0 = (float)ay / atlasHeight;
        g.u1 = (float)(ax + g.width) / atlasWidth; g.v1 = (float)(ay + g.height) / atlasHeight;
    }
    // Nach der Atlas-Kopie sind Font und Rasterizer entbehrlich: Metriken liegen in glyphs, Abdeckungswerte noch im CPU-Atlas.
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    if (!ok) { std::free(pixels); return false; }

    // GLSL 4.60 Core: Ein Vertex enthält Pixelposition (xy) und Atlas-Koordinate (zw). Die Projektion bildet Pixel auf Clip-Koordinaten ab.
    const char* vertexSource = R"(#version 460 core
layout(location=0) in vec4 vertex;
uniform mat4 projection;
out vec2 uv;
void main() { gl_Position = projection * vec4(vertex.xy, 0.0, 1.0); uv = vertex.zw; }
)";
    // Der Fragment-Shader erhält interpolierte UVs. Text verwendet den R8-Kanal als Alpha; solid erzeugt ein deckendes Rechteck ohne Atlas-Abfrage.
    const char* fragmentSource = R"(#version 460 core
in vec2 uv;
layout(location=0) out vec4 fragColor;
uniform sampler2D atlas;
uniform vec3 color;
uniform int solid;
void main() { float alpha = solid != 0 ? 1.0 : texture(atlas, uv).r; fragColor = vec4(color, alpha); }
)";
    // Kompilieren prüft jede Stufe, Linken ihre gemeinsame Schnittstelle. Diagnosen helfen bei GLSL-Fehlern; Shader-Objekte dürfen nach dem Linkversuch gelöscht werden.
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs && fs) {
        program = glCreateProgram();
        glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
        GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[2048]; glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Text program: %s\n", log); ok = false;
        }
    } else ok = false;
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!ok) { std::free(pixels); Shutdown(); return false; }

    // Ein R8-Texel genügt für Glyph-Abdeckung. Lineare Filterung glättet Übergänge; transparente Zellränder vermeiden Einmischen benachbarter Zeichen.
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, GL_R8, atlasWidth, atlasHeight);
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Auch DSA-Uploads berücksichtigen globale Pixel-Unpack-Zustände. Ein gebundener PBO würde den CPU-Zeiger als Buffer-Offset interpretieren; daher temporär abkoppeln.
    GLint unpack[5];
    const GLenum unpackNames[4] = { GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS };
    for (int i = 0; i < 4; ++i) glGetIntegerv(unpackNames[i], &unpack[i]);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack[4]);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 1; i < 4; ++i) glPixelStorei(unpackNames[i], 0);
    // Der CPU-Atlas ist dicht gepackt: Alignment 1 und keine zusätzlichen Zeilen-/Pixelversätze. Nach dem Upload werden die vorherigen Einstellungen restauriert.
    glTextureSubImage2D(texture, 0, 0, 0, atlasWidth, atlasHeight, GL_RED, GL_UNSIGNED_BYTE, pixels);
    for (int i = 0; i < 4; ++i) glPixelStorei(unpackNames[i], unpack[i]);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack[4]);
    std::free(pixels);

    // Der VBO hält genau ein Quad aus sechs Vertices. Das VAO merkt Format und Buffer-Zuordnung; DSA verändert dafür keine fremden VAO-/Array-Buffer-Bindungen.
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    glNamedBufferData(vbo, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 4 * sizeof(float));
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    // Spaltenweise gespeicherte orthografische Matrix: (0,0) wird (-1,+1), (Breite,Höhe) wird (+1,-1). Das negative Y-Vorzeichen erzeugt den Ursprung links oben.
    float projection[16] = {
        2.0f / screenWidth, 0, 0, 0,
        0, -2.0f / screenHeight, 0, 0,
        0, 0, -1, 0,
        -1, 1, 0, 1
    };
    // Uniforms setzen wir direkt am Programm. Der Samplerwert 0 bezeichnet Textureinheit 0, nicht die ID der Atlas-Textur.
    glProgramUniformMatrix4fv(program, glGetUniformLocation(program, "projection"), 1, GL_FALSE, projection);
    glProgramUniform1i(program, glGetUniformLocation(program, "atlas"), 0);
    colorLocation = glGetUniformLocation(program, "color");
    solidLocation = glGetUniformLocation(program, "solid");
    return true;
}

void TextRendering::DrawText(const char* str, float x, float y)
{
    // Die kurze Überladung nutzt denselben Zeichenpfad mit Originalgröße und Weiß.
    DrawText(str, x, y, 1.0f, 1.0f, 1.0f, 1.0f);
}
void TextRendering::DrawText(const char* str, float x, float y, float scale, float r, float g, float b)
{
    if (str && scale > 0) Draw(str, x, y, scale, r, g, b, 0, 0);
}
void TextRendering::DrawRectangle(float x, float y, float width, float height, float r, float g, float b)
{
    // nullptr wählt im gemeinsamen Zeichenpfad das Rechteck; dafür sind keine Glyph-Metriken nötig.
    if (width > 0 && height > 0) Draw(nullptr, x, y, 1, r, g, b, width, height);
}

void TextRendering::Draw(const char* str, float x, float y, float scale,
                         float r, float g, float b, float width, float height)
{
    if (!program) return;
    // OpenGL-Zustände gehören dem gesamten Kontext. Alles, was dieser Pfad verändert, wird vorab gesichert, damit spätere Zeichenoperationen nicht beeinflusst werden.
    GLint oldProgram, oldVao, oldActive, oldTexture, oldSampler, polygon[2];
    GLint srcRGB, dstRGB, srcAlpha, dstAlpha, equationRGB, equationAlpha;
    GLboolean blend = glIsEnabledi(GL_BLEND, 0), depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull = glIsEnabled(GL_CULL_FACE), discard = glIsEnabled(GL_RASTERIZER_DISCARD), mask[4];
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    // Text nutzt Einheit 0. Deshalb sichern wir deren Textur/Sampler nach dem Wechsel sowie separat die zuvor aktive Einheit.
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture); glGetIntegerv(GL_SAMPLER_BINDING, &oldSampler);
    glGetIntegeri_v(GL_BLEND_SRC_RGB, 0, &srcRGB); glGetIntegeri_v(GL_BLEND_DST_RGB, 0, &dstRGB);
    glGetIntegeri_v(GL_BLEND_SRC_ALPHA, 0, &srcAlpha); glGetIntegeri_v(GL_BLEND_DST_ALPHA, 0, &dstAlpha);
    glGetIntegeri_v(GL_BLEND_EQUATION_RGB, 0, &equationRGB); glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA, 0, &equationAlpha);
    glGetIntegerv(GL_POLYGON_MODE, polygon); glGetBooleani_v(GL_COLOR_WRITEMASK, 0, mask);
    // Nicht vormultipliziertes Alpha: RGB = Textfarbe * Abdeckung + Hintergrund * (1 - Abdeckung). Für Ziel-Alpha verwenden wir eigene Faktoren; geändert wird nur Farbausgabe 0.
    glEnablei(GL_BLEND, 0); glBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparatei(0, GL_FUNC_ADD, GL_FUNC_ADD);
    // 2D-Overlays brauchen weder Tiefentest noch Culling; gefüllte Polygone, erlaubte Farbausgabe und aktives Rasterisieren sorgen für sichtbare Quads.
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_RASTERIZER_DISCARD);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // Ein fremder Sampler auf Einheit 0 könnte die Atlas-Filterung überschreiben. Sampler 0 lässt die Texturparameter gelten. Viewport, Framebuffer und Scissor bleiben vorgegeben.
    glUseProgram(program); glBindVertexArray(vao); glBindTexture(GL_TEXTURE_2D, texture); glBindSampler(0, 0);
    glProgramUniform3f(program, colorLocation, r, g, b);
    glProgramUniform1i(program, solidLocation, str ? 0 : 1);
    // y meint den typografischen Zeilenanfang. Die gemeinsame Grundlinie liegt um den skalierten Ascender darunter; verschiedene Buchstaben bleiben so auf derselben Zeile.
    float baseline = y + ascender * scale;
    const unsigned char* cursor = (const unsigned char*)str;
    do {
        float left = x, top = y, right = x + width, bottom = y + height;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        if (cursor) {
            if (!*cursor) break;
            // Byteweise ASCII-Verarbeitung: unsigned verhindert negative Indizes. Steuerzeichen und Nicht-ASCII-Bytes werden ohne Shaping oder Zeilenumbruch übersprungen.
            unsigned char c = *cursor++;
            if (c >= 128 || c < 32 || c == 127) continue;
            const Glyph& g = glyphs[c];
            // Die Bitmap liegt relativ zur Schreibposition: bearingX nach rechts, bearingY von der Grundlinie nach oben. Größe und Metriken werden gemeinsam skaliert.
            left = x + g.bearingX * scale; top = baseline - g.bearingY * scale;
            right = left + g.width * scale; bottom = top + g.height * scale;
            u0 = g.u0; v0 = g.v0; u1 = g.u1; v1 = g.v1;
            // Vorschub und sichtbare Breite sind verschieden. Auch ein Leerzeichen ohne Bitmap verschiebt x, benötigt aber keinen Draw Call.
            x += g.advance * scale;
            if (!g.width || !g.height) continue;
        }
        // Zwei Dreiecke bilden das Quad. Jeder Vertex enthält x, y, u, v; UVs verbinden Bitmap-Oberkante und Quad-Oberkante ohne zusätzlichen Flip.
        float vertices[6][4] = {
            {left, top, u0, v0}, {left, bottom, u0, v1}, {right, bottom, u1, v1},
            {left, top, u0, v0}, {right, bottom, u1, v1}, {right, top, u1, v0}
        };
        // Derselbe kleine VBO wird für jedes sichtbare Zeichen überschrieben. VAO und Programm bleiben während des gesamten Textaufrufs gebunden.
        glNamedBufferSubData(vbo, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    } while (cursor);
    // Bindungen und geänderte Zustände vollständig auf ihre gesicherten Werte zurücksetzen. Die Einheit-0-Bindungen müssen vor Rückkehr zur alten aktiven Einheit restauriert werden.
    glUseProgram(oldProgram); glBindVertexArray(oldVao);
    glBindTexture(GL_TEXTURE_2D, oldTexture); glBindSampler(0, oldSampler); glActiveTexture(oldActive);
    glBlendFuncSeparatei(0, srcRGB, dstRGB, srcAlpha, dstAlpha); glBlendEquationSeparatei(0, equationRGB, equationAlpha);
    if (!blend) glDisablei(GL_BLEND, 0);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (discard) glEnable(GL_RASTERIZER_DISCARD);
    glPolygonMode(GL_FRONT_AND_BACK, polygon[0]); glColorMaski(0, mask[0], mask[1], mask[2], mask[3]);
}

void TextRendering::Shutdown()
{
    // Explizite Freigabe bei noch aktuellem Kontext. Null-IDs erlauben wiederholtes Shutdown; CPU-Glyph-Metriken benötigen keine eigene Speicherfreigabe.
    if (texture) glDeleteTextures(1, &texture);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    texture = vbo = vao = program = 0;
    colorLocation = solidLocation = -1;
}

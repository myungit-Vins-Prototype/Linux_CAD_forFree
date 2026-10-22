#include "forgeCad2026_scene_renderer.h"
#include "cad_display_cache.h"
#include "cad_overlay_renderer.h"
#include <cmath>

namespace ForgeCad::Gui {
void SceneRenderer::initialize() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.025f, 0.035f, 0.050f, 1.0f);
    // 0 se i framebuffer multisample non ci sono (la chiamata fallisce e il valore resta 0).
    GLint samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    while (glGetError() != GL_NO_ERROR) {
    }
    maxSamples_ = samples;
}

void SceneRenderer::clearResources() {
    msaaBuffer_.reset();
    resolveBuffer_.reset();
}

void SceneRenderer::setAntialiasing(int samples) { antialiasing_ = qMax(0, samples); }
int SceneRenderer::antialiasing() const { return maxSamples_ > 0 ? qMin(antialiasing_, maxSamples_) : antialiasing_; }
int SceneRenderer::maxAntialiasing() const { return maxSamples_; }

QSize SceneRenderer::render(const QSize &logicalSize, const std::function<void()> &drawScene) {
    // Con l'antialiasing la scena OpenGL va nel framebuffer multisample;
    // le sovrapposizioni QPainter si disegnano dopo, direttamente nel widget.
    GLint viewport[4] = {0, 0, logicalSize.width(), logicalSize.height()};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const QSize pixels(qMax(1, viewport[2]), qMax(1, viewport[3]));
    QOpenGLFramebufferObject *scene = sceneBuffer(pixels);
    if (scene) {
        scene->bind();
        glViewport(0, 0, pixels.width(), pixels.height());
        glEnable(GL_MULTISAMPLE);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    drawScene();
    makeOpaque();
    if (scene) {
        scene->release();
        QOpenGLFramebufferObject::blitFramebuffer(resolveBuffer_.get(), msaaBuffer_.get());
        QOpenGLFramebufferObject::blitFramebuffer(nullptr, QRect(QPoint(), pixels), resolveBuffer_.get(), QRect(QPoint(), pixels));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
    return pixels;
}

QOpenGLFramebufferObject *SceneRenderer::sceneBuffer(const QSize &size) {
    const int samples = antialiasing();
    if (samples <= 0 || maxSamples_ <= 0 || !QOpenGLFramebufferObject::hasOpenGLFramebufferBlit()) {
        msaaBuffer_.reset();
        resolveBuffer_.reset();
        return nullptr;
    }
    if (!msaaBuffer_ || msaaBuffer_->size() != size || bufferSamples_ != samples) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(samples);
        msaaBuffer_ = std::make_unique<QOpenGLFramebufferObject>(size, format);
        resolveBuffer_ = std::make_unique<QOpenGLFramebufferObject>(size);
        bufferSamples_ = samples;
        if (!msaaBuffer_->isValid() || !resolveBuffer_->isValid()) {
            msaaBuffer_.reset();
            resolveBuffer_.reset();
            antialiasing_ = 0;
            return nullptr;
        }
    }
    return msaaBuffer_.get();
}

void SceneRenderer::makeOpaque() {
    GLfloat clear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
}

void SceneRenderer::setSectionClip(DisplayCache &display, OverlayRenderer &overlay,
                                   const QVector4D &plane, bool enabled) {
    display.setClipPlane(plane, enabled);
    overlay.setClipPlane(plane, enabled);
    if (enabled) glEnable(GL_CLIP_DISTANCE0);
    else glDisable(GL_CLIP_DISTANCE0);
}

void SceneRenderer::drawSectionCaps(DisplayCache &display, OverlayRenderer &overlay,
                                    const QVector<const BodyDisplay *> &solids,
                                    const QVector<QVector3D> &quad, const QVector4D &plane) {
    display.setLightingEnabled(false);
    glEnable(GL_STENCIL_TEST);
    for (const BodyDisplay *body : solids) {
        const BodyDisplay &bodyDisplay = *body;
        glClear(GL_STENCIL_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        glStencilFunc(GL_ALWAYS, 0, 1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        setSectionClip(display, overlay, plane, true);
        display.displayFaces(bodyDisplay);
        setSectionClip(display, overlay, QVector4D(), false);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glStencilFunc(GL_EQUAL, 1, 1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        QVector<QVector3D> triangles;
        for (int corner : {0, 1, 2, 0, 2, 3}) triangles.push_back(quad.at(corner));
        overlay.draw(GL_TRIANGLES, triangles, QVector4D(0.78f, 0.36f, 0.30f, 1.0f));
        glDepthFunc(GL_LEQUAL);
        overlay.setHatch(true);
        overlay.draw(GL_TRIANGLES, triangles, QVector4D(0.45f, 0.14f, 0.12f, 1.0f));
        overlay.setHatch(false);
    }
    // Resta GL_LEQUAL (come in initializeGL): con GL_LESS le linee degli schizzi
    // che stanno sugli spigoli dei corpi non si vedrebbero piu'.
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xff);
}


void SceneRenderer::drawSectionPlane(OverlayRenderer &overlay, const QVector<QVector3D> &quad, bool active) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    QVector<QVector3D> triangles;
    for (int corner : {0, 1, 2, 0, 2, 3}) triangles.push_back(quad.at(corner));
    overlay.draw(GL_TRIANGLES, triangles, QVector4D(0.95f, 0.45f, 0.35f, 0.10f));
    glLineWidth(active ? 2.5f : 1.5f);
    QVector<QVector3D> outline = quad;
    outline.push_back(outline.front());
    overlay.draw(GL_LINE_STRIP, outline, QVector4D(1.0f, 0.55f, 0.45f, active ? 1.0f : 0.75f));
    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
}

void SceneRenderer::drawThreadProfileEdges(OverlayRenderer &overlay, const BodyDisplay &display, const QVector4D &color) {
    for (const QVector<QVector3D> &edge : display.edges)
        if (edge.size() > 2) overlay.draw(GL_LINE_STRIP, edge, color);
}

void SceneRenderer::drawTrimPreview(DisplayCache &cache, const BodyDisplay &display, int displayMode) {
    if (displayMode != 0 && !display.vertices.isEmpty()) {
        cache.setLightingEnabled(false);
        cache.setColor(QVector4D(0.25f, 1.0f, 0.42f, 0.62f));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.0f, -3.0f);
        cache.displayFaces(display);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    if (displayMode != 1 && !display.edges.isEmpty()) {
        cache.setLightingEnabled(false);
        cache.setColor(QVector4D(0.55f, 1.0f, 0.65f, 1.0f));
        glLineWidth(3.0f);
        cache.edges(display);
        glLineWidth(1.0f);
    }
}

void SceneRenderer::drawFeaturePreview(DisplayCache &cache, OverlayRenderer &overlay,
                                       const BodyDisplay &display, const BodyDisplay &retained,
                                       const PreviewStyle &style) {
    // Sottrazione mediante estrusione: il vecchio corpo non puo' restare
    // davanti alla patch, altrimenti chiude visivamente il foro. Disegniamo
    // opache le facce conservate del risultato, con il taglio gia' presente.
    if (!retained.vertices.isEmpty() || !retained.edges.isEmpty()) {
        const QColor &color = style.retainedColor;
        if (style.displayMode != 0 && !retained.vertices.isEmpty()) {
            cache.setLightingEnabled(true);
            cache.setEmission(QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
            cache.setColor(QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f));
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 2.0f);
            cache.displayFaces(retained);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        if (style.displayMode != 1 && !retained.edges.isEmpty()) {
            cache.setLightingEnabled(false);
            cache.setColor(QVector4D(0.82f, 0.91f, 0.96f, 1.0f));
            glLineWidth(1.5f);
            cache.edges(retained);
            glLineWidth(1.0f);
        }
    }
    // La superficie del raccordo e' interna al vecchio spigolo convesso:
    // in sovrapposizione alla base opaca la mostriamo come patch X-ray.
    if (style.xray) glDisable(GL_DEPTH_TEST);
    if (style.displayMode != 0) {
        cache.setLightingEnabled(true);
        cache.setEmission(QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
        cache.setColor(QVector4D(0.95f, 0.66f, 0.28f, style.transparent ? 0.42f : 1.0f));
        if (style.transparent) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);
        if (style.nearestSurface) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            cache.displayFaces(display);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
        }
        cache.displayFaces(display);
        glDisable(GL_POLYGON_OFFSET_FILL);
        if (style.transparent) {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }
    cache.setLightingEnabled(false);
    cache.setColor(QVector4D(1.0f, 0.88f, 0.62f, 1.0f));
    glLineWidth(1.5f);
    if(style.spatial) {
        for(const auto &line:display.edges)overlay.drawWideLineStrip(line,QVector4D(1,.88f,.62f,1),3.5f*style.devicePixelRatio);
    } else if (style.thread && style.displayMode != 0)
        drawThreadProfileEdges(overlay, display, QVector4D(1.0f, 0.88f, 0.62f, 1.0f));
    else
        cache.edges(display);
    if (style.transparent && !display.constructionCurves.isEmpty()) {
        overlay.setStipple(false);
        glLineWidth(1.8f);
        for (const QVector<QVector3D> &curve : display.constructionCurves) {
            overlay.draw(GL_LINE_STRIP, curve, QVector4D(0.30f, 0.95f, 1.0f, 1.0f));
        }
    }
    glLineWidth(1.0f);
    if (style.xray) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    }
}

void SceneRenderer::drawExportMeshPreview(DisplayCache &cache, const BodyDisplay &display, bool quadrangular) {
    if (display.vertices.isEmpty() && display.edges.isEmpty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    if (!display.vertices.isEmpty()) {
        cache.setLightingEnabled(false);
        cache.setColor(quadrangular
            ? QVector4D(0.18f, 0.82f, 0.94f, 0.18f)
            : QVector4D(1.00f, 0.62f, 0.18f, 0.18f));
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-0.75f, -1.0f);
        cache.displayFaces(display);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    cache.setLightingEnabled(false);
    cache.setColor(quadrangular
        ? QVector4D(0.20f, 0.92f, 1.00f, 0.92f)
        : QVector4D(1.00f, 0.72f, 0.25f, 0.92f));
    glLineWidth(1.0f);
    cache.edges(display);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void SceneRenderer::drawBodyFaces(DisplayCache &cache, OverlayRenderer &overlay, const BodyDisplay &display,
                                  const BodyFaceStyle &style, const BodyDisplay &analysisFace,
                                  const QVector<OverlayVertex> &analysisTriangles,
                                  const std::function<void()> &highlightFaces) {
    cache.setLightingEnabled(true);
    QColor color = style.color;
    QVector4D emission(0.0f, 0.0f, 0.0f, 1.0f);
    if (style.hovered) {
        // L'oggetto sotto il puntatore si "accende".
        color = QColor::fromRgbF(color.redF() * 0.75 + style.hoverColor.redF() * 0.25,
                                 color.greenF() * 0.75 + style.hoverColor.greenF() * 0.25,
                                 color.blueF() * 0.75 + style.hoverColor.blueF() * 0.25);
        emission = QVector4D(float(style.hoverColor.redF()) * 0.14f,
                             float(style.hoverColor.greenF()) * 0.14f,
                             float(style.hoverColor.blueF()) * 0.14f, 1.0f);
    }
    // In modalita' schizzo le facce sono trasparenti (opacita' regolabile)
    // e non scrivono la profondita': lo schizzo dietro i corpi resta leggibile.
    const bool seeThrough = style.seeThrough;
    cache.setColor(QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()),
                                     seeThrough ? style.opacity : 1.0f));
    cache.setEmission(emission);
    if (seeThrough) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    // Le facce vanno un poco indietro nella profondita': spigoli e
    // schizzi che stanno sulla faccia si vedono senza spostarli.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 2.0f);
    const bool analyzing = style.analyzing;
    const bool faceOnly = style.faceOnly;
    const bool colorMap = analyzing && (style.analysisMode==1 || style.analysisMode==2);
    cache.setZebra(analyzing && !faceOnly && style.analysisMode==0, style.zebraFrequency, style.zebraAngle);
    if (!colorMap || faceOnly) cache.displayFaces(display);
    cache.setZebra(false,style.zebraFrequency,style.zebraAngle);
    if (faceOnly) glPolygonOffset(0.0f,0.0f);
    if (colorMap) overlay.draw(GL_TRIANGLES,analysisTriangles);
    else if (faceOnly && style.analysisMode==0) {
        cache.setZebra(true,style.zebraFrequency,style.zebraAngle);
        cache.displayFaces(analysisFace);
        cache.setZebra(false,style.zebraFrequency,style.zebraAngle);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (!seeThrough && !analyzing) highlightFaces();
    if (seeThrough) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    cache.setEmission(QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
}

void SceneRenderer::drawBodyEdges(DisplayCache &cache, OverlayRenderer &overlay,
                                  const BodyDisplay &display, const BodyEdgeStyle &style) {
    if (style.curve) {
        // Curve (eliche): sempre, in arancio, piu' spesse sotto il puntatore.
        cache.setLightingEnabled(false);
        cache.setColor(style.hovered
            ? QVector4D(float(style.hoverColor.redF()), float(style.hoverColor.greenF()), float(style.hoverColor.blueF()), 1.0f)
            : QVector4D(1.0f, 0.69f, 0.29f, 1.0f));
        const bool spatial=style.spatial;
        glLineWidth(spatial ? (style.hovered ? 5.0f : 3.5f) : (style.hovered ? 3.0f : 2.2f));
        if(spatial) {
            const QVector4D color=style.hovered ? QVector4D(float(style.hoverColor.redF()),float(style.hoverColor.greenF()),float(style.hoverColor.blueF()),1)
                                          : QVector4D(1,.69f,.29f,1);
            for(const auto &line:display.edges)overlay.drawWideLineStrip(line,color,(style.hovered?5.0f:3.5f)*style.devicePixelRatio);
        } else cache.displayEdges(display);
        glLineWidth(1.0f);
    } else if (style.displayMode != 1) {
        cache.setLightingEnabled(false);
        cache.setColor(QVector4D(0.82f, 0.91f, 0.96f, 1.0f));
        if (style.hiddenEdgesVisible) glDisable(GL_DEPTH_TEST);
        glLineWidth(1.5f);
        // Il filetto fisico e' costruito con molte facce molto
        // piccole. In "ombreggiato con bordi" mostrarne ogni
        // diagonale produce una falsa trama verticale e nasconde il
        // profilo elicoidale. Il wireframe conserva invece l'intera
        // topologia, utile per l'ispezione.
        if (style.thread && style.displayMode != 0)
            drawThreadProfileEdges(overlay, display, QVector4D(0.82f, 0.91f, 0.96f, 1.0f));
        else
            cache.displayEdges(display);
        glLineWidth(1.0f);
        if (style.hiddenEdgesVisible) glEnable(GL_DEPTH_TEST);
    }
}

void SceneRenderer::drawHighlightFaces(DisplayCache &cache, const BodyDisplay &display,
                                       bool whole, const QColor &color) {
    cache.setLightingEnabled(true);
    cache.setColor(QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f));
    cache.setEmission(QVector4D(float(color.redF()) * 0.25f, float(color.greenF()) * 0.25f, float(color.blueF()) * 0.25f, 1.0f));
    // Le facce dei corpi sono spostate indietro (glPolygonOffset 1, 2):
    // senza spostamento l'evidenziazione sta sempre davanti alle facce
    // da cui viene, e senza scrivere la profondita' gli spigoli restano visibili.
    glDepthMask(GL_FALSE);
    if (whole) cache.displayFaces(display);  // anche le ripetizioni instanziate
    else cache.faces(display);
    glDepthMask(GL_TRUE);
    cache.setEmission(QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
}

void SceneRenderer::drawHighlightEdges(DisplayCache &cache, const BodyDisplay &display,
                                       bool whole, const QColor &color) {
    cache.setLightingEnabled(false);
    cache.setColor(QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f));
    glLineWidth(3.0f);
    if (whole) cache.displayEdges(display);
    else cache.edges(display);
    glLineWidth(1.0f);
}

void SceneRenderer::drawSelectedFace(OverlayRenderer &overlay, const BodyDisplay &display,
                                     const QVector<int> &indices, const QColor &color) {
    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.5f);
    for (int index : indices) {
        overlay.draw(GL_LINE_STRIP, display.edges.at(index),
                              QVector4D(float(color.redF()), float(color.greenF()),
                                        float(color.blueF()), 1.0f));
    }
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawTopologyHighlight(OverlayRenderer &overlay, const BodyDisplay &display,
                                          const QVector<int> &indices, const QColor &color, float devicePixelRatio) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    const QVector4D halo(0.03f, 0.05f, 0.07f, 1.0f);
    const QVector4D bright(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f);
    for (int index : indices)
        if (index >= 0 && index < display.edges.size())
            overlay.drawWideLineStrip(display.edges.at(index), halo, 6.0f * devicePixelRatio);
    for (int index : indices)
        if (index >= 0 && index < display.edges.size())
            overlay.drawWideLineStrip(display.edges.at(index), bright, 3.5f * devicePixelRatio);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawPickedEdges(OverlayRenderer &overlay, const BodyDisplay *body,
                                    const BodyDisplay *hoverBody, const QVector<int> &hoverFaceEdges,
                                    const QVector<int> &pickedEdges, const EdgePickStyle &style) {
    // I candidati rispettano la profondita' (a meno della modalita' X-ray),
    // cosi' la scena non si riempie di bordi posteriori. Gli spigoli gia'
    // scelti vengono invece sovrapposti nell'ultimo passaggio: durante il
    // calcolo o il fallimento di un'anteprima la base viene ridisegnata e
    // il suo depth buffer non deve far sparire la conferma della scelta.
    if (style.hiddenEdgesVisible) glDisable(GL_DEPTH_TEST);
    else glEnable(GL_DEPTH_TEST);
    auto draw = [&](const BodyDisplay *source, int index, const QColor &color, float width, bool reliableWidth = false) {
        if (!source) return;
        const QVector<QVector<QVector3D>> &edges = source->edges;
        if (index < 0 || index >= edges.size()) return;
        const QVector4D rgba(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f);
        if (reliableWidth)
            overlay.drawWideLineStrip(edges.at(index), rgba, width * style.devicePixelRatio);
        else {
            glLineWidth(width);
            overlay.draw(GL_LINE_STRIP, edges.at(index), rgba);
        }
    };
    if (body)
        for (int index = 0; index < body->edges.size(); ++index) draw(body, index, QColor(120, 140, 160), 1.5f);
    for (int index : hoverFaceEdges) draw(hoverBody, index, style.hoverColor, 3.0f);

    // Hover e selezione sono conferme dell'interazione, non geometria della
    // scena: si disegnano opachi, senza profondita', con un alone scuro che
    // li separa sia dal corpo sia dall'anteprima semitrasparente. Il picking
    // ha gia' escluso gli spigoli nascosti quando la modalita' X-ray e' spenta.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    const QColor edgeHalo(8, 12, 18);
    if (style.hoverEdge >= 0 && !style.hoveredSelected) {
        draw(hoverBody, style.hoverEdge, edgeHalo, 4.25f, true);
        draw(hoverBody, style.hoverEdge, style.hoverColor, 2.75f, true);
    }
    for (int index : pickedEdges) draw(body, index, edgeHalo, 5.0f, true);
    for (int index : pickedEdges) draw(body, index, style.selectionColor, 3.25f, true);
    glDepthMask(GL_TRUE);
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawBackgroundGradient(OverlayRenderer &overlay, const QSize &logicalSize,
                                            const BackgroundSettings &background) {
    // QPainter puo' lasciare GL_CONSTANT_COLOR e una penna colorata nel
    // blending. Lo sfondo e' opaco: non deve dipendere dal frame precedente.
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const float aspect = float(logicalSize.width()) / float(qMax(1, logicalSize.height()));
    QMatrix4x4 projection;
    projection.ortho(-aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f);
    overlay.setMatrices(projection, QMatrix4x4());
    overlay.setClipPlane(QVector4D(), false);
    const QColor start = background.startColor;
    const QColor end = background.gradient ? background.endColor : start;
    const float radians = background.angle * float(M_PI) / 180.0f;
    const QVector2D direction(std::cos(radians), std::sin(radians));
    const QVector2D across(-direction.y(), direction.x());
    const float extent = qAbs(direction.x()) * aspect + qAbs(direction.y());
    const float middle = -extent + 2.0f * extent * qBound(0.0f, background.position, 1.0f);
    const float span = 2.0f * (aspect + 1.0f);
    const struct { float offset; float mix; } stops[] = {
        {-extent, 0.0f}, {middle, 0.5f}, {extent, 1.0f}};
    QVector<ForgeCad::OverlayVertex> vertices;
    vertices.reserve(6);
    for (const auto &stop : stops) {
        const QVector4D color(float(start.redF() + (end.redF() - start.redF()) * stop.mix),
                              float(start.greenF() + (end.greenF() - start.greenF()) * stop.mix),
                              float(start.blueF() + (end.blueF() - start.blueF()) * stop.mix), 1.0f);
        const QVector2D centre = direction * stop.offset;
        const QVector2D first = centre + across * span;
        const QVector2D second = centre - across * span;
        vertices.push_back({QVector3D(first.x(), first.y(), 0.0f), color});
        vertices.push_back({QVector3D(second.x(), second.y(), 0.0f), color});
    }
    overlay.draw(GL_TRIANGLE_STRIP, vertices);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawAnalysisLines(OverlayRenderer &overlay, const QVector<OverlayVertex> &grid,
                                      const QVector<OverlayVertex> &lines, bool through) {
    const bool depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (through) glDisable(GL_DEPTH_TEST);
    overlay.draw(GL_LINES, grid);
    overlay.draw(GL_LINES, lines);
    if (through && depthEnabled) glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawAxes(OverlayRenderer &overlay, float length, const std::array<QColor, 3> &colors) {
    const float head = 0.09f * length, radius = 0.032f * length;
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    QVector<ForgeCad::OverlayVertex> shafts, cones;
    for (int axis = 0; axis < 3; ++axis) {
        QVector3D direction, side, up;
        direction[axis] = 1.0f;
        side[(axis + 1) % 3] = 1.0f;
        up[(axis + 2) % 3] = 1.0f;
        const QColor &color = colors[axis];
        const QVector4D rgba(float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f);
        const QVector3D tip = direction * length, base = direction * (length - head);
        shafts.push_back({QVector3D(), rgba});
        shafts.push_back({base, rgba});
        for (int k = 0; k < 24; ++k) {
            const float a0 = float(k) * 2.0f * float(M_PI) / 24.0f;
            const float a1 = float(k + 1) * 2.0f * float(M_PI) / 24.0f;
            const QVector3D p0 = base + radius * (std::cos(a0) * side + std::sin(a0) * up);
            const QVector3D p1 = base + radius * (std::cos(a1) * side + std::sin(a1) * up);
            cones.push_back({tip, rgba}); cones.push_back({p0, rgba}); cones.push_back({p1, rgba});
            cones.push_back({base, rgba}); cones.push_back({p1, rgba}); cones.push_back({p0, rgba});
        }
    }
    overlay.draw(GL_LINES, shafts);
    overlay.draw(GL_TRIANGLES, cones);
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawGrid(OverlayRenderer &overlay, const AxesOrientation &orientation, float step) {
    glDisable(GL_DEPTH_TEST);
    // Griglia nel piano orizzontale dell'orientamento (perpendicolare all'asse in alto), poco sotto l'origine.
    const QVector3D a(float(orientation.right[0]), float(orientation.right[1]), float(orientation.right[2]));
    const QVector3D b(float(orientation.toward[0]), float(orientation.toward[1]), float(orientation.toward[2]));
    // Passo 1 con i piani di default, poi segue la loro dimensione (20 passi per lato).
    const float extent = 10.0f * step;
    const QVector3D below = -0.05f * step * QVector3D(float(orientation.up[0]), float(orientation.up[1]), float(orientation.up[2]));
    QVector<QVector3D> lines;
    lines.reserve(84);
    for (int i = -10; i <= 10; ++i) {
        const float f = float(i) * step;
        for (const QVector3D &p : {below + f * a - extent * b, below + f * a + extent * b,
                                   below - extent * a + f * b, below + extent * a + f * b})
            lines.push_back(p);
    }
    overlay.draw(GL_LINES, lines, QVector4D(0.16f, 0.21f, 0.25f, 1.0f));
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawDatumQuad(OverlayRenderer &overlay, const QVector<QVector3D> &corners, const GLfloat *color, float emphasis) {
    QVector<QVector3D> fill;
    for (int corner : {0, 1, 2, 0, 2, 3}) fill.push_back(corners.at(corner));
    overlay.draw(GL_TRIANGLES, fill, QVector4D(color[0], color[1], color[2], 0.07f + 0.10f * emphasis));
    const struct { GLfloat width, alpha, light; } strokes[] = {{4.0f, 0.12f, 0.25f}, {1.4f, 0.65f, 0.40f}};
    for (const auto &stroke : strokes) {
        glLineWidth(stroke.width);
        const QVector4D strokeColor(color[0] + (1.0f - color[0]) * stroke.light,
                                    color[1] + (1.0f - color[1]) * stroke.light,
                                    color[2] + (1.0f - color[2]) * stroke.light,
                                    qMin(1.0f, stroke.alpha * (1.0f + 0.6f * emphasis)));
        QVector<QVector3D> outline = corners;
        outline.push_back(outline.front());
        overlay.draw(GL_LINE_STRIP, outline, strokeColor);
    }
    glLineWidth(1.0f);
}

void SceneRenderer::drawPlanePass(const std::function<void()> &drawPlanes) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawPlanes();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawReferencePlanes(OverlayRenderer &overlay, const std::array<ReferencePlane, 3> &planes) {
    drawPlanePass([&] {
        const GLfloat colors[][3] = {{0.20f, 0.55f, 0.95f}, {0.25f, 0.90f, 0.70f}, {0.95f, 0.45f, 0.25f}};
        const GLfloat baseAlpha[] = {0.08f, 0.06f, 0.06f};
        QVector<ForgeCad::OverlayVertex> fills;
        for (int plane = 0; plane < 3; ++plane) {
            if (!planes[plane].visible) continue;
            const float alpha = baseAlpha[plane] + 0.10f * planes[plane].emphasis;
            const QVector4D color(colors[plane][0], colors[plane][1], colors[plane][2], alpha);
            const QVector<QVector3D> corners = planes[plane].corners;
            for (int corner : {0, 1, 2, 0, 2, 3}) fills.push_back({corners.at(corner), color});
        }
        overlay.draw(GL_TRIANGLES, fills);
        // Bordo: un alone largo e tenue del colore del piano, schiarito, e sopra
        // una linea sottile piu' luminosa dello stesso colore.
        const struct { GLfloat width, alpha, light; } strokes[] = {{4.0f, 0.10f, 0.25f}, {1.3f, 0.55f, 0.40f}};
        for (const auto &stroke : strokes) {
            glLineWidth(stroke.width);
            for (int plane = 0; plane < 3; ++plane) {
                if (!planes[plane].visible) continue;
                const float alpha = qMin(1.0f, stroke.alpha * (1.0f + 0.6f * planes[plane].emphasis));
                const GLfloat *c = colors[plane];
                const QVector4D color(c[0] + (1.0f - c[0]) * stroke.light, c[1] + (1.0f - c[1]) * stroke.light,
                                      c[2] + (1.0f - c[2]) * stroke.light, alpha);
                QVector<QVector3D> corners = planes[plane].corners;
                corners.push_back(corners.front());
                overlay.draw(GL_LINE_STRIP, corners, color);
            }
        }
    });
}

}

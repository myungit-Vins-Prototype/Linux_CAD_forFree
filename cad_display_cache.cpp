#include "cad_display_cache.h"
#include <QOpenGLFunctions>
#include <algorithm>

namespace ForgeCad {
void DisplayCache::clear() { entries_.clear(); }
void DisplayCache::beginFrame() {
    ++frame_;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [this](const auto &e) {
        return e->used + 2 < frame_;
    }), entries_.end());
}
DisplayCache::Entry &DisplayCache::get(const BodyDisplay &display) {
    for (const auto &entry : entries_)
        if (entry->source.vertices.constData() == display.vertices.constData()
            && entry->source.normals.constData() == display.normals.constData()
            && entry->source.edges.constData() == display.edges.constData()) {
            entry->used = frame_;
            return *entry;
        }
    auto entry = std::make_unique<Entry>();
    entry->source = display;
    entry->used = frame_;
    QVector<QVector3D> lines;
    for (const auto &edge : display.edges) {
        entry->starts.append(int(lines.size()));
        entry->counts.append(int(edge.size()));
        lines += edge;
    }
    const auto upload = [](QOpenGLBuffer &buffer, const QVector<QVector3D> &values) {
        if (!buffer.create() || !buffer.bind()) return false;
        buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        buffer.allocate(values.constData(), int(values.size() * sizeof(QVector3D)));
        buffer.release();
        return true;
    };
    entry->ready = upload(entry->positions, display.vertices) && upload(entry->normals, display.normals) && upload(entry->lines, lines);
    entries_.push_back(std::move(entry));
    return *entries_.back();
}
void DisplayCache::faces(const BodyDisplay &display) {
    if (display.vertices.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready) {
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < display.vertices.size(); ++i) {
            const auto &p = display.vertices.at(i), &n = display.normals.at(i);
            glNormal3f(n.x(), n.y(), n.z()); glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
        return;
    }
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    entry.positions.bind();
    glVertexPointer(3, GL_FLOAT, sizeof(QVector3D), nullptr);
    entry.normals.bind();
    glNormalPointer(GL_FLOAT, sizeof(QVector3D), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, int(display.vertices.size()));
    entry.normals.release();
    glPopClientAttrib();
}
void DisplayCache::edges(const BodyDisplay &display) {
    if (display.edges.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready) {
        for (const auto &edge : display.edges) {
            glBegin(GL_LINE_STRIP);
            for (const auto &p : edge) glVertex3f(p.x(), p.y(), p.z());
            glEnd();
        }
        return;
    }
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    entry.lines.bind();
    glVertexPointer(3, GL_FLOAT, sizeof(QVector3D), nullptr);
    for (int i = 0; i < entry.starts.size(); ++i) glDrawArrays(GL_LINE_STRIP, entry.starts.at(i), entry.counts.at(i));
    entry.lines.release();
    glPopClientAttrib();
}
}

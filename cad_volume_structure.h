#pragma once

#include "cad_types.h"
#include "fk_sheet_gyroid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ForgeCad {

inline bool usesSampledGyroidEnvelope(VolumeStructureMode mode) {
    return mode == VolumeStructureMode::CurvedGyroid || mode == VolumeStructureMode::SplineGyroid;
}

inline bool isInterpolatedGyroid(VolumeStructureMode mode) {
    return usesSampledGyroidEnvelope(mode) || mode == VolumeStructureMode::TrimmedSplineGyroid;
}

inline bool isGyroidMode(VolumeStructureMode mode) {
    return mode == VolumeStructureMode::Gyroid || isInterpolatedGyroid(mode);
}

inline bool validVolumetricStructureParameters(const VolumetricStructureParameters &p) {
    const auto positive = [](double value) { return std::isfinite(value) && value > 0; };
    return p.mode >= VolumeStructureMode::Woven && p.mode <= VolumeStructureMode::TrimmedSplineGyroid
        && p.axis >= -1 && p.axis < 3
        && std::isfinite(p.from) && std::isfinite(p.to) && (p.axis < 0 || p.from < p.to)
        && positive(p.samplingPitch) && positive(p.cellPitch) && positive(p.strutWidth)
        && positive(p.filamentWidth) && positive(p.amplitude) && positive(p.wallThickness);
}

inline Kernel::Box volumeStructureRegion(Kernel::Box bounds, int axis, double from, double to) {
    if (axis >= 0) {
        bounds.lo[axis] = std::max(bounds.lo[axis], from);
        bounds.hi[axis] = std::min(bounds.hi[axis], to);
        if (bounds.lo[axis] >= bounds.hi[axis])
            throw std::invalid_argument("Inserisci un intervallo che attraversi il solido.");
    }
    return bounds;
}

struct PreparedVolumeStructure {
    Kernel::Box region;
    Kernel::WovenLatticeSettings settings;
    std::vector<Kernel::Box> zones;
    Kernel::SheetGyroid gyroid;
    VolumeStructureMode mode;
    int axis = -1;
    double from = 0;
    double to = 0;

    Kernel::Body build(const Kernel::Body &source, const Kernel::CurvedGyroidEnvelope *envelope = nullptr) const {
        switch (mode) {
        case VolumeStructureMode::Voxel:
            return Kernel::quantizeSolid(source, settings.samplingPitch, zones);
        case VolumeStructureMode::Cubic:
            return Kernel::latticeSolid(source, settings.samplingPitch, settings.pattern.frame, zones);
        case VolumeStructureMode::Woven:
            return Kernel::wovenLatticeSolid(source, settings.samplingPitch, settings.pattern, zones);
        case VolumeStructureMode::Gyroid:
            return Kernel::sheetGyroidSolid(source, settings.samplingPitch, gyroid, zones);
        case VolumeStructureMode::CurvedGyroid:
        case VolumeStructureMode::SplineGyroid: {
            const auto wall = mode == VolumeStructureMode::SplineGyroid
                            ? Kernel::GyroidWall::Spline : Kernel::GyroidWall::Faceted;
            if (envelope) return Kernel::curvedGyroidSolid(source, *envelope, gyroid, axis, from, to, wall);
            return Kernel::curvedGyroidSolid(source, settings.samplingPitch, gyroid, axis, from, to, wall);
        }
        case VolumeStructureMode::TrimmedSplineGyroid:
            return Kernel::trimmedSplineGyroidSolid(source, settings.samplingPitch, gyroid, axis, from, to);
        }
        throw std::invalid_argument("Tipo di struttura volumetrica non valido.");
    }

    bool containsMaterial(const Kernel::Vec3 &point) const {
        if (mode == VolumeStructureMode::Voxel) return true;
        if (mode == VolumeStructureMode::Cubic) return Kernel::latticeMaterial(point, settings.pattern.frame);
        if (isGyroidMode(mode)) return Kernel::sheetGyroidMaterial(point, gyroid);
        return Kernel::wovenLatticeMaterial(point, settings.pattern);
    }
};

inline PreparedVolumeStructure prepareVolumeStructure(const Kernel::Box &bounds, const VolumetricStructureParameters &p) {
    if (!validVolumetricStructureParameters(p))
        throw std::invalid_argument("Parametri della struttura volumetrica non validi.");
    PreparedVolumeStructure prepared;
    prepared.mode = p.mode;
    prepared.axis = p.axis;
    prepared.from = p.from;
    prepared.to = p.to;
    prepared.region = volumeStructureRegion(bounds, p.axis, p.from, p.to);
    const double half = p.samplingPitch / 2;
    const auto origin = prepared.region.lo + Kernel::Vec3{half, half, half};
    prepared.settings = {{{origin, p.cellPitch, p.strutWidth}, p.filamentWidth, p.amplitude}, p.samplingPitch};
    switch (p.mode) {
    case VolumeStructureMode::Voxel:
        Kernel::validateVoxelization(prepared.region, p.samplingPitch);
        break;
    case VolumeStructureMode::Cubic:
        Kernel::validateCubicLattice(prepared.region, p.samplingPitch, prepared.settings.pattern.frame);
        break;
    case VolumeStructureMode::Woven:
        Kernel::validateWovenLattice(prepared.region, prepared.settings);
        break;
    case VolumeStructureMode::Gyroid:
    case VolumeStructureMode::CurvedGyroid:
    case VolumeStructureMode::SplineGyroid:
    case VolumeStructureMode::TrimmedSplineGyroid: {
        // La fase resta ferma quando si cambia la risoluzione della griglia.
        auto center = (prepared.region.lo + prepared.region.hi) * 0.5;
        if (p.axis >= 0) center[p.axis] = prepared.region.lo[p.axis];
        prepared.gyroid = {center, p.cellPitch, p.wallThickness};
        if (isInterpolatedGyroid(p.mode))
            Kernel::validateCurvedGyroid(bounds, p.samplingPitch, prepared.gyroid);
        else
            Kernel::validateSheetGyroid(prepared.region, p.samplingPitch, prepared.gyroid);
        break;
    }
    }
    if (p.axis >= 0 && !isInterpolatedGyroid(p.mode)) {
        auto zone = bounds.padded(p.samplingPitch);
        zone.lo[p.axis] = p.from;
        zone.hi[p.axis] = p.to;
        prepared.zones.push_back(zone);
    }
    return prepared;
}

}

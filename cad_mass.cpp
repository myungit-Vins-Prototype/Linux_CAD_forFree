#include "cad_mass.h"

#include <cmath>
#include <utility>

#include "fk_curve_algo.h"
#include "fk_mass.h"
#include "fk_quadrature.h"
#include "fk_topology.h"

namespace ForgeCad {

MassReport forgeMassProperties(const Kernel::Body &body) {
    MassReport report;
    report.method = QStringLiteral("kernel ForgeCAD (integrali sulle superfici esatte, errore relativo 1e-12)");
    try {
        if (body.isSheet()) {
            report.kind = MassReport::Kind::Sheet;
            for (Kernel::FaceId f : body.faces()) report.area += Kernel::faceArea(body, f);
        } else {
            const Kernel::MassProperties m = Kernel::massProperties(body);
            report.volume = m.volume;
            report.area = m.area;
            for (int i = 0; i < 3; ++i) {
                report.centroid[i] = m.centroid[i];
                for (int j = 0; j < 3; ++j) report.inertia[i][j] = m.inertia[i][j];
            }
            report.hasCentroid = true;
        }
        report.ok = true;
    } catch (const std::exception &failure) {
        report.error = QString::fromUtf8(failure.what());
    }
    return report;
}

MassReport curveMassProperties(const Kernel::Curve<3> &curve) {
    MassReport report;
    report.kind = MassReport::Kind::Curve;
    report.method = QStringLiteral("curva esatta (quadratura adattiva)");
    try {
        const Kernel::Interval range = curve.domain();
        report.length = Kernel::arcLength(curve, range);
        // Baricentro della curva: \int P |C'| dt / L, tratto per tratto.
        const std::vector<double> breaks = curve.breakpoints(range);
        for (int axis = 0; axis < 3; ++axis) {
            double sum = 0.0;
            for (std::size_t k = 0; k + 1 < breaks.size(); ++k)
                sum += Kernel::detail::adaptiveIntegral(
                    [&](double t) {
                        Kernel::Vec3 d[2];
                        curve.evaluate(t, 1, d);
                        return d[0][axis] * Kernel::norm(d[1]);
                    },
                    breaks[k], breaks[k + 1], 1e-12 * report.length * (1.0 + std::fabs(curve.point(breaks[k])[axis])), 0);
            report.centroid[axis] = sum / report.length;
        }
        report.hasCentroid = true;
        report.ok = true;
    } catch (const std::exception &failure) {
        report.error = QString::fromUtf8(failure.what());
    }
    return report;
}

void inertiaAbout(const MassReport &report, const double point[3], double result[3][3]) {
    // Huygens-Steiner: I_P = I_G + V (|d|^2 E - d d^T), d = G - P (termini fuori diagonale con il segno meno).
    double d[3];
    for (int i = 0; i < 3; ++i) d[i] = report.centroid[i] - point[i];
    const double d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) result[i][j] = report.inertia[i][j] + report.volume * ((i == j ? d2 : 0.0) - d[i] * d[j]);
}

MassReport combineMass(const QVector<MassReport> &parts) {
    if (parts.size() == 1) return parts.first();
    MassReport total;
    total.kind = MassReport::Kind::Solid;
    total.ok = !parts.isEmpty();
    QStringList methods;
    for (const MassReport &part : parts) {
        if (!part.ok) {
            total.ok = false;
            total.error = part.error;
            return total;
        }
        total.volume += part.volume;
        total.area += part.area;
        total.length += part.length;
        for (int i = 0; i < 3; ++i) total.centroid[i] += part.volume * part.centroid[i];
        if (!methods.contains(part.method)) methods.append(part.method);
    }
    total.method = methods.join(QStringLiteral("; "));
    if (!(std::fabs(total.volume) > 0.0)) {
        total.hasCentroid = false;
        return total;
    }
    for (double &c : total.centroid) c /= total.volume;
    total.hasCentroid = true;
    for (const MassReport &part : parts) {
        double about[3][3];
        inertiaAbout(part, total.centroid, about);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) total.inertia[i][j] += about[i][j];
    }
    return total;
}

void principalMoments(const double tensor[3][3], double moments[3], double axes[3][3]) {
    double a[3][3], v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) a[i][j] = 0.5 * (tensor[i][j] + tensor[j][i]);
    // Rotazioni di Jacobi cicliche finche' la parte fuori diagonale e' trascurabile.
    for (int sweep = 0; sweep < 60; ++sweep) {
        const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
        const double diag = a[0][0] * a[0][0] + a[1][1] * a[1][1] + a[2][2] * a[2][2];
        if (off <= 1e-32 * diag || off == 0.0) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (a[p][q] == 0.0) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < 3; ++k) {
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    int order[3] = {0, 1, 2};
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (a[order[j]][order[j]] < a[order[i]][order[i]]) std::swap(order[i], order[j]);
    for (int k = 0; k < 3; ++k) {
        moments[k] = a[order[k]][order[k]];
        for (int i = 0; i < 3; ++i) axes[i][k] = v[i][order[k]];
    }
    // Terna destrorsa: il terzo asse = primo x secondo.
    const double cx = axes[1][0] * axes[2][1] - axes[2][0] * axes[1][1], cy = axes[2][0] * axes[0][1] - axes[0][0] * axes[2][1],
                 cz = axes[0][0] * axes[1][1] - axes[1][0] * axes[0][1];
    if (cx * axes[0][2] + cy * axes[1][2] + cz * axes[2][2] < 0.0)
        for (int i = 0; i < 3; ++i) axes[i][2] = -axes[i][2];
}

}

#include "fk_math.h"

#include "fk_precision.h"

namespace ForgeCad::Kernel {

Frame3::Frame3() : x_(1.0, 0.0, 0.0), y_(0.0, 1.0, 0.0), z_(0.0, 0.0, 1.0) {}

Frame3::Frame3(const Vec3 &origin, const Vec3 &zDirection, const Vec3 &xReference) : origin_(origin) {
    z_ = normalized(zDirection);
    const Vec3 projected = xReference - dot(xReference, z_) * z_;
    if (norm(projected) <= kAngularResolution * norm(xReference))
        throw std::invalid_argument("Frame3: direzione X parallela a Z");
    x_ = normalized(projected);
    x_ = normalized(x_ - dot(x_, z_) * z_);  // seconda passata: ortogonalita' ~1e-16
    y_ = cross(z_, x_);
}

Vec3 Frame3::toGlobal(const Vec3 &localPoint) const {
    return origin_ + directionToGlobal(localPoint);
}

Vec3 Frame3::toLocal(const Vec3 &globalPoint) const {
    return directionToLocal(globalPoint - origin_);
}

Vec3 Frame3::directionToGlobal(const Vec3 &localVector) const {
    return localVector.x() * x_ + localVector.y() * y_ + localVector.z() * z_;
}

Vec3 Frame3::directionToLocal(const Vec3 &globalVector) const {
    return Vec3(dot(globalVector, x_), dot(globalVector, y_), dot(globalVector, z_));
}

Transform3::Transform3() : m_{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}} {}

Transform3 Transform3::translation(const Vec3 &offset) {
    Transform3 result;
    result.t_ = offset;
    return result;
}

// Formula di Rodrigues: R = cos I + sin [k]x + (1 - cos) k k^T.
Transform3 Transform3::rotation(const Vec3 &axisPoint, const Vec3 &axisDirection, double angle) {
    const Vec3 k = normalized(axisDirection);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double v = 1.0 - c;
    Transform3 result;
    result.m_[0][0] = c + v * k.x() * k.x();
    result.m_[0][1] = v * k.x() * k.y() - s * k.z();
    result.m_[0][2] = v * k.x() * k.z() + s * k.y();
    result.m_[1][0] = v * k.y() * k.x() + s * k.z();
    result.m_[1][1] = c + v * k.y() * k.y();
    result.m_[1][2] = v * k.y() * k.z() - s * k.x();
    result.m_[2][0] = v * k.z() * k.x() - s * k.y();
    result.m_[2][1] = v * k.z() * k.y() + s * k.x();
    result.m_[2][2] = c + v * k.z() * k.z();
    result.t_ = axisPoint - result.applyToVector(axisPoint);
    return result;
}

Transform3 Transform3::scaling(const Vec3 &center, double factor) {
    if (!(std::fabs(factor) > 0.0)) throw std::invalid_argument("Transform3: fattore di scala nullo");
    Transform3 result;
    for (int i = 0; i < 3; ++i) result.m_[i][i] = factor;
    result.t_ = center - factor * center;
    return result;
}

Transform3 Transform3::projectionAlong(const Vec3 &normal) {
    const Vec3 n = normalized(normal);
    Transform3 result;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) result.m_[i][j] = (i == j ? 1.0 : 0.0) - n[i] * n[j];
    return result;
}

Transform3 Transform3::fromFrame(const Frame3 &frame) {
    Transform3 result;
    for (int i = 0; i < 3; ++i) {
        result.m_[i][0] = frame.xDir()[i];
        result.m_[i][1] = frame.yDir()[i];
        result.m_[i][2] = frame.zDir()[i];
    }
    result.t_ = frame.origin();
    return result;
}

Vec3 Transform3::applyToPoint(const Vec3 &point) const {
    return applyToVector(point) + t_;
}

Vec3 Transform3::applyToVector(const Vec3 &vector) const {
    Vec3 result;
    for (int i = 0; i < 3; ++i)
        result[i] = m_[i][0] * vector[0] + m_[i][1] * vector[1] + m_[i][2] * vector[2];
    return result;
}

Transform3 Transform3::operator*(const Transform3 &other) const {
    Transform3 result;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            result.m_[i][j] = m_[i][0] * other.m_[0][j] + m_[i][1] * other.m_[1][j] + m_[i][2] * other.m_[2][j];
    result.t_ = applyToPoint(other.t_);
    return result;
}

bool Transform3::isSimilarity(double *scale) const {
    // Colonne ortogonali e della stessa lunghezza.
    double gram[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            gram[i][j] = m_[0][i] * m_[0][j] + m_[1][i] * m_[1][j] + m_[2][i] * m_[2][j];
    const double squared = (gram[0][0] + gram[1][1] + gram[2][2]) / 3.0;
    if (!(squared > 0.0)) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (std::fabs(gram[i][j] - (i == j ? squared : 0.0)) > 1.0e-12 * squared) return false;
    if (scale) *scale = std::sqrt(squared);
    return true;
}

Transform3 Transform3::inverted() const {
    const double (&a)[3][3] = m_;
    const double c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    const double c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    const double c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    const double determinant = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
    if (!(std::fabs(determinant) > 1.0e-300)) throw std::domain_error("Transform3: matrice singolare");
    const double inv = 1.0 / determinant;
    Transform3 result;
    result.m_[0][0] = c00 * inv;
    result.m_[1][0] = c01 * inv;
    result.m_[2][0] = c02 * inv;
    result.m_[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * inv;
    result.m_[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * inv;
    result.m_[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * inv;
    result.m_[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * inv;
    result.m_[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * inv;
    result.m_[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * inv;
    result.t_ = -result.applyToVector(t_);
    return result;
}

}

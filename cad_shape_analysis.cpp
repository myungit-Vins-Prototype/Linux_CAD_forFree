#include "cad_shape_analysis.h"
#include "fk_topology.h"
#include "fk_surface_algo.h"
#include "fk_tessellate.h"
#include <algorithm>
#include <cmath>
namespace ForgeCad {
using namespace Kernel;
bool surfaceCurvature(const Surface &s, double u, double v, SurfaceCurvature &out) {
    out = {};
    try {
        Vec3 d[9]; s.evaluate(u, v, 2, d);
        const Vec3 a = d[3], b = d[1], n = cross(a,b);
        const double E = dot(a,a), F = dot(a,b), G = dot(b,b), det = squaredNorm(n);
        if (!(det > 1e-24 * E * G) || !std::isfinite(det) || det == 0) return false;
        out.normal = n / std::sqrt(det);
        const double e = dot(out.normal,d[6]), f = dot(out.normal,d[4]), g = dot(out.normal,d[2]);
        const double H = (e*G - 2*f*F + g*E)/(2*det), K = (e*g-f*f)/det;
        const double q = std::sqrt(std::max(0.0,H*H-K));
        out.minimum = H-q; out.maximum = H+q;
        const Vec3 au = (a*G-b*F)/det, bv = (b*E-a*F)/det;
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
            out.tensor[i][j] = e*au[i]*au[j]+f*(au[i]*bv[j]+bv[i]*au[j])+g*bv[i]*bv[j];
            if (!std::isfinite(out.tensor[i][j])) return false;
        }
        return std::isfinite(out.minimum) && std::isfinite(out.maximum);
    } catch (const std::exception &) { return false; }
}
bool curveCurvature(const Curve<3> &c, double t, Vec3 &p, Vec3 &k) {
    try {
        Vec3 d[3]; c.evaluate(t,2,d); p=d[0];
        const double speed2=squaredNorm(d[1]);
        if (!(speed2>0) || !std::isfinite(speed2)) return false;
        k=(d[2]-d[1]*(dot(d[1],d[2])/speed2))/speed2;
        return isFinite(p) && isFinite(k);
    } catch (const std::exception &) { return false; }
}
static QVector3D qt(const Vec3 &p) { return {float(p.x()),float(p.y()),float(p.z())}; }
static const QVector4D gray(.5f,.5f,.5f,1), red(1,.15f,.12f,1), amber(1,.65f,.05f,1), green(.15f,.9f,.35f,1), blue(.1f,.45f,1,1);
bool surfaceIsoCurvature(const Surface &surface, double u, double v, bool varyU, bool normalOnly,
                         Vec3 &point, Vec3 &curvature) {
    try {
        Vec3 d[9]; surface.evaluate(u, v, 2, d);
        point = d[0];
        const Vec3 tangent = varyU ? d[3] : d[1];
        const Vec3 second = varyU ? d[6] : d[2];
        const double speed2 = squaredNorm(tangent);
        if (!(speed2 > 0) || !std::isfinite(speed2)) return false;
        curvature = (second - tangent * (dot(tangent, second) / speed2)) / speed2;
        if (normalOnly) {
            const Vec3 n = cross(d[3], d[1]);
            const double n2 = squaredNorm(n);
            if (!(n2 > 1e-24 * squaredNorm(d[3]) * squaredNorm(d[1]))) return false;
            curvature = n * (dot(curvature, n) / n2);
        }
        return isFinite(point) && isFinite(curvature);
    } catch (const std::exception &) { return false; }
}

// Intersezione di una linea UV con l'unione dei triangoli della faccia ritagliata.
// Il ritaglio e' grafico (approssimato sui contorni), le derivate restano esatte.
static std::vector<Interval> isoIntervals(const FaceMesh &mesh, int fixedAxis, double fixed) {
    std::vector<Interval> pieces;
    for (const auto &triangle : mesh.triangles) {
        std::vector<double> cuts;
        for (int side = 0; side < 3; ++side) {
            const Vec2 a = mesh.parameters.at(triangle[side]);
            const Vec2 b = mesh.parameters.at(triangle[(side + 1) % 3]);
            if (a[fixedAxis] == b[fixedAxis]) {
                if (fixed == a[fixedAxis]) { cuts.push_back(a[1-fixedAxis]); cuts.push_back(b[1-fixedAxis]); }
            } else if (fixed >= std::min(a[fixedAxis], b[fixedAxis]) && fixed <= std::max(a[fixedAxis], b[fixedAxis])) {
                const double f = (fixed-a[fixedAxis])/(b[fixedAxis]-a[fixedAxis]);
                cuts.push_back(a[1-fixedAxis] + f*(b[1-fixedAxis]-a[1-fixedAxis]));
            }
        }
        if (cuts.size() >= 2) {
            const auto bounds = std::minmax_element(cuts.begin(), cuts.end());
            if (*bounds.second > *bounds.first) pieces.push_back({*bounds.first, *bounds.second});
        }
    }
    std::sort(pieces.begin(), pieces.end(), [](const Interval &a, const Interval &b) { return a.lo < b.lo; });
    std::vector<Interval> merged;
    for (const auto &piece : pieces) {
        // Solo rumore floating point: non colmare piccoli fori reali.
        const double epsilon = 64 * std::numeric_limits<double>::epsilon() * std::max({1.0, std::abs(piece.lo), std::abs(piece.hi)});
        if (!merged.empty() && piece.lo <= merged.back().hi + epsilon) merged.back().hi = std::max(merged.back().hi, piece.hi);
        else merged.push_back(piece);
    }
    return merged;
}

BodyDisplay analysisFaceDisplay(const BodyDisplay &display, int faceId) {
    BodyDisplay result;
    for (qsizetype i=0; i+2<display.vertices.size(); i+=3) {
        if (i/3>=display.triangleFaces.size() || display.triangleFaces[i/3]!=faceId) continue;
        for (int k=0; k<3; ++k) {
            result.vertices.append(display.vertices[i+k]);
            result.normals.append(display.normals.value(i+k));
        }
        result.triangleFaces.append(faceId);
    }
    return result;
}
static void validateAnalysisFace(const ForgeBody &body, int faceId) {
    if (faceId>=0 && (!body || !body->contains(FaceId(faceId))))
        throw std::invalid_argument("La faccia scelta non appartiene al corpo risultante");
}
static bool touchesAnalysisFace(const Body &body, const Edge &edge, int faceId) {
    if (faceId<0) return true;
    for (const auto fin : {edge.forward,edge.backward})
        if (body.contains(fin) && body.finFace(fin).index==faceId) return true;
    return false;
}

ShapeAnalysis analyzeSurfaceComb(const ExtrusionObject &object, double scale, const SurfaceCombOptions &options) {
    ShapeAnalysis out;
    if (!object.forgeBody) { out.report << QStringLiteral("Seleziona un corpo o una superficie."); return out; }
    if (!std::isfinite(scale) || scale < 0) throw std::invalid_argument("Scala pettine non valida");
    validateAnalysisFace(object.forgeBody, options.faceId);
    TessellationOptions tessellation;
    if (options.faceId>=0) tessellation.faces.push_back(FaceId(options.faceId));
    tessellation.threads = 1;
    const auto mesh = tessellate(*object.forgeBody, tessellation);
    struct Sample { Vec3 point, curvature; bool first; int family; };
    std::vector<Sample> samples;
    int missing = 0, paths = 0;
    double maximum[2] = {};
    const int density = std::clamp(options.samples, 4, 200);
    for (const auto &faceMesh : mesh.faces) {
        if (faceMesh.parameters.empty() || faceMesh.triangles.empty()) continue;
        const auto &face = object.forgeBody->face(faceMesh.face);
        if (!face.surface) { ++missing; continue; }
        Vec2 low = faceMesh.parameters.front(), high = low;
        for (const Vec2 &uv : faceMesh.parameters) for (int axis = 0; axis < 2; ++axis) {
            low[axis] = std::min(low[axis], uv[axis]); high[axis] = std::max(high[axis], uv[axis]);
        }
        for (int family = 0; family < 2; ++family) {
            const int count = std::clamp(family == 0 ? options.uLines : options.vLines, 0, 40);
            for (int line = 0; line < count; ++line) {
                const double fixed = low[family] + (high[family]-low[family])*(line+1)/(count+1);
                for (const Interval &span : isoIntervals(faceMesh, family, fixed)) {
                    const auto breaks = family == 0 ? face.surface->vBreakpoints(span) : face.surface->uBreakpoints(span);
                    for (std::size_t j = 1; j < breaks.size(); ++j) {
                        // Ai tagli periodici puo' restare un intervallo di pochi ULP:
                        // non sovrapporre un intero pettine su quello zero numerico.
                        const double minimumSpan = 1e-12 * (high[1-family]-low[1-family]);
                        if (!(breaks[j]-breaks[j-1] > minimumSpan)) continue;
                        const int localDensity = std::max(2, int(std::ceil(density * (breaks[j]-breaks[j-1]) /
                            (high[1-family]-low[1-family]))) + 1);
                        bool first = true; ++paths;
                        for (int k = 0; k < localDensity; ++k) {
                            // Non attraversare nodi o raccordare l'inviluppo attraverso fori/singolarita'.
                            const double t = breaks[j-1] + (breaks[j]-breaks[j-1])*(k+.0001)/(localDensity-1+.0002);
                            Vec3 p, c;
                            if (!surfaceIsoCurvature(*face.surface, family == 0 ? fixed : t, family == 0 ? t : fixed,
                                                     family == 1, options.normalOnly, p, c)) {
                                ++missing; first = true; continue;
                            }
                            maximum[family] = std::max(maximum[family], norm(c));
                            samples.push_back({p, c, first, family}); first = false;
                        }
                    }
                }
            }
        }
    }
    if (scale == 0 && !samples.empty()) {
        Vec3 low = samples.front().point, high = low;
        for (const auto &sample : samples) for (int axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], sample.point[axis]); high[axis] = std::max(high[axis], sample.point[axis]);
        }
        const double maxK = std::max(maximum[0], maximum[1]);
        scale = maxK > 0 ? .12*distance(low, high)/maxK : 1;
    }
    const QVector4D colors[] = {{.1f,.9f,1,1}, {1,.35f,.85f,1}};
    Vec3 lastPoint, lastTip;
    bool have = false;
    for (const auto &sample : samples) {
        const Vec3 tip = sample.point - sample.curvature*scale;
        if (!isFinite(tip)) { ++missing; have = false; continue; }
        const auto color = colors[sample.family];
        out.lines.append({qt(sample.point), color}); out.lines.append({qt(tip), color});
        if (have && !sample.first) {
            out.grid.append({qt(lastPoint), color}); out.grid.append({qt(sample.point), color});
            out.lines.append({qt(lastTip), color}); out.lines.append({qt(tip), color});
        }
        lastPoint = sample.point; lastTip = tip; have = true;
    }
    out.report << QStringLiteral("%1 tratti U/V. Massimi campionati: U costante %2 mm⁻¹; V costante %3 mm⁻¹. Scala comune %4 mm².")
        .arg(paths).arg(maximum[0],0,'g',5).arg(maximum[1],0,'g',5).arg(scale,0,'g',5);
    if (samples.empty()) out.report << QStringLiteral("Nessuna isoparametrica disponibile: controlla densità U/V e facce.");
    if (missing || mesh.failedFaces) out.report << QStringLiteral("Omissioni: %1 campioni, %2 facce non tessellabili.").arg(missing).arg(mesh.failedFaces);
    return out;
}

ShapeAnalysis analyzeShape(const ExtrusionObject &object, int mode, double scale, double angleTolerance, double curvatureTolerance, int faceId) {
    ShapeAnalysis out;
    int missing=0;
    validateAnalysisFace(object.forgeBody,faceId);
    if (mode==4) {
        double maxK=0;
        struct Sample { Vec3 point, curvature; bool first; };
        std::vector<Sample> samples;
        auto comb=[&](const Curve<3> &curve, Interval range) {
            if (!std::isfinite(range.lo) || !std::isfinite(range.hi)) { ++missing; return; }
            const auto breaks=curve.breakpoints(range);
            for (std::size_t b=1;b<breaks.size();++b) {
                bool have=false;
                for (int i=0;i<=24;++i) {
                    // Un campione interno a ciascun tratto evita di unire due lati di un nodo.
                    const double f=(i+.001)/24.002;
                    Vec3 p,k;
                    if (!curveCurvature(curve,breaks[b-1]+(breaks[b]-breaks[b-1])*f,p,k)) { ++missing; have=false; continue; }
                    maxK=std::max(maxK,norm(k));
                    samples.push_back({p,k,!have}); have=true;
                }
            }
        };
        if (object.curve) comb(*object.curve,object.curve->domain());
        else if(object.forgeBody) for(auto id:object.forgeBody->edges()) {
            const auto &e=object.forgeBody->edge(id); if(e.curve && touchesAnalysisFace(*object.forgeBody,e,faceId)) comb(*e.curve,e.range);
        }
        if(scale==0 && !samples.empty()) {
            Vec3 lo=samples.front().point, hi=lo;
            for(const auto &sample:samples) for(int j=0;j<3;++j) {
                lo[j]=std::min(lo[j],sample.point[j]); hi[j]=std::max(hi[j],sample.point[j]);
            }
            scale=maxK>0 ? .15*distance(lo,hi)/maxK : 1;
        }
        Vec3 last; bool have=false;
        for(const auto &sample:samples) {
            // Convenzione grafica: denti all'esterno della piega.
            const Vec3 tip=sample.point-sample.curvature*scale;
            if(!isFinite(tip)) { ++missing; have=false; continue; }
            out.lines.append({qt(sample.point),blue}); out.lines.append({qt(tip),blue});
            if(have && !sample.first) { out.lines.append({qt(last),amber}); out.lines.append({qt(tip),amber}); }
            last=tip; have=true;
        }
        out.report << QStringLiteral("Curvatura massima campionata: %1 mm⁻¹. Lunghezza denti = curvatura × %2 mm².").arg(maxK,0,'g',5).arg(scale);
    } else if(object.forgeBody && mode==3) {
        const auto &body=*object.forgeBody;
        for (auto id:body.edges()) {
            const auto &e=body.edge(id); if(!e.curve || !touchesAnalysisFace(body,e,faceId)) continue;
            ShapeAnalysis::Border border; border.color=gray;
            double maxAngle=0, maxDelta=0; int valid=0; bool gap=false;
            const bool paired=body.contains(e.forward) && body.contains(e.backward)
                && body.contains(body.finFace(e.forward)) && body.contains(body.finFace(e.backward));
            for(int i=0;i<=32;++i) {
                const double t=e.range.lo+(e.range.hi-e.range.lo)*i/32.0;
                border.points.append(qt(e.curve->point(t)));
                if(!paired || i==0 || i==32) continue;
                const auto &f1=body.fin(e.forward), &f2=body.fin(e.backward);
                const auto &s1=body.face(body.finFace(e.forward)), &s2=body.face(body.finFace(e.backward));
                if(!f1.pcurve || !f2.pcurve || !s1.surface || !s2.surface) continue;
                const Vec2 uv1=f1.pcurve->point(t), uv2=f2.pcurve->point(t);
                SurfaceCurvature a,b;
                if(!surfaceCurvature(*s1.surface,uv1.x(),uv1.y(),a) || !surfaceCurvature(*s2.surface,uv2.x(),uv2.y(),b)) continue;
                gap |= distance(s1.surface->point(uv1.x(),uv1.y()),s2.surface->point(uv2.x(),uv2.y()))>1e-5;
                const double alignment=dot(a.normal,b.normal), sign=alignment<0?-1:1;
                const double angle=std::acos(std::clamp(std::abs(alignment),0.0,1.0))*180/kPi;
                double delta=0;
                for(int r=0;r<3;++r) for(int c=0;c<3;++c) delta+=std::pow(a.tensor[r][c]-sign*b.tensor[r][c],2);
                maxAngle=std::max(maxAngle,angle); maxDelta=std::max(maxDelta,std::sqrt(delta)); ++valid;
            }
            QString state=QStringLiteral("bordo libero / non valutabile");
            if(paired && valid==31) {
                if(gap) { border.color=red; state=QStringLiteral("scarto di posizione > 0,00001 mm"); }
                else if(maxAngle>angleTolerance) {border.color=red; state=QStringLiteral("salto di tangenza");}
                else if(maxDelta>curvatureTolerance) {border.color=amber; state=QStringLiteral("salto di curvatura");}
                else {border.color=green; state=QStringLiteral("regolare nei campioni");}
            } else if(paired) ++missing;
            out.borders.append(border);
            out.report << QStringLiteral("E%1: %2; angolo %3°; Δcurvatura %4 mm⁻¹ (%5/31 campioni)")
                .arg(id.index).arg(state).arg(maxAngle,0,'g',4).arg(maxDelta,0,'g',4).arg(valid);
        }
    } else if(object.forgeBody && (mode==1 || mode==2)) {
        const auto &d=object.display;
        double maxK=0;
        // Un valore per triangolo: proiezione del baricentro sulla faccia esatta.
        for(qsizetype i=0;i+2<d.vertices.size();i+=3) {
            if(faceId>=0 && (i/3>=d.triangleFaces.size() || d.triangleFaces[i/3]!=faceId)) continue;
            QVector4D color=gray;
            if(i/3<d.triangleFaces.size()) {
                const FaceId id(d.triangleFaces[i/3]);
                if(object.forgeBody->contains(id)) try {
                    const auto &surface=object.forgeBody->face(id).surface;
                    if (!surface) throw std::domain_error("Superficie assente");
                    const auto &s=*surface;
                    const QVector3D q=(d.vertices[i]+d.vertices[i+1]+d.vertices[i+2])/3;
                    const auto uv=projectPoint(s,Vec3(q.x(),q.y(),q.z())); SurfaceCurvature k;
                    if(surfaceCurvature(s,uv.u,uv.v,k)) {
                        const double value=std::max(std::abs(k.minimum),std::abs(k.maximum)); maxK=std::max(maxK,value);
                        if(mode==1) {
                            const float f=float(std::clamp(value/scale,0.0,1.0));
                            color=QVector4D(f,1-std::abs(2*f-1),1-f,1);
                        } else {
                            const double tol=curvatureTolerance;
                            color=(std::abs(k.minimum)<=tol || std::abs(k.maximum)<=tol)?green:
                                (k.minimum*k.maximum<0?blue:amber);
                        }
                    } else ++missing;
                } catch(const std::exception &) { ++missing; }
                else ++missing;
            } else ++missing;
            for(int j=0;j<3;++j) out.triangles.append({d.vertices[i+j],color});
        }
        out.report << QStringLiteral("Massimo campionato |k|: %1 mm⁻¹. Valori al baricentro proiettato di ciascun triangolo.").arg(maxK,0,'g',5);
    }
    if(missing) out.report << QStringLiteral("%1 campioni/giunzioni non valutabili (grigio nelle mappe; omessi nel pettine).").arg(missing);
    return out;
}
}

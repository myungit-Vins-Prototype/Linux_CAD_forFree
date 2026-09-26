#include "cad_kernel.h"

#include "cad_curve_solver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeFix_Wire.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <memory>

namespace ForgeCad {
namespace {

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

QString failureMessage(const Standard_Failure &failure) {
    const char *message = failure.GetMessageString();
    return message && *message ? QString::fromLocal8Bit(message) : QStringLiteral("errore del kernel geometrico");
}

// Spigoli esatti di tutte le entita' dello schizzo, nello spazio 3D.
Handle(TopTools_HSequenceOfShape) sketchEdges(const SketchObject &sketch) {
    Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
    const gp_Pln plane(sketchAxes(sketch));
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (sketch.isConstructionSegment(index)) continue;
        const SketchSegment &segment = sketch.segments.at(index);
        const gp_Pnt first = sketchToWorld(segment.first, sketch);
        const gp_Pnt second = sketchToWorld(segment.second, sketch);
        if (first.Distance(second) <= Precision::Confusion()) continue;
        BRepBuilderAPI_MakeEdge edge(first, second);
        if (edge.IsDone()) edges->Append(edge.Edge());
    }
    for (const CurveObject &curve : sketch.curves) {
        if (curve.construction) continue;
        for (const Handle(Geom2d_Curve) &piece : curveGeometry(curve)) {
            BRepBuilderAPI_MakeEdge edge(GeomAPI::To3d(piece, plane));
            if (edge.IsDone()) edges->Append(edge.Edge());
        }
    }
    return edges;
}

double faceArea(const TopoDS_Face &face) {
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    return std::abs(properties.Mass());
}

gp_Pnt firstVertexPoint(const TopoDS_Shape &shape) {
    TopExp_Explorer explorer(shape, TopAbs_VERTEX);
    return explorer.More() ? BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current())) : gp_Pnt();
}

}

gp_Ax3 sketchAxes(int plane) {
    if (plane == 1) return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    if (plane == 2) return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(-1, 0, 0), gp_Dir(0, 0, 1));
    return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
}

gp_Pnt sketchToWorld(const QPointF &point, int plane) {
    const gp_Ax3 axes = sketchAxes(plane);
    return axes.Location().Translated(gp_Vec(axes.XDirection()) * point.x()
                                      + gp_Vec(axes.YDirection()) * point.y());
}

QVector3D sketchToDisplay(const QPointF &point, int plane) {
    const gp_Pnt world = sketchToWorld(point, plane);
    return QVector3D(float(world.X()), float(world.Y()), float(world.Z()));
}

QPointF worldToSketch(const gp_Pnt &point, int plane) {
    const gp_Ax3 axes = sketchAxes(plane);
    const gp_Vec offset(axes.Location(), point);
    return QPointF(offset.Dot(gp_Vec(axes.XDirection())), offset.Dot(gp_Vec(axes.YDirection())));
}

gp_Vec extrusionVector(int plane, double distance) {
    if (plane == 1) return gp_Vec(0.0, distance, 0.0);
    if (plane == 2) return gp_Vec(distance, 0.0, 0.0);
    return gp_Vec(0.0, 0.0, distance);
}

gp_Ax3 sketchAxes(const SketchObject &sketch) {
    if (sketch.plane != kFacePlane && !sketch.customFrame) return sketchAxes(sketch.plane);
    const SketchFrame &f = sketch.frame;
    return gp_Ax3(gp_Pnt(f.origin[0], f.origin[1], f.origin[2]), gp_Dir(f.normal[0], f.normal[1], f.normal[2]),
                  gp_Dir(f.xAxis[0], f.xAxis[1], f.xAxis[2]));
}

gp_Pnt sketchToWorld(const QPointF &point, const SketchObject &sketch) {
    const gp_Ax3 axes = sketchAxes(sketch);
    return axes.Location().Translated(gp_Vec(axes.XDirection()) * point.x() + gp_Vec(axes.YDirection()) * point.y());
}

QVector3D sketchToDisplay(const QPointF &point, const SketchObject &sketch) {
    const gp_Pnt world = sketchToWorld(point, sketch);
    return QVector3D(float(world.X()), float(world.Y()), float(world.Z()));
}

QPointF worldToSketch(const gp_Pnt &point, const SketchObject &sketch) {
    const gp_Ax3 axes = sketchAxes(sketch);
    const gp_Vec offset(axes.Location(), point);
    return QPointF(offset.Dot(gp_Vec(axes.XDirection())), offset.Dot(gp_Vec(axes.YDirection())));
}

gp_Vec extrusionVector(const SketchObject &sketch, double distance) {
    if (sketch.plane != kFacePlane && !sketch.customFrame) return extrusionVector(sketch.plane, distance);
    return gp_Vec(sketchAxes(sketch).Direction()) * distance;
}

SketchFrame referenceSketchFrame(int plane, const AxesOrientation &o) {
    const gp_Vec right(o.right[0], o.right[1], o.right[2]), up(o.up[0], o.up[1], o.up[2]), toward(o.toward[0], o.toward[1], o.toward[2]);
    const gp_Vec axis = plane == 0 ? gp_Vec(0, 0, 1) : plane == 1 ? gp_Vec(0, 1, 0) : gp_Vec(1, 0, 0);
    // In quale vista standard il piano si vede di fronte: frontale (verso
    // l'osservatore), superiore (in alto) o destra; la normale guarda chi osserva.
    const double c[3] = {axis.Dot(right), axis.Dot(up), axis.Dot(toward)};
    int k = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(c[i]) > std::abs(c[k])) k = i;
    const gp_Vec normal = axis * (c[k] < 0.0 ? -1.0 : 1.0);
    gp_Vec screenUp = k == 1 ? -toward : up;  // nella vista superiore in alto c'e' il fondo
    screenUp -= normal * screenUp.Dot(normal);
    if (screenUp.Magnitude() < 1e-9) screenUp = right.Crossed(normal);
    screenUp.Normalize();
    const gp_Vec x = screenUp.Crossed(normal);
    SketchFrame frame;
    for (int i = 0; i < 3; ++i) {
        frame.origin[i] = 0.0;
        frame.xAxis[i] = x.Coord(i + 1);
        frame.normal[i] = normal.Coord(i + 1);
    }
    return frame;
}

SketchFrame faceSketchFrame(const gp_Pnt &point, const gp_Dir &normal, const gp_Dir &upDirection) {
    const gp_Vec n(normal);
    // Origine: proiezione dell'origine del modello sul piano.
    const gp_Vec origin = n * gp_Vec(gp_Pnt(0, 0, 0), point).Dot(n);
    const gp_Vec upAxis(upDirection);
    gp_Vec up = upAxis - n * n.Dot(upAxis);
    if (up.Magnitude() < 1e-9) up = gp_Vec(0, 1, 0) - n * n.Y();
    if (up.Magnitude() < 1e-9) up = gp_Vec(1, 0, 0) - n * n.X();
    up.Normalize();
    const gp_Vec x = up.Crossed(n);
    SketchFrame frame;
    for (int k = 0; k < 3; ++k) {
        frame.origin[k] = origin.Coord(k + 1);
        frame.xAxis[k] = x.Coord(k + 1);
        frame.normal[k] = n.Coord(k + 1);
    }
    return frame;
}

bool buildSketchProfile(const SketchObject &sketch, TopoDS_Shape &profile, bool &closed, QString *error) {
    closed = false;
    try {
        Handle(TopTools_HSequenceOfShape) edges = sketchEdges(sketch);
        if (edges->IsEmpty()) {
            setError(error, QStringLiteral("Lo schizzo non contiene geometria."));
            return false;
        }
        Handle(TopTools_HSequenceOfShape) wires;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, kSketchConnectionTolerance, Standard_False, wires);

        QVector<TopoDS_Face> faces;
        QVector<TopoDS_Wire> openWires;
        const gp_Pln plane(sketchAxes(sketch));
        for (int index = 1; index <= wires->Length(); ++index) {
            TopoDS_Wire wire = TopoDS::Wire(wires->Value(index));
            ShapeFix_Wire fix;
            fix.Load(wire);
            fix.SetPrecision(kSketchConnectionTolerance);
            fix.FixConnected();
            wire = fix.Wire();
            if (!BRep_Tool::IsClosed(wire)) {
                openWires.append(wire);
                continue;
            }
            BRepBuilderAPI_MakeFace face(plane, wire, Standard_True);
            if (face.IsDone()) faces.append(face.Face());
            else openWires.append(wire);
        }

        if (faces.isEmpty()) {
            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            for (const TopoDS_Wire &wire : openWires) builder.Add(compound, wire);
            profile = compound;
            return true;
        }

        // Contorni annidati: profondita' pari = materiale, dispari = foro.
        std::sort(faces.begin(), faces.end(), [](const TopoDS_Face &a, const TopoDS_Face &b) {
            return faceArea(a) > faceArea(b);
        });
        QVector<int> depth(faces.size(), 0);
        QVector<int> parent(faces.size(), -1);
        for (int inner = 0; inner < faces.size(); ++inner) {
            const gp_Pnt probe = firstVertexPoint(faces.at(inner));
            for (int outer = 0; outer < inner; ++outer) {
                BRepClass_FaceClassifier classifier(faces.at(outer), probe, kSketchConnectionTolerance);
                if (classifier.State() == TopAbs_IN) {
                    ++depth[inner];
                    parent[inner] = outer;
                }
            }
        }
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (int index = 0; index < faces.size(); ++index) {
            if (depth.at(index) % 2 != 0) continue;
            TopoDS_Shape material = faces.at(index);
            for (int hole = 0; hole < faces.size(); ++hole) {
                if (parent.at(hole) != index || depth.at(hole) != depth.at(index) + 1) continue;
                BRepAlgoAPI_Cut cut(material, faces.at(hole));
                if (!cut.IsDone() || cut.HasErrors()) {
                    setError(error, QStringLiteral("Impossibile ricavare un foro nel profilo."));
                    return false;
                }
                material = cut.Shape();
            }
            builder.Add(compound, material);
        }
        profile = compound;
        closed = true;
        return true;
    } catch (const Standard_Failure &failure) {
        setError(error, failureMessage(failure));
        return false;
    }
}

TopoDS_Shape buildExtrusion(const SketchObject &sketch, double distance, bool &solid, QString *error) {
    solid = false;
    if (std::abs(distance) <= Precision::Confusion()) {
        setError(error, QStringLiteral("La distanza di estrusione e' nulla."));
        return {};
    }
    TopoDS_Shape profile;
    bool closed = false;
    if (!buildSketchProfile(sketch, profile, closed, error)) return {};
    try {
        BRepPrimAPI_MakePrism prism(profile, extrusionVector(sketch, distance), Standard_True);
        if (!prism.IsDone()) {
            setError(error, QStringLiteral("Estrusione non riuscita."));
            return {};
        }
        const TopoDS_Shape shape = prism.Shape();
        if (!BRepCheck_Analyzer(shape).IsValid()) {
            setError(error, QStringLiteral("L'estrusione ha prodotto una forma non valida."));
            return {};
        }
        solid = closed && isSolidShape(shape);
        return shape;
    } catch (const Standard_Failure &failure) {
        setError(error, failureMessage(failure));
        return {};
    }
}

bool sketchRevolutionAxis(const SketchObject &sketch, int axis, QPointF &point, QPointF &direction, QString *error) {
    if (axis == -1 || axis == -2) {
        point = QPointF(0.0, 0.0);
        direction = axis == -1 ? QPointF(1.0, 0.0) : QPointF(0.0, 1.0);
        return true;
    }
    if (axis < 0 || axis >= sketch.segments.size()) {
        setError(error, QStringLiteral("L'asse di rivoluzione non esiste piu' nello schizzo."));
        return false;
    }
    const SketchSegment &segment = sketch.segments.at(axis);
    const QPointF delta = segment.second - segment.first;
    const double length = std::hypot(delta.x(), delta.y());
    if (length <= kSketchConnectionTolerance) {
        setError(error, QStringLiteral("L'asse di rivoluzione ha lunghezza nulla."));
        return false;
    }
    point = segment.first;
    direction = delta / length;
    return true;
}

int revolutionProfileSide(const SketchObject &sketch, const QPointF &point, const QPointF &direction, QString *error) {
    double lowest = 0.0, highest = 0.0;
    const auto consider = [&](const QPointF &q) {
        const QPointF r = q - point;
        const double side = direction.x() * r.y() - direction.y() * r.x();
        lowest = std::min(lowest, side);
        highest = std::max(highest, side);
    };
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (sketch.isConstructionSegment(index)) continue;
        consider(sketch.segments.at(index).first);
        consider(sketch.segments.at(index).second);
    }
    // I campioni di visualizzazione servono solo a decidere il lato (la
    // geometria resta quella esatta).
    for (const CurveObject &curve : sketch.curves) {
        if (curve.construction) continue;
        for (const QPointF &sample : curve.samples) consider(sample);
    }
    const double tolerance = kSketchConnectionTolerance;
    if (lowest < -tolerance && highest > tolerance) {
        setError(error, QStringLiteral("Il profilo attraversa l'asse di rivoluzione: deve stare tutto da una parte."));
        return 0;
    }
    if (lowest >= -tolerance && highest <= tolerance) {
        setError(error, QStringLiteral("Il profilo giace sull'asse di rivoluzione."));
        return 0;
    }
    return highest > tolerance ? 1 : -1;
}

TopoDS_Shape buildRevolution(const SketchObject &sketch, int axis, double angleDegrees, QString *error) {
    QPointF point, direction;
    if (!sketchRevolutionAxis(sketch, axis, point, direction, error)) return {};
    if (std::abs(angleDegrees) <= 1.0e-9) {
        setError(error, QStringLiteral("L'angolo di rivoluzione e' nullo."));
        return {};
    }
    if (revolutionProfileSide(sketch, point, direction, error) == 0) return {};
    TopoDS_Shape profile;
    bool closed = false;
    if (!buildSketchProfile(sketch, profile, closed, error)) return {};
    if (!closed) {
        setError(error, QStringLiteral("La rivoluzione richiede un profilo chiuso."));
        return {};
    }
    try {
        const gp_Ax3 axes = sketchAxes(sketch);
        const gp_Dir worldDirection(gp_Vec(axes.XDirection()) * direction.x() + gp_Vec(axes.YDirection()) * direction.y());
        const gp_Ax1 revolutionAxis(sketchToWorld(point, sketch), worldDirection);
        const bool full = std::abs(angleDegrees) >= 360.0 - 1.0e-9;
        std::unique_ptr<BRepPrimAPI_MakeRevol> revolution =
            full ? std::make_unique<BRepPrimAPI_MakeRevol>(profile, revolutionAxis, Standard_True)
                 : std::make_unique<BRepPrimAPI_MakeRevol>(profile, revolutionAxis, angleDegrees * M_PI / 180.0, Standard_True);
        if (!revolution->IsDone()) {
            setError(error, QStringLiteral("Rivoluzione non riuscita."));
            return {};
        }
        const TopoDS_Shape shape = revolution->Shape();
        if (!BRepCheck_Analyzer(shape).IsValid()) {
            setError(error, QStringLiteral("La rivoluzione ha prodotto una forma non valida."));
            return {};
        }
        return shape;
    } catch (const Standard_Failure &failure) {
        setError(error, failureMessage(failure));
        return {};
    }
}

gp_Ax3 primitiveAxes(const PrimitiveParameters &parameters) {
    gp_Ax3 axes = sketchAxes(parameters.plane);
    axes.SetLocation(gp_Pnt(parameters.origin[0], parameters.origin[1], parameters.origin[2]));
    return axes;
}

QString primitiveError(const PrimitiveParameters &parameters) {
    const double *size = parameters.size;
    const double confusion = Precision::Confusion();
    switch (parameters.kind) {
    case PrimitiveKind::Box:
        if (size[0] <= confusion || size[1] <= confusion || size[2] <= confusion)
            return QStringLiteral("Le dimensioni del parallelepipedo devono essere positive.");
        break;
    case PrimitiveKind::Cylinder:
        if (size[0] <= confusion || size[1] <= confusion) return QStringLiteral("Raggio e altezza del cilindro devono essere positivi.");
        break;
    case PrimitiveKind::Sphere:
        if (size[0] <= confusion) return QStringLiteral("Il raggio della sfera deve essere positivo.");
        break;
    case PrimitiveKind::Cone:
        if (size[2] <= confusion || size[0] < 0.0 || size[1] < 0.0 || std::max(size[0], size[1]) <= confusion)
            return QStringLiteral("Il cono richiede un'altezza positiva e almeno un raggio positivo.");
        if (std::abs(size[0] - size[1]) <= confusion) return QStringLiteral("Con i due raggi uguali usa il cilindro.");
        break;
    case PrimitiveKind::Torus:
        if (size[1] <= confusion || size[0] <= size[1] + confusion)
            return QStringLiteral("Il raggio minore del toro deve essere positivo e minore del maggiore.");
        break;
    }
    return {};
}

TopoDS_Shape buildPrimitive(const PrimitiveParameters &parameters, QString *error) {
    const QString invalid = primitiveError(parameters);
    if (!invalid.isEmpty()) {
        setError(error, invalid);
        return {};
    }
    try {
        const gp_Ax2 axes = primitiveAxes(parameters).Ax2();
        const double *size = parameters.size;
        TopoDS_Shape shape;
        switch (parameters.kind) {
        case PrimitiveKind::Box: shape = BRepPrimAPI_MakeBox(axes, size[0], size[1], size[2]).Shape(); break;
        case PrimitiveKind::Cylinder: shape = BRepPrimAPI_MakeCylinder(axes, size[0], size[1]).Shape(); break;
        case PrimitiveKind::Sphere: shape = BRepPrimAPI_MakeSphere(axes, size[0]).Shape(); break;
        case PrimitiveKind::Cone: shape = BRepPrimAPI_MakeCone(axes, size[0], size[1], size[2]).Shape(); break;
        case PrimitiveKind::Torus: shape = BRepPrimAPI_MakeTorus(axes, size[0], size[1]).Shape(); break;
        }
        if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid()) {
            setError(error, QStringLiteral("La primitiva non e' valida."));
            return {};
        }
        return shape;
    } catch (const Standard_Failure &failure) {
        setError(error, failureMessage(failure));
        return {};
    }
}

TopoDS_Shape buildBlend(const TopoDS_Shape &base, const QVector<EdgePoint> &points, double size, bool chamfer, QString *error) {
    if (base.IsNull()) {
        setError(error, QStringLiteral("Il corpo da raccordare non ha geometria valida."));
        return {};
    }
    if (points.isEmpty() || !(size > Precision::Confusion())) {
        setError(error, QStringLiteral("Servono almeno uno spigolo e una misura positiva."));
        return {};
    }
    try {
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(base, TopAbs_EDGE, edges);
        Bnd_Box box;
        BRepBndLib::Add(base, box);
        const double reach = 1e-3 * std::max(1.0, std::sqrt(box.SquareExtent()));
        std::unique_ptr<BRepFilletAPI_MakeFillet> fillet;
        std::unique_ptr<BRepFilletAPI_MakeChamfer> bevel;
        if (chamfer) bevel = std::make_unique<BRepFilletAPI_MakeChamfer>(base);
        else fillet = std::make_unique<BRepFilletAPI_MakeFillet>(base);
        for (const EdgePoint &point : points) {
            const TopoDS_Shape vertex = BRepBuilderAPI_MakeVertex(gp_Pnt(point.x, point.y, point.z)).Shape();
            int best = 0;
            double closest = reach;
            for (int index = 1; index <= edges.Extent(); ++index) {
                BRepExtrema_DistShapeShape distance(vertex, edges(index));
                if (distance.IsDone() && distance.Value() < closest) {
                    closest = distance.Value();
                    best = index;
                }
            }
            if (best == 0) {
                setError(error, QStringLiteral("Uno degli spigoli scelti non esiste piu' nel corpo."));
                return {};
            }
            if (chamfer) bevel->Add(size, TopoDS::Edge(edges(best)));
            else fillet->Add(size, TopoDS::Edge(edges(best)));
        }
        BRepBuilderAPI_MakeShape &maker = chamfer ? static_cast<BRepBuilderAPI_MakeShape &>(*bevel) : static_cast<BRepBuilderAPI_MakeShape &>(*fillet);
        maker.Build();
        if (!maker.IsDone()) {
            setError(error, chamfer ? QStringLiteral("Smusso non riuscito (distanza troppo grande?).")
                                    : QStringLiteral("Raccordo non riuscito (raggio troppo grande?)."));
            return {};
        }
        const TopoDS_Shape shape = maker.Shape();
        if (!BRepCheck_Analyzer(shape).IsValid()) {
            setError(error, QStringLiteral("Il raccordo ha prodotto una forma non valida."));
            return {};
        }
        return shape;
    } catch (const Standard_Failure &failure) {
        setError(error, failureMessage(failure));
        return {};
    }
}

TopoDS_Shape booleanOperation(const TopoDS_Shape &first, const TopoDS_Shape &second,
                              BooleanOperation operation, QString *error) {
    if (first.IsNull() || second.IsNull()) {
        setError(error, QStringLiteral("Uno degli operandi non ha geometria valida."));
        return {};
    }
    try {
        TopoDS_Shape result;
        const auto run = [&](BRepAlgoAPI_BooleanOperation &algorithm) {
            algorithm.SetRunParallel(Standard_True);
            algorithm.Build();
            if (!algorithm.IsDone() || algorithm.HasErrors()) return false;
            result = algorithm.Shape();
            return true;
        };
        bool done = false;
        if (operation == BooleanOperation::Union) {
            BRepAlgoAPI_Fuse algorithm;
            TopTools_ListOfShape arguments, tools;
            arguments.Append(first);
            tools.Append(second);
            algorithm.SetArguments(arguments);
            algorithm.SetTools(tools);
            done = run(algorithm);
        } else if (operation == BooleanOperation::Intersection) {
            BRepAlgoAPI_Common algorithm;
            TopTools_ListOfShape arguments, tools;
            arguments.Append(first);
            tools.Append(second);
            algorithm.SetArguments(arguments);
            algorithm.SetTools(tools);
            done = run(algorithm);
        } else {
            BRepAlgoAPI_Cut algorithm;
            TopTools_ListOfShape arguments, tools;
            arguments.Append(first);
            tools.Append(second);
            algorithm.SetArguments(arguments);
            algorithm.SetTools(tools);
            done = run(algorithm);
        }
        if (!done) {
            setError(error, QStringLiteral("L'operazione booleana non e' riuscita."));
            return {};
        }
        TopExp_Explorer faces(result, TopAbs_FACE);
        if (!faces.More()) {
            setError(error, QStringLiteral("Il risultato dell'operazione e' vuoto: i due solidi non si sovrappongono."));
            return {};
        }
        ShapeUpgrade_UnifySameDomain unify(result, Standard_True, Standard_True, Standard_True);
        unify.Build();
        result = unify.Shape();
        if (!BRepCheck_Analyzer(result).IsValid()) {
            setError(error, QStringLiteral("L'operazione booleana ha prodotto una forma non valida."));
            return {};
        }
        return result;
    } catch (const Standard_Failure &failure) {
        setError(error, failureMessage(failure));
        return {};
    }
}

bool isSolidShape(const TopoDS_Shape &shape) {
    return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More();
}

void tessellate(const TopoDS_Shape &shape, int quality, BodyDisplay &display) {
    display = {};
    display.quality = quality;
    if (shape.IsNull()) return;
    try {
        Bnd_Box box;
        BRepBndLib::Add(shape, box);
        if (box.IsVoid()) return;
        const double diagonal = std::sqrt(box.SquareExtent());
        const double deflection = diagonal * (quality <= 0 ? 4.0e-3 : quality == 1 ? 1.0e-3 : 2.0e-4);
        const double angular = quality <= 0 ? 0.5 : quality == 1 ? 0.25 : 0.1;
        BRepTools::Clean(shape);
        BRepMesh_IncrementalMesh mesher(shape, deflection, Standard_False, angular, Standard_True);

        for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
            const TopoDS_Face face = TopoDS::Face(explorer.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
            if (triangulation.IsNull()) continue;
            if (!triangulation->HasNormals()) BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);
            const gp_Trsf transform = location.Transformation();
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            for (int index = 1; index <= triangulation->NbTriangles(); ++index) {
                int nodes[3];
                triangulation->Triangle(index).Get(nodes[0], nodes[1], nodes[2]);
                if (reversed) std::swap(nodes[1], nodes[2]);
                for (int node : nodes) {
                    const gp_Pnt point = triangulation->Node(node).Transformed(transform);
                    gp_Dir normal = triangulation->Normal(node).Transformed(transform);
                    if (reversed) normal.Reverse();
                    display.vertices.append(QVector3D(float(point.X()), float(point.Y()), float(point.Z())));
                    display.normals.append(QVector3D(float(normal.X()), float(normal.Y()), float(normal.Z())));
                }
            }
        }

        // Spigoli veri del B-rep, escluse le cuciture delle superfici chiuse.
        TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        for (int index = 1; index <= edgeFaces.Extent(); ++index) {
            const TopoDS_Edge edge = TopoDS::Edge(edgeFaces.FindKey(index));
            if (BRep_Tool::Degenerated(edge)) continue;
            bool seam = false;
            for (const TopoDS_Shape &face : edgeFaces.FindFromIndex(index))
                seam = seam || BRep_Tool::IsClosed(edge, TopoDS::Face(face));
            if (seam) continue;
            BRepAdaptor_Curve curve(edge);
            GCPnts_TangentialDeflection discretizer(curve, angular * 0.5, deflection, 2);
            QVector<QVector3D> polyline;
            for (int point = 1; point <= discretizer.NbPoints(); ++point) {
                const gp_Pnt value = discretizer.Value(point);
                polyline.append(QVector3D(float(value.X()), float(value.Y()), float(value.Z())));
            }
            if (polyline.size() >= 2) display.edges.append(polyline);
        }
    } catch (const Standard_Failure &) {
        display.vertices.clear();
        display.normals.clear();
        display.edges.clear();
    }
}

bool pickFace(const TopoDS_Shape &shape, const QVector3D &origin, const QVector3D &direction, FaceHit &hit) {
    if (shape.IsNull()) return false;
    try {
        IntCurvesFace_ShapeIntersector intersector;
        intersector.Load(shape, Precision::Confusion());
        const gp_Lin line(gp_Pnt(origin.x(), origin.y(), origin.z()), gp_Dir(direction.x(), direction.y(), direction.z()));
        intersector.Perform(line, 0.0, Precision::Infinite());
        if (!intersector.IsDone() || intersector.NbPnt() == 0) return false;
        int nearest = 1;
        for (int index = 2; index <= intersector.NbPnt(); ++index)
            if (intersector.WParameter(index) < intersector.WParameter(nearest)) nearest = index;
        const TopoDS_Face face = intersector.Face(nearest);
        hit = {};
        hit.distance = intersector.WParameter(nearest);
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(shape, TopAbs_FACE, faces);
        hit.face = faces.FindIndex(face) - 1;
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane) {
            const gp_Pln plane = surface.Plane();
            gp_Dir normal = plane.Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
            hit.planar = true;
            for (int k = 0; k < 3; ++k) {
                hit.point[k] = plane.Location().Coord(k + 1);
                hit.normal[k] = normal.Coord(k + 1);
            }
        }
        // Un punto interno di ogni spigolo (le cuciture e gli spigoli degeneri non sono bordi).
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(face, TopAbs_EDGE, edges);
        for (int index = 1; index <= edges.Extent(); ++index) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(index));
            if (BRep_Tool::Degenerated(edge) || BRep_Tool::IsClosed(edge, face)) continue;
            BRepAdaptor_Curve curve(edge);
            const gp_Pnt p = curve.Value(0.5 * (curve.FirstParameter() + curve.LastParameter()));
            hit.edges.append({p.X(), p.Y(), p.Z()});
        }
        return true;
    } catch (const Standard_Failure &) {
        return false;
    }
}

bool intersectRay(const TopoDS_Shape &shape, const QVector3D &origin, const QVector3D &direction,
                  double &distance) {
    if (shape.IsNull()) return false;
    try {
        IntCurvesFace_ShapeIntersector intersector;
        intersector.Load(shape, Precision::Confusion());
        const gp_Lin line(gp_Pnt(origin.x(), origin.y(), origin.z()),
                          gp_Dir(direction.x(), direction.y(), direction.z()));
        intersector.Perform(line, 0.0, Precision::Infinite());
        if (!intersector.IsDone() || intersector.NbPnt() == 0) return false;
        double nearest = Precision::Infinite();
        for (int index = 1; index <= intersector.NbPnt(); ++index)
            nearest = std::min(nearest, intersector.WParameter(index));
        distance = nearest;
        return true;
    } catch (const Standard_Failure &) {
        return false;
    }
}

}

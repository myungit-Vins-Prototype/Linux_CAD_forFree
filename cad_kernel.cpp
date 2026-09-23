#include "cad_kernel.h"

#include "cad_curve_solver.h"

#include <BRepAdaptor_Curve.hxx>
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
#include <BRepPrimAPI_MakePrism.hxx>
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
    const gp_Pln plane(sketchAxes(sketch.plane));
    for (const SketchSegment &segment : sketch.segments) {
        const gp_Pnt first = sketchToWorld(segment.first, sketch.plane);
        const gp_Pnt second = sketchToWorld(segment.second, sketch.plane);
        if (first.Distance(second) <= Precision::Confusion()) continue;
        BRepBuilderAPI_MakeEdge edge(first, second);
        if (edge.IsDone()) edges->Append(edge.Edge());
    }
    for (const CurveObject &curve : sketch.curves) {
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
        const gp_Pln plane(sketchAxes(sketch.plane));
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
        BRepPrimAPI_MakePrism prism(profile, extrusionVector(sketch.plane, distance), Standard_True);
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

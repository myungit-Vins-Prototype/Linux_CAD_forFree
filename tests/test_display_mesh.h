// Incluso nella classe amica del viewport. Geometria esatta separata dalla
// precisione float della scena; nessun documento originale e nessun contesto GL.
static void displayMeshTests() {
    using namespace ForgeCad;
    using namespace ForgeCad::Kernel;
    const auto check = [](const Body &body, const BodyDisplay &mesh) {
        require(mesh.vertices.size() % 3 == 0 && mesh.normals.size() == mesh.vertices.size()
                && mesh.triangleFaces.size() * 3 == mesh.vertices.size(), "array display e mappa facce coerenti");
        const auto faces=body.faces();
        for (qsizetype i=0; i<mesh.vertices.size(); i+=3) {
            const auto &a=mesh.vertices[i], &b=mesh.vertices[i+1], &c=mesh.vertices[i+2];
            long double u[3],v[3];
            for(int j=0;j<3;++j) {
                require(std::isfinite(a[j]) && std::isfinite(b[j]) && std::isfinite(c[j]), "vertici display finiti");
                u[j]=static_cast<long double>(b[j])-a[j];v[j]=static_cast<long double>(c[j])-a[j];
            }
            require(u[1]*v[2]!=u[2]*v[1] || u[2]*v[0]!=u[0]*v[2] || u[0]*v[1]!=u[1]*v[0],
                    "nessun triangolo display collassato dopo conversione float");
            require(std::find(faces.begin(),faces.end(),FaceId(mesh.triangleFaces[i/3]))!=faces.end(),
                    "ID del triangolo appartiene ancora al B-rep");
        }
        require(mesh.faceIds.size()==mesh.faceLabelPoints.size(), "etichette e ID facce allineati");
        for(int face:mesh.faceIds) require(mesh.triangleFaces.contains(face), "etichetta soltanto su faccia con triangoli visibili");
        require(mesh.edgeIds.size()==mesh.edges.size(), "ID dei bordi conservati");
    };
    for(int quality:{0,1,2}) {
        // La larghezza resta esatta in double, ma a x=1e8 i due lati
        // coincidono in float: quattro facce collassano, due rimangono.
        const Body partial=makeBox(Frame3(Vec3(1e8,0,0),Vec3(0,0,1),Vec3(1,0,0)),1,4,4);
        const auto original=writeBodyBinary(partial);
        BodyDisplay mesh;forgeTessellate(partial,quality,mesh);check(partial,mesh);
        require(mesh.triangleFaces.size()==4 && mesh.faceIds.size()==2 && mesh.edges.size()==12,
                "rimosse solo le facce collassate, conservati i bordi esatti");
        BodyDisplay patch,retained;
        forgeExtrusionPreviewDisplay({},partial,quality,patch,3,&retained);check(partial,patch);
        require(patch.vertices==mesh.vertices && patch.normals==mesh.normals && patch.triangleFaces==mesh.triangleFaces
                && retained.vertices.isEmpty(), "anteprima fresca applica lo stesso filtro della scena");
        require(writeBodyBinary(partial)==original, "filtro della scena e anteprima non modifica la geometria esatta");

        const Body collapsed=makeBox(Frame3(Vec3(1e8,1e8,1e8),Vec3(0,0,1),Vec3(1,0,0)),1,1,1);
        forgeTessellate(collapsed,quality,mesh);check(collapsed,mesh);
        require(mesh.vertices.isEmpty() && mesh.faceIds.isEmpty() && !mesh.edges.isEmpty(),
                "facce completamente collassate senza triangoli o etichette spurie");
        const Body thin=makeBox(Frame3(),1e-5,4,4);
        forgeTessellate(thin,quality,mesh);check(thin,mesh);
        TessellationOptions options;
        options.deflection=std::sqrt(32.0+1e-10)*(quality==0 ? 4e-3 : quality==1 ? 1e-3 : 2e-4);
        options.angle=quality==0 ? 0.5 : quality==1 ? 0.25 : 0.1;
        const auto exactMesh=tessellate(thin,options);
        std::size_t exactTriangles=0;
        for(const auto &face:exactMesh.faces) exactTriangles+=face.triangles.size();
        require(exactTriangles>=12 && mesh.triangleFaces.size()==qsizetype(exactTriangles) && mesh.faceIds.size()==6,
                "tutti i triangoli sottili rappresentabili della tassellazione double sono conservati");
    }
    const Body base=makeBox(Frame3(),2,4,4), result=makeBox(Frame3(),3,4,4);
    BodyDisplay full,fresh,retained;
    forgeTessellate(result,1,full);
    forgeExtrusionPreviewDisplay({std::make_shared<const Body>(base)},result,1,fresh,3,&retained);
    check(result,fresh);check(result,retained);
    require(!fresh.vertices.isEmpty() && !retained.vertices.isEmpty()
            && fresh.vertices.size()+retained.vertices.size()==full.vertices.size(),
            "patch e facce conservate suddividono la superficie senza perdere triangoli");
    // Ogni triangolo valido della scena conserva la faccia selezionabile
    // anche dopo la compattazione dei buffer.
    for(qsizetype i=0;i<full.vertices.size();i+=3) {
        const QVector3D center=(full.vertices[i]+full.vertices[i+1]+full.vertices[i+2])/3;
        const QVector3D normal=full.normals[i];FaceHit hit;
        require(forgePickFace(result,center+normal*10,-normal,hit,full.rayIndex.get())
                && hit.face==full.triangleFaces[i/3], "picking esatto concorde con ID del triangolo visibile");
    }
    BodyDisplay old=full;
    const int patchFace=fresh.triangleFaces.first();
    const auto append=[&](const QVector3D &a,const QVector3D &b,const QVector3D &c) {
        old.vertices.append(a);old.vertices.append(b);old.vertices.append(c);
        for(int i=0;i<3;++i)old.normals.append(QVector3D(1,0,0));
        old.triangleFaces.append(patchFace);
    };
    append({3,0,0},{3,1,1},{3,2,2}); // collineari, tutti distinti
    append({3,0,0},{3,1.1234567f,2.7654321f},{3,1.1234567f,2.7654321f}); // cross(v,v), anche con FMA
    append({3,0,0},{3,1,0},{3,0,std::numeric_limits<float>::infinity()});
    append({3,0,0},{3,1e-25f,0},{3,0,1e-25f}); // valido, area non rappresentabile in float
    const auto snapshot=old.vertices;
    BodyDisplay reused;forgeBlendPreviewDisplay(base,result,1,reused,3,&old);check(result,reused);
    require(reused.vertices.size()==fresh.vertices.size()+3 && reused.triangleFaces.size()==fresh.triangleFaces.size()+1,
            "riuso cache precedente filtra degeneri e non finiti ma conserva triangolo piccolissimo");
    require(reused.vertices.mid(0,fresh.vertices.size())==fresh.vertices && reused.normals.mid(0,fresh.normals.size())==fresh.normals,
            "riuso preserva ordine e normali dei triangoli validi");
    require(old.vertices.constData()==snapshot.constData() && old.vertices.size()==full.vertices.size()+12,
            "filtro anteprima non modifica cache sorgente o snapshot");
    std::cout << "PASS display float, anteprime, cache precedenti e picking\n";
}

#include "cad_display_cache.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QSurfaceFormat>
#include <iostream>
#include <memory>
#include <stdexcept>

static void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
static BodyDisplay triangles(int count) {
    BodyDisplay display;
    for(int i=0;i<count;++i) {
        display.vertices.append(QVector3D(0,0,0));display.vertices.append(QVector3D(1,0,0));display.vertices.append(QVector3D(0,1,0));
        for(int k=0;k<3;++k)display.normals.append(QVector3D(0,0,1));
    }
    return display;
}
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);const bool gl=app.arguments().contains("--gl");
    QOpenGLContext context;QOffscreenSurface surface;
    if(gl) {
        context.setFormat(format);
        if(!context.create()){std::cout<<"SKIP contesto GL non disponibile\n";return 77;}
        surface.setFormat(context.format());surface.create();
        if(!context.makeCurrent(&surface)){std::cout<<"SKIP superficie GL non disponibile\n";return 77;}
    }
    try {
        // La superficie offscreen non garantisce un framebuffer predefinito
        // utilizzabile su macOS. Mantenerlo vivo fino al rilascio della cache.
        std::unique_ptr<QOpenGLFramebufferObject> framebuffer;
        if(gl) {
            framebuffer=std::make_unique<QOpenGLFramebufferObject>(64,64);
            require(framebuffer->isValid() && framebuffer->bind(),"framebuffer di prova disponibile");
            context.functions()->glViewport(0,0,64,64);
        }
        ForgeCad::DisplayCache cache;cache.beginFrame();
        BodyDisplay high=triangles(10000);high.instancedBase=std::make_shared<BodyDisplay>();
        std::weak_ptr<const BodyDisplay> old=high.instancedBase;
        cache.faces(high);cache.endFrame();high={};
        require(!old.expired(),"la cache mantiene vivo lo storage necessario al frame corrente");
        require(cache.statistics().entries==1,"una risorsa per la mesh corrente");
        BodyDisplay low=triangles(1);
        cache.beginFrame();cache.faces(low);cache.endFrame();
        require(old.expired(),"mesh precedente rilasciata alla fine del primo frame sostitutivo, senza ridisegni futuri");
        require(cache.statistics().entries==1,"la cache non somma vecchia e nuova qualita'");
        const auto storage=cache.statistics().sourceStorageBytes;
        for(int frame=0;frame<50;++frame) {
            cache.beginFrame();cache.faces(low);cache.faces(low);cache.endFrame();
            require(cache.statistics().entries==1 && cache.statistics().sourceStorageBytes==storage,
                    "mesh ancora usata condivisa e riutilizzata senza crescita a ogni passata/frame");
        }
        BodyDisplay alternate=low;alternate.normals[0]=QVector3D(0,1,0);
        cache.beginFrame();cache.faces(low);cache.faces(alternate);cache.endFrame();
        require(cache.statistics().entries==2,"risorse diverse usate nello stesso frame restano entrambe disponibili");
        cache.beginFrame();cache.faces(low);cache.endFrame();
        require(cache.statistics().entries==1,"risorsa non usata eliminata alla fine del frame");
        cache.beginFrame();cache.endFrame();
        require(cache.statistics().entries==0 && cache.statistics().sourceStorageBytes==0,
                "scena vuota non trattiene mesh o VBO dei frame precedenti");
        cache.clear();
        if(gl)require(context.functions()->glGetError()==GL_NO_ERROR,"ciclo reale VBO/VAO senza errori GL");
        std::cout<<"PASS memoria cache, rilascio al frame e riuso"<<(gl?" con OpenGL":" senza GPU")<<"\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<std::endl;return 1;}
    if(gl)context.doneCurrent();
    return 0;
}

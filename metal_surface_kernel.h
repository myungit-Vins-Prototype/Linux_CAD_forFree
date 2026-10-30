#pragma once

// Shader incorporato nel binario: valutazione B-spline/NURBS del solo display.
// Coppie hi/lo float conservano circa 48 bit senza richiedere float64 Metal.
inline constexpr const char *kMetalSurfaceKernel = R"MSL(
#include <metal_stdlib>
using namespace metal;
#pragma clang fp contract(off)
typedef float2 DS;
DS ds(float x) { return DS(x,0); }
DS add(DS a,DS b) {
    float s=a.x+b.x,v=s-a.x;
    float e=((a.x-(s-v))+(b.x-v))+a.y+b.y;
    float h=s+e; return DS(h,e-(h-s));
}
DS sub(DS a,DS b) { return add(a,-b); }
DS mul(DS a,DS b) {
    float p=a.x*b.x;
    float e=((fma(a.x,b.x,-p)+a.x*b.y)+a.y*b.x)+a.y*b.y;
    float h=p+e;return DS(h,e-(h-p));
}
DS dsDivide(DS a,DS b) {
    float q=a.x/b.x;
    DS r=sub(a,mul(b,ds(q)));
    DS z=add(ds(q),ds((r.x+r.y)/b.x));
    r=sub(a,mul(b,z));
    return add(z,ds((r.x+r.y)/b.x));
}
bool less(DS a,DS b) { return a.x<b.x || (a.x==b.x && a.y<b.y); }
bool zero(DS a) { return a.x==0 && a.y==0; }
struct Params { uint pu,pv,nu,nv,uOffset,vOffset,poleOffset,weightOffset,rational,count; };
int span(device const DS *knots,int p,int poles,DS t) {
    int n=poles-1;
    if(!less(t,knots[n+1])) { int s=n;while(s>p && !less(knots[s],knots[n+1]))--s;return s; }
    if(!less(knots[p],t)) { int s=p;while(s<n && !less(knots[p],knots[s+1]))++s;return s; }
    int lo=p,hi=n+2;
    while(lo<hi) { int mid=(lo+hi)/2;if(less(t,knots[mid]))hi=mid;else lo=mid+1; }
    return lo-1;
}
void basis(device const DS *knots,int s,DS u,int p,thread DS *n,thread DS *dn) {
    DS left[16],right[16],previous[16];n[0]=ds(1);
    for(int j=1;j<=p;++j) {
        left[j]=sub(u,knots[s+1-j]);right[j]=sub(knots[s+j],u);
        if(j==p)for(int r=0;r<p;++r)previous[r]=n[r];
        DS saved=ds(0);
        for(int r=0;r<j;++r) {
            DS t=dsDivide(n[r],add(right[r+1],left[j-r]));
            n[r]=add(saved,mul(right[r+1],t));saved=mul(left[j-r],t);
        }
        n[j]=saved;
    }
    for(int k=0;k<=p;++k) {
        DS d=ds(0);
        if(p>0) {
            if(k>=1) { DS h=sub(knots[s+k],knots[s-p+k]);if(!zero(h))d=add(d,dsDivide(previous[k-1],h)); }
            if(k<p) { DS h=sub(knots[s+k+1],knots[s-p+k+1]);if(!zero(h))d=sub(d,dsDivide(previous[k],h)); }
            d=mul(d,ds(float(p)));
        }
        dn[k]=d;
    }
}
kernel void surfaces(device const DS *data[[buffer(0)]],device const DS *uv[[buffer(1)]],
                     device DS *out[[buffer(2)]],constant Params &s[[buffer(3)]],uint index[[thread_position_in_grid]]) {
    if(index>=s.count)return;
    device const DS *uk=data+s.uOffset,*vk=data+s.vOffset;
    int su=span(uk,int(s.pu),int(s.nu),uv[2*index]);
    int sv=span(vk,int(s.pv),int(s.nv),uv[2*index+1]);
    DS nu[16],du[16],nv[16],dv[16];
    basis(uk,su,uv[2*index],int(s.pu),nu,du);basis(vk,sv,uv[2*index+1],int(s.pv),nv,dv);
    DS a[3]={ds(0),ds(0),ds(0)},au[3]={ds(0),ds(0),ds(0)},av[3]={ds(0),ds(0),ds(0)};
    DS w=ds(0),wu=ds(0),wv=ds(0);
    for(int i=0;i<=int(s.pu);++i)for(int j=0;j<=int(s.pv);++j) {
        uint pole=uint(su-int(s.pu)+i)*s.nv+uint(sv-int(s.pv)+j);
        DS weight=s.rational?data[s.weightOffset+pole]:ds(1);
        DS b=mul(mul(nu[i],nv[j]),weight),bu=mul(mul(du[i],nv[j]),weight),bv=mul(mul(nu[i],dv[j]),weight);
        for(int c=0;c<3;++c) { DS x=data[s.poleOffset+3*pole+uint(c)];a[c]=add(a[c],mul(b,x));au[c]=add(au[c],mul(bu,x));av[c]=add(av[c],mul(bv,x)); }
        w=add(w,b);wu=add(wu,bu);wv=add(wv,bv);
    }
    DS pu[3],pv[3];
    for(int c=0;c<3;++c) {
        DS p=s.rational?dsDivide(a[c],w):a[c];out[6*index+uint(c)]=p;
        pu[c]=s.rational?dsDivide(sub(au[c],mul(wu,p)),w):au[c];
        pv[c]=s.rational?dsDivide(sub(av[c],mul(wv,p)),w):av[c];
    }
    DS normal[3]={sub(mul(pu[1],pv[2]),mul(pu[2],pv[1])),sub(mul(pu[2],pv[0]),mul(pu[0],pv[2])),sub(mul(pu[0],pv[1]),mul(pu[1],pv[0]))};
    float3 x=float3(pu[0].x,pu[1].x,pu[2].x),y=float3(pv[0].x,pv[1].x,pv[2].x);
    float3 n=float3(normal[0].x,normal[1].x,normal[2].x);
    bool regular=length(n)>1e-11f*max(dot(x,x),dot(y,y));
    for(uint c=0;c<3;++c)out[6*index+3+c]=regular?normal[c]:ds(0);
}
)MSL";

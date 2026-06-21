#include "GpuSimulation.h"
#include "GL/gl3w.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace cb {

// Genome words (uints) per cell: 4 quantized weights packed per uint.
static constexpr int kGenomeWords = (kGenomeSize + 3) / 4;

// ---------------------------------------------------------------------------
// Simulation compute shader (body). One invocation per acting cell of the
// current 3x3 phase. The cell reads its 3x3 Moore neighborhood and the noise
// resource fields, runs its neural network, and performs one action. A small
// generated header (#version + topology #defines) is prepended in buildSimSrc.
// ---------------------------------------------------------------------------
static const char* kSimBody = R"GLSL(
layout(local_size_x = 64) in;

layout(std430, binding=0) buffer B0 { uint  kind[]; };
layout(std430, binding=1) buffer B1 { uint  dir[]; };
layout(std430, binding=2) buffer B2 { int   age[]; };
layout(std430, binding=3) buffer B3 { float energy[]; };
layout(std430, binding=4) buffer B4 { float mineral[]; };
layout(std430, binding=5) buffer B5 { uint  genome[]; };
layout(std430, binding=6) buffer B6 { uint  marker[]; };  // packed RGB "scent" tag
layout(std430, binding=7) buffer B7 { uint  hib[]; };     // 1 = hibernating (asleep)
layout(std430, binding=8) buffer B8 { float sig[]; };     // emitted pheromone field
layout(std430, binding=9) buffer B9 { float mem[]; };     // recurrent state, NR per cell
layout(std430, binding=10) buffer B10 { float hp[]; };    // health (combat damages this)

uniform int  uW, uH, uPhase, uActW, uActH;
uniform uint uTick, uSeed;
uniform float uPhoto, uMineralRate, uMetab, uHibMetab, uActionCost, uDivide,
              uGive, uAttack, uMaxHp, uRegen, uRegenCost,
              uMaxEnergy, uMaxMineral, uStartEnergy, uMutChance,
              uTime, uEnvScale, uEnvDrift, uDayNight;
uniform int  uKinDist, uMaxAge, uMutCount, uMutDelta, uMarkerDrift;

int idx(int x, int y) { return y * uW + x; }

// --- genome (quantized weights) -------------------------------------------
int  gbyte(int i, int k) { return int((genome[i*GW + (k>>2)] >> uint((k&3)*8)) & 0xFFu); }
void setByte(int i, int k, int v) {
    int w = i*GW + (k>>2); uint sh = uint((k&3)*8);
    genome[w] = (genome[w] & ~(0xFFu << sh)) | ((uint(v) & 0xFFu) << sh);
}
float wt(int i, int k) { int b = gbyte(i,k); if (b > 127) b -= 256; return float(b) * WSCALE; }

// kin = matching "scent". Each cell carries a 3-byte RGB marker that is heritable
// and drifts on mutating reproduction, independent of the behavior weights. Like
// an ant colony odor / MHC self-marker: recognition compares only the tag, so a
// lineage can in principle evolve a marker that mimics another's. Cheap: two
// loads and an L1 over three channels.
bool isRel(int s, int o) {
    uint ms = marker[s], mo = marker[o];
    int dr = abs(int(ms & 0xFFu)        - int(mo & 0xFFu));
    int dg = abs(int((ms >> 8)  & 0xFFu) - int((mo >> 8)  & 0xFFu));
    int db = abs(int((ms >> 16) & 0xFFu) - int((mo >> 16) & 0xFFu));
    return (dr + dg + db) <= uKinDist;
}

// --- per-cell rng ----------------------------------------------------------
uint rseed;
uint rnext(){ rseed^=rseed<<13; rseed^=rseed>>17; rseed^=rseed<<5; return rseed; }
int  rint(int m){ return m<=0?0:int(rnext()%uint(m)); }
float rfloat(){ return float(rnext()>>8)*(1.0/16777216.0); }

// --- neighbor addressing ---------------------------------------------------
int nbX(int x,uint f,int n){ n+=int(f); if(n>=8)n-=8; int xt=x;
    if(n==0||n==6||n==7){ xt--; if(xt<0)xt=uW-1; } else if(n>=2&&n<=4){ xt++; if(xt>=uW)xt=0; } return xt; }
int nbY(int y,uint f,int n){ n+=int(f); if(n>=8)n-=8; int yt=y;
    if(n<=2)yt--; else if(n>=4&&n<=6)yt++; return yt; }
int frontX(int x,uint f){ return nbX(x,f,0); }
int frontY(int y,uint f){ return nbY(y,f,0); }

// --- environment (animated fBm) -------------------------------------------
float hash21(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){ vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
    float a=hash21(i),b=hash21(i+vec2(1,0)),c=hash21(i+vec2(0,1)),d=hash21(i+vec2(1,1));
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y); }
float fbm(vec2 p){ float s=0.0,a=0.5; for(int o=0;o<4;o++){ s+=a*vnoise(p); p*=2.0; a*=0.5; } return s; }
float dayMul(){ return (uDayNight>0.0001) ? (0.7+0.3*sin(uTime*uDayNight)) : 1.0; }
float lightAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(uTime*uEnvDrift,0.0));
    float vgrad=1.0 - float(y)/float(uH)*0.85;   // steep: top lit, bottom dark
    return clamp(pv*vgrad*dayMul(),0.0,1.0); }
float mineralAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(13.7,7.3) - vec2(0.0,uTime*uEnvDrift));
    float vgrad=0.15 + float(y)/float(uH)*0.85;  // mirror: minerals rich at bottom
    return clamp(pv*vgrad,0.0,1.0); }

// --- actions (act in the facing direction) ---------------------------------
void moveCell(int i,int j){
    kind[j]=kind[i]; dir[j]=dir[i]; age[j]=age[i]; energy[j]=energy[i]; mineral[j]=mineral[i];
    for(int k=0;k<GW;k++) genome[j*GW+k]=genome[i*GW+k];
    marker[j]=marker[i]; hib[j]=hib[i];
    sig[j]=sig[i]; for(int k=0;k<NR;k++) mem[j*NR+k]=mem[i*NR+k];
    hp[j]=hp[i];
    kind[i]=0u;
}
void doMove(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt); if(kind[j]==0u) moveCell(i,j); }

void rotate(int i,float turn){ int d=int(dir[i]); d += (turn>0.0)?1:7; dir[i]=uint(d%8); }

void photo(int i,int x,int y){ energy[i]+= uPhoto*lightAt(x,y); }
void mineralG(int i,int x,int y){ energy[i]+= uMineralRate*mineralAt(x,y); }

// Scavenging only: Eat consumes a *corpse* (organic) in front. It no longer kills
// living cells - that role belongs to Attack. This stops kin-clusters from eating
// their own (cannibalism quietly broke cooperation) and creates a real food chain:
// photosynthesiser -> Attack kills it -> corpse -> Eat scavenges it.
void doEat(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt);
    if(kind[j]!=2u) return;                      // only organic corpses
    energy[i]+=energy[j]; mineral[i]+=mineral[j]; kind[j]=0u; }

// Targeted altruism: feed only a *kin* cell in front, and only one that is
// needier than us. Cheap (uGive ~10%) so a clone-cluster can shuttle energy from
// the lit cells to the shaded ones without the giver being out-competed - the
// condition that lets cooperation survive selection (Hamilton: r*b > c).
void doGive(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt);
    if(kind[j]!=1u) return;
    if(!isRel(i,j)) return;              // only feed kin
    if(energy[j]>=energy[i]) return;     // only feed the needier
    float h=energy[i]*uGive; energy[i]-=h; energy[j]+=h; if(energy[j]>uMaxEnergy) energy[j]=uMaxEnergy; }

int findEmpty(int x,int y,uint f){ for(int n=0;n<8;n++){ int yt=nbY(y,f,n); if(yt<0||yt>=uH) continue;
    int xt=nbX(x,f,n); if(kind[idx(xt,yt)]==0u) return n; } return 8; }

void mutateChild(int j){
    for(int m=0;m<uMutCount;m++){ int k=rint(GN); int b=gbyte(j,k); if(b>127)b-=256;
        int nb=clamp(b + rint(2*uMutDelta+1)-uMutDelta, -128, 127); setByte(j,k,nb); }
    // drift the scent marker independently of the behavior weights
    uint mk=marker[j];
    int r=int(mk&0xFFu), g=int((mk>>8)&0xFFu), bb=int((mk>>16)&0xFFu);
    r =clamp(r +rint(2*uMarkerDrift+1)-uMarkerDrift, 0, 255);
    g =clamp(g +rint(2*uMarkerDrift+1)-uMarkerDrift, 0, 255);
    bb=clamp(bb+rint(2*uMarkerDrift+1)-uMarkerDrift, 0, 255);
    marker[j]=uint(r)|(uint(g)<<8)|(uint(bb)<<16); }

void doDouble(int i,int x,int y){ if(energy[i]<uDivide) return;
    int n=findEmpty(x,y,dir[i]); if(n==8) return;
    energy[i]-=uDivide;
    int xt=nbX(x,dir[i],n),yt=nbY(y,dir[i],n); int j=idx(xt,yt);
    kind[j]=1u; dir[j]=dir[i]; age[j]=0; marker[j]=marker[i]; hib[j]=0u;
    for(int k=0;k<GW;k++) genome[j*GW+k]=genome[i*GW+k];
    sig[j]=0.0; for(int k=0;k<NR;k++) mem[j*NR+k]=0.0;   // child starts with a blank mind-state
    hp[j]=uMaxHp;                                         // ...and full health
    energy[i]*=0.5; energy[j]=energy[i]; mineral[i]*=0.5; mineral[j]=mineral[i];
    if(rfloat()<uMutChance) mutateChild(j); }

// Predation with group defense. Damages the HP of any living cell in front -
// including kin: the net is *not* stopped from attacking its own, it must learn
// (via kin selection) that killing your relatives is a losing strategy. The blow
// is blunted by the victim's own kin wall: every kin neighbour the target has
// shields it (~12% each), so a lone cell is easy prey while a packed colony is
// nearly immune. A kill leaves a corpse holding the victim's full energy +
// minerals (HP, not energy, is what the strike spends) - so energy is preserved
// in the corpse and a scavenger recovers it with Eat.
void doAttack(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt); if(kind[j]!=1u) return;
    int support=0;
    for(int n=0;n<8;n++){ int yy=nbY(yt,0u,n); if(yy<0||yy>=uH) continue; int xx=nbX(xt,0u,n);
        if(kind[idx(xx,yy)]==1u && isRel(j,idx(xx,yy))) support++; }
    float dmg=uAttack*(1.0 - 0.12*float(support)); if(dmg<0.0) dmg=0.0;
    hp[j]-=dmg;
    if(hp[j]<=0.0) kind[j]=2u; }                  // killed -> corpse keeps full energy + minerals

void main(){
    uint t = gl_GlobalInvocationID.x;
    if(t >= uint(uActW*uActH)) return;
    int px=uPhase%3, py=uPhase/3;
    int cx = px + int(t % uint(uActW))*3;
    int cy = py + int(t / uint(uActW))*3;
    if(cx>=uW || cy>=uH) return;
    int i = idx(cx,cy);
    if(kind[i]!=1u) return;             // empty or organic: skip

    uint h = uSeed ^ (uint(i)*2654435761u) ^ (uTick*2246822519u);
    h += 0x9e3779b9u; h=(h^(h>>16))*0x85ebca6bu; h=(h^(h>>13))*0xc2b2ae35u; h^=h>>16;
    rseed = (h==0u)?1u:h;

    // --- senses ---
    uint f = dir[i];
    int fx=frontX(cx,f), fy=frontY(cy,f);
    bool frontOn = (fy>=0 && fy<uH);
    int facedEmpty=0,facedKin=0,facedNon=0,facedOrg=0; float fe=0.0,fsig=0.0;
    if(frontOn){ int j=idx(fx,fy); uint kj=kind[j];
        if(kj==0u) facedEmpty=1; else if(kj==2u) facedOrg=1;
        else { fe=energy[j]/uMaxEnergy; fsig=sig[j]; if(isRel(i,j)) facedKin=1; else facedNon=1; } }
    else facedEmpty=1;
    // resources in the faced cell vs here give a directional gradient; sampling the
    // left/right neighbours too lets the net climb the drifting fields without first
    // having to spin in place to scan.
    float frontLight = frontOn ? lightAt(fx,fy)   : 0.0;
    float frontMin   = frontOn ? mineralAt(fx,fy) : 0.0;
    int lx=nbX(cx,f,6), ly=nbY(cy,f,6);     // n=6: 90 deg left of facing
    int rx=nbX(cx,f,2), ry=nbY(cy,f,2);     // n=2: 90 deg right of facing
    float leftLight  = (ly>=0&&ly<uH) ? lightAt(lx,ly) : 0.0;
    float rightLight = (ry>=0&&ry<uH) ? lightAt(rx,ry) : 0.0;
    // neighbourhood census + average pheromone of surrounding kin (the channel a
    // colony coordinates over: "hungry here", "danger here", ...).
    int kin=0,nonkin=0,org=0; float kinSig=0.0;
    for(int n=0;n<8;n++){ int yt=nbY(cy,f,n); if(yt<0||yt>=uH) continue; int xt=nbX(cx,f,n);
        int j=idx(xt,yt); uint kj=kind[j];
        if(kj==2u) org++; else if(kj==1u){ if(isRel(i,j)){ kin++; kinSig+=sig[j]; } else nonkin++; } }
    float kinSigAvg = (kin>0) ? kinSig/float(kin) : 0.0;

    float x[NI];
    x[0]=energy[i]/uMaxEnergy;
    x[1]=mineral[i]/uMaxMineral;
    x[2]=lightAt(cx,cy);
    x[3]=mineralAt(cx,cy);
    x[4]=frontLight;         // env ahead (vs x[2]/x[3] here -> drift direction)
    x[5]=frontMin;
    x[6]=leftLight;
    x[7]=rightLight;
    x[8]=float(facedEmpty);
    x[9]=float(facedKin);
    x[10]=float(facedNon);
    x[11]=float(facedOrg);
    x[12]=fe;                // energy of the cell in front (eat/give/attack target)
    x[13]=float(kin)/8.0;
    x[14]=float(nonkin)/8.0;
    x[15]=float(org)/8.0;
    x[16]=fsig;              // pheromone of the cell in front
    x[17]=kinSigAvg;         // mean pheromone of surrounding kin
    x[18]=rfloat()*2.0-1.0;  // noise (stochastic policy / symmetry breaking)
    for(int r=0;r<NR;r++) x[19+r]=mem[i*NR+r];   // recurrent memory from last tick
    x[23]=hp[i]/uMaxHp;      // own health (so the net can flee / play safe when wounded)

    // --- forward: h = tanh(W1.x + b1); o = W2.h + b2 ---
    float o[NO];
    for(int oi=0; oi<NO; oi++) o[oi] = wt(i, B2OFF+oi);
    for(int hh=0; hh<NH; hh++){
        float s = wt(i, B1OFF+hh);
        for(int ii=0; ii<NI; ii++) s += x[ii]*wt(i, ii*NH+hh);
        float hv = tanh(s);
        for(int oi=0; oi<NO; oi++) o[oi] += hv*wt(i, W2OFF + hh*NO + oi);
    }

    int act=0; float best=o[0];
    for(int a=1;a<NA;a++){ if(o[a]>best){ best=o[a]; act=a; } }

    // Commit recurrent state and the emitted pheromone every tick (squashed to
    // bounded ranges so they can't blow up). Done before any action so a cell that
    // moves/divides this tick carries its freshly-updated mind-state along.
    for(int r=0;r<NR;r++) mem[i*NR+r]=tanh(o[ORECUR+r]);
    sig[i]=0.5+0.5*tanh(o[OSIG]);

    // Hibernation (anabiosis). An asleep cell runs the net only to decide whether
    // to keep sleeping: while hibernating the single available choice is to wake.
    // It pays a tiny metabolism and does not age, so it can wait out famine/dark.
    if(hib[i]==1u){
        energy[i]-=uHibMetab;
        if(act!=9) hib[i]=0u;           // chose something other than "sleep" -> wake (acts next tick)
        if(energy[i]<=0.0) kind[i]=0u;
        return;                         // no action, no aging while asleep
    }
    if(act==9){                         // awake cell decides to hibernate
        hib[i]=1u; energy[i]-=uHibMetab;
        if(energy[i]<=0.0) kind[i]=0u;
        return;
    }

    // active actions (move/eat/give/divide/attack) cost energy; photo/mineral/
    // rotate/idle only pay the per-tick metabolism below.
    if(act==1||act==5||act==6||act==7||act==8) energy[i]-=uActionCost;

    if(act==1) doMove(i,cx,cy);
    else if(act==2) rotate(i,o[OTURN]);
    else if(act==3) photo(i,cx,cy);
    else if(act==4) mineralG(i,cx,cy);
    else if(act==5) doEat(i,cx,cy);
    else if(act==6) doGive(i,cx,cy);
    else if(act==7) doDouble(i,cx,cy);
    else if(act==8) doAttack(i,cx,cy);

    if(kind[i]!=1u) return;             // may have moved away
    if(energy[i]>uMaxEnergy) energy[i]=uMaxEnergy;
    // slow self-repair: spend energy to regrow lost HP (wounds heal if you survive
    // and stay fed), so hit-and-run and attrition both become real strategies.
    if(hp[i]<uMaxHp && energy[i]>0.0){
        float heal=min(uRegen, uMaxHp-hp[i]); float cost=heal*uRegenCost;
        if(cost>energy[i]){ heal=energy[i]/uRegenCost; cost=energy[i]; }
        hp[i]+=heal; energy[i]-=cost;
    }
    energy[i]-=uMetab; age[i]+=1;
    if(energy[i]<=0.0){ kind[i]=0u; return; }
    if(age[i]>uMaxAge) kind[i]=2u;      // die of old age -> organic
}
)GLSL";

// Colorize pass: state -> RGBA8, with an atomic alive counter. The #version and
// topology #defines are prepended at compile time (shared with the sim shader).
static const char* kColorBody = R"GLSL(
layout(local_size_x=8, local_size_y=8) in;
layout(rgba8, binding=0) uniform writeonly image2D uOut;
layout(std430, binding=0) buffer B0 { uint  kind[]; };
layout(std430, binding=2) buffer B2 { int   age[]; };
layout(std430, binding=3) buffer B3 { float energy[]; };
layout(std430, binding=6) buffer B6 { uint  marker[]; };  // packed RGB "scent" tag
layout(std430, binding=7) buffer B7 { uint  hib[]; };     // 1 = hibernating
layout(std430, binding=8) buffer B8 { float sig[]; };     // emitted pheromone field
layout(std430, binding=11) buffer BC { uint  aliveCount[]; };

uniform int uW, uH, uMode, uMaxAge;
uniform float uTime, uEnvScale, uEnvDrift, uDayNight;

vec3 rgb(uint c){ return vec3(float(c&0xFFu),float((c>>8)&0xFFu),float((c>>16)&0xFFu))/255.0; }
float hash21(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){ vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
    float a=hash21(i),b=hash21(i+vec2(1,0)),c=hash21(i+vec2(0,1)),d=hash21(i+vec2(1,1));
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y); }
float fbm(vec2 p){ float s=0.0,a=0.5; for(int o=0;o<4;o++){ s+=a*vnoise(p); p*=2.0; a*=0.5; } return s; }
float dayMul(){ return (uDayNight>0.0001) ? (0.7+0.3*sin(uTime*uDayNight)) : 1.0; }
float lightAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(uTime*uEnvDrift,0.0));
    return clamp(pv*(1.0-float(y)/float(uH)*0.85)*dayMul(),0.0,1.0); }
float mineralAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(13.7,7.3) - vec2(0.0,uTime*uEnvDrift));
    return clamp(pv*(0.15+float(y)/float(uH)*0.85),0.0,1.0); }

// Clan color = the cell's scent marker, the same tag isRel() recognizes, so what
// you see on screen is exactly what the cells treat as kin. Lifted toward pastel
// so colonies read clearly against the dark background.
vec3 markerColor(int i){ vec3 c=rgb(marker[i]);
    c = mix(c, vec3(0.85), 0.18);
    return c; }
// Environment shown under cells. Brightness = how good the spot is (light +
// minerals). Kept dim so living cells stand out on top.
vec3 envColor(int x,int y){ float L=lightAt(x,y), M=mineralAt(x,y);
    return vec3(L*0.9, L*0.7+M*0.25, M*0.9); }

void main(){
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if(p.x>=uW||p.y>=uH) return;
    int i = p.y*uW + p.x;
    uint k = kind[i];
    if(k==1u) atomicAdd(aliveCount[0], 1u);

    vec3 env = envColor(p.x, p.y);
    vec3 c;
    if(uMode==3){                                    // Environment: full heatmap
        c = env;
        if(k==1u)      c = mix(c, markerColor(i), 0.75);
        else if(k==2u) c = mix(c, vec3(0.2), 0.6);
    } else {
        c = env * 0.22 + vec3(0.04);                 // dim env background
        if(k==2u)      c = mix(c, vec3(0.32), 0.85); // organic / dead
        else if(k==1u){
            if(uMode==0)      c = markerColor(i);    // Family / clan (scent marker)
            else if(uMode==1){ float e=energy[i]; c=vec3(min(e/1000.0,1.0), min(e/2000.0,1.0)*0.647, 0.0); }
            else if(uMode==2){ float a=clamp(float(age[i])/float(max(uMaxAge,1)),0.0,1.0); c=vec3(a,0.0,a); }
            else if(uMode==4){                        // Signal: pheromone heatmap
                float s=clamp(sig[i],0.0,1.0);
                c = mix(vec3(0.02,0.06,0.16), vec3(0.2,1.0,0.85), s) + vec3(s*s*0.6,0.0,0.0);
            }
        }
    }
    if(k==1u && hib[i]==1u) c = mix(c, vec3(0.10,0.16,0.34), 0.6);  // asleep: cold/dim
    imageStore(uOut, p, vec4(c,1.0));
}
)GLSL";

// ---------------------------------------------------------------------------
namespace {
// #version + topology #defines shared by both compute shaders.
std::string shaderHeader() {
    char hdr[1024];
    std::snprintf(hdr, sizeof(hdr),
        "#version 430\n"
        "#define NI %d\n#define NH %d\n#define NO %d\n"
        "#define NA %d\n#define NR %d\n"
        "#define OTURN %d\n#define ORECUR %d\n#define OSIG %d\n"
        "#define GN %d\n#define GW %d\n#define WSCALE %ff\n"
        "#define B1OFF %d\n#define W2OFF %d\n#define B2OFF %d\n",
        kNNInputs, kNNHidden, kNNOutputs,
        kActCount, kNNRecur,
        kOutTurn, kOutRecur, kOutSignal,
        kGenomeSize, kGenomeWords, kWeightScale,
        kNNInputs*kNNHidden,
        kNNInputs*kNNHidden + kNNHidden,
        kNNInputs*kNNHidden + kNNHidden + kNNHidden*kNNOutputs);
    return std::string(hdr);
}
std::string buildSimSrc()   { return shaderHeader() + kSimBody; }
std::string buildColorSrc() { return shaderHeader() + kColorBody; }

unsigned compileCompute(const char* src) {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        fprintf(stderr, "compute compile error:\n%s\n", log);
        glDeleteShader(sh); return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(sh);
    if (!ok) {
        char log[4096]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "compute link error:\n%s\n", log);
        glDeleteProgram(prog); return 0;
    }
    return prog;
}
} // namespace

GpuSimulation::~GpuSimulation() {
    if (simProg_)   glDeleteProgram(simProg_);
    if (colorProg_) glDeleteProgram(colorProg_);
    if (tex_)       glDeleteTextures(1, &tex_);
    glDeleteBuffers(2, counter_);
    glDeleteBuffers(kNumBuffers, buf_);
}

void GpuSimulation::cacheUniformLocations() {
    auto S = [&](const char* nm){ return glGetUniformLocation(simProg_, nm); };
    sl_ = SimLoc{
        S("uW"), S("uH"), S("uPhase"), S("uActW"), S("uActH"), S("uTick"), S("uSeed"), S("uTime"),
        S("uPhoto"), S("uMineralRate"), S("uMetab"), S("uHibMetab"), S("uActionCost"), S("uDivide"),
        S("uGive"), S("uAttack"),
        S("uMaxHp"), S("uRegen"), S("uRegenCost"),
        S("uMaxEnergy"), S("uMaxMineral"), S("uStartEnergy"), S("uMutChance"),
        S("uEnvScale"), S("uEnvDrift"), S("uDayNight"),
        S("uKinDist"), S("uMaxAge"), S("uMutCount"), S("uMutDelta"), S("uMarkerDrift"),
    };
    auto C = [&](const char* nm){ return glGetUniformLocation(colorProg_, nm); };
    cl_ = ColLoc{ C("uW"), C("uH"), C("uMode"), C("uMaxAge"),
                  C("uTime"), C("uEnvScale"), C("uEnvDrift"), C("uDayNight") };
}

bool GpuSimulation::init(const WorldState& seed, const Config& cfg) {
    cfg_ = cfg;
    std::string simSrc = buildSimSrc();
    std::string colorSrc = buildColorSrc();
    simProg_   = compileCompute(simSrc.c_str());
    colorProg_ = compileCompute(colorSrc.c_str());
    if (!simProg_ || !colorProg_) return false;
    cacheUniformLocations();

    width_ = seed.width; height_ = seed.height;

    glGenBuffers(kNumBuffers, buf_);
    glGenBuffers(2, counter_);
    for (int b = 0; b < 2; ++b) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, counter_[b]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);
    }

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width_, height_);

    upload(seed);
    ok_ = true;
    return true;
}

void GpuSimulation::upload(const WorldState& w) {
    width_ = w.width; height_ = w.height;
    const int n = w.size();

    auto fill = [&](int slot, const void* data, size_t bytes) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[slot]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, GL_DYNAMIC_DRAW);
    };

    std::vector<uint32_t> u(n);
    auto u8to32 = [&](const std::vector<uint8_t>& s) { for (int i=0;i<n;++i) u[i]=s[i]; return u.data(); };

    fill(0, u8to32(w.kind),      n * sizeof(uint32_t));
    fill(1, u8to32(w.direction), n * sizeof(uint32_t));
    fill(2, w.age.data(),        n * sizeof(int32_t));
    fill(3, w.energy.data(),     n * sizeof(float));
    fill(4, w.mineral.data(),    n * sizeof(float));

    std::vector<uint32_t> packed((size_t)n * kGenomeWords, 0);
    for (int c = 0; c < n; ++c) {
        const uint8_t* m = w.mindAt(c);
        for (int k = 0; k < kGenomeSize; ++k)
            packed[(size_t)c * kGenomeWords + (k >> 2)] |= (uint32_t)m[k] << ((k & 3) * 8);
    }
    fill(5, packed.data(), packed.size() * sizeof(uint32_t));
    fill(6, w.marker.data(), n * sizeof(uint32_t));
    fill(7, w.hibernating.data(), n * sizeof(uint32_t));
    fill(8, w.signal.data(), n * sizeof(float));
    fill(9, w.mem.data(), (size_t)n * kNNRecur * sizeof(float));
    fill(10, w.hp.data(), n * sizeof(float));
}

void GpuSimulation::bindBuffers() const {
    for (int s = 0; s < kNumBuffers; ++s)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s, buf_[s]);
}

void GpuSimulation::setSimUniforms() {
    glUniform1i(sl_.W, width_); glUniform1i(sl_.H, height_);
    glUniform1f(sl_.photo, cfg_.photoEnergy); glUniform1f(sl_.mineralRate, cfg_.mineralRate);
    glUniform1f(sl_.metab, cfg_.metabolism); glUniform1f(sl_.hibMetab, cfg_.hibernationMetab);
    glUniform1f(sl_.actionCost, cfg_.actionCost);
    glUniform1f(sl_.divide, cfg_.divideCost);
    glUniform1f(sl_.give, cfg_.giveFraction); glUniform1f(sl_.attack, cfg_.attackDamage);
    glUniform1f(sl_.maxHp, cfg_.maxHp); glUniform1f(sl_.regen, cfg_.regenRate);
    glUniform1f(sl_.regenCost, cfg_.regenCost);
    glUniform1f(sl_.maxEnergy, cfg_.maxEnergy); glUniform1f(sl_.maxMineral, cfg_.maxMineral);
    glUniform1f(sl_.startEnergy, cfg_.startEnergy); glUniform1f(sl_.mutChance, cfg_.mutationChance);
    glUniform1f(sl_.envScale, cfg_.envScale); glUniform1f(sl_.envDrift, cfg_.envDrift);
    glUniform1f(sl_.dayNight, cfg_.dayNightSpeed);
    glUniform1i(sl_.kinDist, cfg_.kinMarkerDist); glUniform1i(sl_.maxAge, cfg_.maxAge);
    glUniform1i(sl_.mutCount, cfg_.mutationCount); glUniform1i(sl_.mutDelta, cfg_.mutationDelta);
    glUniform1i(sl_.markerDrift, cfg_.markerDrift);
}

void GpuSimulation::step(int ticks) {
    if (!ok_) return;
    glUseProgram(simProg_);
    bindBuffers();
    setSimUniforms();
    glUniform1ui(sl_.seed, (uint32_t)seed_);

    for (int t = 0; t < ticks; ++t) {
        ++tick_;
        glUniform1ui(sl_.tick, (uint32_t)tick_);
        glUniform1f(sl_.time, (float)tick_);
        for (int phase = 0; phase < 9; ++phase) {
            int px = phase % 3, py = phase / 3;
            int actW = (width_  - px + 2) / 3;
            int actH = (height_ - py + 2) / 3;
            glUniform1i(sl_.phase, phase);
            glUniform1i(sl_.actW, actW);
            glUniform1i(sl_.actH, actH);
            GLuint groups = (GLuint)((actW * actH + 63) / 64);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }
}

int GpuSimulation::colorize(DisplayMode mode, int maxAge) {
    if (!ok_) return 0;
    const int cur  = curCounter_;
    const int prev = cur ^ 1;

    // Read last frame's count first: its GPU work finished a frame ago, so this
    // does not stall the pipeline (a synchronous read of the current frame's
    // counter would). The displayed alive count lags by one frame, which is
    // imperceptible.
    if (counterPrimed_) {
        uint32_t alive = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, counter_[prev]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(alive), &alive);
        lastAlive_ = (int)alive;
    }

    uint32_t zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counter_[cur]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zero), &zero);

    glUseProgram(colorProg_);
    bindBuffers();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, counter_[cur]);
    glBindImageTexture(0, tex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glUniform1i(cl_.W, width_);
    glUniform1i(cl_.H, height_);
    glUniform1i(cl_.mode, (int)mode);
    glUniform1i(cl_.maxAge, maxAge);
    glUniform1f(cl_.time, (float)tick_);
    glUniform1f(cl_.envScale, cfg_.envScale);
    glUniform1f(cl_.envDrift, cfg_.envDrift);
    glUniform1f(cl_.dayNight, cfg_.dayNightSpeed);

    glDispatchCompute((width_ + 7) / 8, (height_ + 7) / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    curCounter_ = prev;
    counterPrimed_ = true;
    return lastAlive_;
}

void GpuSimulation::download(WorldState& w) const {
    if (!ok_) return;
    w.resize(width_, height_);
    const int n = w.size();
    std::vector<uint32_t> u(n);
    auto get32 = [&](int slot) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[slot]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n * sizeof(uint32_t), u.data());
    };
    auto to8 = [&](std::vector<uint8_t>& d) { for (int i = 0; i < n; ++i) d[i] = (uint8_t)u[i]; };

    get32(0); to8(w.kind);
    get32(1); to8(w.direction);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[2]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n*sizeof(int32_t), w.age.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[3]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n*sizeof(float), w.energy.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[4]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n*sizeof(float), w.mineral.data());

    std::vector<uint32_t> packed((size_t)n * kGenomeWords);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[5]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, packed.size()*sizeof(uint32_t), packed.data());
    for (int c = 0; c < n; ++c) {
        uint8_t* m = w.mindAt(c);
        for (int k = 0; k < kGenomeSize; ++k)
            m[k] = (uint8_t)((packed[(size_t)c*kGenomeWords + (k>>2)] >> ((k&3)*8)) & 0xFF);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[6]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n * sizeof(uint32_t), w.marker.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[7]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n * sizeof(uint32_t), w.hibernating.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[8]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n * sizeof(float), w.signal.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[9]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (size_t)n * kNNRecur * sizeof(float), w.mem.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[10]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n * sizeof(float), w.hp.data());
}

} // namespace cb

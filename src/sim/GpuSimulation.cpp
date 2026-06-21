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
layout(std430, binding=6) buffer B6 { uint  fam[]; };

uniform int  uW, uH, uPhase, uActW, uActH;
uniform uint uTick, uSeed;
uniform float uPhoto, uMineralRate, uLiveCost, uEatCost, uDoubleCost, uGeneAtt,
              uMaxEnergy, uMaxMineral, uStartEnergy, uMutChance,
              uTime, uEnvScale, uEnvDrift, uDayNight;
uniform int  uKinDist, uMaxAge, uMutCount, uMutDelta;

int idx(int x, int y) { return y * uW + x; }

// --- genome (quantized weights) -------------------------------------------
int  gbyte(int i, int k) { return int((genome[i*GW + (k>>2)] >> uint((k&3)*8)) & 0xFFu); }
void setByte(int i, int k, int v) {
    int w = i*GW + (k>>2); uint sh = uint((k&3)*8);
    genome[w] = (genome[w] & ~(0xFFu << sh)) | ((uint(v) & 0xFFu) << sh);
}
float wt(int i, int k) { int b = gbyte(i,k); if (b > 127) b -= 256; return float(b) * WSCALE; }

void unpackFam(uint c, out int r, out int g, out int b) { r=int(c&0xFFu); g=int((c>>8)&0xFFu); b=int((c>>16)&0xFFu); }
uint packFam(int r, int g, int b) { return uint(clamp(r,0,255)) | (uint(clamp(g,0,255))<<8) | (uint(clamp(b,0,255))<<16); }

// kin = similar family color (cheap clan tag that drifts on mutation)
bool isRel(int s, int o) {
    int r1,g1,b1,r2,g2,b2; unpackFam(fam[s],r1,g1,b1); unpackFam(fam[o],r2,g2,b2);
    return (abs(r1-r2)+abs(g1-g2)+abs(b1-b2)) <= uKinDist;
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
float dayMul(){ return (uDayNight>0.0001) ? (0.5+0.5*sin(uTime*uDayNight)) : 1.0; }
float lightAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(uTime*uEnvDrift,0.0));
    float vgrad=1.0 - float(y)/float(uH)*0.5;
    return clamp(pv*vgrad*dayMul(),0.0,1.0); }
float mineralAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(13.7,7.3) - vec2(0.0,uTime*uEnvDrift));
    float vgrad=0.5 + float(y)/float(uH)*0.5;
    return clamp(pv*vgrad,0.0,1.0); }

// --- actions (act in the facing direction) ---------------------------------
void moveCell(int i,int j){
    kind[j]=kind[i]; dir[j]=dir[i]; age[j]=age[i]; energy[j]=energy[i]; mineral[j]=mineral[i]; fam[j]=fam[i];
    for(int k=0;k<GW;k++) genome[j*GW+k]=genome[i*GW+k];
    kind[i]=0u;
}
void doMove(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt); if(kind[j]==0u) moveCell(i,j); }

void rotate(int i,float turn){ int d=int(dir[i]); d += (turn>0.0)?1:7; dir[i]=uint(d%8); }

void photo(int i,int x,int y){ energy[i]+= uPhoto*lightAt(x,y); }
void mineralG(int i,int x,int y){ energy[i]+= uMineralRate*mineralAt(x,y); }

void doEat(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    energy[i]-=uEatCost;
    if(yt<0||yt>=uH) return; int j=idx(xt,yt);
    if(kind[j]==0u) return;
    if(kind[j]==2u){ energy[i]+=energy[j]; kind[j]=0u; return; }
    energy[i]+=energy[j]; mineral[i]+=mineral[j]; kind[j]=0u; }

void doGive(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt);
    if(kind[j]!=1u) return; if(!isRel(i,j)) return;
    float h=energy[i]*0.25; energy[i]-=h; energy[j]+=h; if(energy[j]>uMaxEnergy) energy[j]=uMaxEnergy; }

int findEmpty(int x,int y,uint f){ for(int n=0;n<8;n++){ int yt=nbY(y,f,n); if(yt<0||yt>=uH) continue;
    int xt=nbX(x,f,n); if(kind[idx(xt,yt)]==0u) return n; } return 8; }

void mutateChild(int j){
    for(int m=0;m<uMutCount;m++){ int k=rint(GN); int b=gbyte(j,k); if(b>127)b-=256;
        b += rint(2*uMutDelta+1)-uMutDelta; b=clamp(b,-128,127); setByte(j,k,b); }
    int r,g,bb; unpackFam(fam[j],r,g,bb); r+=rint(13)-6; g+=rint(13)-6; bb+=rint(13)-6; fam[j]=packFam(r,g,bb); }

void doDouble(int i,int x,int y){ if(energy[i]<uDoubleCost) return;
    int n=findEmpty(x,y,dir[i]); if(n==8) return;
    energy[i]-=uDoubleCost;
    int xt=nbX(x,dir[i],n),yt=nbY(y,dir[i],n); int j=idx(xt,yt);
    kind[j]=1u; dir[j]=dir[i]; age[j]=0; fam[j]=fam[i];
    for(int k=0;k<GW;k++) genome[j*GW+k]=genome[i*GW+k];
    energy[i]*=0.5; energy[j]=energy[i]; mineral[i]*=0.5; mineral[j]=mineral[i];
    if(rfloat()<uMutChance) mutateChild(j); }

void doAttack(int i,int x,int y){ uint f=dir[i]; int xt=frontX(x,f),yt=frontY(y,f);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt); if(kind[j]!=1u) return;
    energy[i]-=uGeneAtt; int k=rint(GN); setByte(j,k,gbyte(i,k)); }

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
    int facedEmpty=0,facedKin=0,facedNon=0,facedOrg=0; float fe=0.0;
    if(fy>=0 && fy<uH){ int j=idx(fx,fy); uint kj=kind[j];
        if(kj==0u) facedEmpty=1; else if(kj==2u) facedOrg=1;
        else { fe=energy[j]/uMaxEnergy; if(isRel(i,j)) facedKin=1; else facedNon=1; } }
    else facedEmpty=1;
    int kin=0,nonkin=0,org=0;
    for(int n=0;n<8;n++){ int yt=nbY(cy,f,n); if(yt<0||yt>=uH) continue; int xt=nbX(cx,f,n);
        uint kj=kind[idx(xt,yt)];
        if(kj==2u) org++; else if(kj==1u){ if(isRel(i,idx(xt,yt))) kin++; else nonkin++; } }

    float x[NI];
    x[0]=1.0;
    x[1]=energy[i]/uMaxEnergy;
    x[2]=mineral[i]/uMaxMineral;
    x[3]=float(age[i])/float(uMaxAge);
    x[4]=lightAt(cx,cy);
    x[5]=mineralAt(cx,cy);
    x[6]=float(facedEmpty);
    x[7]=float(facedKin);
    x[8]=float(facedNon);
    x[9]=float(facedOrg);
    x[10]=fe;
    x[11]=float(kin)/8.0;
    x[12]=float(nonkin)/8.0;
    x[13]=float(org)/8.0;
    x[14]=rfloat()*2.0-1.0;
    x[15]=0.0;

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
    for(int a=1;a<9;a++){ if(o[a]>best){ best=o[a]; act=a; } }

    if(act==1) doMove(i,cx,cy);
    else if(act==2) rotate(i,o[9]);
    else if(act==3) photo(i,cx,cy);
    else if(act==4) mineralG(i,cx,cy);
    else if(act==5) doEat(i,cx,cy);
    else if(act==6) doGive(i,cx,cy);
    else if(act==7) doDouble(i,cx,cy);
    else if(act==8) doAttack(i,cx,cy);

    if(kind[i]!=1u) return;             // may have moved away
    if(energy[i]>uMaxEnergy) energy[i]=uMaxEnergy;
    energy[i]-=uLiveCost; age[i]+=1;
    if(energy[i]<=0.0){ kind[i]=0u; return; }
    if(age[i]>uMaxAge) kind[i]=2u;      // die of old age -> organic
}
)GLSL";

// Colorize pass: state -> RGBA8, with an atomic alive counter.
static const char* kColorSrc = R"GLSL(
#version 430
layout(local_size_x=8, local_size_y=8) in;
layout(rgba8, binding=0) uniform writeonly image2D uOut;
layout(std430, binding=0) buffer B0 { uint  kind[]; };
layout(std430, binding=2) buffer B2 { int   age[]; };
layout(std430, binding=3) buffer B3 { float energy[]; };
layout(std430, binding=6) buffer B6 { uint  fam[]; };
layout(std430, binding=7) buffer BC { uint  aliveCount[]; };

uniform int uW, uH, uMode, uMaxAge;
uniform float uTime, uEnvScale, uEnvDrift, uDayNight;

vec3 rgb(uint c){ return vec3(float(c&0xFFu),float((c>>8)&0xFFu),float((c>>16)&0xFFu))/255.0; }
float hash21(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){ vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
    float a=hash21(i),b=hash21(i+vec2(1,0)),c=hash21(i+vec2(0,1)),d=hash21(i+vec2(1,1));
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y); }
float fbm(vec2 p){ float s=0.0,a=0.5; for(int o=0;o<4;o++){ s+=a*vnoise(p); p*=2.0; a*=0.5; } return s; }
float dayMul(){ return (uDayNight>0.0001) ? (0.5+0.5*sin(uTime*uDayNight)) : 1.0; }
float lightAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(uTime*uEnvDrift,0.0));
    return clamp(pv*(1.0-float(y)/float(uH)*0.5)*dayMul(),0.0,1.0); }
float mineralAt(int x,int y){ vec2 uv=vec2(float(x),float(y))/float(uH);
    float pv=fbm(uv*uEnvScale + vec2(13.7,7.3) - vec2(0.0,uTime*uEnvDrift));
    return clamp(pv*(0.5+float(y)/float(uH)*0.5),0.0,1.0); }

void main(){
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if(p.x>=uW||p.y>=uH) return;
    int i = p.y*uW + p.x;
    uint k = kind[i];
    if(k==1u) atomicAdd(aliveCount[0], 1u);

    vec3 c = vec3(0.118);
    if(uMode==3){                                    // Environment field
        float L=lightAt(p.x,p.y), M=mineralAt(p.x,p.y);
        c = vec3(L*0.85, L*0.85*0.6, M*0.85);
        if(k==1u) c = mix(c, rgb(fam[i]), 0.6);
    } else if(k!=0u){
        if(k==2u)               c = vec3(0.25);      // organic / dead
        else if(uMode==0)       c = rgb(fam[i]);     // Family / clan
        else if(uMode==1){ float e=energy[i]; c=vec3(min(e/1000.0,1.0), min(e/2000.0,1.0)*0.647, 0.0); }
        else if(uMode==2){ float a=clamp(float(age[i])/float(max(uMaxAge,1)),0.0,1.0); c=vec3(a,0.0,a); }
    }
    imageStore(uOut, p, vec4(c,1.0));
}
)GLSL";

// ---------------------------------------------------------------------------
namespace {
std::string buildSimSrc() {
    char hdr[512];
    std::snprintf(hdr, sizeof(hdr),
        "#version 430\n"
        "#define NI %d\n#define NH %d\n#define NO %d\n"
        "#define GN %d\n#define GW %d\n#define WSCALE %ff\n"
        "#define B1OFF %d\n#define W2OFF %d\n#define B2OFF %d\n",
        kNNInputs, kNNHidden, kNNOutputs,
        kGenomeSize, kGenomeWords, kWeightScale,
        kNNInputs*kNNHidden,
        kNNInputs*kNNHidden + kNNHidden,
        kNNInputs*kNNHidden + kNNHidden + kNNHidden*kNNOutputs);
    return std::string(hdr) + kSimBody;
}

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
    if (counter_)   glDeleteBuffers(1, &counter_);
    glDeleteBuffers(kNumBuffers, buf_);
}

bool GpuSimulation::init(const WorldState& seed, const Config& cfg) {
    cfg_ = cfg;
    std::string simSrc = buildSimSrc();
    simProg_   = compileCompute(simSrc.c_str());
    colorProg_ = compileCompute(kColorSrc);
    if (!simProg_ || !colorProg_) return false;

    width_ = seed.width; height_ = seed.height;

    glGenBuffers(kNumBuffers, buf_);
    glGenBuffers(1, &counter_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counter_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

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

    std::vector<uint32_t> family(n);
    for (int i = 0; i < n; ++i)
        family[i] = w.fr[i] | ((uint32_t)w.fg[i] << 8) | ((uint32_t)w.fb[i] << 16);
    fill(6, family.data(), n * sizeof(uint32_t));
}

void GpuSimulation::bindBuffers() const {
    for (int s = 0; s < kNumBuffers; ++s)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s, buf_[s]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, counter_);
}

void GpuSimulation::setSimUniforms() {
    auto I = [&](const char* nm, int v){ glUniform1i(glGetUniformLocation(simProg_, nm), v); };
    auto F = [&](const char* nm, float v){ glUniform1f(glGetUniformLocation(simProg_, nm), v); };
    I("uW", width_); I("uH", height_);
    F("uPhoto", cfg_.photoEnergy); F("uMineralRate", cfg_.mineralRate);
    F("uLiveCost", cfg_.liveCost); F("uEatCost", cfg_.eatCost);
    F("uDoubleCost", cfg_.doubleCost); F("uGeneAtt", cfg_.geneAttackCost);
    F("uMaxEnergy", cfg_.maxEnergy); F("uMaxMineral", cfg_.maxMineral);
    F("uStartEnergy", cfg_.startEnergy); F("uMutChance", cfg_.mutationChance);
    F("uEnvScale", cfg_.envScale); F("uEnvDrift", cfg_.envDrift); F("uDayNight", cfg_.dayNightSpeed);
    I("uKinDist", cfg_.kinColorDist); I("uMaxAge", cfg_.maxAge);
    I("uMutCount", cfg_.mutationCount); I("uMutDelta", cfg_.mutationDelta);
}

void GpuSimulation::step(int ticks) {
    if (!ok_) return;
    glUseProgram(simProg_);
    bindBuffers();
    setSimUniforms();
    glUniform1ui(glGetUniformLocation(simProg_, "uSeed"), (uint32_t)seed_);

    for (int t = 0; t < ticks; ++t) {
        ++tick_;
        glUniform1ui(glGetUniformLocation(simProg_, "uTick"), (uint32_t)tick_);
        glUniform1f(glGetUniformLocation(simProg_, "uTime"), (float)tick_);
        for (int phase = 0; phase < 9; ++phase) {
            int px = phase % 3, py = phase / 3;
            int actW = (width_  - px + 2) / 3;
            int actH = (height_ - py + 2) / 3;
            glUniform1i(glGetUniformLocation(simProg_, "uPhase"), phase);
            glUniform1i(glGetUniformLocation(simProg_, "uActW"), actW);
            glUniform1i(glGetUniformLocation(simProg_, "uActH"), actH);
            GLuint groups = (GLuint)((actW * actH + 63) / 64);
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }
}

int GpuSimulation::colorize(DisplayMode mode, int maxAge) {
    if (!ok_) return 0;
    uint32_t zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counter_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zero), &zero);

    glUseProgram(colorProg_);
    bindBuffers();
    glBindImageTexture(0, tex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glUniform1i(glGetUniformLocation(colorProg_, "uW"), width_);
    glUniform1i(glGetUniformLocation(colorProg_, "uH"), height_);
    glUniform1i(glGetUniformLocation(colorProg_, "uMode"), (int)mode);
    glUniform1i(glGetUniformLocation(colorProg_, "uMaxAge"), maxAge);
    glUniform1f(glGetUniformLocation(colorProg_, "uTime"), (float)tick_);
    glUniform1f(glGetUniformLocation(colorProg_, "uEnvScale"), cfg_.envScale);
    glUniform1f(glGetUniformLocation(colorProg_, "uEnvDrift"), cfg_.envDrift);
    glUniform1f(glGetUniformLocation(colorProg_, "uDayNight"), cfg_.dayNightSpeed);

    glDispatchCompute((width_ + 7) / 8, (height_ + 7) / 8, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    uint32_t alive = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, counter_);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(alive), &alive);
    return (int)alive;
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

    get32(6); for (int i=0;i<n;++i){ w.fr[i]=u[i]&0xFF; w.fg[i]=(u[i]>>8)&0xFF; w.fb[i]=(u[i]>>16)&0xFF; }
}

} // namespace cb

#include "GpuSimulation.h"
#include "GL/gl3w.h"
#include <cstdio>
#include <vector>

namespace cb {

// ---------------------------------------------------------------------------
// Compute shader: one invocation per acting cell of the current 3x3 phase.
// A direct translation of the CPU Simulation VM operating on SSBO-backed SoA
// state. Genome is packed 4 commands per uint (16 uints / cell).
// ---------------------------------------------------------------------------
static const char* kSimSrc = R"GLSL(
#version 430
layout(local_size_x = 64) in;

layout(std430, binding=0) buffer B0 { uint  kind[]; };
layout(std430, binding=1) buffer B1 { uint  adr[]; };
layout(std430, binding=2) buffer B2 { uint  dir[]; };
layout(std430, binding=3) buffer B3 { uint  mask[]; };
layout(std430, binding=4) buffer B4 { int   age[]; };
layout(std430, binding=5) buffer B5 { float energy[]; };
layout(std430, binding=6) buffer B6 { float mineral[]; };
layout(std430, binding=7) buffer B7 { uint  genome[]; };
layout(std430, binding=8) buffer B8 { uint  col[]; };
layout(std430, binding=9) buffer B9 { uint  fam[]; };

uniform int  uW, uH, uPhase, uActW, uActH;
uniform uint uTick, uSeed;
uniform float uPhoto, uMutation, uLiveCost, uEatCost, uDoubleCost, uGeneAtt,
              uMaxEnergy, uMaxMineral, uStartEnergy;
uniform int  uMaxGeneDiff, uMaxAge;

const int GN = 64;

int idx(int x, int y) { return y * uW + x; }

uint gene(int i, int k) { return (genome[i*16 + (k>>2)] >> uint((k&3)*8)) & 0xFFu; }
void setGene(int i, int k, uint v) {
    int w = i*16 + (k>>2); uint sh = uint((k&3)*8);
    genome[w] = (genome[w] & ~(0xFFu << sh)) | ((v & 0xFFu) << sh);
}

void unpack(uint c, out int r, out int g, out int b) { r=int(c&0xFFu); g=int((c>>8)&0xFFu); b=int((c>>16)&0xFFu); }
uint packc(int r, int g, int b) { return uint(clamp(r,0,255)) | (uint(clamp(g,0,255))<<8) | (uint(clamp(b,0,255))<<16); }
void goRed(int i,int n){ int r,g,b; unpack(col[i],r,g,b); r+=n; n/=2; g-=n; b-=n; col[i]=packc(r,g,b); }
void goGreen(int i,int n){ int r,g,b; unpack(col[i],r,g,b); g+=n; n/=2; r-=n; b-=n; col[i]=packc(r,g,b); }
void goBlue(int i,int n){ int r,g,b; unpack(col[i],r,g,b); b+=n; n/=2; g-=n; r-=n; col[i]=packc(r,g,b); }

uint rseed;
uint rnext(){ rseed^=rseed<<13; rseed^=rseed>>17; rseed^=rseed<<5; return rseed; }
int  rint(int m){ return m<=0?0:int(rnext()%uint(m)); }
float rfloat(){ return float(rnext()>>8)*(1.0/16777216.0); }

int  param(int i){ return int(gene(i, int((adr[i]+1u) % uint(GN)))); }
void incAdr(int i,int a){ adr[i]=uint((int(adr[i])+a) % GN); }
void jmpAdr(int i,int a){ incAdr(i, int(gene(i, int((adr[i]+uint(a)) % uint(GN))))); }

int nbX(int x,uint f,int n){ n+=int(f); if(n>=8)n-=8; int xt=x;
    if(n==0||n==6||n==7){ xt--; if(xt<0)xt=uW-1; } else if(n>=2&&n<=4){ xt++; if(xt>=uW)xt=0; } return xt; }
int nbY(int y,uint f,int n){ n+=int(f); if(n>=8)n-=8; int yt=y;
    if(n<=2)yt--; else if(n>=4&&n<=6)yt++; return yt; }

bool isRel(int s,int o){ if(mask[s]>0u) return true; int d=0;
    for(int k=0;k<GN;k++){ if(gene(s,k)!=gene(o,k)){ if(++d>uMaxGeneDiff) return false; } } return true; }

void moveCell(int i,int j){
    kind[j]=kind[i]; adr[j]=adr[i]; dir[j]=dir[i]; mask[j]=mask[i]; age[j]=age[i];
    energy[j]=energy[i]; mineral[j]=mineral[i]; col[j]=col[i]; fam[j]=fam[i];
    for(int k=0;k<16;k++) genome[j*16+k]=genome[i*16+k];
    kind[i]=0u;
}

void mutate(int i){
    setGene(i, rint(GN), uint(rint(GN)));
    int r,g,b; unpack(fam[i],r,g,b);
    r+=rint(13)-6; g+=rint(13)-6; b+=rint(13)-6; fam[i]=packc(r,g,b);
}

int doMove(int i,int x,int y){ uint f=dir[i]; int n=param(i)%8; int xt=nbX(x,f,n),yt=nbY(y,f,n);
    if(yt<0||yt>=uH) return 3; int j=idx(xt,yt);
    if(kind[j]==0u){ moveCell(i,j); return 2; }
    if(kind[j]==2u) return 4; if(isRel(i,j)) return 6; return 5; }

int doEat(int i,int x,int y){ uint f=dir[i]; int n=param(i)%8; int xt=nbX(x,f,n),yt=nbY(y,f,n);
    energy[i] -= (mask[i]>0u)? uEatCost*0.5 : uEatCost;
    if(yt<0||yt>=uH) return 3; int j=idx(xt,yt);
    if(kind[j]==0u) return 2;
    if(kind[j]==2u){ energy[i]+=energy[j]; kind[j]=0u; goRed(i,50); return 4; }
    energy[i]+=energy[j]; mineral[i]+=mineral[j]; goRed(i,50); kind[j]=0u; return 5; }

int doGive(int i,int x,int y){ uint f=dir[i]; int n=param(i)%8; int xt=nbX(x,f,n),yt=nbY(y,f,n);
    if(yt<0||yt>=uH) return 3; int j=idx(xt,yt);
    if(kind[j]==0u) return 2; if(mask[j]>0u) return 2; if(kind[j]==2u) return 4;
    float h=energy[i]*0.25; energy[i]-=h; energy[j]+=h;
    if(mineral[i]>3.0){ float m=mineral[i]*0.25; mineral[i]-=m; mineral[j]+=m; if(mineral[j]>999.0)mineral[j]=999.0; }
    return 5; }

int doCare(int i,int x,int y){ uint f=dir[i]; int n=param(i)%8; int xt=nbX(x,f,n),yt=nbY(y,f,n);
    if(yt<0||yt>=uH) return 3; int j=idx(xt,yt);
    if(kind[j]==0u) return 2; if(mask[j]>0u) return 2; if(kind[j]==2u) return 4;
    if(energy[i]>energy[j]){ float h=(energy[i]-energy[j])*0.5; energy[i]-=h; energy[j]+=h; }
    if(mineral[i]>mineral[j]){ float m=(mineral[i]-mineral[j])*0.5; mineral[i]-=m; mineral[j]+=m; }
    return 5; }

int doSee(int i,int x,int y){ uint f=dir[i]; int n=param(i)%8; int xt=nbX(x,f,n),yt=nbY(y,f,n);
    if(yt<0||yt>=uH) return 3; int j=idx(xt,yt);
    if(kind[j]==0u) return 2; if(mask[j]>0u) return 2; if(kind[j]==2u) return 4;
    return isRel(i,j)?6:5; }

int findEmpty(int x,int y,uint f){ for(int n=0;n<8;n++){ int yt=nbY(y,f,n); if(yt<0||yt>=uH) continue;
    int xt=nbX(x,f,n); if(kind[idx(xt,yt)]==0u) return n; } return 8; }

void doDouble(int i,int x,int y){ energy[i]-=uDoubleCost; if(energy[i]<=0.0) return;
    int n=findEmpty(x,y,dir[i]); if(n==8){ energy[i]=uStartEnergy; return; }
    int xt=nbX(x,dir[i],n),yt=nbY(y,dir[i],n); int j=idx(xt,yt);
    kind[j]=1u; adr[j]=0u; dir[j]=dir[i]; mask[j]=0u; age[j]=0;
    for(int k=0;k<16;k++) genome[j*16+k]=genome[i*16+k];
    energy[i]*=0.5; energy[j]=energy[i]; mineral[i]*=0.5; mineral[j]=mineral[i];
    col[j]=col[i]; fam[j]=fam[i];
    if(rfloat()<uMutation) mutate(j); }

int isFull(int x,int y,uint f){ for(int n=0;n<8;n++){ int yt=nbY(y,f,n); if(yt<0||yt>=uH) continue;
    int xt=nbX(x,f,n); if(kind[idx(xt,yt)]==0u) return 2; } return 1; }

void doGeneAtt(int i,int x,int y){ uint f=dir[i]; int n=param(i)%8; int xt=nbX(x,f,n),yt=nbY(y,f,n);
    if(yt<0||yt>=uH) return; int j=idx(xt,yt); if(kind[j]!=1u) return;
    energy[i]-=uGeneAtt; int g=rint(GN); setGene(j,g,gene(i,g)); }

void photo(int i,int y){ energy[i] += (float(uH-y)*uPhoto)/6.0; goGreen(i,5); }
void mineralG(int i,int y){ energy[i] += (float(y)*uPhoto)/6.0; goBlue(i,5); }
void rotate(int i){ dir[i]=uint((int(dir[i])+param(i))%8); }
int checkEnergy(int i){ return energy[i]  < uMaxEnergy  * float(param(i))/float(GN) ? 2:3; }
int checkMineral(int i){ return mineral[i] < uMaxMineral * float(param(i))/float(GN) ? 2:3; }
int checkLevel(int i,int y){ return y < uH * param(i) / GN ? 2:3; }
int checkAge(int i){ return age[i] < uMaxAge * param(i) / GN ? 2:3; }
void toOrganic(int i){ kind[i]=2u; col[i]=packc(100,100,100); }

void main(){
    uint t = gl_GlobalInvocationID.x;
    if(t >= uint(uActW*uActH)) return;
    int px=uPhase%3, py=uPhase/3;
    int cx = px + int(t % uint(uActW))*3;
    int cy = py + int(t / uint(uActW))*3;
    if(cx>=uW || cy>=uH) return;
    int i = idx(cx,cy);
    if(kind[i]==0u) return;
    if(mask[i]>0u) mask[i]-=1u;
    if(kind[i]==2u) return;

    uint h = uSeed ^ (uint(i)*2654435761u) ^ (uTick*2246822519u);
    h += 0x9e3779b9u; h=(h^(h>>16))*0x85ebca6bu; h=(h^(h>>13))*0xc2b2ae35u; h^=h>>16;
    rseed = (h==0u)?1u:h;

    for(int cyc=0; cyc<15; cyc++){
        int cmd = int(gene(i, int(adr[i])));
        bool brk=false;
        if(cmd==0){ mutate(i); incAdr(i,1); }
        else if(cmd==8){ jmpAdr(i,param(i)); }
        else if(cmd==16){ doDouble(i,cx,cy); incAdr(i,1); brk=true; }
        else if(cmd==23){ rotate(i); incAdr(i,2); }
        else if(cmd==26){ jmpAdr(i,doMove(i,cx,cy)); brk=true; }
        else if(cmd==32){ photo(i,cy); incAdr(i,1); brk=true; }
        else if(cmd==33){ mineralG(i,cy); incAdr(i,1); brk=true; }
        else if(cmd==34){ incAdr(i,doEat(i,cx,cy)); brk=true; }
        else if(cmd==36||cmd==37){ incAdr(i,doGive(i,cx,cy)); brk=true; }
        else if(cmd==38||cmd==39){ incAdr(i,doCare(i,cx,cy)); brk=true; }
        else if(cmd==40){ incAdr(i,doSee(i,cx,cy)); }
        else if(cmd==41){ incAdr(i,checkLevel(i,cy)); }
        else if(cmd==42){ incAdr(i,checkEnergy(i)); }
        else if(cmd==43){ incAdr(i,checkMineral(i)); }
        else if(cmd==44){ incAdr(i,checkAge(i)); }
        else if(cmd==46){ incAdr(i,isFull(cx,cy,dir[i])); }
        else if(cmd==52){ doGeneAtt(i,cx,cy); incAdr(i,2); brk=true; }
        else { incAdr(i,cmd); }
        if(brk) break;
    }

    if(kind[i]!=1u) return;
    if(energy[i]>=uMaxEnergy) energy[i]=uMaxEnergy;
    energy[i]-=uLiveCost; age[i]+=1;
    if(energy[i]<=0.0){ kind[i]=0u; return; }
    if(age[i]>uMaxAge) toOrganic(i);
}
)GLSL";

// Colorize pass: state -> RGBA8 image, with an atomic alive counter.
static const char* kColorSrc = R"GLSL(
#version 430
layout(local_size_x=8, local_size_y=8) in;
layout(rgba8, binding=0) uniform writeonly image2D uOut;

layout(std430, binding=0)  buffer B0 { uint  kind[]; };
layout(std430, binding=4)  buffer B4 { int   age[]; };
layout(std430, binding=5)  buffer B5 { float energy[]; };
layout(std430, binding=8)  buffer B8 { uint  col[]; };
layout(std430, binding=9)  buffer B9 { uint  fam[]; };
layout(std430, binding=10) buffer BC { uint  aliveCount[]; };

uniform int uW, uH, uMode, uMaxAge;
vec3 rgb(uint c){ return vec3(float(c&0xFFu),float((c>>8)&0xFFu),float((c>>16)&0xFFu))/255.0; }

void main(){
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if(p.x>=uW||p.y>=uH) return;
    int i = p.y*uW + p.x;
    uint k = kind[i];
    vec3 c = vec3(0.118);
    if(k!=0u){
        if(k==1u) atomicAdd(aliveCount[0], 1u);
        if(uMode==0)      c = rgb(col[i]);
        else if(uMode==1){ float e=energy[i]; c=vec3(min(e/1000.0,1.0), min(e/2000.0,1.0)*0.647, 0.0); }
        else if(uMode==2)  c = rgb(k==2u ? col[i] : fam[i]);
        else if(uMode==3){ float a=clamp(float(age[i])/float(max(uMaxAge,1)),0.0,1.0); c=vec3(a,0.0,a); }
    }
    imageStore(uOut, p, vec4(c,1.0));
}
)GLSL";

// ---------------------------------------------------------------------------
namespace {
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
    simProg_   = compileCompute(kSimSrc);
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
    auto u8to32 = [&](const std::vector<uint8_t>& s) {
        for (int i = 0; i < n; ++i) u[i] = s[i];
        return u.data();
    };

    fill(0, u8to32(w.kind),      n * sizeof(uint32_t));
    fill(1, u8to32(w.adr),       n * sizeof(uint32_t));
    fill(2, u8to32(w.direction), n * sizeof(uint32_t));
    fill(3, u8to32(w.mask),      n * sizeof(uint32_t));
    fill(4, w.age.data(),        n * sizeof(int32_t));
    fill(5, w.energy.data(),     n * sizeof(float));
    fill(6, w.mineral.data(),    n * sizeof(float));

    std::vector<uint32_t> packed((size_t)n * 16, 0);
    for (int c = 0; c < n; ++c) {
        const uint8_t* m = w.mindAt(c);
        for (int k = 0; k < kGenomeSize; ++k)
            packed[(size_t)c * 16 + (k >> 2)] |= (uint32_t)m[k] << ((k & 3) * 8);
    }
    fill(7, packed.data(), packed.size() * sizeof(uint32_t));

    std::vector<uint32_t> color(n), family(n);
    for (int i = 0; i < n; ++i) {
        color[i]  = w.cr[i] | ((uint32_t)w.cg[i] << 8) | ((uint32_t)w.cb[i] << 16);
        family[i] = w.fr[i] | ((uint32_t)w.fg[i] << 8) | ((uint32_t)w.fb[i] << 16);
    }
    fill(8, color.data(),  n * sizeof(uint32_t));
    fill(9, family.data(), n * sizeof(uint32_t));
}

void GpuSimulation::bindBuffers() const {
    for (int s = 0; s < kNumBuffers; ++s)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, s, buf_[s]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, counter_);
}

void GpuSimulation::setSimUniforms() {
    auto I = [&](const char* nm, int v){ glUniform1i(glGetUniformLocation(simProg_, nm), v); };
    auto F = [&](const char* nm, float v){ glUniform1f(glGetUniformLocation(simProg_, nm), v); };
    I("uW", width_); I("uH", height_);
    F("uPhoto", cfg_.photoEnergy); F("uMutation", cfg_.mutationChance);
    F("uLiveCost", cfg_.liveCost); F("uEatCost", cfg_.eatCost);
    F("uDoubleCost", cfg_.doubleCost); F("uGeneAtt", cfg_.geneAttackCost);
    F("uMaxEnergy", cfg_.maxEnergy); F("uMaxMineral", cfg_.maxMineral);
    F("uStartEnergy", cfg_.startEnergy);
    I("uMaxGeneDiff", cfg_.maxGeneDifference); I("uMaxAge", cfg_.maxAge);
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
    get32(1); to8(w.adr);
    get32(2); to8(w.direction);
    get32(3); to8(w.mask);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[4]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n*sizeof(int32_t), w.age.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[5]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n*sizeof(float), w.energy.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[6]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, n*sizeof(float), w.mineral.data());

    std::vector<uint32_t> packed((size_t)n * 16);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf_[7]);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, packed.size()*sizeof(uint32_t), packed.data());
    for (int c = 0; c < n; ++c) {
        uint8_t* m = w.mindAt(c);
        for (int k = 0; k < kGenomeSize; ++k)
            m[k] = (uint8_t)((packed[(size_t)c*16 + (k>>2)] >> ((k&3)*8)) & 0xFF);
    }

    get32(8); for (int i=0;i<n;++i){ w.cr[i]=u[i]&0xFF; w.cg[i]=(u[i]>>8)&0xFF; w.cb[i]=(u[i]>>16)&0xFF; }
    get32(9); for (int i=0;i<n;++i){ w.fr[i]=u[i]&0xFF; w.fg[i]=(u[i]>>8)&0xFF; w.fb[i]=(u[i]>>16)&0xFF; }
}

} // namespace cb

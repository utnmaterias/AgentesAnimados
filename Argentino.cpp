#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <ole2.h>
#include <gdiplus.h>
#include <vector>
#include <commdlg.h>
#include <cmath>
#include <vfw.h>

#pragma comment(lib, "vfw32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

static HICON g_hIconBig  = NULL;
static HICON g_hIconSmall = NULL;
static float g_banderazoDir = 1.0f;

constexpr UINT  WM_TRAY_MSG   = WM_APP + 100;
constexpr UINT  WM_AGENT_TIMER = WM_APP + 101;
constexpr float AGENT_SCALE   = 2.0f;
constexpr int   AGENT_SIZE    = (int)(128 * AGENT_SCALE);
constexpr double PI           = 3.14159265358979323846;

// ============================================================
// PELOTA INTERACTIVA CON FÍSICA
// ============================================================
class PelotaInteractiva {
public:
    float x, y, vx, vy;
    float rotation;
    float squashX, squashY;
    float radius;
    bool  dragging;

    PelotaInteractiva()
        : x(64.f), y(64.f), vx(30.f), vy(-20.f),
          rotation(0), squashX(1.f), squashY(1.f),
          radius(10.f), dragging(false) {}

    void actualizar(float dt) {
        vy += 120.f * dt;
        vx *= 0.998f;
        vy *= 0.998f;
        x  += vx * dt;
        y  += vy * dt;
        rotation += (vx / radius) * dt;
        squashX  += (1.f - squashX) * 8.f * dt;
        squashY  += (1.f - squashY) * 8.f * dt;

        float limit = 128.f - radius;
        if (x < radius) { x = radius; vx =  fabsf(vx)*0.85f; aplicarSquash(fabsf(vx)*0.008f, true);  }
        if (x > limit)  { x = limit;  vx = -fabsf(vx)*0.85f; aplicarSquash(fabsf(vx)*0.008f, true);  }
        if (y < radius) { y = radius; vy =  fabsf(vy)*0.85f; aplicarSquash(fabsf(vy)*0.008f, false); }
        if (y > limit)  { y = limit;  vy = -fabsf(vy)*0.85f; aplicarSquash(fabsf(vy)*0.008f, false); }
    }

    void aplicarSquash(float intensidad, bool horizontal) {
        float s = fminf(intensidad, 0.35f);
        if (horizontal) { squashX = 1.f + s; squashY = 1.f - s; }
        else            { squashX = 1.f - s; squashY = 1.f + s; }
    }

    void onClick(float clickX, float clickY) {
        float dx   = x - clickX, dy = y - clickY;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 0.1f) dist = 0.1f;
        float fuerza = 250.f;
        vx = (dx / dist) * fuerza;
        vy = (dy / dist) * fuerza;
        aplicarSquash(0.3f, fabsf(dx) > fabsf(dy));
    }

    bool hitTest(float mx, float my) {
        float dx = mx - x, dy = my - y;
        return (dx*dx + dy*dy) <= (radius * radius * 1.5f);
    }

    void dibujar(Graphics& g) {
        float shadowY      = 128.f - 2.f;
        float heightRatio  = fmaxf(0.3f, 1.f - (shadowY - y) * 0.008f);
        SolidBrush shadow(Color((BYTE)(40 * heightRatio), 0, 0, 0));
        g.FillEllipse(&shadow,
            (int)(x - radius * heightRatio),
            (int)(shadowY - 2),
            (int)(radius * 2 * heightRatio), 3);

        Matrix oldM; g.GetTransform(&oldM);
        g.TranslateTransform(x, y);
        g.ScaleTransform(squashX, squashY);
        g.RotateTransform(rotation * 180.f / (float)PI);

        SolidBrush white(Color(255, 245, 245, 255));
        g.FillEllipse(&white,
            (int)-radius, (int)-radius,
            (int)(radius*2), (int)(radius*2));

        SolidBrush black(Color(255, 35, 35, 40));
        PointF center[5];
        for (int i = 0; i < 5; i++) {
            float a   = (float)i * 72.f * (float)PI / 180.f - (float)PI / 2.f;
            center[i].X = cosf(a) * radius * 0.4f;
            center[i].Y = sinf(a) * radius * 0.4f;
        }
        g.FillPolygon(&black, center, 5);

        for (int p = 0; p < 5; p++) {
            float ba = (float)p * 72.f * (float)PI / 180.f;
            float px = cosf(ba) * radius * 0.75f;
            float py = sinf(ba) * radius * 0.75f;
            PointF side[5];
            for (int i = 0; i < 5; i++) {
                float a   = (float)i * 72.f * (float)PI / 180.f + ba;
                side[i].X = px + cosf(a) * radius * 0.22f;
                side[i].Y = py + sinf(a) * radius * 0.22f;
            }
            g.FillPolygon(&black, side, 5);
        }
        g.SetTransform(&oldM);

        // Brillo
        Matrix shineM; g.GetTransform(&shineM);
        g.TranslateTransform(x, y);
        g.ScaleTransform(squashX, squashY);
        SolidBrush shine(Color(140, 255, 255, 255));
        g.FillEllipse(&shine,
            (int)(-radius * 0.5f), (int)(-radius * 0.6f),
            (int)(radius * 0.4f),  (int)(radius * 0.25f));
        Pen outline(Color(80, 40, 40, 45), 0.5f);
        g.DrawEllipse(&outline,
            (int)-radius, (int)-radius,
            (int)(radius*2), (int)(radius*2));
        g.SetTransform(&oldM);
    }
};

// ============================================================
// MESSI CON BANDERA
// ============================================================
class MessiConBandera {
public:
    enum Estado {
        IDLE, HABLANDO, PENSANDO, CAMINANDO,
        CELEBRANDO, DURMIENDO, BANDERAZO,
        JUGANDO, COMIENDO_PIZZA, TOMANDO_MATE,
        LLAMANDO_DEPAUL, PATEANDO_PELOTA
    };

    // Pelota pública para acceso desde WndProc
    PelotaInteractiva pelotaJuego;
    bool  justKicked    = false;

private:
    struct Confeto {
        float x, y, vx, vy, rot, rotV, size;
        int   colorIdx;
        float life;
        bool  circle;
    };
    struct SteamParticle {
        float x, y, vy, vx, size, life;
    };

    float anim, idleTimer, blinkTimer, waveTime, mouseInfluence;
    float lastMouseX, lastMouseY;
    bool  hasLastMouse;
    Estado estado;
    float jumpY, jumpVel;
    float lookTargetX, lookTargetY;
    float drawCx, drawCy;
    std::vector<Confeto>       confeti;
    std::vector<SteamParticle> steamParticles;

    float kickCooldown  = 0;
    float golFlashTimer = 0;

    // ── Bandera ──────────────────────────────────────────────
    void dibujarBandera(Graphics& g, float px, float py,
                        float time, float intensity)
    {
        float pH = 22.f;
        Pen polePen(Color(255, 160, 140, 120), 1.5f);
        polePen.SetStartCap(LineCapRound);
        g.DrawLine(&polePen, px, py, px, py - pH);

        SolidBrush ballCol(Color(255, 218, 175, 42));
        g.FillEllipse(&ballCol,
            (int)(px - 1.5f), (int)(py - pH - 2.5f), 3, 3);

        float fW = 26.f, fH = 15.f;
        int   strips = 16;
        float topY   = py - pH;

        SolidBrush colC(Color(255, 100, 180, 230));
        SolidBrush colW(Color(255, 245, 245, 250));
        SolidBrush* cols[] = { &colC, &colW, &colC };
        float sh = fH / 3.0f;

        for (int i = 0; i < strips; i++) {
            float u0 = (float)i / strips;
            float u1 = (float)(i + 1) / strips;
            float a0 = u0 * u0 * 3.5f * intensity;
            float a1 = u1 * u1 * 3.5f * intensity;
            float dx0 = sinf(time*2.8f + u0*5.5f)*a0
                       + sinf(time*1.9f + u0*9.0f)*a0*0.25f
                       + sinf(time*4.1f + u0*3.0f)*a0*0.15f;
            float dx1 = sinf(time*2.8f + u1*5.5f)*a1
                       + sinf(time*1.9f + u1*9.0f)*a1*0.25f
                       + sinf(time*4.1f + u1*3.0f)*a1*0.15f;
            float dy0 = cosf(time*2.2f + u0*4.5f)*a0*0.12f;
            float dy1 = cosf(time*2.2f + u1*4.5f)*a1*0.12f;
            float x0  = px + u0 * fW + dx0;
            float x1  = px + u1 * fW + dx1;
            for (int s = 0; s < 3; s++) {
                PointF pts[4] = {
                    PointF(x0, topY +  s      * sh + dy0),
                    PointF(x1, topY +  s      * sh + dy1),
                    PointF(x1, topY + (s + 1) * sh + dy1),
                    PointF(x0, topY + (s + 1) * sh + dy0)
                };
                g.FillPolygon(cols[s], pts, 4);
            }
        }
        float su  = 0.5f;
        float sa  = su * su * 3.5f * intensity;
        float sdx = sinf(time*2.8f + su*5.5f)*sa
                   + sinf(time*1.9f + su*9.0f)*sa*0.25f;
        float sdy = cosf(time*2.2f + su*4.5f)*sa*0.12f;
        g.FillEllipse(&ballCol,
            (REAL)(px + su*fW + sdx - 1.5f),
            (REAL)(topY + fH*0.5f + sdy - 1.5f),
            (REAL)3, (REAL)3);
    }

    // ── De Paul ──────────────────────────────────────────────
    void dibujarDePaul(Graphics& g, float cx, float cy,
                       float t, float sc)
    {
        Matrix oldMat; g.GetTransform(&oldMat);
        g.TranslateTransform(cx, cy);
        g.ScaleTransform(sc, sc);
        g.TranslateTransform(-cx, -cy);

        float phase = sinf(t * 1.5f);
        float y     = cy - fabsf(sinf(t)) * 0.8f;
        float x     = cx;

        SolidBrush shadow(Color(45,0,0,0));
        g.FillEllipse(&shadow, (int)(cx-7), (int)(cy+10), 14, 4);

        SolidBrush skin    (Color(255,210,175,150));
        SolidBrush hairCol (Color(255, 45, 35, 25));
        SolidBrush jerseyW (Color(255,245,245,250));
        SolidBrush celeste (Color(255,100,180,230));
        SolidBrush shortsC (Color(255, 30, 50,120));
        SolidBrush bootC   (Color(255, 20, 20, 25));

        float hipY = y + 6;
        Pen sPen (Color(255, 30, 50,120), 4);
        sPen.SetStartCap(LineCapRound); sPen.SetEndCap(LineCapRound);
        Pen skPen(Color(255,240,240,245), 3);
        skPen.SetStartCap(LineCapRound); skPen.SetEndCap(LineCapRound);

        float sL = phase * 0.5f, sR = -phase * 0.5f;
        g.DrawLine(&sPen,  x-2.5f, hipY, x-2.5f+sL*5, hipY+10);
        g.DrawLine(&skPen, x-2.5f+sL*5, hipY+10, x-2.5f+sL*6, hipY+14);
        g.FillEllipse(&bootC, (int)(x-2.5f+sL*6)-3, (int)(hipY+13), 6, 4);

        g.DrawLine(&sPen,  x+2.5f, hipY, x+2.5f+sR*5, hipY+10);
        g.DrawLine(&skPen, x+2.5f+sR*5, hipY+10, x+2.5f+sR*6, hipY+14);
        g.FillEllipse(&bootC, (int)(x+2.5f+sR*6)-3, (int)(hipY+13), 6, 4);

        g.FillRectangle(&jerseyW, (int)x-5, (int)y-5, 10, 12);
        g.FillRectangle(&celeste, (int)x-5, (int)y-5,  3, 12);
        g.FillRectangle(&celeste, (int)x+2, (int)y-5,  3, 12);

        FontFamily ffN(L"Arial");
        Font nFont(&ffN, 5, FontStyleBold, UnitPixel);
        SolidBrush numC(Color(180,30,50,120));
        StringFormat nFmt;
        nFmt.SetAlignment(StringAlignmentCenter);
        nFmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"5",-1,&nFont, PointF(x,y+1), &nFmt, &numC);

        Pen armP (Color(255,100,180,230),3);
        armP.SetStartCap(LineCapRound); armP.SetEndCap(LineCapRound);
        Pen foreP(Color(255,210,175,150),3);
        foreP.SetStartCap(LineCapRound); foreP.SetEndCap(LineCapRound);

        float hLx = x-6+phase*3, hLy = y+6;
        g.DrawLine(&armP,  x-5, y-3, (x-5+hLx)*0.5f, y+1);
        g.DrawLine(&foreP, (x-5+hLx)*0.5f, y+1, hLx, hLy);
        g.FillEllipse(&skin, (int)hLx-2, (int)hLy-2, 4, 4);

        float hRx = x+6-phase*3, hRy = y+6;
        g.DrawLine(&armP,  x+5, y-3, (x+5+hRx)*0.5f, y+1);
        g.DrawLine(&foreP, (x+5+hRx)*0.5f, y+1, hRx, hRy);
        g.FillEllipse(&skin, (int)hRx-2, (int)hRy-2, 4, 4);

        float headY = y - 10;
        g.FillEllipse(&skin,    (int)x-5,  (int)headY-5, 11, 12);
        g.FillEllipse(&hairCol, (int)x-6,  (int)headY-7, 13,  8);

        SolidBrush eyeW(Color(255,252,248));
        SolidBrush eyeP(Color(255, 60, 40, 25));
        g.FillEllipse(&eyeW, (int)x-4, (int)headY-1, 3, 3);
        g.FillEllipse(&eyeW, (int)x+1, (int)headY-1, 3, 3);
        g.FillEllipse(&eyeP, (REAL)x-3.5f, (REAL)headY, 2.f, 2.f);
        g.FillEllipse(&eyeP, (REAL)x+1.5f, (REAL)headY, 2.f, 2.f);

        Pen smilePen(Color(255,180,80,90), 1.f);
        smilePen.SetStartCap(LineCapRound);
        g.DrawArc(&smilePen, (int)x-3, (int)headY+2, 6, 4, 200, 140);

        g.SetTransform(&oldMat);
    }

public:
    MessiConBandera()
        : anim(0), idleTimer(0), blinkTimer(0), estado(IDLE),
          waveTime(0), mouseInfluence(0),
          lastMouseX(0), lastMouseY(0), hasLastMouse(false),
          jumpY(0), jumpVel(0),
          lookTargetX(0), lookTargetY(0),
          drawCx(64.f), drawCy(80.f) {}

    void onMouseClick(float mx, float my) {
        if (estado == PATEANDO_PELOTA && pelotaJuego.hitTest(mx, my)) {
            pelotaJuego.onClick(mx, my);
            justKicked = true;
        }
    }

    void intentarPateoFuerte(float mx, float my) {
        if (estado == PATEANDO_PELOTA && pelotaJuego.hitTest(mx, my)) {
            pelotaJuego.onClick(mx, my);
            anim = 0; justKicked = true; golFlashTimer = 2.0f;
        }
    }

    void setEstado(Estado e) {
        if (e != estado) {
            anim = 0;
            if (e == CELEBRANDO)    { jumpVel = 18.0f; confeti.clear(); }
            if (e == DURMIENDO)     { blinkTimer = 0; }
            if (e == COMIENDO_PIZZA || e == TOMANDO_MATE)
                steamParticles.clear();
            if (e == LLAMANDO_DEPAUL)
                { steamParticles.clear(); confeti.clear(); }
            if (e == PATEANDO_PELOTA) {
                pelotaJuego     = PelotaInteractiva();
                pelotaJuego.x   = 64.f;
                pelotaJuego.y   = 40.f;
                pelotaJuego.vx  = 50.f;
                pelotaJuego.vy  = -30.f;
                kickCooldown    = 0;
                justKicked      = false;
                golFlashTimer   = 0;
            }
        }
        estado = e;
    }

    Estado getEstado() const { return estado; }

    void onMouseMove(float mx, float my) {
        if (hasLastMouse) {
            float delta = fabsf(mx - lastMouseX);
            mouseInfluence = fminf(mouseInfluence + delta*0.15f, 3.0f);
        }
        lastMouseX = mx; lastMouseY = my; hasLastMouse = true;
        lookTargetX = (mx - 64.0f) * 0.05f;
        lookTargetY = (my - 60.0f) * 0.05f;
        if (lookTargetX >  1.5f) lookTargetX =  1.5f;
        if (lookTargetX < -1.5f) lookTargetX = -1.5f;
        if (lookTargetY >  1.5f) lookTargetY =  1.5f;
        if (lookTargetY < -1.5f) lookTargetY = -1.5f;
    }

    void actualizar(float dt) {
        blinkTimer += dt;
        float windSpeed = 2.5f + mouseInfluence * 2.0f;
        waveTime      += dt * windSpeed;
        mouseInfluence *= 0.96f;

        if (estado == IDLE) {
            idleTimer += dt;
            if (idleTimer > 10.0f) setEstado(DURMIENDO);
        } else {
            idleTimer = 0;
        }

        if (estado == CELEBRANDO) {
            jumpVel -= 45.0f * dt;
            jumpY   += jumpVel * dt;
            if (jumpY <= 0.0f) { jumpY = 0.0f; jumpVel = 16.0f; }
            anim += dt * 10.f;
            if (confeti.size() < 80 && rand() % 3 != 0) {
                Confeto c;
                c.x      = drawCx + (float)(rand()%50 - 25);
                c.y      = drawCy - 35 - (float)(rand()%20);
                c.vx     = (float)(rand()%30 - 15) * 0.8f;
                c.vy     = -3.0f - (float)(rand()%20)*0.15f;
                c.rot    = (float)(rand()%360);
                c.rotV   = (float)(rand()%400 - 200);
                c.size   = 1.5f + (float)(rand()%15)*0.1f;
                c.colorIdx = rand()%5;
                c.life   = 2.5f + (float)(rand()%15)*0.1f;
                c.circle = (rand()%3 == 0);
                confeti.push_back(c);
            }
        } else if (estado == HABLANDO   || estado == PENSANDO  ||
                   estado == CAMINANDO  || estado == BANDERAZO ||
                   estado == JUGANDO    || estado == PATEANDO_PELOTA ||
                   estado == COMIENDO_PIZZA || estado == TOMANDO_MATE ||
                   estado == LLAMANDO_DEPAUL) {
            anim += dt * 7.f;
        }

        for (auto& c : confeti) {
            c.vy += 35.0f * dt; c.vx *= 0.99f;
            c.x  += c.vx * dt; c.y  += c.vy * dt;
            c.rot += c.rotV * dt; c.life -= dt;
        }
        for (int i = (int)confeti.size()-1; i >= 0; i--)
            if (confeti[i].life <= 0) confeti.erase(confeti.begin()+i);

        if (estado == TOMANDO_MATE) {
            if (steamParticles.size() < 20 && rand()%3 == 0) {
                SteamParticle sp;
                sp.x    = drawCx - 13 + (float)(rand()%6 - 3);
                sp.y    = drawCy - 10;
                sp.vx   = (float)(rand()%10 - 5)*0.3f;
                sp.vy   = -6.0f - (float)(rand()%4);
                sp.size = 1.2f + (float)(rand()%8)*0.1f;
                sp.life = 1.2f + (float)(rand()%8)*0.1f;
                steamParticles.push_back(sp);
            }
        }
        for (auto& sp : steamParticles) {
            sp.x  += sp.vx * dt; sp.y += sp.vy * dt;
            sp.vy *= 0.985f;
            sp.vx += (float)(rand()%10-5)*0.4f*dt;
            sp.size += 0.6f*dt; sp.life -= dt;
        }
        for (int i=(int)steamParticles.size()-1; i>=0; i--)
            if (steamParticles[i].life<=0) steamParticles.erase(steamParticles.begin()+i);

        if (estado == PATEANDO_PELOTA) {
            pelotaJuego.actualizar(dt);
            if (kickCooldown > 0) kickCooldown -= dt;

            float currentPhase = sinf(anim * 1.5f);
            float footX = drawCx + currentPhase * 7.f;
            float footY = drawCy + 16.f;
            float dx    = pelotaJuego.x - footX;
            float dy    = pelotaJuego.y - footY;
            float dist  = sqrtf(dx*dx + dy*dy);

            if (dist < pelotaJuego.radius + 5.f && kickCooldown <= 0) {
                float force = 120.f;
                pelotaJuego.vx += (dx/dist)*force;
                pelotaJuego.vy += (dy/dist)*force - 40.f;
                pelotaJuego.aplicarSquash(0.25f, fabsf(dx)>fabsf(dy));
                kickCooldown = 0.3f;
                justKicked   = true;
            }
        }
    }

    void dibujar(Graphics& g, float cx, float cy) {
        drawCx = cx; drawCy = cy;
        float x = cx, y = cy;

        bool active = (estado == HABLANDO    || estado == PENSANDO    ||
                       estado == CAMINANDO   || estado == CELEBRANDO  ||
                       estado == BANDERAZO   || estado == JUGANDO     ||
                       estado == PATEANDO_PELOTA || estado == COMIENDO_PIZZA ||
                       estado == TOMANDO_MATE || estado == LLAMANDO_DEPAUL);

        float t           = anim;
        float breathScale = 1.0f + sinf(idleTimer*1.6f)*0.012f;
        bool  celebrating = (estado == CELEBRANDO);

        float bounce;
        if      (estado == CELEBRANDO)     bounce = fabsf(sinf(t))*1.8f;
        else if (estado == BANDERAZO)      bounce = fabsf(sinf(t))*1.2f;
        else if (estado == PATEANDO_PELOTA)bounce = fabsf(sinf(t))*1.5f;
        else if (estado == COMIENDO_PIZZA) bounce = fabsf(sinf(t*0.5f))*0.3f;
        else if (estado == LLAMANDO_DEPAUL && anim >= 21.0f) bounce = fabsf(sinf(t*0.8f))*0.5f;
        else if (estado == JUGANDO)        bounce = fabsf(sinf(t))*1.8f;
        else if (active)                   bounce = fabsf(sinf(t))*1.8f;
        else                               bounce = sinf(idleTimer*1.8f)*0.5f;

        y -= bounce + jumpY;

        float phase;
        if      (estado == BANDERAZO)      phase = sinf(t);
        else if (estado == JUGANDO)        phase = sinf(t*1.5f);
        else if (estado == PATEANDO_PELOTA)phase = sinf(t*1.5f);
        else if (estado == COMIENDO_PIZZA) phase = 0;
        else if (estado == TOMANDO_MATE)   phase = 0;
        else if (estado == LLAMANDO_DEPAUL) phase = (anim >= 21.0f) ? sinf((anim-21.0f)*1.5f) : 0;
        else if (active)                   phase = sinf(t);
        else                               phase = 0.f;

        if (estado == IDLE || estado == DURMIENDO)
            x += sinf(idleTimer*0.8f)*0.8f;

        // ── Brushes ──────────────────────────────────────────
        SolidBrush skin       (Color(255,225,190,170));
        SolidBrush hairCol    (Color(255, 70, 50, 30));
        SolidBrush beardCol   (Color(200, 60, 45, 25));
        SolidBrush jerseyWhite(Color(255,245,245,250));
        SolidBrush celeste    (Color(255,100,180,230));
        SolidBrush bootCol    (Color(255, 20, 20, 25));

        // ── Sombra ───────────────────────────────────────────
        SolidBrush shadow(Color(60,0,0,0));
        float shadowScale = 1.0f - (bounce+jumpY)*0.02f;
        g.FillEllipse(&shadow,
            (int)(cx-13*shadowScale), (int)(cy+16),
            (int)(26*shadowScale), 7);

        // ── Piernas ──────────────────────────────────────────
        float sL = phase, sR = -phase;
        if (estado == JUGANDO) { sL = phase*0.6f; sR = -phase*0.6f; }

        float hipY  = y + 10, hipLx = x-3.5f, hipRx = x+3.5f;
        float fLx   = hipLx + sL*7.f;
        float fLy   = hipY  + 16 - ((sL>0)?sL:0)*4.5f;
        float fRx   = hipRx + sR*7.f;
        float fRy   = hipY  + 16 - ((sR>0)?sR:0)*4.5f;
        float kLx   = (hipLx+fLx)*.5f + sL*2.5f;
        float kLy   = (hipY +fLy)*.5f - 1.f - ((sL>0)?sL:0)*1.5f;
        float kRx   = (hipRx+fRx)*.5f + sR*2.5f;
        float kRy   = (hipY +fRy)*.5f - 1.f - ((sR>0)?sR:0)*1.5f;

        Pen shorts(Color(255,30,50,120), 6);
        shorts.SetStartCap(LineCapRound); shorts.SetEndCap(LineCapRound);
        Pen socks(Color(255,240,240,245), 5);

        auto dLeg = [&](float hx,float hy, float kx,float ky,
                        float fx,float fy) {
            float midY = (hy+ky)*0.5f;
            g.DrawLine(&shorts, hx, hy, kx, midY);
            g.DrawLine(&socks,  kx, midY, fx, fy);
            Pen stripe(Color(255,100,180,230), 5);
            g.DrawLine(&stripe,
                (kx+fx)*0.5f, (midY+fy)*0.5f-2,
                (kx+fx)*0.5f, (midY+fy)*0.5f+2);
            g.FillEllipse(&bootCol, (int)fx-4, (int)fy-2, 9, 5);
        };

        if (sL <= sR) {
            dLeg(hipLx,hipY, kLx,kLy, fLx,fLy);
            dLeg(hipRx,hipY, kRx,kRy, fRx,fRy);
        } else {
            dLeg(hipRx,hipY, kRx,kRy, fRx,fRy);
            dLeg(hipLx,hipY, kLx,kLy, fLx,fLy);
        }

        // ── Torso ────────────────────────────────────────────
        float torsoStretch = breathScale;
        GraphicsPath jerseyPath;
        PointF jp[] = {
            PointF(x-8, y-8*torsoStretch),
            PointF(x+8, y-8*torsoStretch),
            PointF(x+7, y+11*torsoStretch),
            PointF(x-7, y+11*torsoStretch)
        };
        jerseyPath.AddPolygon(jp, 4);
        g.FillPath(&jerseyWhite, &jerseyPath);

        {
            GraphicsContainer c = g.BeginContainer();
            g.SetClip(&jerseyPath);
            for (int i = -2; i <= 2; i++)
                g.FillRectangle(&celeste,
                    RectF(x+i*5.f-1.5f, y-9*torsoStretch,
                          3.f, 22*torsoStretch));
            g.EndContainer(c);
        }

        FontFamily ffNum(L"Arial");
        Font numFont(&ffNum, 7, FontStyleBold, UnitPixel);
        SolidBrush numColor(Color(180,30,50,120));
        g.DrawString(L"10",-1,&numFont,
            PointF(x-4, y-2*torsoStretch), &numColor);
        g.FillEllipse(&skin, (int)x-3, (int)(y-10*torsoStretch), 6, 5);

        Pen sleeve (Color(255,100,180,230), 4);
        sleeve.SetStartCap(LineCapRound); sleeve.SetEndCap(LineCapRound);
        Pen forearm(Color(255,225,190,170), 4);
        forearm.SetStartCap(LineCapRound); forearm.SetEndCap(LineCapRound);

        // ── Brazo izquierdo ──────────────────────────────────
        float shLx = x-8, shLy = y-5*torsoStretch;
        float hLx, hLy, eLx, eLy;

        if (celebrating) {
            float pump  = fabsf(sinf(t*8.f))*3.f;
            float shake = sinf(t*15.f)*1.0f;
            hLx = shLx - 6 + shake; hLy = shLy - 15 - pump;
        } else if (estado == BANDERAZO) {
            float aL = -phase*1.2f;
            hLx = shLx - 2 + aL*5.f; hLy = shLy + 16 + fabsf(aL);
        } else if (estado == HABLANDO) {
            hLx = shLx - 2; hLy = shLy + 15;
        } else if (estado == JUGANDO) {
            hLx = shLx - 5 + sinf(t*2.f)*2.f; hLy = shLy + 8;
        } else if (estado == DURMIENDO) {
            hLx = shLx - 1; hLy = shLy + 15 + fminf(idleTimer*0.2f, 3.f);
        } else if (estado == COMIENDO_PIZZA) {
            hLx = shLx - 2; hLy = shLy + 15;
        } else if (estado == TOMANDO_MATE) {
            hLx = shLx - 9; hLy = shLy + 14;
        } else if (estado == LLAMANDO_DEPAUL) {
            if (anim < 14.0f) { hLx = shLx-2; hLy = shLy+15; }
            else { hLx = shLx-5+phase*3; hLy = shLy+8; }
        } else {
            float mult = (estado==CAMINANDO ? 1.2f : 1.f);
            float aL   = -phase*mult;
            hLx = shLx-2+aL*5.f; hLy = shLy+16+fabsf(aL);
        }

        eLx = (shLx+hLx)*.5f - 2.f;
        eLy = (shLy+hLy)*.5f + 1.f;
        g.DrawLine(&sleeve,  shLx, shLy, eLx, eLy);
        g.DrawLine(&forearm, eLx,  eLy,  hLx, hLy);
        g.FillEllipse(&skin, (int)hLx-3, (int)hLy-3, 6, 6);

        // ── Brazo derecho ────────────────────────────────────
        float shRx = x+8, shRy = y-5*torsoStretch;
        float handRx, handRy, elbowRx, elbowRy;

        if (celebrating) {
            float pump  = fabsf(sinf(t*8.f))*3.f;
            float shake = sinf(t*15.f)*1.0f;
            handRx  = shRx+6+shake; handRy  = shRy-15-pump;
            elbowRx = (shRx+handRx)*.5f+2.f;
            elbowRy = (shRy+handRy)*.5f+1.f;
        } else if (estado == BANDERAZO) {
            float flagSway = sinf(t*1.5f)*1.0f;
            handRx  = shRx+4+flagSway; handRy  = shRy-22;
            elbowRx = (shRx+handRx)*.5f+5.f;
            elbowRy = (shRy+handRy)*.5f-1.f;
        } else if (estado == HABLANDO) {
            float wave = sinf(t*6.f);
            handRx  = shRx+2+wave*4.f; handRy  = shRy-15+fabsf(wave)*2.f;
            elbowRx = (shRx+handRx)*.5f+3.f;
            elbowRy = (shRy+handRy)*.5f;
        } else if (estado == JUGANDO) {
            handRx  = shRx+5+sinf(t*2.f+1.f)*2.f; handRy  = shRy+8;
            elbowRx = (shRx+handRx)*.5f+2.f;
            elbowRy = (shRy+handRy)*.5f+1.f;
        } else if (estado == DURMIENDO) {
            float droop = fminf(idleTimer*0.3f, 6.f);
            handRx  = x+11; handRy  = shRy+10+droop;
            elbowRx = (shRx+handRx)*.5f+2.f;
            elbowRy = (shRy+handRy)*.5f+1.f;
        } else if (estado == COMIENDO_PIZZA) {
            if (anim < 7.0f) {
                float slideIn = 1.0f - anim/7.0f;
                handRx = shRx+10+slideIn*18; handRy = shRy+12;
            } else if (anim < 14.0f) {
                handRx = shRx+10; handRy = shRy+12;
            } else {
                float eat = sinf((anim-14.0f)*1.0f);
                handRx = shRx+4+eat*3.f;
                handRy = shRy+14-(eat+1.0f)*12;
            }
            elbowRx = (shRx+handRx)*.5f+3.f;
            elbowRy = (shRy+handRy)*.5f+1.f;
        } else if (estado == TOMANDO_MATE) {
            float mc = sinf(anim*0.7f);
            handRx  = shRx+5;
            handRy  = shRy+14-(mc+1.0f)*13;
            elbowRx = (shRx+handRx)*.5f+4.f;
            elbowRy = (shRy+handRy)*.5f+2.f;
        } else if (estado == LLAMANDO_DEPAUL) {
            if (anim < 14.0f) {
                handRx  = shRx+5; handRy  = shRy-17;
                elbowRx = (shRx+handRx)*.5f+4.f;
                elbowRy = (shRy+handRy)*.5f+2.f;
            } else {
                handRx  = shRx+5-phase*3; handRy  = shRy+8;
                elbowRx = (shRx+handRx)*.5f+2.f;
                elbowRy = (shRy+handRy)*.5f+1.f;
            }
        } else {
            float mult = (estado==CAMINANDO ? 1.2f : 1.f);
            float aR   = phase*mult;
            handRx  = shRx+2+aR*5.f; handRy  = shRy+16+fabsf(aR);
            elbowRx = (shRx+handRx)*.5f+2.f;
            elbowRy = (shRy+handRy)*.5f+1.f;
        }

        g.DrawLine(&sleeve,  shRx,   shRy,   elbowRx, elbowRy);
        g.DrawLine(&forearm, elbowRx,elbowRy, handRx,  handRy);
        g.FillEllipse(&skin, (int)handRx-3, (int)handRy-3, 6, 6);

        // ── Bandera ──────────────────────────────────────────
        if (estado == BANDERAZO)
            dibujarBandera(g, handRx, handRy,
                           waveTime, 1.8f+mouseInfluence*0.3f);

        // ── Cabeza ───────────────────────────────────────────
        float headTilt = sinf(idleTimer*1.2f)*2.f;
        float headBob  = sinf(idleTimer*2.f)*0.6f;
        float headCy   = y - 18 + headBob;
        float headCx   = x + headTilt*0.3f;

        Matrix oldMatrix; g.GetTransform(&oldMatrix);
        float neckX = x, neckY = y-10*torsoStretch;
        g.TranslateTransform(neckX, neckY);
        g.RotateTransform(headTilt);
        g.TranslateTransform(-neckX, -neckY);

        g.FillEllipse(&skin, (int)headCx-8, (int)headCy-8, 17, 19);

        // Barba
        GraphicsPath beardPath;
        beardPath.AddBezier(PointF(headCx-8,headCy),PointF(headCx-8,headCy+6),PointF(headCx-5,headCy+11),PointF(headCx,headCy+12));
        beardPath.AddBezier(PointF(headCx,headCy+12),PointF(headCx+5,headCy+11),PointF(headCx+8,headCy+6),PointF(headCx+8,headCy));
        beardPath.AddBezier(PointF(headCx+8,headCy),PointF(headCx+6,headCy+4),PointF(headCx+3,headCy+8),PointF(headCx,headCy+9));
        beardPath.AddBezier(PointF(headCx,headCy+9),PointF(headCx-3,headCy+8),PointF(headCx-6,headCy+4),PointF(headCx-8,headCy));
        beardPath.CloseFigure();
        g.FillPath(&beardCol, &beardPath);

        // Cabello base
        GraphicsPath baseHair;
        baseHair.AddArc((REAL)headCx-9,(REAL)headCy-11,(REAL)19,(REAL)14,(REAL)180,(REAL)180);
        baseHair.AddLine(PointF(headCx+9,headCy-4), PointF(headCx+9,headCy-1));
        baseHair.AddLine(PointF(headCx-9,headCy-1), PointF(headCx-9,headCy-4));
        baseHair.CloseFigure();
        g.FillPath(&hairCol, &baseHair);

        float tuftSway = sinf(idleTimer*2.5f)*1.5f
                       + (active ? sinf(t*4.f)*1.f : 0.f);
        GraphicsPath frontTuft;
        frontTuft.AddBezier(
            PointF(headCx-2,headCy-10),
            PointF(headCx-1+tuftSway,headCy-6),
            PointF(headCx+1+tuftSway,headCy-3),
            PointF(headCx+3+tuftSway*0.5f,headCy-1));
        frontTuft.AddBezier(
            PointF(headCx+3+tuftSway*0.5f,headCy-1),
            PointF(headCx+2+tuftSway,headCy-4),
            PointF(headCx+4,headCy-8),
            PointF(headCx+3,headCy-10));
        frontTuft.CloseFigure();
        g.FillPath(&hairCol, &frontTuft);

        // ── Ojos ─────────────────────────────────────────────
        bool blink = false;
        if (estado==CAMINANDO || estado==BANDERAZO || estado==JUGANDO)
            blink = false;
        else if (estado == DURMIENDO)
            blink = true;
        else {
            float bc = fmodf(blinkTimer, 4.0f);
            blink = (bc < 0.12f) || (bc > 0.28f && bc < 0.40f);
        }

        SolidBrush eyeW(Color(255,252,248));
        SolidBrush eyeP(Color(255, 80, 55, 30));

        if (blink) {
            Pen el(Color(255,200,170,155), 2);
            g.DrawLine(&el, headCx-6, headCy-1, headCx-1, headCy-1);
            g.DrawLine(&el, headCx+1, headCy-1, headCx+6, headCy-1);
        } else {
            float lookX = lookTargetX, lookY = lookTargetY;
            if (estado == PENSANDO) { lookX = 0.0f;  lookY = -1.5f; }
            if (estado == JUGANDO)  { lookX = (x+phase*7.f-headCx)*0.15f; lookY = 1.0f; }
            if (estado == COMIENDO_PIZZA) { lookX = 1.2f; lookY = 0.5f; }
            if (estado == TOMANDO_MATE) {
                float mc = sinf(anim*0.7f);
                lookX = 1.0f; lookY = (mc>0) ? -1.2f : 0.3f;
            }
            if (estado == LLAMANDO_DEPAUL) {
                if (anim < 14.0f) { lookX = 1.0f; lookY = -1.2f; }
                else              { lookX = 1.5f; lookY =  0.0f; }
            }
            if (lookX >  1.5f) lookX =  1.5f;
            if (lookX < -1.5f) lookX = -1.5f;
            if (lookY >  1.5f) lookY =  1.5f;
            if (lookY < -1.5f) lookY = -1.5f;

            g.FillEllipse(&eyeW, (int)(headCx-6), (int)headCy-3, 5, 4);
            g.FillEllipse(&eyeW, (int)(headCx+1), (int)headCy-3, 5, 4);
            g.FillEllipse(&eyeP, (int)(headCx-5+lookX),(int)(headCy-2+lookY),3,3);
            g.FillEllipse(&eyeP, (int)(headCx+2+lookX),(int)(headCy-2+lookY),3,3);
            SolidBrush shine(Color(255,255,255,200));
            g.FillEllipse(&shine,(int)(headCx-5+lookX),(int)(headCy-3+lookY),1,1);
            g.FillEllipse(&shine,(int)(headCx+2+lookX),(int)(headCy-3+lookY),1,1);
        }

        // ── Boca ─────────────────────────────────────────────
        float mouthOpen = 0.f;
        if      (estado == HABLANDO)    mouthOpen = fabsf(sinf(anim*4.f))*2.0f;
        else if (celebrating)           mouthOpen = 2.5f+fabsf(sinf(anim*3.f))*1.0f;
        else if (estado == JUGANDO)     mouthOpen = fabsf(sinf(anim*2.f))*0.8f;
        else if (estado == PATEANDO_PELOTA) mouthOpen = fabsf(sinf(anim*2.f))*1.2f;
        else if (estado == COMIENDO_PIZZA && anim >= 14.0f) {
            float eat = sinf((anim-14.0f)*1.0f);
            mouthOpen = (eat>0.3f) ? (eat-0.3f)*4.0f : 0;
        }
        else if (estado == TOMANDO_MATE) {
            float mc = sinf(anim*0.7f);
            mouthOpen = (mc>0.2f) ? (mc-0.2f)*1.5f : 0;
        }
        else if (estado == LLAMANDO_DEPAUL && anim < 14.0f) {
            mouthOpen = fabsf(sinf(anim*3.f))*1.5f;
        }

        if (mouthOpen > 0.3f) {
            GraphicsPath mouthPath;
            float mw = 7.f;
            float mx2 = headCx - mw*0.5f;
            float my2 = headCy + 4.5f;
            mouthPath.AddBezier(
                PointF(mx2,my2),
                PointF(mx2+mw*0.3f,my2-0.8f),
                PointF(mx2+mw*0.7f,my2-0.8f),
                PointF(mx2+mw,my2));
            mouthPath.AddBezier(
                PointF(mx2+mw,my2),
                PointF(mx2+mw*0.7f,my2+mouthOpen+1.f),
                PointF(mx2+mw*0.3f,my2+mouthOpen+1.f),
                PointF(mx2,my2));
            mouthPath.CloseFigure();
            SolidBrush mouthInner(Color(255,80,30,30));
            g.FillPath(&mouthInner, &mouthPath);
            Pen lipBorder(Color(255,190,90,100), 1.2f);
            lipBorder.SetStartCap(LineCapRound);
            lipBorder.SetEndCap(LineCapRound);
            g.DrawPath(&lipBorder, &mouthPath);
            if (mouthOpen > 1.5f) {
                SolidBrush teeth(Color(255,245,240,230));
                g.FillRectangle(&teeth,
                    RectF(mx2+1.f, my2,
                          mw-2.f, fminf(mouthOpen*0.4f,2.f)));
            }
        } else {
            GraphicsPath closedLips;
            float mw  = 6.f;
            float mx2 = headCx - mw*0.5f;
            float my2 = headCy + 5.f;
            closedLips.AddBezier(
                PointF(mx2,my2),
                PointF(mx2+mw*0.3f,my2-1.f),
                PointF(mx2+mw*0.5f,my2-0.5f),
                PointF(mx2+mw*0.5f,my2-0.5f));
            closedLips.AddBezier(
                PointF(mx2+mw*0.5f,my2-0.5f),
                PointF(mx2+mw*0.7f,my2-1.f),
                PointF(mx2+mw,my2),
                PointF(mx2+mw,my2));
            closedLips.AddBezier(
                PointF(mx2+mw,my2),
                PointF(mx2+mw*0.7f,my2+1.5f),
                PointF(mx2+mw*0.3f,my2+1.5f),
                PointF(mx2,my2));
            closedLips.CloseFigure();
            SolidBrush lipColor(Color(255,190,90,100));
            g.FillPath(&lipColor, &closedLips);
        }

        if (estado == DURMIENDO) {
            FontFamily ffZ(L"Arial");
            Font zFont(&ffZ,
                8+(int)(fmodf(blinkTimer,2.0f)*2),
                FontStyleBold, UnitPixel);
            SolidBrush zCol(Color(200,100,180,230));
            g.DrawString(L"Z",-1,&zFont,
                PointF(headCx+10, headCy-15-fmodf(blinkTimer,2.0f)*10),
                &zCol);
        }

        g.SetTransform(&oldMatrix);

        // ═════════════════════════════════════════════════════
        // PROPS
        // ═════════════════════════════════════════════════════

        // ── Pizza ────────────────────────────────────────────
        if (estado == COMIENDO_PIZZA) {
            if (anim < 14.0f) {
                float openness = fmaxf(0, (anim-7.0f)/7.0f);
                SolidBrush boxRed(Color(255,185,45,35));
                g.FillRectangle(&boxRed, (int)(handRx-11),(int)(handRy-1), 22,11);
                Pen boxBrd(Color(255,150,30,20), 0.8f);
                g.DrawRectangle(&boxBrd, (int)(handRx-11),(int)(handRy-1), 22,11);
                if (openness > 0.01f) {
                    Matrix lm; g.GetTransform(&lm);
                    g.TranslateTransform(handRx, handRy-1);
                    g.RotateTransform(-openness*65.0f);
                    g.TranslateTransform(-handRx, -(handRy-1));
                    SolidBrush lidCol(Color(255,195,55,40));
                    g.FillRectangle(&lidCol,(int)(handRx-11),(int)(handRy-11),22,10);
                    Pen lidBrd(Color(255,150,30,20), 0.8f);
                    g.DrawRectangle(&lidBrd,(int)(handRx-11),(int)(handRy-11),22,10);
                    g.SetTransform(&lm);
                }
                if (openness < 0.5f) {
                    FontFamily ffP(L"Arial");
                    Font pFont(&ffP, 4, FontStyleBold, UnitPixel);
                    SolidBrush pTxt(Color(255,255,220,180));
                    StringFormat pFmt;
                    pFmt.SetAlignment(StringAlignmentCenter);
                    pFmt.SetLineAlignment(StringAlignmentCenter);
                    g.DrawString(L"PIZZA",-1,&pFont,
                        PointF(handRx,handRy+4),&pFmt,&pTxt);
                }
            } else {
                float ppx = handRx, ppy = handRy+4;
                SolidBrush cheese   (Color(255,245,210,90));
                SolidBrush crustC   (Color(255,190,130,70));
                SolidBrush pepperoni(Color(255,185,35,25));
                SolidBrush herb     (Color(255,60,150,50));
                PointF pts[3]={PointF(ppx,ppy-7),PointF(ppx-5,ppy+5),PointF(ppx+5,ppy+5)};
                g.FillPolygon(&cheese, pts, 3);
                Pen crustPen(Color(255,190,130,70), 2.5f);
                crustPen.SetStartCap(LineCapRound);
                crustPen.SetEndCap(LineCapRound);
                g.DrawLine(&crustPen, ppx-5,ppy+5, ppx+5,ppy+5);
                g.FillEllipse(&pepperoni,(int)(ppx-2),(int)ppy,3,3);
                g.FillEllipse(&pepperoni,(REAL)(ppx+1),(REAL)(ppy+2),(REAL)2.5f,(REAL)2.5f);
                g.FillEllipse(&herb,(REAL)(ppx-3),(REAL)(ppy+3),(REAL)1.5f,(REAL)1.5f);
                g.FillEllipse(&herb,(REAL)(ppx+3),(REAL)(ppy+1),(REAL)1.5f,(REAL)1.5f);

                float pulling = -sinf((anim-14.0f)*1.0f);
                if (pulling > 0.3f) {
                    Pen cheeseStr(Color(180,240,200,80), 0.7f);
                    cheeseStr.SetStartCap(LineCapRound);
                    float mouthX = headCx, mouthY = headCy+5;
                    for (int i = 0; i < 3; i++) {
                        float sx  = ppx-2+i*2, sy = ppy-5;
                        float cpx2= (sx+mouthX)*0.5f+sinf(anim*6+i*2)*2;
                        float cpy2= (sy+mouthY)*0.5f-2;
                        g.DrawBezier(&cheeseStr,sx,sy,cpx2-1,cpy2,cpx2+1,cpy2-2,mouthX-1+i,mouthY);
                    }
                }
            }
            if (anim >= 14.0f) {
                float eat = sinf((anim-14.0f)*1.0f);
                if (eat>0.7f && fmodf(anim-14.0f,10.0f)<2.0f) {
                    SolidBrush txtCol(Color(200,255,220,50));
                    FontFamily ffE(L"Arial");
                    Font eFont(&ffE,7,FontStyleBold,UnitPixel);
                    g.DrawString(L"¡Ñam!",-1,&eFont,
                        PointF(x-10,y-42-sinf(anim)*2),&txtCol);
                }
            }
        }

        // ── Mate ─────────────────────────────────────────────
        if (estado == TOMANDO_MATE) {
            float mx2 = handRx, my2 = handRy+4;
            SolidBrush mateCol(Color(255,95,75,45));
            SolidBrush rimCol (Color(255,140,120,80));
            g.FillEllipse(&mateCol,(int)(mx2-4),(int)(my2-2),8,10);
            g.FillEllipse(&rimCol, (int)(mx2-4.5f),(int)(my2-3),9,3);
            Pen bombPen(Color(255,195,195,205), 1.5f);
            bombPen.SetStartCap(LineCapRound);
            g.DrawLine(&bombPen, mx2+1,my2-4, mx2+3,my2-11);
            SolidBrush tipC(Color(255,205,205,215));
            g.FillEllipse(&tipC,(int)(mx2+3)-1,(int)(my2-12),2,2);

            float tx2 = hLx, ty2 = hLy-6;
            SolidBrush thermoBody(Color(255,175,175,185));
            SolidBrush thermoCap (Color(255, 35, 35, 40));
            g.FillRectangle(&thermoBody,(int)(tx2-3),(int)(ty2-8),6,16);
            g.FillRectangle(&thermoCap, (int)(tx2-3.5f),(int)(ty2-10),7,3);
            SolidBrush hiLite(Color(90,215,215,225));
            g.FillRectangle(&hiLite,(REAL)(tx2-1),(REAL)(ty2-7),(REAL)1.5f,(REAL)13);

            FontFamily ffSt(L"Arial");
            Font stFont(&ffSt, 2.5f, FontStyleRegular, UnitPixel);
            SolidBrush stCol(Color(120,50,50,55));
            StringFormat stFmt;
            stFmt.SetAlignment(StringAlignmentCenter);
            stFmt.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"STANLEY",-1,&stFont,PointF(tx2,ty2),&stFmt,&stCol);

            float mc = sinf(anim*0.7f);
            if (mc>0.6f && fmodf(anim,7.0f)<1.5f) {
                SolidBrush txtCol(Color(190,100,180,230));
                FontFamily ffM(L"Arial");
                Font mFont(&ffM,5,FontStyleItalic,UnitPixel);
                g.DrawString(L"Ahhh...",-1,&mFont,
                    PointF(x-8,y-40-sinf(anim*0.5f)*2),&txtCol);
            }
        }

        // ── Celular ──────────────────────────────────────────
        if (estado == LLAMANDO_DEPAUL && anim < 14.0f) {
            float ppx2 = handRx, ppy2 = handRy-2;
            bool ringing = (fmodf(anim,1.5f) < 0.75f);
            SolidBrush phoneBody(Color(255,25,25,30));
            g.FillRectangle(&phoneBody,(int)(ppx2-3),(int)(ppy2-6),6,11);
            SolidBrush screenCol(ringing
                ? Color(255,80,200,120) : Color(255,35,35,45));
            g.FillRectangle(&screenCol,(int)(ppx2-2.5f),(int)(ppy2-5),5,8);
            SolidBrush notchCol(Color(255,15,15,20));
            g.FillRectangle(&notchCol,(int)(ppx2-1),(int)(ppy2-5),2,1);
            if (ringing) {
                Pen vibPen(Color(180,80,200,120), 0.7f);
                vibPen.SetStartCap(LineCapRound);
                for (int i=0; i<3; i++) {
                    float off = (i-1)*3.f;
                    g.DrawLine(&vibPen, ppx2+4,ppy2-4+off, ppx2+7,ppy2-5+off);
                    g.DrawLine(&vibPen, ppx2-4,ppy2-4+off, ppx2-7,ppy2-5+off);
                }
            }
            float bubA = 180 + sinf(anim*3)*75;
            SolidBrush bubBg(Color((int)bubA,255,255,255));
            g.FillEllipse(&bubBg,(int)(headCx+8),(int)(headCy-22),24,12);
            g.FillEllipse(&bubBg,(int)(headCx+12),(int)(headCy-12),4,4);
            g.FillEllipse(&bubBg,(int)(headCx+10),(int)(headCy-7),3,3);
            FontFamily ffT(L"Arial");
            Font tFont(&ffT, 3.5f, FontStyleRegular, UnitPixel);
            SolidBrush tCol(Color(255,30,30,35));
            g.DrawString(L"Llamando...",-1,&tFont,
                PointF(headCx+9,headCy-20),&tCol);
        }

        // ── Pelota jugando (simple) ───────────────────────────
        if (estado == JUGANDO) {
            float groundY      = y + 16.f;
            float footOffsetX  = phase * 7.f;
            float ballX2       = x + footOffsetX + 3.f;
            float ballBounce2  = fabsf(sinf(t*6.f))*1.5f;
            float ballRadius2  = 5.f;
            float ballY2       = groundY - ballRadius2 - ballBounce2;

            float shadowAlpha2 = (BYTE)(60.f*(1.f-ballBounce2*0.2f));
            SolidBrush ballShadow(Color(shadowAlpha2,0,0,0));
            float sw = ballRadius2*2.f*(1.f-ballBounce2*0.1f);
            g.FillEllipse(&ballShadow,
                (int)(ballX2-sw*0.5f),(int)(groundY-1),(int)sw,2);

            float ballRotation2 = phase*400.f;
            Matrix ballMatrix; g.GetTransform(&ballMatrix);
            g.TranslateTransform(ballX2, ballY2);
            g.RotateTransform(ballRotation2);
            g.TranslateTransform(-ballX2, -ballY2);

            SolidBrush ballWhite(Color(255,245,245,255));
            g.FillEllipse(&ballWhite,
                (int)(ballX2-ballRadius2),(int)(ballY2-ballRadius2),
                (int)(ballRadius2*2),(int)(ballRadius2*2));

            SolidBrush pentagon(Color(255,20,20,25));
            PointF cp[5];
            for (int i=0;i<5;i++){
                float a=(float)i*72.f*(float)PI/180.f - 90.f*(float)PI/180.f;
                cp[i].X = ballX2+cosf(a)*2.5f;
                cp[i].Y = ballY2+sinf(a)*2.5f;
            }
            g.FillPolygon(&pentagon,cp,5);
            for (int p=0;p<3;p++){
                float ba=(float)p*120.f*(float)PI/180.f;
                float ppx3=ballX2+cosf(ba)*4.f, ppy3=ballY2+sinf(ba)*4.f;
                PointF sp[5];
                for (int i=0;i<5;i++){
                    float a=(float)i*72.f*(float)PI/180.f+ba;
                    sp[i].X=ppx3+cosf(a)*1.5f;
                    sp[i].Y=ppy3+sinf(a)*1.5f;
                }
                g.FillPolygon(&pentagon,sp,5);
            }
            SolidBrush shine2(Color(120,255,255,255));
            g.FillEllipse(&shine2,(int)(ballX2-3),(int)(ballY2-4),3,2);
            Pen outline2(Color(100,0,0,0), 0.5f);
            g.DrawEllipse(&outline2,
                (int)(ballX2-ballRadius2),(int)(ballY2-ballRadius2),
                (int)(ballRadius2*2),(int)(ballRadius2*2));
            g.SetTransform(&ballMatrix);
        }

        // ── Pelota física (patear) ────────────────────────────
        if (estado == PATEANDO_PELOTA) {
            pelotaJuego.dibujar(g);
            float speed = sqrtf(pelotaJuego.vx*pelotaJuego.vx
                               +pelotaJuego.vy*pelotaJuego.vy);
            if (speed > 150.f) {
                golFlashTimer += 0.05f;
                if (fmodf(golFlashTimer,2.0f) < 1.0f) {
                    float ga = fminf(speed*0.5f, 200.f);
                    SolidBrush golTxt(Color((int)ga,255,220,50));
                    FontFamily ffG(L"Arial");
                    Font gFont(&ffG,7,FontStyleBold,UnitPixel);
                    StringFormat gFmt;
                    gFmt.SetAlignment(StringAlignmentCenter);
                    g.DrawString(L"¡PUAJ!",-1,&gFont,
                        PointF(pelotaJuego.x,pelotaJuego.y-15),
                        &gFmt,&golTxt);
                }
            } else {
                golFlashTimer = 0;
            }
        }

        // ── Burbujas pensar ──────────────────────────────────
        if (estado == PENSANDO) {
            float bubbleT = fmodf(idleTimer,1.5f);
            for (int i=0;i<3;i++) {
                float bt = bubbleT + i*0.4f;
                if (bt < 1.5f) {
                    float alpha = 255.0f*(1.0f-bt/1.5f);
                    float sz    = 1.5f+i*1.5f;
                    SolidBrush bb(Color((int)alpha,255,255,255));
                    g.FillEllipse(&bb,
                        headCx+12+i*6-sz/2,
                        headCy-12-i*6-bt*8-sz/2, sz,sz);
                }
            }
        }

        // ── Vapor ────────────────────────────────────────────
        if (estado == TOMANDO_MATE) {
            for (auto& sp : steamParticles) {
                float alpha = fminf(sp.life*2.0f,1.0f)*80.0f;
                SolidBrush steamCol(Color((int)alpha,235,235,240));
                g.FillEllipse(&steamCol,
                    (int)(sp.x-sp.size/2),(int)(sp.y-sp.size/2),
                    (int)sp.size,(int)sp.size);
            }
        }

        // ── Confeti + GOL ─────────────────────────────────────
        if (celebrating) {
            Color confColors[] = {
                Color(255,100,180,230), Color(255,245,245,250),
                Color(255,218,175, 42), Color(255, 30, 50,120),
                Color(255,200,100,110)
            };
            for (auto& c : confeti) {
                float alpha = fminf(c.life*1.5f,1.0f)*255.0f;
                Color base  = confColors[c.colorIdx];
                Color col((int)alpha,base.GetR(),base.GetG(),base.GetB());
                SolidBrush brush(col);
                Matrix cm; g.GetTransform(&cm);
                g.TranslateTransform(c.x,c.y);
                g.RotateTransform(c.rot);
                if (c.circle)
                    g.FillEllipse(&brush,-c.size/2,-c.size/2,c.size,c.size);
                else
                    g.FillRectangle(&brush,-c.size/2,-c.size/4,c.size,c.size/2);
                g.SetTransform(&cm);
            }
            if (anim > 0.5f) {
                float gp = (anim-0.5f)*3.f;
                float gs = 1.0f+sinf(gp*3.f)*0.25f;
                float ga = fminf(gp*2.f,1.0f)*255.f;
                float gy = y-45-sinf(gp*2.f)*3.f;
                Matrix gm; g.GetTransform(&gm);
                g.TranslateTransform(x,gy);
                g.ScaleTransform(gs,gs);
                FontFamily ffG(L"Arial");
                Font gFont(&ffG,11,FontStyleBold,UnitPixel);
                StringFormat fmt;
                fmt.SetAlignment(StringAlignmentCenter);
                fmt.SetLineAlignment(StringAlignmentCenter);
                SolidBrush gOut(Color((int)ga,30,50,120));
                float offs[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
                for (auto& o : offs)
                    g.DrawString(L"¡GOL!",-1,&gFont,PointF(o[0],o[1]),&fmt,&gOut);
                SolidBrush gTxt(Color((int)ga,255,220,50));
                g.DrawString(L"¡GOL!",-1,&gFont,PointF(0,0),&fmt,&gTxt);
                g.SetTransform(&gm);
            }
        }

        // ── Banderazo texto ──────────────────────────────────
        if (estado == BANDERAZO) {
            float bt    = fmodf(idleTimer,2.0f);
            float alpha = 200.0f+sinf(bt*(float)PI)*55.0f;
            SolidBrush bCol(Color((int)alpha,100,180,230));
            FontFamily ffB(L"Arial");
            Font bFont(&ffB,6,FontStyleBold,UnitPixel);
            StringFormat bFmt;
            bFmt.SetAlignment(StringAlignmentCenter);
            bFmt.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"VAMOS\nARGENTINA!",-1,&bFont,
                PointF(x,y-42-sinf(bt*(float)PI)*3),&bFmt,&bCol);
        }

        // ── De Paul + pases ──────────────────────────────────
        if (estado == LLAMANDO_DEPAUL && anim >= 21.0f) {
            float depaulX    = 106.0f;
            float depaulY    = 80.0f;
            float depaulScale= 0.55f;
            float depaulAnim = anim - 21.0f;

            dibujarDePaul(g, depaulX, depaulY, depaulAnim, depaulScale);

            if (fmodf(depaulAnim,6.0f) < 3.5f) {
                float fadeIn = fminf(depaulAnim/3.0f, 1.0f);
                int   ba2    = (int)(200*fadeIn);
                SolidBrush dbub(Color(ba2,255,255,255));
                g.FillEllipse(&dbub,(int)(depaulX-14),(int)(depaulY-24),28,11);
                g.FillEllipse(&dbub,(int)(depaulX-5), (int)(depaulY-15),4,4);
                g.FillEllipse(&dbub,(int)(depaulX-3), (int)(depaulY-11),3,3);
                FontFamily ffD(L"Arial");
                Font dFont(&ffD,4,FontStyleBold,UnitPixel);
                SolidBrush dCol(Color(ba2,30,50,120));
                StringFormat dFmt;
                dFmt.SetAlignment(StringAlignmentCenter);
                dFmt.SetLineAlignment(StringAlignmentCenter);
                g.DrawString(L"¡Dale Leo!",-1,&dFont,
                    PointF(depaulX,depaulY-20),&dFmt,&dCol);
            }

            float passT    = depaulAnim*0.15f;
            float passNorm = (sinf(passT)+1.0f)*0.5f;
            float ballX3   = cx+5+(depaulX-cx-5)*passNorm;
            float groundY2 = cy+16.0f;
            float arcHeight= sinf(passNorm*(float)PI)*28.0f;
            float ballY3   = groundY2-5.0f-arcHeight;
            float ballR    = 4.0f;

            float shA = (float)(50*(1.0f-arcHeight/35.0f));
            if (shA < 0) shA = 0;
            SolidBrush bs(Color((BYTE)shA,0,0,0));
            float bsw = ballR*2*(1.0f-arcHeight*0.015f);
            g.FillEllipse(&bs,(int)(ballX3-bsw*0.5f),(int)(groundY2-1),(int)bsw,2);

            float ballRot3 = passT*200;
            Matrix bm; g.GetTransform(&bm);
            g.TranslateTransform(ballX3,ballY3);
            g.RotateTransform(ballRot3);
            g.TranslateTransform(-ballX3,-ballY3);

            SolidBrush ballW(Color(255,245,245,255));
            g.FillEllipse(&ballW,
                (int)(ballX3-ballR),(int)(ballY3-ballR),
                (int)(ballR*2),(int)(ballR*2));
            SolidBrush pentC(Color(255,20,20,25));
            PointF pc[5];
            for (int i=0;i<5;i++){
                float a=(float)i*72.f*(float)PI/180.f-90.f*(float)PI/180.f;
                pc[i].X=ballX3+cosf(a)*2.f;
                pc[i].Y=ballY3+sinf(a)*2.f;
            }
            g.FillPolygon(&pentC,pc,5);
            SolidBrush bshine(Color(100,255,255,255));
            g.FillEllipse(&bshine,(int)(ballX3-2),(int)(ballY3-3),2,2);
            Pen bout(Color(80,0,0,0), 0.5f);
            g.DrawEllipse(&bout,
                (int)(ballX3-ballR),(int)(ballY3-ballR),
                (int)(ballR*2),(int)(ballR*2));
            g.SetTransform(&bm);

            if (arcHeight > 10.0f) {
                float ta = fminf((arcHeight-10.0f)/15.0f,1.0f)*200;
                SolidBrush passTxt(Color((int)ta,255,220,50));
                FontFamily ffP(L"Arial");
                Font pFont(&ffP,5,FontStyleBold,UnitPixel);
                StringFormat pFmt;
                pFmt.SetAlignment(StringAlignmentCenter);
                g.DrawString(L"PASE!",-1,&pFont,
                    PointF(ballX3,ballY3-8),&pFmt,&passTxt);
            }
        }
    } // fin dibujar
};   // fin clase

// ============================================================
// AVI EXPORTER
// ============================================================
class AviExporter {
    struct AviContext {
        PAVIFILE   pFile   = NULL;
        PAVISTREAM pStream = NULL;
        ~AviContext() {
            if (pStream) AVIStreamRelease(pStream);
            if (pFile)   AVIFileRelease(pFile);
            AVIFileExit();
        }
    };
public:
    static bool Export(MessiConBandera& agente, const wchar_t* path,
                       int frames=60, int fps=30, int w=256, int h=256)
    {
        AVIFileInit();
        AviContext ctx;
        auto estadoPrevio = agente.getEstado();
        agente.setEstado(MessiConBandera::HABLANDO);
        float dt = 1.f / fps;

        if (AVIFileOpenW(&ctx.pFile,path,OF_CREATE|OF_WRITE,NULL) != 0)
            { agente.setEstado(estadoPrevio); return false; }

        AVISTREAMINFO si = {};
        si.fccType              = streamtypeVIDEO;
        si.fccHandler           = 0;
        si.dwScale              = 1;
        si.dwRate               = fps;
        si.dwSuggestedBufferSize= w*h*3;
        si.rcFrame              = {0,0,(SHORT)w,(SHORT)h};

        if (AVIFileCreateStream(ctx.pFile,&ctx.pStream,&si) != 0)
            { agente.setEstado(estadoPrevio); return false; }

        BITMAPINFOHEADER bih = {};
        bih.biSize        = sizeof(bih);
        bih.biWidth       = w; bih.biHeight = h;
        bih.biPlanes      = 1; bih.biBitCount = 24;
        bih.biCompression = BI_RGB;
        bih.biSizeImage   = ((w*3+3)/4)*4*h;

        if (AVIStreamSetFormat(ctx.pStream,0,&bih,sizeof(bih)) != 0)
            { agente.setEstado(estadoPrevio); return false; }

        for (int f = 0; f < frames; f++) {
            Gdiplus::Bitmap  bmp(w, h, PixelFormat24bppRGB);
            Gdiplus::Graphics gg(&bmp);
            gg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gg.Clear(Gdiplus::Color(0,0,0,0));
            gg.ScaleTransform((float)w/128.f,(float)h/128.f);
            agente.dibujar(gg, 64.f, 80.f);
            agente.actualizar(dt);

            Gdiplus::BitmapData bmpData;
            Gdiplus::Rect rect(0,0,w,h);
            bmp.LockBits(&rect,Gdiplus::ImageLockModeRead,
                         PixelFormat24bppRGB,&bmpData);
            int stride = bih.biSizeImage / h;
            std::vector<BYTE> rgbBuf(bih.biSizeImage, 0);
            for (int row=0;row<h;row++)
                memcpy(&rgbBuf[(h-1-row)*stride],
                       (BYTE*)bmpData.Scan0+row*bmpData.Stride, w*3);
            bmp.UnlockBits(&bmpData);
            LONG written = 0;
            if (AVIStreamWrite(ctx.pStream,f,1,rgbBuf.data(),
                bih.biSizeImage,AVIIF_KEYFRAME,NULL,&written) != AVIERR_OK)
                break;
        }
        agente.setEstado(estadoPrevio);
        return true;
    }
};

// ============================================================
// VENTANA Y MENÚ
// ============================================================
static constexpr UINT ID_TRAY_SALUDAR       = 3001;
static constexpr UINT ID_TRAY_CELEBRAR      = 3002;
static constexpr UINT ID_TRAY_EXPORT_AVI    = 3003;
static constexpr UINT ID_TRAY_MINIMIZAR     = 3004;
static constexpr UINT ID_TRAY_PROYECTOS     = 3005;
static constexpr UINT ID_TRAY_SALIR         = 3006;
static constexpr UINT ID_TRAY_BANDERAZO     = 3007;
static constexpr UINT ID_TRAY_JUGAR         = 3008;
static constexpr UINT ID_TRAY_PIZZA         = 3009;
static constexpr UINT ID_TRAY_MATE          = 3010;
static constexpr UINT ID_TRAY_DEPAUL        = 3011;
static constexpr UINT ID_TRAY_PATEAR_PELOTA = 3012;

MessiConBandera g_agente;
HWND   hwnd        = NULL;
NOTIFYICONDATAA g_nid  = {};
HMENU  g_hTrayMenu = NULL;

LRESULT CALLBACK AgentWndProc(HWND hwnd, UINT msg,
                               WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE:
        SetTimer(hwnd, WM_AGENT_TIMER, 16, NULL);
        return 0;

    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hwnd,msg,wParam,lParam);
        return (hit==HTCLIENT) ? HTCAPTION : hit;
    }

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd,&rc);
        HBRUSH hMag = CreateSolidBrush(RGB(255,0,255));
        FillRect((HDC)wParam,&rc,hMag);
        DeleteObject(hMag);
        return TRUE;
    }

    case WM_ENTERSIZEMOVE:
        if (g_agente.getEstado() != MessiConBandera::HABLANDO  &&
            g_agente.getEstado() != MessiConBandera::PENSANDO  &&
            g_agente.getEstado() != MessiConBandera::BANDERAZO &&
            g_agente.getEstado() != MessiConBandera::JUGANDO   &&
            g_agente.getEstado() != MessiConBandera::PATEANDO_PELOTA)
            g_agente.setEstado(MessiConBandera::CAMINANDO);
        return 0;

    case WM_EXITSIZEMOVE:
        if (g_agente.getEstado() == MessiConBandera::CAMINANDO)
            g_agente.setEstado(MessiConBandera::IDLE);
        return 0;

    case WM_TIMER:
        if (wParam == WM_AGENT_TIMER) {
            if (g_agente.getEstado() == MessiConBandera::BANDERAZO) {
                RECT rc; GetWindowRect(hwnd,&rc);
                int screenW = GetSystemMetrics(SM_CXSCREEN);
                int newX = rc.left + (int)(g_banderazoDir*160.0f*0.016f);
                if (newX+AGENT_SIZE > screenW) { g_banderazoDir=-1.0f; newX=screenW-AGENT_SIZE; }
                if (newX < 0)                  { g_banderazoDir= 1.0f; newX=0; }
                SetWindowPos(hwnd,NULL,newX,rc.top,0,0,SWP_NOSIZE|SWP_NOZORDER);
            }
            g_agente.actualizar(0.016f);
            InvalidateRect(hwnd,NULL,FALSE);
        }
        if (wParam == 9999) {
            KillTimer(hwnd,9999);
            g_agente.setEstado(MessiConBandera::IDLE);
        }
        return 0;

    case WM_MOUSEMOVE: {
        float mx = (float)GET_X_LPARAM(lParam)/AGENT_SCALE;
        float my = (float)GET_Y_LPARAM(lParam)/AGENT_SCALE;
        g_agente.onMouseMove(mx,my);
        return 0;
    }

    // ── Click izquierdo: patear ──────────────────────────────
    case WM_LBUTTONDOWN: {
        float mx = (float)GET_X_LPARAM(lParam)/AGENT_SCALE;
        float my = (float)GET_Y_LPARAM(lParam)/AGENT_SCALE;
        g_agente.onMouseClick(mx, my);   // método público
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc    = BeginPaint(hwnd,&ps);
        HDC memDC  = CreateCompatibleDC(hdc);
        HBITMAP bmp= CreateCompatibleBitmap(hdc,AGENT_SIZE,AGENT_SIZE);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC,bmp);
        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.Clear(Color(0,0,0,0));
            g.ScaleTransform(AGENT_SCALE,AGENT_SCALE);
            g_agente.dibujar(g,64.f,80.f);
        }
        BitBlt(hdc,0,0,AGENT_SIZE,AGENT_SIZE,memDC,0,0,SRCCOPY);
        SelectObject(memDC,oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        EndPaint(hwnd,&ps);
        return 0;
    }

    case WM_RBUTTONUP: {
        ModifyMenuW(g_hTrayMenu,ID_TRAY_BANDERAZO,MF_STRING|MF_BYCOMMAND,
            ID_TRAY_BANDERAZO,
            (g_agente.getEstado()==MessiConBandera::BANDERAZO)
                ? L"Detener Banderazo" : L"🌍 Banderazo");
        ModifyMenuW(g_hTrayMenu,ID_TRAY_JUGAR,MF_STRING|MF_BYCOMMAND,
            ID_TRAY_JUGAR,
            (g_agente.getEstado()==MessiConBandera::JUGANDO)
                ? L"Detener Jugar" : L"⚽ Jugar");
        ModifyMenuW(g_hTrayMenu,ID_TRAY_PATEAR_PELOTA,MF_STRING|MF_BYCOMMAND,
            ID_TRAY_PATEAR_PELOTA,
            (g_agente.getEstado()==MessiConBandera::PATEANDO_PELOTA)
                ? L"Detener Mover Pelota" : L"⚽ Mover Pelota");
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(hwnd);
        TrackPopupMenu(g_hTrayMenu,TPM_RIGHTBUTTON|TPM_BOTTOMALIGN,
                       p.x,p.y,0,hwnd,NULL);
        PostMessage(hwnd,WM_NULL,0,0);
        return 0;
    }

    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);
        if (cmd == ID_TRAY_SALUDAR) {
            KillTimer(hwnd,9999);
            g_agente.setEstado(MessiConBandera::HABLANDO);
            SetTimer(hwnd,9999,2500,NULL);
        }
        else if (cmd == ID_TRAY_CELEBRAR) {
            KillTimer(hwnd,9999);
            g_agente.setEstado(MessiConBandera::CELEBRANDO);
            SetTimer(hwnd,9999,3000,NULL);
        }
        else if (cmd == ID_TRAY_BANDERAZO) {
            if (g_agente.getEstado()==MessiConBandera::BANDERAZO)
                g_agente.setEstado(MessiConBandera::IDLE);
            else {
                KillTimer(hwnd,9999);
                ShowWindow(hwnd,SW_SHOW);
                SetForegroundWindow(hwnd);
                g_banderazoDir = 1.0f;
                g_agente.setEstado(MessiConBandera::BANDERAZO);
            }
        }
        else if (cmd == ID_TRAY_JUGAR) {
            if (g_agente.getEstado()==MessiConBandera::JUGANDO)
                g_agente.setEstado(MessiConBandera::IDLE);
            else {
                KillTimer(hwnd,9999);
                ShowWindow(hwnd,SW_SHOW);
                SetForegroundWindow(hwnd);
                g_agente.setEstado(MessiConBandera::JUGANDO);
            }
        }
        else if (cmd == ID_TRAY_PATEAR_PELOTA) {
            if (g_agente.getEstado()==MessiConBandera::PATEANDO_PELOTA)
                g_agente.setEstado(MessiConBandera::IDLE);
            else {
                KillTimer(hwnd,9999);
                ShowWindow(hwnd,SW_SHOW);
                SetForegroundWindow(hwnd);
                g_agente.setEstado(MessiConBandera::PATEANDO_PELOTA);
            }
        }
        else if (cmd == ID_TRAY_PIZZA) { KillTimer(hwnd, 9999); g_agente.setEstado(MessiConBandera::COMIENDO_PIZZA); SetTimer(hwnd, 9999, 9000, NULL); }
        else if (cmd == ID_TRAY_MATE) { KillTimer(hwnd, 9999); g_agente.setEstado(MessiConBandera::TOMANDO_MATE); SetTimer(hwnd, 9999, 11000, NULL); }
        else if (cmd == ID_TRAY_DEPAUL) { KillTimer(hwnd, 9999); g_agente.setEstado(MessiConBandera::LLAMANDO_DEPAUL); SetTimer(hwnd, 9999, 14000, NULL); }
        else if (cmd == ID_TRAY_EXPORT_AVI) {
            wchar_t szFile[MAX_PATH] = L"messi_saludo.avi";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"Video AVI (*.avi)\0*.avi\0";
            ofn.lpstrFile   = szFile;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_OVERWRITEPROMPT;
            ofn.lpstrDefExt = L"avi";
            if (GetSaveFileNameW(&ofn)) {
                SetCursor(LoadCursor(NULL,IDC_WAIT));
                KillTimer(hwnd,9999);
                bool ok = AviExporter::Export(g_agente,szFile,60,30,256,256);
                if (g_agente.getEstado()==MessiConBandera::HABLANDO)
                    SetTimer(hwnd,9999,2000,NULL);
                SetCursor(LoadCursor(NULL,IDC_ARROW));
                MessageBoxW(hwnd,
                    ok ? L"Video AVI exportado!" : L"Error exportando.",
                    ok ? L"OK" : L"Error",
                    ok ? MB_ICONINFORMATION : MB_ICONERROR);
            }
        }
        else if (cmd == ID_TRAY_MINIMIZAR)
            ShowWindow(hwnd,SW_HIDE);
        else if (cmd == ID_TRAY_PROYECTOS) {
            const char* nombre = "Anibal Zanutti";
            const char* email  = "anibal@zanutti.com.ar";
            const char* web    = "https://www.zanutti.com.ar";
            const char* github = "https://github.com/utnmaterias";
            char msg[512];
            snprintf(msg,sizeof(msg),
                "    Desarrollador C++ / Win32 / GDI+\n\n\n"
                "    Nombre:   %s\n    Email:    %s\n"
                "    Web:      %s\n    GitHub:   %s\n\n"
                "    Desea abrir mi pagina web?",
                nombre,email,web,github);
            if (MessageBoxA(hwnd,msg,"Mis Proyectos",
                MB_YESNO|MB_ICONINFORMATION|MB_TOPMOST)==IDYES)
                ShellExecuteA(NULL,"open",web,NULL,NULL,SW_SHOWNORMAL);
        }
        else if (cmd == ID_TRAY_SALIR)
            DestroyWindow(hwnd);
        return 0;
    }

    case WM_TRAY_MSG:
        if (LOWORD(lParam)==WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd,SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        else if (LOWORD(lParam)==WM_RBUTTONUP) {
            ModifyMenuW(g_hTrayMenu,ID_TRAY_BANDERAZO,MF_STRING|MF_BYCOMMAND, ID_TRAY_BANDERAZO,
                (g_agente.getEstado()==MessiConBandera::BANDERAZO)
                    ? L"Detener Banderazo" : L"🌍 Banderazo");
            ModifyMenuW(g_hTrayMenu,ID_TRAY_JUGAR,MF_STRING|MF_BYCOMMAND, ID_TRAY_JUGAR,
                (g_agente.getEstado()==MessiConBandera::JUGANDO)
                    ? L"Detener Jugar" : L"⚽ Jugar");
            ModifyMenuW(g_hTrayMenu,ID_TRAY_PATEAR_PELOTA,MF_STRING|MF_BYCOMMAND, ID_TRAY_PATEAR_PELOTA,
                (g_agente.getEstado()==MessiConBandera::PATEANDO_PELOTA)
                    ? L"Detener Mover Pelota" : L"⚽ Mover Pelota");
            POINT p; GetCursorPos(&p);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(g_hTrayMenu,TPM_RIGHTBUTTON|TPM_BOTTOMALIGN,
                           p.x,p.y,0,hwnd,NULL);
            PostMessage(hwnd,WM_NULL,0,0);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd,WM_AGENT_TIMER);
        Shell_NotifyIcon(NIM_DELETE,&g_nid);
        if (g_hTrayMenu)  DestroyMenu(g_hTrayMenu);
        if (g_hIconBig)   DestroyIcon(g_hIconBig);
        if (g_hIconSmall) DestroyIcon(g_hIconSmall);
        PostQuitMessage(0);
        return 0;

    } // fin switch
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// ============================================================
// ICONO
// ============================================================
static HICON CrearIconoMessi(int size) {
    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = size;
    bi.bV5Height      = -size;    // top-down
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    void* bits = NULL;
    HDC   hdc  = GetDC(NULL);
    HBITMAP hbm= CreateDIBSection(hdc,(BITMAPINFO*)&bi,
                                  DIB_RGB_COLORS,&bits,NULL,0);
    ReleaseDC(NULL,hdc);
    if (!hbm) return NULL;

    HDC     mem = CreateCompatibleDC(NULL);
    HBITMAP ob  = (HBITMAP)SelectObject(mem,hbm);

    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(Color(0,0,0,0));

        // Fondo celeste
        SolidBrush bg(Color(255,100,180,230));
        g.FillEllipse(&bg, RectF(0,0,(REAL)size,(REAL)size));

        // Franja blanca
        SolidBrush white(Color(255,255,255,255));
        g.FillRectangle(&white,
            RectF(0,(REAL)(size*0.38f),
                  (REAL)size,(REAL)(size*0.24f)));

        // Número 10
        FontFamily ff(L"Arial");
        Font numFont(&ff, (REAL)(size*0.35f), FontStyleBold, UnitPixel);
        SolidBrush numCol(Color(255,30,50,120));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"10",-1,&numFont,
            PointF((REAL)(size*0.5f),(REAL)(size*0.5f)),&fmt,&numCol);

        // Borde
        Pen border(Color(80,0,50,100),(REAL)(size*0.04f));
        g.DrawEllipse(&border, RectF(0,0,(REAL)size,(REAL)size));
    }

    SelectObject(mem,ob);
    DeleteDC(mem);

    HBITMAP  mask = CreateBitmap(size,size,1,1,NULL);
    ICONINFO ii   = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = mask;
    ii.hbmColor = hbm;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(hbm);
    return icon;
}

// ============================================================
// WINMAIN
// ============================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    ULONG_PTR       gdiToken;
    GdiplusStartupInput gsi;
    GdiplusStartup(&gdiToken,&gsi,NULL);

    g_hIconBig   = CrearIconoMessi(32);
    g_hIconSmall = CrearIconoMessi(16);

    WNDCLASSEXA wc = {};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = AgentWndProc;
    wc.hInstance    = hInst;
    wc.lpszClassName= "Argentino_v1";
    wc.hCursor      = LoadCursor(NULL,IDC_ARROW);
    wc.style        = CS_HREDRAW|CS_VREDRAW;
    wc.hIcon        = g_hIconBig;
    wc.hIconSm      = g_hIconSmall;
    RegisterClassExA(&wc);

    hwnd = CreateWindowExA(
        WS_EX_LAYERED|WS_EX_TOPMOST,
        "Argentino_v1", "",
        WS_POPUP|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,
        AGENT_SIZE,AGENT_SIZE,
        NULL,NULL,hInst,NULL);

    // Íconos en la ventana
    SendMessage(hwnd,WM_SETICON,ICON_BIG,  (LPARAM)g_hIconBig);
    SendMessage(hwnd,WM_SETICON,ICON_SMALL,(LPARAM)g_hIconSmall);

    // Transparencia por color clave (magenta)
    SetLayeredWindowAttributes(hwnd,RGB(0,0,0),0,LWA_COLORKEY);

    // Tray icon
    ZeroMemory(&g_nid,sizeof(g_nid));
    g_nid.cbSize          = sizeof(g_nid);
    g_nid.hWnd            = hwnd;
    g_nid.uID             = 1;
    g_nid.uFlags          = NIF_MESSAGE|NIF_ICON|NIF_TIP;
    g_nid.uCallbackMessage= WM_TRAY_MSG;
    g_nid.hIcon           = g_hIconSmall;
    lstrcpyA(g_nid.szTip,"Argentino v1.0");
    Shell_NotifyIconA(NIM_ADD,&g_nid);

    // Menú
    g_hTrayMenu = CreatePopupMenu();
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_SALUDAR,       L"👋 Saludar");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_CELEBRAR,      L"🎉 Celebrar Gol");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_BANDERAZO,     L"🌍 Banderazo");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_JUGAR,         L"⚽ Jugar");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_PATEAR_PELOTA, L"⚽ Mover Pelota");

    AppendMenuW(g_hTrayMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_PIZZA,         L"🍕 Comer Pizza");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_MATE,          L"🧉 Tomar Mate");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_DEPAUL,        L"📞 Llamar a De Paul");

    AppendMenuW(g_hTrayMenu, MF_SEPARATOR,0,NULL);
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_EXPORT_AVI,    L"📹 Guardar Saludo AVI");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_MINIMIZAR,     L"👁 Minimizar");
    AppendMenuW(g_hTrayMenu, MF_SEPARATOR,0,NULL);
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_PROYECTOS,     L"💡 Conoce mis proyectos");
    AppendMenuW(g_hTrayMenu, MF_STRING, ID_TRAY_SALIR,         L"❌ Salir");

    MSG msg;
    while (GetMessage(&msg,NULL,0,0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiToken);
    return 0;
}




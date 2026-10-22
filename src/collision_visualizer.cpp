#include "collision_visualizer.hpp"
#include "profiler.hpp"
#include <cmath>
#include <atomic>

namespace MK9TradeFix {

    extern std::atomic<bool> g_GamePaused;

    struct CollisionCalib {
        // Pushbox
        float pushRadiusScale = 1.0f;
        float pushHeightScale = 1.0f;
        float pushYOffset     = 0.0f;   // lateral
        float pushZOffset     = 0.0f;   // height

        // Hurtbox
        float hurtSizeScale   = 1.0f;
        float hurtForwardBias = 0.0f;
        float hurtHeightBias  = 0.0f;

        // Limb Hitbox
        float limbPadding     = 18.0f;  // current hardcoded radius padding
        float limbForward     = 22.0f;  // current facingSign * 22.0f
        float limbScaleX      = 1.0f;
        float limbScaleY      = 1.0f;
        float limbScaleZ      = 1.0f;

        // Prop / Weapon Hitbox
        float propLenScale    = 1.0f;
        float propRadScale    = 1.0f;
        float propCenterBias  = 0.0f;
    };
    static CollisionCalib g_Calib;

    std::atomic<bool> g_DrawDebugHitboxes{false};
    std::atomic<bool> g_DrawDebugHurtboxes{false};
    std::atomic<bool> g_DrawDebugPushboxes{false};

    // Native Engine Debug Primitives (UE3 / MKKE.exe)
    typedef void (__stdcall *tDrawDebugLineNative)(
        float sx, float sy, float sz,
        float ex, float ey, float ez,
        uint8_t r, uint8_t g, uint8_t b,
        int bPersistent
    );
    static tDrawDebugLineNative fnDrawDebugLine = (tDrawDebugLineNative)0x00722B30;

    typedef void (__stdcall *tDrawDebugBoxNative)(
        float cx, float cy, float cz,
        float ex, float ey, float ez,
        uint8_t r, uint8_t g, uint8_t b,
        int bPersistent
    );
    static tDrawDebugBoxNative fnDrawDebugBox = (tDrawDebugBoxNative)0x00722BA0;

    typedef void (__stdcall *tDrawDebugSphereNative)(
        float cx, float cy, float cz,
        float radius,
        uint32_t segments,
        uint8_t r, uint8_t g, uint8_t b,
        int bPersistent
    );
    static tDrawDebugSphereNative fnDrawDebugSphere = (tDrawDebugSphereNative)0x007234A0;

    typedef void (__cdecl *tFlushPersistentDebugLinesNative)();
    static tFlushPersistentDebugLinesNative fnFlushPersistentDebugLines = (tFlushPersistentDebugLinesNative)0x00730110;

    // Fast precomputed 8-segment unit circle (cos, sin)
    static const float kCircle8[8][2] = {
        {  1.000000f,  0.000000f },
        {  0.707107f,  0.707107f },
        {  0.000000f,  1.000000f },
        { -0.707107f,  0.707107f },
        { -1.000000f,  0.000000f },
        { -0.707107f, -0.707107f },
        {  0.000000f, -1.000000f },
        {  0.707107f, -0.707107f }
    };

    static const float kCircle4[4][2] = {
        {  1.000000f,  0.000000f },
        {  0.000000f,  1.000000f },
        { -1.000000f,  0.000000f },
        {  0.000000f, -1.000000f }
    };

    static inline void DrawDebugCylinderOptimized(
        float cx, float cy, float cz,
        float radius, float halfHeight,
        uint8_t r, uint8_t g, uint8_t b,
        int bPersistent = 0
    ) {
        if (!fnDrawDebugLine) return;
        float zTop = cz + halfHeight;
        float zBot = cz - halfHeight;

        float topPts[8][2];
        float botPts[8][2];

        for (int i = 0; i < 8; ++i) {
            topPts[i][0] = cx + kCircle8[i][0] * radius;
            topPts[i][1] = cy + kCircle8[i][1] * radius;
            botPts[i][0] = topPts[i][0];
            botPts[i][1] = topPts[i][1];
        }

        // Draw top & bottom circular rings (8 + 8 lines)
        for (int i = 0; i < 8; ++i) {
            int next = (i + 1) % 8;
            fnDrawDebugLine(topPts[i][0], topPts[i][1], zTop, topPts[next][0], topPts[next][1], zTop, r, g, b, bPersistent);
            fnDrawDebugLine(botPts[i][0], botPts[i][1], zBot, botPts[next][0], botPts[next][1], zBot, r, g, b, bPersistent);
        }

        // Draw 4 vertical boundary struts (4 lines)
        fnDrawDebugLine(topPts[0][0], topPts[0][1], zTop, botPts[0][0], botPts[0][1], zBot, r, g, b, bPersistent);
        fnDrawDebugLine(topPts[2][0], topPts[2][1], zTop, botPts[2][0], botPts[2][1], zBot, r, g, b, bPersistent);
        fnDrawDebugLine(topPts[4][0], topPts[4][1], zTop, botPts[4][0], botPts[4][1], zBot, r, g, b, bPersistent);
        fnDrawDebugLine(topPts[6][0], topPts[6][1], zTop, botPts[6][0], botPts[6][1], zBot, r, g, b, bPersistent);
    }

    static inline void DrawDebugSphereOptimized(
        float cx, float cy, float cz,
        float radius,
        uint8_t r, uint8_t g, uint8_t b,
        int bPersistent = 0
    ) {
        if (!fnDrawDebugLine) return;
        if (radius <= 0.5f || radius > 300.0f) return;

        // 1. Camera-Facing Screen Plane Ring (8-segment smooth circle facing 2.5D fight camera)
        for (int i = 0; i < 8; ++i) {
            int next = (i + 1) % 8;
            fnDrawDebugLine(cx, cy + kCircle8[i][0] * radius, cz + kCircle8[i][1] * radius,
                            cx, cy + kCircle8[next][0] * radius, cz + kCircle8[next][1] * radius,
                            r, g, b, bPersistent);
        }

        // 2. Horizontal Depth Ring (4 lines) & Sagittal Depth Ring (4 lines)
        for (int i = 0; i < 4; ++i) {
            int next = (i + 1) % 4;
            fnDrawDebugLine(cx + kCircle4[i][0] * radius, cy + kCircle4[i][1] * radius, cz,
                            cx + kCircle4[next][0] * radius, cy + kCircle4[next][1] * radius, cz,
                            r, g, b, bPersistent);
            fnDrawDebugLine(cx + kCircle4[i][0] * radius, cy, cz + kCircle4[i][1] * radius,
                            cx + kCircle4[next][0] * radius, cy, cz + kCircle4[next][1] * radius,
                            r, g, b, bPersistent);
        }
    }

    static inline void DrawDebugCapsuleOptimized(
        float x1, float y1, float z1,
        float x2, float y2, float z2,
        float r1,
        uint8_t r, uint8_t g, uint8_t b,
        int bPersistent = 0
    ) {
        if (!fnDrawDebugLine) return;
        if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(z1) ||
            !std::isfinite(x2) || !std::isfinite(y2) || !std::isfinite(z2) ||
            !std::isfinite(r1)) return;
        if (r1 <= 0.5f || r1 > 200.0f) return;

        float dx = x2 - x1;
        float dy = y2 - y1;
        float dz = z2 - z1;
        float lenSq = dx * dx + dy * dy + dz * dz;

        // If endpoints are nearly identical (e.g. single joint / head), draw sphere
        if (lenSq < 9.0f) {
            DrawDebugSphereOptimized(x1, y1, z1, r1, r, g, b, bPersistent);
            return;
        }

        // Draw central skeletal bone segment (1 line)
        fnDrawDebugLine(x1, y1, z1, x2, y2, z2, r, g, b, bPersistent);

        // Compute orthonormal basis perpendicular to segment axis
        float len = sqrtf(lenSq);
        float ax = dx / len;
        float ay = dy / len;
        float az = dz / len;

        // Perpendicular vector u (Depth / Screen Normal)
        float upX = 1.0f, upY = 0.0f, upZ = 0.0f;
        if (fabsf(ax) > 0.85f) {
            upX = 0.0f; upY = 0.0f; upZ = 1.0f;
        }
        float uX = ay * upZ - az * upY;
        float uY = az * upX - ax * upZ;
        float uZ = ax * upY - ay * upX;
        float uLen = sqrtf(uX * uX + uY * uY + uZ * uZ);
        if (uLen > 0.0001f) {
            uX /= uLen; uY /= uLen; uZ /= uLen;
        }

        // Vector w = a x u
        float wX = ay * uZ - az * uY;
        float wY = az * uX - ax * uZ;
        float wZ = ax * uY - ay * uX;

        // Draw 4 longitudinal boundary silhouette ribs connecting P1 and P2 (4 lines)
        fnDrawDebugLine(x1 + uX * r1, y1 + uY * r1, z1 + uZ * r1, x2 + uX * r1, y2 + uY * r1, z2 + uZ * r1, r, g, b, bPersistent);
        fnDrawDebugLine(x1 - uX * r1, y1 - uY * r1, z1 - uZ * r1, x2 - uX * r1, y2 - uY * r1, z2 - uZ * r1, r, g, b, bPersistent);
        fnDrawDebugLine(x1 + wX * r1, y1 + wY * r1, z1 + wZ * r1, x2 + wX * r1, y2 + wY * r1, z2 + wZ * r1, r, g, b, bPersistent);
        fnDrawDebugLine(x1 - wX * r1, y1 - wY * r1, z1 - wZ * r1, x2 - wX * r1, y2 - wY * r1, z2 - wZ * r1, r, g, b, bPersistent);

        // Draw 4-point cross rings at P1 and P2 in the (u, w) plane (4 + 4 lines)
        for (int i = 0; i < 4; ++i) {
            int next = (i + 1) % 4;
            float c1 = kCircle4[i][0] * r1;
            float s1 = kCircle4[i][1] * r1;
            float c2 = kCircle4[next][0] * r1;
            float s2 = kCircle4[next][1] * r1;

            // P1 ring
            fnDrawDebugLine(x1 + uX * c1 + wX * s1, y1 + uY * c1 + wY * s1, z1 + uZ * c1 + wZ * s1,
                            x1 + uX * c2 + wX * s2, y1 + uY * c2 + wY * s2, z1 + uZ * c2 + wZ * s2,
                            r, g, b, bPersistent);

            // P2 ring
            fnDrawDebugLine(x2 + uX * c1 + wX * s1, y2 + uY * c1 + wY * s1, z2 + uZ * c1 + wZ * s1,
                            x2 + uX * c2 + wX * s2, y2 + uY * c2 + wY * s2, z2 + uZ * c2 + wZ * s2,
                            r, g, b, bPersistent);
        }
    }

    void SafeFlushDebugPrimitives() {
        __try {
            uintptr_t pEngine = *(uintptr_t*)0x00E86AB0;
            if (!pEngine || (uintptr_t)pEngine < 0x01000000) return;

            // Flush persistent batcher via engine native function (0x00730110)
            if (fnFlushPersistentDebugLines) {
                fnFlushPersistentDebugLines();
            }

            // Also ensure non-persistent and persistent LineBatcher array counts are zeroed
            uintptr_t pBatcher = *(uintptr_t*)(pEngine + 0x140);
            if (pBatcher && (uintptr_t)pBatcher > 0x01000000) {
                uintptr_t vtable = *(uintptr_t*)pBatcher;
                if (vtable > 0x00400000 && vtable < 0x01000000) {
                    *(uint32_t*)(pBatcher + 0x264) = 0;
                    *(uint32_t*)(pBatcher + 0x4C) |= 0x04;
                }
            }
            uintptr_t pPersist = *(uintptr_t*)(pEngine + 0x144);
            if (pPersist && (uintptr_t)pPersist > 0x01000000) {
                uintptr_t vtable = *(uintptr_t*)pPersist;
                if (vtable > 0x00400000 && vtable < 0x01000000) {
                    *(uint32_t*)(pPersist + 0x264) = 0;
                    *(uint32_t*)(pPersist + 0x4C) |= 0x04;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void EnsureLineBatcherVisible() {
        __try {
            uintptr_t pEngine = *(uintptr_t*)0x00E86AB0;
            if (!pEngine || (uintptr_t)pEngine < 0x01000000) return;

            uintptr_t pBatcher = *(uintptr_t*)(pEngine + 0x140);
            if (pBatcher && (uintptr_t)pBatcher > 0x01000000) {
                uintptr_t vtable = *(uintptr_t*)pBatcher;
                if (vtable > 0x00400000 && vtable < 0x01000000) {
                    *(uint32_t*)(pBatcher + 0x08) &= ~0x00100000;
                }
            }
            uintptr_t pPersist = *(uintptr_t*)(pEngine + 0x144);
            if (pPersist && (uintptr_t)pPersist > 0x01000000) {
                uintptr_t vtable = *(uintptr_t*)pPersist;
                if (vtable > 0x00400000 && vtable < 0x01000000) {
                    *(uint32_t*)(pPersist + 0x08) &= ~0x00100000;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool IsCharacterInVisualActiveFrames(void* pChar) {
        if (!pChar || (uintptr_t)pChar < 0x01000000) return false;

        __try {
            // 0. Native strike list (PLAYER_COL_INFO+0x14 count > 0 == attack spheres live this frame)
            {
                uintptr_t col = *(uintptr_t*)((uintptr_t)pChar + 0x2F10);
                int hid = *(int*)((uintptr_t)pChar + 0x2F14);
                if (IsValidUserPtr((void*)col) && hid != -1 && *(int*)(col + 4) == hid) {
                    uintptr_t lst = *(uintptr_t*)(col + 0x14);
                    if (IsValidUserPtr((void*)lst) && *(uint32_t*)lst > 0) return true;
                }
            }

            // 1. Fast check explicit active hitbox counter (+0x7A3C)
            uint32_t activeHitboxes = *(uint32_t*)((uintptr_t)pChar + 0x7A3C);
            if (activeHitboxes > 0) return true;

            // 2. Action state quick reject / accept
            uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            if (actionState == 0x0000 || actionState == 0x0101 || actionState == 0x0100) return false;
            if (actionState == 0x4600 || actionState == 0x4201 || actionState == 0x4203 || 
                (actionState >= 0x6200 && actionState <= 0x620B)) return false;

            // Standard attack execution action states (0x5200 normal strike, 0x5300 uppercut)
            if (actionState == 0x5200 || actionState == 0x5300) return true;

            // 3. Authoritative Move Script Execution State (+0x2F84 pkg, +0x2F88 func)
            uintptr_t scriptPkg  = *(uintptr_t*)((uintptr_t)pChar + 0x2F84);
            int       scriptFunc = *(int*)((uintptr_t)pChar + 0x2F88);

            if (scriptPkg != 0 && scriptFunc != -1) {
                // Check native collision info active volumes (+0x2F10)
                void* pColInfo = *(void**)((uintptr_t)pChar + 0x2F10);
                if (pColInfo && (uintptr_t)pColInfo > 0x01000000) {
                    uint32_t* pVolList = *(uint32_t**)((uintptr_t)pColInfo + 0x14);
                    if (pVolList && (uintptr_t)pVolList > 0x01000000 && pVolList[0] > 0) return true;
                }

                // While paused: keep the attack hitbox visible if frozen during an attack execution
                if (g_GamePaused.load()) {
                    if (actionState >= 0x4800 && actionState <= 0x5400) return true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return false;
    }

    // =====================================================================
    // NATIVE COLLISION DATA (recovered from MKKE.exe, see docs/COLLISION_SYSTEM_RE.md)
    //
    // pChar+0x2F10 = PLAYER_COL_INFO* (handle; valid when col[1] == pChar+0x2F14)
    //   col+0x14  -> ATTACK list : {u32 count, u32 lastCount, entry[36] @ +0x08, prevEntry[36] @ +0x5A8}
    //   col+0x18  -> HURT array  : count @ col+0x184
    //   col+0x1C  =  PUSH array (inline) : count @ col+0x188, radius scale @ col+0x18C
    //   entry (0x28): +00 bone FName, +04 active, +08 pos xyz, +14 radius, +18 prevPos xyz, +24 prevRadius
    //
    // All melee tests (0x008B0CD0 hit, 0x008B0910 sphere, 0x008B1080 push) are 2D circle tests in the
    // fight plane: u = dot(P - O, A), v = P.z  with O = 0x00DF0BEC, A = 0x00DF0C14 (0x008B4320).
    // Inverse (0x008B43A0): P = (O.x + u*A.x, O.y + u*A.y, v).
    // =====================================================================
    namespace NativeCol {
        static const uintptr_t kPlaneOrigin = 0x00DF0BEC;
        static const uintptr_t kPlaneAxis   = 0x00DF0C14;
        static const uintptr_t kFrameCtr    = 0x00E89BE0;
        // Last contact (written by 0x008AF630 / 0x008AF6A0)
        static const uintptr_t kHitAtkInfo  = 0x00DF0BA4; // attacker PlayerInfo
        static const uintptr_t kHitAtkPos   = 0x00DF0BAC; // attack sphere center xyz
        static const uintptr_t kHitAtkFrame = 0x00DF0BB8;
        static const uintptr_t kHitDefPos   = 0x00DF0BC4; // hurt sphere center xyz

        static const int kEntryStride   = 0x28;
        static const int kMaxAttack     = 0x24;
        static const int kPrevAtkOffset = 0x5A8;

        static uintptr_t GetColInfo(void* pChar) {
            uintptr_t col = *(uintptr_t*)((uintptr_t)pChar + 0x2F10);
            if (!IsValidUserPtr((void*)col) || col == 0xDEADBEEF) return 0;
            int hid = *(int*)((uintptr_t)pChar + 0x2F14);
            if (hid == -1 || *(int*)(col + 4) != hid) return 0;
            return col;
        }

        // Project a world point onto the fight plane and back, yielding the exact point the engine tests.
        static inline void PlaneSnap(const float* p, float& ox, float& oy, float& oz,
                                     const float* O, const float* A) {
            float u = (p[0] - O[0]) * A[0] + (p[1] - O[1]) * A[1] + (p[2] - O[2]) * A[2];
            ox = O[0] + u * A[0];
            oy = O[1] + u * A[1];
            oz = p[2];
        }

        // Circle lying in the fight plane (spanned by axis A and world Z).
        static void DrawPlaneCircle(float cx, float cy, float cz, float r, const float* A,
                                    uint8_t R, uint8_t G, uint8_t B) {
            if (!fnDrawDebugLine || !(r > 0.5f) || r > 400.0f) return;
            float ax = A[0], ay = A[1];
            float len = sqrtf(ax * ax + ay * ay);
            if (len < 1e-4f) { ax = 0.0f; ay = 1.0f; } else { ax /= len; ay /= len; }
            static const int N = 16;
            float px = cx + ax * r, py = cy + ay * r, pz = cz;
            for (int i = 1; i <= N; ++i) {
                float t = (float)i * (6.2831853f / N);
                float c = cosf(t), s = sinf(t);
                float nx = cx + ax * r * c, ny = cy + ay * r * c, nz = cz + r * s;
                fnDrawDebugLine(px, py, pz, nx, ny, nz, R, G, B, 0);
                px = nx; py = ny; pz = nz;
            }
        }

        static void DrawEntryArray(uintptr_t arr, uint32_t count, uint32_t maxCount, float rScale,
                                   bool requireActive, const float* O, const float* A,
                                   uint8_t R, uint8_t G, uint8_t B) {
            if (!IsValidUserPtr((void*)arr)) return;
            if (count > maxCount) count = maxCount;
            for (uint32_t i = 0; i < count; ++i) {
                uintptr_t e = arr + i * kEntryStride;
                if (requireActive && *(uint32_t*)(e + 0x04) == 0) continue;
                const float* p = (const float*)(e + 0x08);
                float r = *(float*)(e + 0x14) * rScale;
                if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || !std::isfinite(r)) continue;
                float x, y, z;
                PlaneSnap(p, x, y, z, O, A);
                DrawPlaneCircle(x, y, z, r, A, R, G, B);
            }
        }

        static const uintptr_t kVtMKCollisionObj = 0x00D6A9B0;

        static void RenderProjectiles(void* pChar, const float* O, const float* A) {
            if (!pChar || (uintptr_t)pChar < 0x01000000) return;
            __try {
                uintptr_t node = *(uintptr_t*)((uintptr_t)pChar + 0x7F48 + 0x04);
                for (int guard = 0; node && guard < 128; ++guard) {
                    if (!IsValidUserPtr((void*)node)) break;
                    uintptr_t obj    = *(uintptr_t*)(node + 0x04);
                    uint32_t  serial = *(uint32_t*)(node + 0x08);
                    uintptr_t next   = *(uintptr_t*)(node + 0x0C);
                    bool pending     = *(uint32_t*)(node + 0x18) != 0;

                    if (!pending && obj && obj != 0xDEADBEEF && serial != 0xFFFFFFFF &&
                        IsValidUserPtr((void*)obj) && *(uint32_t*)(obj + 0x04) == serial &&
                        *(uintptr_t*)obj == kVtMKCollisionObj &&
                        *(uint32_t*)(obj + 0x104) == 0) { // not spent
                        
                        uintptr_t actor = *(uintptr_t*)(obj + 0x08);
                        if (IsValidUserPtr((void*)actor)) {
                            const float* pos = (const float*)(actor + 0xDC);
                            if (std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2])) {
                                float rad = *(float*)(obj + 0xF8);
                                if (!std::isfinite(rad) || rad <= 0.5f) rad = 10.0f;
                                uint32_t full3D = *(uint32_t*)(obj + 0x1B8);
                                
                                if (full3D) {
                                    // Full 3D Projectile Sphere (Vibrant Magenta: 255, 60, 220)
                                    DrawDebugSphereOptimized(pos[0], pos[1], pos[2], rad, 255, 60, 220, 0);
                                } else {
                                    float x, y, z;
                                    PlaneSnap(pos, x, y, z, O, A);
                                    // 2D Fight Plane Projectile Circle (Vibrant Magenta: 255, 60, 220)
                                    DrawPlaneCircle(x, y, z, rad, A, 255, 60, 220);
                                }
                            }
                        }
                    }
                    node = next;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        static bool HasActiveProjectiles(void* pChar) {
            if (!pChar || (uintptr_t)pChar < 0x01000000) return false;
            __try {
                uintptr_t node = *(uintptr_t*)((uintptr_t)pChar + 0x7F48 + 0x04);
                for (int guard = 0; node && guard < 64; ++guard) {
                    if (!IsValidUserPtr((void*)node)) break;
                    uintptr_t obj    = *(uintptr_t*)(node + 0x04);
                    uint32_t  serial = *(uint32_t*)(node + 0x08);
                    uintptr_t next   = *(uintptr_t*)(node + 0x0C);
                    bool pending     = *(uint32_t*)(node + 0x18) != 0;

                    if (!pending && obj && obj != 0xDEADBEEF && serial != 0xFFFFFFFF &&
                        IsValidUserPtr((void*)obj) && *(uint32_t*)(obj + 0x04) == serial &&
                        *(uintptr_t*)obj == kVtMKCollisionObj &&
                        *(uint32_t*)(obj + 0x104) == 0 &&
                        IsValidUserPtr(*(void**)(obj + 0x08))) {
                        return true;
                    }
                    node = next;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            return false;
        }

        // Returns bitmask of what was drawn natively: 1 = push, 2 = hurt, 4 = hit
        static int Render(void* pChar, bool push, bool hurt, bool hit) {
            uintptr_t col = GetColInfo(pChar);
            if (!col) return 0;
            const float* O = (const float*)kPlaneOrigin;
            const float* A = (const float*)kPlaneAxis;
            if (!std::isfinite(A[0]) || !std::isfinite(A[1]) || (fabsf(A[0]) + fabsf(A[1])) < 1e-4f) return 0;

            int drawn = 0;
            if (push) {
                float scale = *(float*)(col + 0x18C);
                if (!std::isfinite(scale) || scale <= 0.0f) scale = 1.0f;
                DrawEntryArray(col + 0x1C, *(uint32_t*)(col + 0x188), 16, scale, true, O, A, 0, 220, 255);
                drawn |= 1;
            }
            if (hurt) {
                DrawEntryArray(*(uintptr_t*)(col + 0x18), *(uint32_t*)(col + 0x184), 48, 1.0f, true, O, A, 40, 240, 60);
                drawn |= 2;
            }
            if (hit) {
                // 1. Melee strikes from colInfo + 0x14
                uintptr_t lst = *(uintptr_t*)(col + 0x14);
                if (IsValidUserPtr((void*)lst)) {
                    uint32_t n = *(uint32_t*)lst;
                    if (n > 0) {
                        // Live strike spheres added by script this frame (0x008AFF90 / 0x008B0360)
                        DrawEntryArray(lst + 0x08, n, kMaxAttack, 1.0f, false, O, A, 255, 30, 30);
                    } else {
                        // Active count zeroed: show previous attack spheres in orange
                        // (persists through recovery on whiff OR hit until next move starts)
                        uint32_t pn = *(uint32_t*)(lst + 4);
                        if (pn > 0) {
                            DrawEntryArray(lst + kPrevAtkOffset, pn, kMaxAttack, 1.0f, false, O, A, 255, 140, 0);
                        }
                        // Contact dots: yellow (attacker) + white (defender) — only on hit confirmation
                        uintptr_t myInfo = *(uintptr_t*)((uintptr_t)pChar + 0x7638);
                        uint32_t now = *(uint32_t*)kFrameCtr;
                        uint32_t hf  = *(uint32_t*)kHitAtkFrame;
                        if (myInfo && *(uintptr_t*)kHitAtkInfo == myInfo && (now - hf) <= 1) {
                            float cx, cy, cz;
                            PlaneSnap((const float*)kHitAtkPos, cx, cy, cz, O, A);
                            DrawPlaneCircle(cx, cy, cz, 3.0f, A, 255, 255, 0);
                            PlaneSnap((const float*)kHitDefPos, cx, cy, cz, O, A);
                            DrawPlaneCircle(cx, cy, cz, 3.0f, A, 255, 255, 255);
                        }
                    }
                    drawn |= 4;
                }
                // 2. Detached projectile / prop strikes from Character + 0x7F48
                RenderProjectiles(pChar, O, A);
            }
            return drawn;
        }

        static bool HasActiveStrikes(void* pChar) {
            uintptr_t col = GetColInfo(pChar);
            if (!col) return HasActiveProjectiles(pChar);
            uintptr_t lst = *(uintptr_t*)(col + 0x14);
            return (IsValidUserPtr((void*)lst) && *(uint32_t*)lst > 0) || HasActiveProjectiles(pChar);
        }
    }

    void RenderDebugCollisionPrimitives(void* pChar, int playerIdx) {
        if (!g_DrawDebugHitboxes.load(std::memory_order_relaxed) &&
            !g_DrawDebugHurtboxes.load(std::memory_order_relaxed) &&
            !g_DrawDebugPushboxes.load(std::memory_order_relaxed)) {
            return;
        }

        if (!IsValidUserPtr(pChar)) return;
        PROFILE_START();

        __try {
            // 1. Authoritative Character World Coordinates
            float charLat    = *(float*)((uintptr_t)pChar + 0x1E8);
            float charDepth  = *(float*)((uintptr_t)pChar + 0x1EC);
            float charHeight = *(float*)((uintptr_t)pChar + 0x1F0);
            if (!std::isfinite(charLat) || !std::isfinite(charDepth) || !std::isfinite(charHeight)) {
                PROFILE_END(PROF_HITBOX_3D);
                return;
            }
            if (fabsf(charLat) > 100000.0f || fabsf(charDepth) > 100000.0f || fabsf(charHeight) > 100000.0f) {
                PROFILE_END(PROF_HITBOX_3D);
                return;
            }

            // 0. Authoritative engine volumes (exact data used by the native collision tests)
            const int nativeDrawn = NativeCol::Render(pChar,
                g_DrawDebugPushboxes.load(std::memory_order_relaxed),
                g_DrawDebugHurtboxes.load(std::memory_order_relaxed),
                g_DrawDebugHitboxes.load(std::memory_order_relaxed));
            if (nativeDrawn == 7) {
                PROFILE_END(PROF_HITBOX_3D);
                return;
            }

            // Determine Facing Direction (along Stage Lateral Y)
            float facingSign = 1.0f;
            void* pOpp = (playerIdx == 1) ? GetP2Char() : GetP1Char();
            if (IsValidUserPtr(pOpp)) {
                float oppLat = *(float*)((uintptr_t)pOpp + 0x1E8);
                if (std::isfinite(oppLat)) facingSign = (oppLat >= charLat) ? 1.0f : -1.0f;
            } else {
                float fVal = *(float*)((uintptr_t)pChar + 0x2E4);
                if (std::isfinite(fVal) && fabsf(fVal) > 0.01f) facingSign = (fVal >= 0.0f) ? 1.0f : -1.0f;
            }

            void* pColInfo = *(void**)((uintptr_t)pChar + 0x2F10);
            float pelvisZ = 87.2f;
            if (IsValidUserPtr(pColInfo)) {
                float colDepth = *(float*)((uintptr_t)pColInfo + 0x24);
                float colLat   = *(float*)((uintptr_t)pColInfo + 0x28);
                if (std::isfinite(colDepth) && std::isfinite(colLat) && fabsf(colDepth) < 1000.0f && fabsf(colLat) < 10000.0f) {
                    charDepth = colDepth;
                    charLat = colLat;
                }
                float pz = *(float*)((uintptr_t)pColInfo + 0x20 + 0x0C);
                if (std::isfinite(pz) && pz > 5.0f && pz < 400.0f) {
                    pelvisZ = pz;
                }
            }

            // Authoritative Crouch & Stance Detection
            uint32_t actionState = *(uint32_t*)((uintptr_t)pChar + 0x1D4);
            uint32_t btnMask = *(uint32_t*)((uintptr_t)pChar + 0x78C4);
            uint32_t stanceFlags = *(uint32_t*)((uintptr_t)pChar + 0x2E8);
            bool isCrouched = (actionState == 0x101 || actionState == 0x100 || 
                               (actionState >= 0x100 && actionState <= 0x108) ||
                               actionState == 0x5000 || actionState == 0x5201 || 
                               (actionState >= 0x6200 && actionState <= 0x6208) || 
                               ((btnMask & 0x02) != 0) || 
                               ((stanceFlags & 0x80800) == 0x80800) ||
                               (pelvisZ > 10.0f && pelvisZ < 70.0f));

            // 2. Pushbox Dimensions: 3D True Vertical Cylinder (Cyan: 0, 220, 255)
            if (!(nativeDrawn & 1) && g_DrawDebugPushboxes.load(std::memory_order_relaxed)) {
                float halfRadius = isCrouched ? 26.0f : 24.0f;
                float halfHeight = isCrouched ? 42.0f : 65.0f;
                void* pCyl = *(void**)((uintptr_t)pChar + 0x1A0);
                if (IsValidUserPtr(pCyl)) {
                    float cr = *(float*)((uintptr_t)pCyl + 0x48);
                    float ch = *(float*)((uintptr_t)pCyl + 0x4C);
                    if (std::isfinite(cr) && cr > 5.0f && cr < 300.0f) halfRadius = cr;
                    if (std::isfinite(ch) && ch > 10.0f && ch < 500.0f) halfHeight = isCrouched ? (ch * 0.35f) : (ch * 0.5f);
                } else {
                    float colExtent = *(float*)((uintptr_t)pChar + 0x180);
                    if (std::isfinite(colExtent) && colExtent > 5.0f && colExtent < 300.0f) halfRadius = colExtent;
                }
                float airborneOffset = (pelvisZ > 95.0f) ? (pelvisZ - 87.2f) : 0.0f;
                float pushLat = charLat + g_Calib.pushYOffset;
                float pushZ   = charHeight + airborneOffset + halfHeight + g_Calib.pushZOffset;
                float drawRadius = halfRadius * g_Calib.pushRadiusScale;
                float drawHeight = halfHeight * g_Calib.pushHeightScale;
                DrawDebugCylinderOptimized(charDepth, pushLat, pushZ, drawRadius, drawHeight, 0, 220, 255, 0);
            }

            // 3. Authentic Skeletal Hurtboxes: Complete Articulated 3D Swept Capsules (Green: 40, 240, 60)
            if (!(nativeDrawn & 2) && g_DrawDebugHurtboxes.load(std::memory_order_relaxed)) {
                bool drawnHurtboxes = false;
                if (IsValidUserPtr(pColInfo)) {
                    // Extract the 7 canonical articulated skeletal joints from pColInfo + 0x20
                    // Ext[0] = Pelvis/Hips, Ext[1] = Lower Torso, Ext[4] = Upper Chest/Head,
                    // Ext[5] = Right Arm/Hand, Ext[6] = Left Arm/Hand, Ext[2] = Right Leg/Foot, Ext[3] = Left Leg/Foot
                    struct JointPos { float x, y, z; bool valid; };
                    JointPos joints[7];

                    for (int i = 0; i < 7; ++i) {
                        uintptr_t entry = (uintptr_t)pColInfo + 0x20 + i * 0x28;
                        uint32_t bActive = *(uint32_t*)(entry + 0x00);
                        float d1 = *(float*)(entry + 0x04);
                        float l1 = *(float*)(entry + 0x08);
                        float h1 = *(float*)(entry + 0x0C);
                        if (bActive == 1 && std::isfinite(d1) && std::isfinite(l1) && std::isfinite(h1) &&
                            (fabsf(l1) > 1.0f || fabsf(h1) > 1.0f)) {
                            joints[i] = { d1, l1 + (facingSign * g_Calib.hurtForwardBias), h1 + g_Calib.hurtHeightBias, true };
                        } else {
                            joints[i] = { 0.0f, 0.0f, 0.0f, false };
                        }
                    }

                    float s = g_Calib.hurtSizeScale;

                    // 1. Spine / Torso Capsule (Ext[0] Pelvis -> Ext[1] Mid-Torso)
                    if (joints[0].valid && joints[1].valid) {
                        DrawDebugCapsuleOptimized(joints[0].x, joints[0].y, joints[0].z,
                                                 joints[1].x, joints[1].y, joints[1].z, 19.0f * s, 40, 240, 60, 0);
                        drawnHurtboxes = true;
                    } else if (joints[0].valid) {
                        DrawDebugSphereOptimized(joints[0].x, joints[0].y, joints[0].z, 20.0f * s, 40, 240, 60, 0);
                        drawnHurtboxes = true;
                    }

                    // 2. Chest / Neck / Head Capsule (Ext[1] Mid-Torso -> Ext[4] Chest -> Head)
                    if (joints[4].valid) {
                        float headZ = joints[4].z + 26.0f;
                        float headY = joints[4].y + (facingSign * 6.0f);
                        if (joints[1].valid) {
                            DrawDebugCapsuleOptimized(joints[1].x, joints[1].y, joints[1].z,
                                                     joints[4].x, joints[4].y, joints[4].z, 18.0f * s, 40, 240, 60, 0);
                        }
                        // Head sphere
                        DrawDebugSphereOptimized(joints[4].x, headY, headZ, 14.0f * s, 40, 240, 60, 0);
                        drawnHurtboxes = true;
                    }

                    // 3. Right Arm Capsule (Ext[4] Chest/Shoulder -> Ext[5] Right Hand)
                    if (joints[5].valid) {
                        float rootX = joints[4].valid ? joints[4].x + 18.0f : charDepth + 18.0f;
                        float rootY = joints[4].valid ? joints[4].y : charLat;
                        float rootZ = joints[4].valid ? joints[4].z : charHeight + 115.0f;
                        DrawDebugCapsuleOptimized(rootX, rootY, rootZ,
                                                 joints[5].x, joints[5].y, joints[5].z, 11.0f * s, 40, 240, 60, 0);
                        drawnHurtboxes = true;
                    }

                    // 4. Left Arm Capsule (Ext[4] Chest/Shoulder -> Ext[6] Left Hand)
                    if (joints[6].valid) {
                        float rootX = joints[4].valid ? joints[4].x - 18.0f : charDepth - 18.0f;
                        float rootY = joints[4].valid ? joints[4].y : charLat;
                        float rootZ = joints[4].valid ? joints[4].z : charHeight + 115.0f;
                        DrawDebugCapsuleOptimized(rootX, rootY, rootZ,
                                                 joints[6].x, joints[6].y, joints[6].z, 11.0f * s, 40, 240, 60, 0);
                        drawnHurtboxes = true;
                    }

                    // 5. Right Leg Capsule (Ext[0] Pelvis -> Ext[2] Right Foot)
                    if (joints[2].valid) {
                        float hipX = joints[0].valid ? joints[0].x + 12.0f : charDepth + 12.0f;
                        float hipY = joints[0].valid ? joints[0].y : charLat;
                        float hipZ = joints[0].valid ? joints[0].z : charHeight + 85.0f;
                        DrawDebugCapsuleOptimized(hipX, hipY, hipZ,
                                                 joints[2].x, joints[2].y, joints[2].z, 13.0f * s, 40, 240, 60, 0);
                        drawnHurtboxes = true;
                    }

                    // 6. Left Leg Capsule (Ext[0] Pelvis -> Ext[3] Left Foot)
                    if (joints[3].valid) {
                        float hipX = joints[0].valid ? joints[0].x - 12.0f : charDepth - 12.0f;
                        float hipY = joints[0].valid ? joints[0].y : charLat;
                        float hipZ = joints[0].valid ? joints[0].z : charHeight + 85.0f;
                        DrawDebugCapsuleOptimized(hipX, hipY, hipZ,
                                                 joints[3].x, joints[3].y, joints[3].z, 13.0f * s, 40, 240, 60, 0);
                        drawnHurtboxes = true;
                    }
                }

                // Fallback Hurtboxes if pColInfo invalid
                if (!drawnHurtboxes) {
                    float s = g_Calib.hurtSizeScale;
                    float fb = facingSign * g_Calib.hurtForwardBias;
                    float hb = g_Calib.hurtHeightBias;
                    if (isCrouched) {
                        DrawDebugCapsuleOptimized(charDepth, charLat + facingSign * 16.0f + fb, charHeight + 58.0f + hb,
                                                 charDepth, charLat + facingSign * 16.0f + fb, charHeight + 58.0f + hb, 12.0f * s, 40, 240, 60, 0);
                        DrawDebugCapsuleOptimized(charDepth, charLat + facingSign * 10.0f + fb, charHeight + 38.0f + hb,
                                                 charDepth, charLat + facingSign * 10.0f + fb, charHeight + 38.0f + hb, 15.0f * s, 40, 240, 60, 0);
                        DrawDebugCapsuleOptimized(charDepth, charLat + facingSign * 4.0f + fb,  charHeight + 14.0f + hb,
                                                 charDepth, charLat + facingSign * 4.0f + fb,  charHeight + 14.0f + hb, 18.0f * s, 40, 240, 60, 0);
                    } else {
                        DrawDebugCapsuleOptimized(charDepth, charLat + facingSign * 14.0f + fb, charHeight + 100.0f + hb,
                                                 charDepth, charLat + facingSign * 14.0f + fb, charHeight + 100.0f + hb, 12.0f * s, 40, 240, 60, 0);
                        DrawDebugCapsuleOptimized(charDepth, charLat + facingSign * 8.0f + fb,  charHeight + 66.0f + hb,
                                                 charDepth, charLat + facingSign * 8.0f + fb,  charHeight + 66.0f + hb,  16.0f * s, 40, 240, 60, 0);
                        DrawDebugCapsuleOptimized(charDepth, charLat + facingSign * 2.0f + fb,  charHeight + 22.0f + hb,
                                                 charDepth, charLat + facingSign * 2.0f + fb,  charHeight + 22.0f + hb,  18.0f * s, 40, 240, 60, 0);
                    }
                }
            }

            // 4. Attack Hitbox Volumes: 3D Spheres & Oriented Weapon Capsules (Hitbox Red: 255, 30, 30)
            if (!(nativeDrawn & 4) && g_DrawDebugHitboxes.load(std::memory_order_relaxed)) {
                uint32_t activeHitboxes = *(uint32_t*)((uintptr_t)pChar + 0x7A3C);
                bool isHurt = (actionState == 0x4600 || actionState == 0x4201 || actionState == 0x4203 || 
                               (actionState >= 0x6200 && actionState <= 0x620B));
                bool isAttacking = !isHurt && (activeHitboxes > 0 || IsCharacterInVisualActiveFrames(pChar));

                if (isAttacking && IsValidUserPtr(pColInfo)) {
                    bool drawnHitbox = false;
                    bool foundNativeWeaponCol = false;

                    // Single-pass Prop Traversal (Weapon Props & Projectiles)
                    void* pPropList = *(void**)((uintptr_t)pChar + 0x2F1C);
                    if (IsValidUserPtr(pPropList)) {
                        uintptr_t pNode = (uintptr_t)pPropList;
                        int safety = 0;
                        while (IsValidUserPtr((void*)pNode) && safety < 4) {
                            safety++;
                            uintptr_t pActor = *(uintptr_t*)(pNode + 0x04);
                            if (IsValidUserPtr((void*)pActor)) {
                                uintptr_t comp = *(uintptr_t*)(pActor + 0x08);
                                if (IsValidUserPtr((void*)comp)) {
                                    float cDepth  = *(float*)(comp + 0xDC);
                                    float cLat    = *(float*)(comp + 0xE0);
                                    float cHeight = *(float*)(comp + 0xE4);
                                    float rad     = *(float*)(pActor + 0xF8);
                                    float len     = *(float*)(pActor + 0x100);

                                    if (std::isfinite(cLat) && std::isfinite(cDepth) && std::isfinite(cHeight) &&
                                        (fabsf(cLat) > 1.0f || fabsf(cHeight) > 1.0f) && 
                                        rad > 1.0f && rad < 200.0f && len > 5.0f && len < 500.0f) {
                                        
                                        bool isWeaponStance = ((stanceFlags & 0x200) != 0) || (len > 100.0f && rad <= 25.0f);
                                        if (isWeaponStance) {
                                            float drawRad = rad * g_Calib.propRadScale;
                                            DrawDebugSphereOptimized(cDepth, cLat, cHeight, drawRad * 1.5f, 255, 30, 30, 0);
                                            foundNativeWeaponCol = true;
                                            drawnHitbox = true;
                                        }
                                    } else if (std::isfinite(cLat) && std::isfinite(cHeight) && (fabsf(cLat) > 1.0f || fabsf(cHeight) > 1.0f)) {
                                        // Projectile prop hitbox
                                        DrawDebugSphereOptimized(cDepth, cLat, cHeight, 18.0f, 255, 30, 30, 0);
                                    }
                                }
                            }
                            pNode = *(uintptr_t*)(pNode + 0x0C);
                        }
                    }

                    // Fallback for single-handed socket weapons (e.g. Scorpion b+BP sword)
                    if (!foundNativeWeaponCol && (stanceFlags & 0x200) != 0) {
                        uintptr_t entry6 = (uintptr_t)pColInfo + 0x20 + 6 * 0x28;
                        uint32_t bActive6 = *(uint32_t*)(entry6 + 0x00);
                        float d1 = *(float*)(entry6 + 0x04);
                        float l1 = *(float*)(entry6 + 0x08);
                        float h1 = *(float*)(entry6 + 0x0C);
                        float d2 = *(float*)(entry6 + 0x14);
                        float l2 = *(float*)(entry6 + 0x18);
                        float h2 = *(float*)(entry6 + 0x1C);

                        if (bActive6 == 1 && std::isfinite(d1) && std::isfinite(l1) && std::isfinite(h1) &&
                            (d1 != 0.0f || l1 != 0.0f || h1 != 0.0f)) {
                            float tipD = (d1 + d2) * 0.5f;
                            float tipL = (l1 + l2) * 0.5f + (facingSign * 25.0f);
                            float tipH = (h1 + h2) * 0.5f;
                            float drawSwordRad = 16.0f * g_Calib.propRadScale;
                            DrawDebugSphereOptimized(tipD, tipL, tipH, drawSwordRad, 255, 30, 30, 0);
                            foundNativeWeaponCol = true;
                            drawnHitbox = true;
                        }
                    }

                    // Striking Limb Hitbox: 3D Sphere Wireframe placed on the striking joint
                    if (!foundNativeWeaponCol) {
                        int targetExtremity = -1;
                        void* pInfo = *(void**)((uintptr_t)pChar + 0x7638);
                        if (IsValidUserPtr(pInfo)) {
                            uint32_t curr = *(uint32_t*)((uintptr_t)pInfo + 0x574);
                            if (curr == 0) curr = *(uint32_t*)((uintptr_t)pInfo + 0x570);
                            if (curr > 0) {
                                uintptr_t pRec = (uintptr_t)pInfo + 0x48 + (curr - 1) * 0x2C;
                                uint32_t moveId = *(uint32_t*)(pRec + 0x18);
                                switch (moveId) {
                                    case 0x39: case 0x3D: targetExtremity = 5; break; // FP (Right Hand)
                                    case 0x3A: case 0x3E: targetExtremity = 6; break; // BP (Left Hand)
                                    case 0x3B: case 0x3F: targetExtremity = 2; break; // FK (Right Foot)
                                    case 0x3C: case 0x40: targetExtremity = 3; break; // BK (Left Foot)
                                    default: break;
                                }
                            }
                        }

                        // Determine extremity with maximum extension if move ID not mapped
                        if (targetExtremity == -1) {
                            float bestReach = -9999.0f;
                            const int strikingExtremities[4] = { 5, 6, 2, 3 };
                            for (int idx = 0; idx < 4; idx++) {
                                int e = strikingExtremities[idx];
                                uintptr_t entry = (uintptr_t)pColInfo + 0x20 + e * 0x28;
                                float l1 = *(float*)(entry + 0x08);
                                float l2 = *(float*)(entry + 0x18);
                                float limbL = (std::isfinite(l2) && fabsf(l2) > 1.0f) ? l2 : l1;
                                if (std::isfinite(limbL) && fabsf(limbL) > 1.0f) {
                                    float reach = (limbL - charLat) * facingSign;
                                    if (reach > bestReach) {
                                        bestReach = reach;
                                        targetExtremity = e;
                                    }
                                }
                            }
                        }

                        if (targetExtremity >= 0 && targetExtremity < 7) {
                            uintptr_t entry = (uintptr_t)pColInfo + 0x20 + targetExtremity * 0x28;
                            float d1 = *(float*)(entry + 0x04);
                            float l1 = *(float*)(entry + 0x08);
                            float h1 = *(float*)(entry + 0x0C);
                            float d2 = *(float*)(entry + 0x14);
                            float l2 = *(float*)(entry + 0x18);
                            float h2 = *(float*)(entry + 0x1C);

                            if (std::isfinite(d1) && std::isfinite(l1) && std::isfinite(h1) &&
                                (d1 != 0.0f || l1 != 0.0f || h1 != 0.0f)) {
                                bool isLegStrike = (targetExtremity == 2 || targetExtremity == 3);
                                float forwardExt = isLegStrike ? 20.0f : 16.0f;
                                float hitRad = (isLegStrike ? 20.0f : 18.0f) * g_Calib.limbScaleY;

                                float cx = (std::isfinite(d2) && fabsf(d2) > 0.01f) ? d2 : d1;
                                float cy = (std::isfinite(l2) && fabsf(l2) > 1.0f) ? (l2 + facingSign * forwardExt) : (l1 + facingSign * (forwardExt + 8.0f));
                                float cz = (std::isfinite(h2) && fabsf(h2) > 1.0f) ? h2 : h1;

                                DrawDebugSphereOptimized(cx, cy, cz, hitRad, 255, 30, 30, 0);
                                drawnHitbox = true;
                            }
                        }
                    }

                    if (!drawnHitbox && activeHitboxes > 0) {
                        float reach = facingSign * (75.0f + g_Calib.limbForward);
                        float hitZ = isCrouched ? 32.0f : 78.0f;
                        float rad = 18.0f * g_Calib.limbScaleY;
                        DrawDebugSphereOptimized(charDepth, charLat + reach, charHeight + hitZ, rad, 255, 30, 30, 0);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        PROFILE_END(PROF_HITBOX_3D);
    }

    bool IsDrawDebugHitboxes() {
        return g_DrawDebugHitboxes.load();
    }

    void SetDrawDebugHitboxes(bool enable) {
        bool prev = g_DrawDebugHitboxes.exchange(enable);
        if (prev != enable) {
            SafeFlushDebugPrimitives();
            LogTrace(">>> Attack Hitbox (Red) Visualizer: %s <<<", enable ? "ENABLED" : "DISABLED");
        }
    }

    bool IsDrawDebugHurtboxes() {
        return g_DrawDebugHurtboxes.load();
    }

    void SetDrawDebugHurtboxes(bool enable) {
        bool prev = g_DrawDebugHurtboxes.exchange(enable);
        if (prev != enable) {
            SafeFlushDebugPrimitives();
            LogTrace(">>> Body Hurtbox (Green) Visualizer: %s <<<", enable ? "ENABLED" : "DISABLED");
        }
    }

    bool IsDrawDebugPushboxes() {
        return g_DrawDebugPushboxes.load();
    }

    void SetDrawDebugPushboxes(bool enable) {
        bool prev = g_DrawDebugPushboxes.exchange(enable);
        if (prev != enable) {
            SafeFlushDebugPrimitives();
            LogTrace(">>> Position Pushbox (Cyan) Visualizer: %s <<<", enable ? "ENABLED" : "DISABLED");
        }
    }
}

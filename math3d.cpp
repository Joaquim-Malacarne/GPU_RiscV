#include "math3d.h"
#include <cmath>

const float CUBE_VERTS[8][3] = {
    {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},  // face traseira
    {-1,-1,+1}, {+1,-1,+1}, {+1,+1,+1}, {-1,+1,+1}   // face dianteira
};

const int CUBE_EDGES[12][2] = {
    {0,1},{1,2},{2,3},{3,0},   // face traseira
    {4,5},{5,6},{6,7},{7,4},   // face dianteira
    {0,4},{1,5},{2,6},{3,7}    // arestas laterais
};

void rotate_yx(float v[3], float ax, float ay) {
    // Rotação em torno de Y
    float x =  v[0] * cosf(ay) + v[2] * sinf(ay);
    float z = -v[0] * sinf(ay) + v[2] * cosf(ay);
    v[0] = x;  v[2] = z;
    // Rotação em torno de X
    float y =  v[1] * cosf(ax) - v[2] * sinf(ax);
    z       =  v[1] * sinf(ax) + v[2] * cosf(ax);
    v[1] = y;  v[2] = z;
}

void project(const float v[3], int W, int H, float& sx, float& sy) {
    float d = 3.5f / (3.5f + v[2]);          // fator de perspectiva
    sx = ( v[0] * d * 0.45f + 0.5f) * W;
    sy = (-v[1] * d * 0.45f + 0.5f) * H;     // y invertido para coordenadas de tela
}

void bresenham(std::vector<uint32_t>& mask, int W, int H,
               int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        if (x0 >= 0 && x0 < W && y0 >= 0 && y0 < H)
            mask[y0 * W + x0] = 1;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

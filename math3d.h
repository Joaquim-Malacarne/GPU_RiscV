#ifndef MATH3D_H
#define MATH3D_H

#include <cstdint>
#include <vector>

// Vértices do cubo unitário centrado na origem
extern const float CUBE_VERTS[8][3];

// 12 arestas do cubo (pares de índices de vértice)
extern const int CUBE_EDGES[12][2];

// Aplica rotação em torno de Y (ângulo ay) seguida de X (ângulo ax) ao vértice v
void rotate_yx(float v[3], float ax, float ay);

// Projeção perspectiva: coordenadas 3D → tela (y cresce para baixo)
void project(const float v[3], int W, int H, float& sx, float& sy);

// Rasteriza um segmento de reta na máscara — layout row-major: índice = y×W + x
void bresenham(std::vector<uint32_t>& mask, int W, int H,
               int x0, int y0, int x1, int y1);

#endif // MATH3D_H

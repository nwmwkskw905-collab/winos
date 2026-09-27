#include <metal_stdlib>
using namespace metal;

// Portico — shaders do pipeline interno (subconjunto de comandos → tela).
// Vértice: posição 2D em coordenadas do jogo + cor RGB.

struct GameUniforms {
    float2 invSize;   // 1/largura, 1/altura do framebuffer interno
    float  pad0;
    float  pad1;
};

struct GameVertexIn {
    float2 pos;
    float3 color;
};

struct GameVertexOut {
    float4 pos [[position]];
    float3 color;
};

vertex GameVertexOut game_vertex(uint vid [[vertex_id]],
                                 const device GameVertexIn* verts [[buffer(0)]],
                                 constant GameUniforms& u [[buffer(1)]]) {
    GameVertexIn v = verts[vid];
    GameVertexOut out;
    float2 ndc;
    ndc.x = v.pos.x * u.invSize.x * 2.0 - 1.0;
    ndc.y = 1.0 - v.pos.y * u.invSize.y * 2.0;  // y do jogo cresce para baixo
    out.pos = float4(ndc, 0.0, 1.0);
    out.color = v.color;
    return out;
}

fragment float4 game_fragment(GameVertexOut in [[stage_in]]) {
    return float4(in.color, 1.0);
}

// Upscale do framebuffer interno para o drawable (blit com filtro).

struct BlitVertexOut {
    float4 pos [[position]];
    float2 uv;
};

vertex BlitVertexOut blit_vertex(uint vid [[vertex_id]]) {
    // triângulo grande cobrindo a tela
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    float2 uvs[3] = { float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0) };
    BlitVertexOut out;
    out.pos = float4(positions[vid], 0.0, 1.0);
    out.uv = uvs[vid];
    return out;
}

fragment float4 blit_fragment(BlitVertexOut in [[stage_in]],
                              texture2d<float> tex [[texture(0)]],
                              sampler s [[sampler(0)]]) {
    return tex.sample(s, in.uv);
}

#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut vertexShader(uint vertexID [[vertex_id]]) {
    float2 pos[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0),
        float2(-1.0, 1.0),  float2(1.0, -1.0), float2(1.0, 1.0)
    };
    float2 tex[6] = {
        float2(0.0, 1.0), float2(1.0, 1.0), float2(0.0, 0.0),
        float2(0.0, 0.0), float2(1.0, 1.0), float2(1.0, 0.0)
    };
    VertexOut out;
    out.position = float4(pos[vertexID], 0.0, 1.0);
    out.texCoord = tex[vertexID];
    return out;
}

fragment float4 yuvToRGBFragmentShader(VertexOut in [[stage_in]],
                                       texture2d<float> yTex [[texture(0)]],
                                       texture2d<float> uTex [[texture(1)]],
                                       texture2d<float> vTex [[texture(2)]]) {
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    float y = yTex.sample(s, in.texCoord).r;
    float u = uTex.sample(s, in.texCoord).r;
    float v = vTex.sample(s, in.texCoord).r;

    // BT.709 YUV limited range to RGB
    float c = y - 16.0 / 255.0;
    float d = u - 0.5;
    float e = v - 0.5;

    float r = 1.164 * c + 1.793 * e;
    float g = 1.164 * c - 0.213 * d - 0.533 * e;
    float b = 1.164 * c + 2.112 * d;

    return float4(clamp(r, 0.0, 1.0),
                  clamp(g, 0.0, 1.0),
                  clamp(b, 0.0, 1.0), 1.0);
}

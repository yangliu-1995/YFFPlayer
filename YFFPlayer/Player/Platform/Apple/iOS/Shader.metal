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

// YUV420P (3个分离平面) fragment shader
fragment float4 yuvToRGBFragmentShader(VertexOut in [[stage_in]],
                                       texture2d<float> yTex [[texture(0)]],
                                       texture2d<float> uTex [[texture(1)]],
                                       texture2d<float> vTex [[texture(2)]]) {
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    float y = yTex.sample(s, in.texCoord).r;
    float u = uTex.sample(s, in.texCoord).r;
    float v = vTex.sample(s, in.texCoord).r;

    // BT.709 YUV full range to RGB (VideoToolbox通常输出full range)
    float r = y + 1.402 * (v - 0.5);
    float g = y - 0.344 * (u - 0.5) - 0.714 * (v - 0.5);
    float b = y + 1.772 * (u - 0.5);

    return float4(clamp(r, 0.0, 1.0),
                  clamp(g, 0.0, 1.0),
                  clamp(b, 0.0, 1.0), 1.0);
}

// NV12 (2个平面，UV交错) fragment shader
fragment float4 nv12ToRGBFragmentShader(VertexOut in [[stage_in]],
                                        texture2d<float> yTex [[texture(0)]],
                                        texture2d<float> uvTex [[texture(1)]]) {
    constexpr sampler s(address::clamp_to_edge, filter::linear);
    float y = yTex.sample(s, in.texCoord).r;
    float2 uv = uvTex.sample(s, in.texCoord).rg;
    float u = uv.r;
    float v = uv.g;

    // BT.709 YUV full range to RGB
    float r = y + 1.402 * (v - 0.5);
    float g = y - 0.344 * (u - 0.5) - 0.714 * (v - 0.5);
    float b = y + 1.772 * (u - 0.5);

    return float4(clamp(r, 0.0, 1.0),
                  clamp(g, 0.0, 1.0),
                  clamp(b, 0.0, 1.0), 1.0);
}

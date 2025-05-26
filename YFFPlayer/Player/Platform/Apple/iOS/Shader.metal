#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut vertexShader(uint vertexID [[vertex_id]],
                            constant vector_uint2 *viewportSize [[buffer(0)]]) {
    float2 pixelSpacePosition = float2(vertexID == 0 || vertexID == 1 ? 0.0 : 1.0,
                                      vertexID == 0 || vertexID == 2 ? 1.0 : 0.0); // 翻转 y 坐标

    float2 viewportSizeF = float2(*viewportSize);
    float2 position = pixelSpacePosition * 2.0 - 1.0;

    VertexOut out;
    out.position = float4(position, 0.0, 1.0);
    out.texCoord = float2(pixelSpacePosition.x, 1.0 - pixelSpacePosition.y); // 再次确保纹理 y 坐标翻转
    return out;
}

fragment float4 yuvToRGBFragmentShader(VertexOut in [[stage_in]],
                                      texture2d<float> yTexture [[texture(0)]],
                                      texture2d<float> uTexture [[texture(1)]],
                                      texture2d<float> vTexture [[texture(2)]]) {
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);

    float y = yTexture.sample(textureSampler, in.texCoord).r;
    float u = uTexture.sample(textureSampler, in.texCoord).r - 0.5;
    float v = vTexture.sample(textureSampler, in.texCoord).r - 0.5;

    // YUV to RGB 转换矩阵
    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;

    return float4(r, g, b, 1.0);
}

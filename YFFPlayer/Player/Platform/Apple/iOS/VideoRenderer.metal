#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

struct ColorUniforms {
    float brightness;
    float contrast;
    float saturation;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

// Color adjustment helper function
float4 applyColorAdjustments(float4 color, constant ColorUniforms &uniforms) {
    // Apply brightness
    color.rgb += uniforms.brightness;
    
    // Apply contrast
    color.rgb = (color.rgb - 0.5) * uniforms.contrast + 0.5;
    
    // Apply saturation
    float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    color.rgb = mix(float3(gray), color.rgb, uniforms.saturation);
    
    // Clamp values
    color.rgb = clamp(color.rgb, 0.0, 1.0);
    
    return color;
}

// Fragment shader for YUV420P format
fragment float4 fragment_yuv420p(VertexOut in [[stage_in]],
                                texture2d<float> yTexture [[texture(0)]],
                                texture2d<float> uTexture [[texture(1)]],
                                texture2d<float> vTexture [[texture(2)]],
                                constant ColorUniforms &uniforms [[buffer(0)]]) {
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);
    
    // Sample YUV components
    float y = yTexture.sample(textureSampler, in.texCoord).r;
    float u = uTexture.sample(textureSampler, in.texCoord).r - 0.5;
    float v = vTexture.sample(textureSampler, in.texCoord).r - 0.5;
    
    // BT.709 YUV to RGB conversion matrix
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    float4 color = float4(r, g, b, 1.0);
    
    return applyColorAdjustments(color, uniforms);
}

// Fragment shader for NV12 format
fragment float4 fragment_nv12(VertexOut in [[stage_in]],
                             texture2d<float> yTexture [[texture(0)]],
                             texture2d<float> uvTexture [[texture(1)]],
                             constant ColorUniforms &uniforms [[buffer(0)]]) {
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);
    
    // Sample Y and UV components
    float y = yTexture.sample(textureSampler, in.texCoord).r;
    float2 uv = uvTexture.sample(textureSampler, in.texCoord).rg;
    
    float u = uv.r - 0.5;
    float v = uv.g - 0.5;
    
    // BT.709 YUV to RGB conversion matrix
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    float4 color = float4(r, g, b, 1.0);
    
    return applyColorAdjustments(color, uniforms);
}

// Fragment shader for RGB24 format
fragment float4 fragment_rgb24(VertexOut in [[stage_in]],
                              texture2d<float> rgbTexture [[texture(0)]],
                              constant ColorUniforms &uniforms [[buffer(0)]]) {
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);
    
    // Sample RGB color
    float4 color = rgbTexture.sample(textureSampler, in.texCoord);
    
    return applyColorAdjustments(color, uniforms);
}
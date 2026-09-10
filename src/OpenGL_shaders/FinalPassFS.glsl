#version 140

uniform sampler2D WideInputTex;
uniform bool uWideScene;
uniform float uWideRatio;
uniform ivec2 uWideBrightness;
uniform sampler2D MainInputTexA;
uniform sampler2D MainInputTexB;
uniform sampler2DArray AuxInputTex;

layout(std140) uniform ubFinalPassConfig
{
    bvec4 uScreenSwap[48]; // one bool per scanline
    int uScaleFactor;
    int uAuxLayer;
    int uDispModeA;
    int uDispModeB;
    int uBrightModeA;
    int uBrightModeB;
    int uBrightFactorA;
    int uBrightFactorB;
    float uAuxColorFactor;
};

smooth in vec3 fTexcoord;

out vec4 oTopColor;
out vec4 oBottomColor;

ivec3 MasterBrightness(ivec3 color, int brightmode, int evy)
{
    if (brightmode == 1)
    {
        // up
        color += (((0x3F - color) * evy) >> 4);
    }
    else if (brightmode == 2)
    {
        // down
        color -= (((color * evy) + 0xF) >> 4);
    }

    return color;
}

void main()
{
    vec2 nativeCoord = vec2((fTexcoord.x - 0.5) * uWideRatio + 0.5, fTexcoord.y);
    bool wing = nativeCoord.x < 0.0 || nativeCoord.x >= 1.0;
    ivec4 col_main = ivec4(texture(MainInputTexA, nativeCoord, 0) * 255.0) >> 2;
    ivec4 col_sub = ivec4(texture(MainInputTexB, nativeCoord, 0) * 255.0) >> 2;
    if (wing) {
        col_main = uWideScene ? ivec4(texture(WideInputTex, fTexcoord.xy) * 255.0) >> 2 : ivec4(0);
        col_sub = ivec4(0);
        if (uWideScene && uWideBrightness.x == 2)
            col_main += (((63 - col_main) * uWideBrightness.y + 8) >> 4);
        else if (uWideScene && uWideBrightness.x == 3)
            col_main -= ((col_main * uWideBrightness.y + 7) >> 4);
    }

    ivec3 output_main, output_sub;

    if (uDispModeA == 0)
    {
        // screen disabled (white)
        output_main = ivec3(63, 63, 63);
    }
    else if (uDispModeA == 1)
    {
        // BG/OBJ layers
        output_main = col_main.rgb;
    }
    else
    {
        // VRAM display / mainmem FIFO
        output_main = ivec3(texture(AuxInputTex, vec3(nativeCoord.x, fTexcoord.z, uAuxLayer)).rgb * uAuxColorFactor);
    }

    if (uDispModeB == 0)
    {
        // screen disabled (white)
        output_sub = ivec3(63, 63, 63);
    }
    else
    {
        // BG/OBJ layers
        output_sub = col_sub.rgb;
    }

    if (wing && !uWideScene) output_main = ivec3(0);
    if (wing) output_sub = ivec3(0);

    if (uDispModeA != 0)
        output_main = MasterBrightness(output_main, uBrightModeA, uBrightFactorA);
    if (uDispModeB != 0)
        output_sub = MasterBrightness(output_sub, uBrightModeB, uBrightFactorB);

    output_main = (output_main << 2) | (output_main >> 6);
    output_sub = (output_sub << 2) | (output_sub >> 6);

    int line = int(fTexcoord.y * 192);
    bool swapbit = uScreenSwap[line>>2][line&0x3];

    if (!swapbit)
    {
        oTopColor = vec4(vec3(output_sub) / 255.0, 1.0);
        oBottomColor = vec4(vec3(output_main) / 255.0, 1.0);
    }
    else
    {
        oTopColor = vec4(vec3(output_main) / 255.0, 1.0);
        oBottomColor = vec4(vec3(output_sub) / 255.0, 1.0);
    }
}

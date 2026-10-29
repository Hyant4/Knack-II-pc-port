#version 450
layout(location=0) out vec2 packed_parameter;
layout(location=1) out float ordinary_parameter;
void main() {
    const vec2 xy[3] = vec2[3](vec2(-1,-1),vec2(1,-1),vec2(-1,1));
    const float w[3] = float[3](1,2,4);
    const vec3 normal[3] = vec3[3](vec3(1,0,0),vec3(0,1,0),vec3(0,0,1));
    uint i=gl_VertexIndex;
    gl_Position=vec4(xy[i]*w[i],0,w[i]);
    packed_parameter=uintBitsToFloat(uvec2(packHalf2x16(normal[i].xy),packHalf2x16(vec2(normal[i].z,0))));
    ordinary_parameter=float(i+1)*0.25;
}

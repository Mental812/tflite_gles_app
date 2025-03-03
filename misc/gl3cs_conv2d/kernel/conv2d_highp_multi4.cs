#version 310 es
layout(local_size_x = 8, local_size_y = 4, local_size_z = 8) in;
layout(std430) buffer;
precision highp float;

layout(binding = 1) writeonly buffer B1 { vec4 data[]; } output_data_0;
layout(binding = 0) readonly  buffer B0 { vec4 data[]; } input_data_0;
layout(binding = 3) readonly  buffer B3 { vec4 data[]; } bias;
layout(binding = 2) readonly  buffer B2 { vec4 data[]; } weights;


uniform float clip;
uniform int input_data_0_h;
uniform int input_data_0_w;
uniform int output_data_0_h;
uniform int output_data_0_w;
uniform int src_depth;
uniform int weights_h;
uniform int weights_w;
uniform int workload_x;
uniform int workload_y;
uniform int workload_z;

void main() {

  ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);
  if (gid.x >= workload_x || gid.y >= workload_y || gid.z >= workload_z) {
    return;
  }

  /* convolution */
  highp vec4 value_0 = vec4(0);
  highp vec4 result0 = vec4(0);
  highp vec4 result1 = vec4(0);
  highp vec4 result2 = vec4(0);
  highp vec4 result3 = vec4(0);
  vec4 f;
  for (int l = 0; l < src_depth; ++l) {
    vec4 input0 = input_data_0.data[gid.x * 4 + 0 + input_data_0_w * (gid.y + input_data_0_h * (l))];
    vec4 input1 = input_data_0.data[gid.x * 4 + 1 + input_data_0_w * (gid.y + input_data_0_h * (l))];
    vec4 input2 = input_data_0.data[gid.x * 4 + 2 + input_data_0_w * (gid.y + input_data_0_h * (l))];
    vec4 input3 = input_data_0.data[gid.x * 4 + 3 + input_data_0_w * (gid.y + input_data_0_h * (l))];

    f = weights.data[0 + weights_w * (l + weights_h * gid.z)];
    result0[0] += dot(input0, f);
    result1[0] += dot(input1, f);
    result2[0] += dot(input2, f);
    result3[0] += dot(input3, f);

    f = weights.data[1 + weights_w * (l + weights_h * gid.z)];
    result0[1] += dot(input0, f);
    result1[1] += dot(input1, f);
    result2[1] += dot(input2, f);
    result3[1] += dot(input3, f);

    f = weights.data[2 + weights_w * (l + weights_h * gid.z)];
    result0[2] += dot(input0, f);
    result1[2] += dot(input1, f);
    result2[2] += dot(input2, f);
    result3[2] += dot(input3, f);

    f = weights.data[3 + weights_w * (l + weights_h * gid.z)];
    result0[3] += dot(input0, f);
    result1[3] += dot(input1, f);
    result2[3] += dot(input2, f);
    result3[3] += dot(input3, f);
  }

  /* add bias */
  vec4 b = bias.data[gid.z];
  result0 += b;
  result1 += b;
  result2 += b;
  result3 += b;

  /* Relu6 */
  result0 = clamp(result0, vec4(0.0), vec4(clip));
  output_data_0.data[gid.x * 4 + 0 + output_data_0_w * (gid.y + output_data_0_h * (gid.z))] = result0;

  result1 = clamp(result1, vec4(0.0), vec4(clip));
  output_data_0.data[gid.x * 4 + 1 + output_data_0_w * (gid.y + output_data_0_h * (gid.z))] = result1;

  result2 = clamp(result2, vec4(0.0), vec4(clip));
  output_data_0.data[gid.x * 4 + 2 + output_data_0_w * (gid.y + output_data_0_h * (gid.z))] = result2;

  result3 = clamp(result3, vec4(0.0), vec4(clip));
  output_data_0.data[gid.x * 4 + 3 + output_data_0_w * (gid.y + output_data_0_h * (gid.z))] = result3;
}


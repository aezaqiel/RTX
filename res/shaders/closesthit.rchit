#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hit_value;
hitAttributeEXT vec2 attribs;

void main()
{
    const vec3 barycentric = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    hit_value = barycentric;
}

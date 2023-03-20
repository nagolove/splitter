#version 100

precision mediump float;

varying vec2 fragTexCoord;
varying vec4 fragColor;

//out vec4 finalColor;
//uniform vec2 displacement;

uniform sampler2D texture0;
uniform sampler2D mask_texture;

void main()
{
    vec2 uv = fragTexCoord;

    vec4 col = texture2D(texture0, uv).rgba;
    vec4 stencil_col = texture2D(mask_texture, uv).rgba;

    if (stencil_col.r > 0.5 &&
        stencil_col.g > 0.5 &&
        stencil_col.g > 0.5)
        col.a = 0.;

    gl_FragColor = col;
    gl_FragColor = vec4(uv.x, uv.y, 1., 1.);
}


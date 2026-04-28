#if (!defined(GL_ES) && __VERSION__ >= 130) || (defined(GL_ES) && __VERSION__ >= 300)
    #define NEW_GLSL
#endif

#if !defined(GL_ES) && defined(GL_ARB_explicit_attrib_location) && !defined(DISABLE_GL_ARB_explicit_attrib_location)
    #extension GL_ARB_explicit_attrib_location: enable
    #define EXPLICIT_ATTRIB_LOCATION
#endif

#if !defined(GL_ES) && defined(GL_ARB_shading_language_420pack) && !defined(DISABLE_GL_ARB_shading_language_420pack)
    #extension GL_ARB_shading_language_420pack: enable
    #define RUNTIME_CONST
    #define EXPLICIT_TEXTURE_LAYER
#endif

#if !defined(GL_ES) && defined(GL_ARB_explicit_uniform_location) && !defined(DISABLE_GL_ARB_explicit_uniform_location)
    #extension GL_ARB_explicit_uniform_location: enable
    #define EXPLICIT_UNIFORM_LOCATION
#endif

#if defined(GL_ES) && __VERSION__ >= 300
    #define EXPLICIT_ATTRIB_LOCATION
    /* EXPLICIT_TEXTURE_LAYER, EXPLICIT_UNIFORM_LOCATION and RUNTIME_CONST is not
       available in OpenGL ES */
#endif

/* Precision qualifiers are not supported in GLSL 1.20 */
#if !defined(GL_ES) && __VERSION__ == 120
    #define highp
    #define mediump
    #define lowp
#endif

#ifndef NEW_GLSL
#define in varying
#define fragmentColor gl_FragColor
#endif

/* Uniform Buffers */

#define MAX_COLOR_LAYERS 8

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 1)
#endif
uniform highp vec4 layerColors[MAX_COLOR_LAYERS];

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 9)
#endif
uniform highp float layerThresholds[MAX_COLOR_LAYERS];

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 17)
#endif
uniform int layerCount;

#ifdef EXPLICIT_UNIFORM_LOCATION
layout(location = 18)
#endif
uniform highp float layerSmoothness;

/* Inputs */

in highp float interpolatedLight;
in highp float worldHeight;

/* Outputs */

#ifdef NEW_GLSL
#ifdef EXPLICIT_ATTRIB_LOCATION
layout(location = 0)
#endif
out highp vec4 fragmentColor;
#endif

void main()
{
    highp vec3 col = layerColors[0].rgb;

    for(int i = 1; i < MAX_COLOR_LAYERS; ++i)
    {
        if(i >= layerCount) break;

        highp float t = layerThresholds[i];

        if(layerSmoothness > 0.0)
        {
            highp float blend = smoothstep(t - layerSmoothness, t + layerSmoothness, worldHeight);
            col = mix(col, layerColors[i].rgb, blend);
        }
        else if(worldHeight >= t)
        {
            col = layerColors[i].rgb;
        }
    }

    highp float light;

    if(gl_FrontFacing)
    {
        light = interpolatedLight;
    }
    else
    {
        light = (1.0 - interpolatedLight) * 0.08;
    }

    fragmentColor = vec4(col * light, 1.0);
}

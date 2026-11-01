# `*.material.json` material — format `arcane.material`, version 2

Schema: `schemas/arcane.material.schema.json`. Rules shared with all files: `canonical-json.md`.
The file name is free (`glow.material.json`). A material names a GLSL fragment shader file and
the values of its uniforms; it is used for sprites and for post-process passes. Migration ignores
material files.

```json
{
  "blend": "additive",
  "format": "arcane.material",
  "params": [
    { "name": "glow", "value": [1.0, 0.5, 0.25] },
    { "name": "strength", "value": [0.75] }
  ],
  "shader": "assets/glow.frag",
  "version": 2
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `shader` | project-relative path | a `.frag` file (fragment shader body, see below); required |
| `blend` | `"alpha"` \| `"additive"` \| `"opaque"` | default `alpha` on read; always written |
| `params[].name` | string | GLSL identifier, not starting with `u_` or `gl_`; unique, sorted bytewise |
| `params[].value` | 1 to 4 finite numbers | binary32; the count picks the uniform type: 1 `float`, 2 `vec2`, 3 `vec3`, 4 `vec4` |

Unknown fields are errors. `u_` is reserved for engine uniforms.

## GLSL contract

The `.frag` file is GLSL 330 core fragment source holding only the shader body (no `#version`,
no declarations of the names below). The engine prepends:

```glsl
#version 330 core
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform vec2 u_resolution;
uniform <type> <name>;   // one per param, type from the value count
```

The file must define `void main()` and write `frag_color`. `u_time` is seconds of game time,
`u_resolution` the render target size in pixels.

- Sprites: `u_texture` is the sprite texture (atlas region UVs already applied to `v_uv`) and
  `v_color` the sprite tint.
- Post-process: `u_texture` is the previous pass (or the scene) and `v_color` is white
  (`vec4(1.0)`).

Blend modes: `alpha` is straight alpha blending, `additive` adds the source (times alpha) to the
target, `opaque` disables blending.

## Errors

Reading fails (and `validate` reports) when:

- the file is not canonical-format JSON, has an unknown field, or is not `arcane.material` version 2;
- `shader` is missing, is not a project-relative path, or does not end in `.frag`;
- `blend` is not one of the three names;
- a param has an empty or non-identifier name, a `u_` or `gl_` prefix, or a duplicate name;
- a param value is not an array of 1 to 4 finite numbers.

`validate` also reports a `shader` file that does not exist in the project.

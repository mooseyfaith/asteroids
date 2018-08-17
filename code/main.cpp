#include <platform.h>
#include <memory_growing_stack.h>
#include <immediate_render.h>
#include <ui_render.h>
#include <mesh.h>
#include <tga.h>

#include <stdlib.h>

#define Template_Geometry_Dimension_Count 3
#include "geometry.h"

struct Ship_Entity;

enum Entity_Kind {
    Ship_Kind = 0,
    Asteroid_Kind,
    Bullet_Kind,
    Entity_Kind_Count,
};

struct Entity {
    mat4x3f to_world_transform;
    f32 orientation;
    f32 scale;
    f32 radius;
    
    vec3f velocity;
    vec3f angular_rotation_axis;
    f32   angular_velocity;
    
    vec4 diffuse_color;
    vec4 specular_color;
    bool is_light;
    u32 kind;
    Mesh *mesh;
    Ship_Entity *ship;
    Entity *parent;
    bool mark_for_destruction;
    
    u32 hp;
};

struct Body {
    Sphere3f sphere;
    vec3f velocity;
    //vec3f next_center;
    //vec3f next_velocity;
    
    f32 velocity_accumulated_orientation;
    u32 velocity_change_count;
    
    //f32 max_timestep;
    u32 entity_index;
    u32 first_clone_index;
    bool destroy_on_collision;
    bool was_destroyed;
};

#define Template_Array_Type      Body_Array
#define Template_Array_Data_Type Body
#include "template_array.h"

struct Clone_Body {
    Sphere3f sphere;
    u32 body_index;
    u32 offset_to_next_body;
};

#define Template_Array_Type      Clone_Body_Array
#define Template_Array_Data_Type Clone_Body
#include "template_array.h"

struct Collision_Pair {
    Body *body_pair[2];
    Sphere3f spheres[2];
    u32 collision_kind;
};

#define Template_Array_Type      Collision_Pair_Array
#define Template_Array_Data_Type Collision_Pair
#include "template_array.h"

struct Ship_Entity {
    Entity *entity;
    f32 thruster_intensity;
};

struct Draw_Entity {
    mat4x3f to_world_transform;
    Mesh *mesh;
    vec4f color;
    f32 shininess;
};

struct Light_Entity {
    vec3f world_position;
    vec4f diffuse_color;
    vec4f specular_color;
    f32   attenuation;
};

#define Template_Array_Type Entity_Buffer
#define Template_Array_Data_Type Entity
#define Template_Array_Is_Buffer
#include "template_array.h"

#define Template_Array_Type      Draw_Entity_Buffer
#define Template_Array_Data_Type Draw_Entity
#define Template_Array_Is_Buffer
#include "template_array.h"

#define Template_Array_Type      Light_Entity_Buffer
#define Template_Array_Data_Type Light_Entity
#define Template_Array_Is_Buffer
#include "template_array.h"

// all aliginged to vec4, since layout (std140) sux!!!
struct Camera_Uniform_Block {
    mat4f camera_to_clip_projection;
    // actually a 4x3 but after each vec3 ther is 1 f32 padding
    mat4f world_to_camera_transform;
    vec3f camera_world_position;
    f32 padding0;
};

#define MAX_LIGHT_COUNT 10

struct Lighting_Uniform_Block {
    vec4f diffuse_colors[MAX_LIGHT_COUNT];
    vec4f specular_colors[MAX_LIGHT_COUNT];
    vec4f world_positions_and_attenuations[MAX_LIGHT_COUNT];
    u32 count;
    u32 padding[3];
};

enum {
    Camera_Uniform_Block_Index = 0,
    Lighting_Uniform_Block_Index,
};

struct Application_State {
    Memory_Growing_Stack_Allocator_Info persistent_memory;
    Memory_Growing_Stack_Allocator_Info transient_memory;
    
    Pixel_Rectangle main_window_area;
    bool main_window_is_fullscreen;
    
    Immediate_Render_Context immediate_render_context;
    UI_Render_Context ui_render_context;
    
    struct UI_Font_Material {
        Render_Material base;
        
        struct Shader {
            GLuint program_object;
            
            union {
                struct {
                    GLint u_texture;
                    GLint u_alpha_threshold;
                };
                GLint uniforms[2];
            };
        } shader;
        
        Texture *texture;
    } ui_font_material;
    
    FT_Library font_lib;
    Font font;
    
    mat4f camera_to_clip_projection;
    mat4f clip_to_camera_projection;
    
    struct Camera {
        union { mat4x3f to_world_transform, inverse_view_matrix; };
    } debug_camera, camera;
    
    union { mat4x3f world_to_camera_transform, view_matrix; };
    
    f32 debug_camera_alpha;
    f32 debug_camera_beta;
    
    vec2f last_mouse_window_position;
    
    Mesh ship_mesh, asteroid_mesh, beam_mesh, planet_mesh;
    
    union {
        struct {
            GLuint projection_uniform_buffer_object;
            GLuint lighting_uniform_buffer_object;
        };
        
        GLuint uniform_buffer_objects[2];
    };
    
    struct {
        GLuint program_object;
        
        union {
            
#define PHONG_UNIFORMS \
            u_object_to_world_transform, \
            u_bone_transforms, \
            u_shininess, \
            u_ambient_color, \
            u_diffuse_texture, \
            u_diffuse_color, \
            u_normal_map
            
            struct { GLint PHONG_UNIFORMS; };
            
            // sadly we cannot automate this,
            // but make_shader_program will catch a missmatch
            // while parsing the uniform_names string
            GLint uniforms[7];
        };
        
        GLuint camera_uniform_block;
        GLuint lighting_uniform_block;
        
    } phong_shader;
    
    struct {
        GLuint program_object;
        
        union {
            
#define WATER_SHADER_UNIFORMS \
            u_object_to_world_transform, \
            u_bone_transforms, \
            u_phase, \
            u_shininess, \
            u_ambient_color, \
            u_diffuse_texture, \
            u_diffuse_color, \
            u_normal_map
            
            struct { GLint WATER_SHADER_UNIFORMS; };
            
            // sadly we cannot automate this,
            // but make_shader_program will catch a missmatch
            // while parsing the uniform_names string
            GLint uniforms[8];
        };
        
        GLuint camera_uniform_block;
        GLuint lighting_uniform_block;
        
    } water_shader;
    
    Texture asteroid_normal_map;
    Texture asteroid_ambient_occlusion_map;
    Entity_Buffer entities;
    
    Ship_Entity ship;
    Entity *ship_thrusters;
    Entity *beam;
    
    u8_array *debug_mesh_vertex_buffers;
    u32 debug_mesh_vertex_count;
    
    bool in_debug_mode;
    bool debug_use_game_controls;
    bool pause_game;
};

Pixel_Dimensions const Reference_Resolution = { 1280, 720 };

f32 const Debug_Camera_Move_Speed = 50.0f;
f32 const Debug_Camera_Mouse_Sensitivity = 2.0f * PIf / 2048.0f;
vec3f const Debug_Camera_Axis_Alpha = VEC3_Z_AXIS;
vec3f const Debug_Camera_Axis_Beta  = VEC3_X_AXIS;

void debug_update_camera(Application_State *state) {
    quatf rotation = make_quat(Debug_Camera_Axis_Alpha, state->debug_camera_alpha);
    rotation = multiply(rotation, make_quat(Debug_Camera_Axis_Beta, state->debug_camera_beta));
    
    state->debug_camera.to_world_transform = make_transform(rotation, state->debug_camera.to_world_transform.translation);
}

void bind_ui_font_material(any new_material_pointer, any old_material_pointer) {
    Application_State::UI_Font_Material *new_material = CAST_P(Application_State::UI_Font_Material, new_material_pointer);
    
    // shader
    if (!old_material_pointer) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
        
        glUseProgram(new_material->shader.program_object);
        glActiveTexture(GL_TEXTURE0 + 0);
        glUniform1i(new_material->shader.u_texture, 0);
        glUniform1f(new_material->shader.u_alpha_threshold, 0.0f);
    }
    
    // material
    glBindTexture(GL_TEXTURE_2D, new_material->texture->object);
}

void load_phong_shader(Application_State *state, Platform_API *platform_api)
{
    defer { assert(state->phong_shader.program_object); };
    
    string shader_source = platform_api->read_file(S("shaders/phong.shader.txt"), &state->transient_memory.allocator);
    assert(shader_source.count);
    
    defer { free(&state->transient_memory.allocator, shader_source.data); };
    
    Shader_Attribute_Info attributes[] = {
        { Vertex_Position_Index, "a_position" },
        { Vertex_Normal_Index,   "a_normal" },
        { Vertex_Tangent_Index,  "a_tangent" },
        { Vertex_UV_Index,       "a_uv" },
    };
    
    string uniform_names = S(STRINGIFY(PHONG_UNIFORMS));
    
    string global_defines = S(
        "#version 150\n"
        "#define MAX_LIGHT_COUNT 10\n"
        "#define WITH_DIFFUSE_COLOR\n"
        //"#define WITH_DIFFUSE_TEXTURE\n"
        "#define WITH_NORMAL_MAP\n"
        //"#define TANGENT_TRANSFORM_PER_FRAGMENT\n"
        );
    
    string vertex_shader_sources[] = {
        global_defines,
        S("#define VERTEX_SHADER\n"),
        shader_source,
    };
    
    string fragment_shader_sources[] = {
        global_defines,
        S("#define FRAGMENT_SHADER\n"),
        shader_source,
    };
    
    GLuint shader_objects[2];
    shader_objects[0] = make_shader_object(GL_VERTEX_SHADER, ARRAY_WITH_COUNT(vertex_shader_sources), &state->transient_memory.allocator);
    shader_objects[1] = make_shader_object(GL_FRAGMENT_SHADER, ARRAY_WITH_COUNT(fragment_shader_sources), &state->transient_memory.allocator);
    
    if (!shader_objects[0] || !shader_objects[1])
        return;
    
    GLint uniforms[ARRAY_COUNT(state->phong_shader.uniforms)];
    
    GLuint program_object = make_shader_program(ARRAY_WITH_COUNT(shader_objects), true, ARRAY_WITH_COUNT(attributes), uniform_names, ARRAY_WITH_COUNT(uniforms), &state->transient_memory.allocator
                                                );
    
    if (program_object) {
        if (state->phong_shader.program_object) {
            glUseProgram(0);
            glDeleteProgram(state->phong_shader.program_object);
        }
        
        state->phong_shader.camera_uniform_block = glGetUniformBlockIndex(program_object, "Camera_Uniform_Block");
        glUniformBlockBinding(program_object, state->phong_shader.camera_uniform_block, Camera_Uniform_Block_Index);
        
        state->phong_shader.lighting_uniform_block = glGetUniformBlockIndex(program_object, "Lighting_Uniform_Block");
        glUniformBlockBinding(program_object, state->phong_shader.lighting_uniform_block, Lighting_Uniform_Block_Index);
        
        
        state->phong_shader.program_object = program_object;
        COPY(state->phong_shader.uniforms, uniforms, sizeof(uniforms));
    }
}

void load_water_shader(Application_State *state, Platform_API *platform_api)
{
    defer { assert(state->water_shader.program_object); };
    
    string shader_source = platform_api->read_file(S("shaders/water.shader.txt"), &state->transient_memory.allocator);
    assert(shader_source.count);
    
    defer { free(&state->transient_memory.allocator, shader_source.data); };
    
    Shader_Attribute_Info attributes[] = {
        { Vertex_Position_Index, "a_position" },
        { Vertex_Normal_Index,   "a_normal" },
        { Vertex_Tangent_Index,  "a_tangent" },
        { Vertex_UV_Index,       "a_uv" },
    };
    
    string uniform_names = S(STRINGIFY(WATER_SHADER_UNIFORMS));
    
    string global_defines = S(
        "#version 150\n"
        "#define MAX_LIGHT_COUNT 10\n"
        "#define WITH_DIFFUSE_COLOR\n"
        //"#define WITH_DIFFUSE_TEXTURE\n"
        //"#define WITH_NORMAL_MAP\n"
        //"#define TANGENT_TRANSFORM_PER_FRAGMENT\n"
        );
    
    string vertex_shader_sources[] = {
        global_defines,
        S("#define VERTEX_SHADER\n"),
        shader_source,
    };
    
    string fragment_shader_sources[] = {
        global_defines,
        S("#define FRAGMENT_SHADER\n"),
        shader_source,
    };
    
    GLuint shader_objects[2];
    shader_objects[0] = make_shader_object(GL_VERTEX_SHADER, ARRAY_WITH_COUNT(vertex_shader_sources), &state->transient_memory.allocator);
    shader_objects[1] = make_shader_object(GL_FRAGMENT_SHADER, ARRAY_WITH_COUNT(fragment_shader_sources), &state->transient_memory.allocator);
    
    if (!shader_objects[0] || !shader_objects[1])
        return;
    
    GLint uniforms[ARRAY_COUNT(state->water_shader.uniforms)];
    
    GLuint program_object = make_shader_program(ARRAY_WITH_COUNT(shader_objects), true, ARRAY_WITH_COUNT(attributes), uniform_names, ARRAY_WITH_COUNT(uniforms), &state->transient_memory.allocator
                                                );
    
    if (program_object) {
        if (state->water_shader.program_object) {
            glUseProgram(0);
            glDeleteProgram(state->water_shader.program_object);
        }
        
        state->water_shader.camera_uniform_block = glGetUniformBlockIndex(program_object, "Camera_Uniform_Block");
        glUniformBlockBinding(program_object, state->water_shader.camera_uniform_block, Camera_Uniform_Block_Index);
        
        state->water_shader.lighting_uniform_block = glGetUniformBlockIndex(program_object, "Lighting_Uniform_Block");
        glUniformBlockBinding(program_object, state->water_shader.lighting_uniform_block, Lighting_Uniform_Block_Index);
        
        state->water_shader.program_object = program_object;
        COPY(state->water_shader.uniforms, uniforms, sizeof(uniforms));
    }
}

f32 random_f32(f32 min, f32 max) {
    return (f32)rand() * (max - min) / RAND_MAX + min;
}

vec3f random_unit_vector(bool random_x = true, bool random_y = true, bool random_z = true) {
    vec3f result;
    
    if (random_x)
        result.x = random_f32(-1.0f, 1.0f);
    else
        result.x = 0.0f;
    
    if (random_y)
        result.y = random_f32(-1.0f, 1.0f);
    else
        result.y = 0.0f;
    
    if (random_z)
        result.z = random_f32(-1.0f, 1.0f);
    else
        result.z = 0.0f;
    
    return normalize_or_zero(result);
}

u32 get_collision_kind(u32 entity_kind_a, u32 entity_kind_b) {
    if (entity_kind_a < entity_kind_b)
        return entity_kind_a * (Entity_Kind_Count - (entity_kind_a - 1) * 0.5f) + entity_kind_b - entity_kind_a;
    else
        return entity_kind_b * (Entity_Kind_Count - (entity_kind_b - 1) * 0.5f) + entity_kind_a - entity_kind_b;
}

u32 no_collision_kind() {
    return get_collision_kind(Entity_Kind_Count, Entity_Kind_Count);
}

void spawn_asteroid(Application_State *state, vec3f position, vec3f velocity, f32 scale, vec4f color) {
    Entity *asteroid = push(&state->entities, {});
    asteroid->velocity = random_unit_vector(true, true, false) * random_f32(3.0f, 10.0f);
    asteroid->diffuse_color = color;
    asteroid->kind = Asteroid_Kind;
    asteroid->mesh = &state->asteroid_mesh;
    asteroid->scale = scale;
    asteroid->radius = scale * 1.0f;
    
    asteroid->to_world_transform = MAT4X3_IDENTITY;
    asteroid->to_world_transform.translation = position;
    asteroid->angular_rotation_axis = random_unit_vector();
    asteroid->angular_velocity = random_f32(0.0f, 2 * PIf);
    
    asteroid->hp = 32;
}

void spawn_bullet(Application_State *state) {
    Entity *bullet = push(&state->entities, {});
    
    const f32 Bullet_Velocity = 30;
    
    bullet->velocity = state->ship.entity->to_world_transform.up * Bullet_Velocity;
    bullet->diffuse_color = vec4f{ 0.2f, 0.2f, 1.0f, 1.0f };
    bullet->kind = Bullet_Kind;
    bullet->mesh = &state->beam_mesh;
    bullet->scale = 1.0f;
    bullet->radius = bullet->scale * 1.0f;
    
    bullet->to_world_transform = state->ship.entity->to_world_transform;
    bullet->to_world_transform.translation += bullet->to_world_transform.up * (state->ship.entity->radius * 2);
    bullet->orientation = state->ship.entity->orientation;
    bullet->angular_rotation_axis = VEC3_Z_AXIS;
    bullet->angular_velocity = 0;
}

APP_INIT_DEC(application_init) {
    init_memory_stack_allocators();
    init_memory_growing_stack_allocators();
    platform_api->sync_allocators(global_allocate_functions, global_reallocate_functions, global_free_functions);
    
    auto persistent_memory = make_growing_stack_allocator(&platform_api->allocator);
    auto *state = ALLOCATE(&persistent_memory.allocator, Application_State);
    *state = {};
    
    state->persistent_memory = persistent_memory;
    state->transient_memory = make_growing_stack_allocator(&platform_api->allocator);
    
    init_gl();
    
    state->debug_camera_alpha = 0.0f;
    state->debug_camera_beta = PIf * 0.25f;
    state->debug_camera.to_world_transform.translation = vec3f{ 0.0f, -30.0f, 30.0f };
    debug_update_camera(state);
    
    state->camera_to_clip_projection = make_perspective_fov_projection(60.0f, width_over_height(Reference_Resolution));
    state->clip_to_camera_projection = make_inverse_perspective_projection(state->camera_to_clip_projection);
    
    init_immediate_render_context(&state->immediate_render_context, KILO(128), platform_api->read_file, &state->persistent_memory.allocator);
    
    {
        init_ui_render_context(&state->ui_render_context, KILO(10), 512, KILO(4), &state->persistent_memory.allocator);
        
        string vertex_shader_source = platform_api->read_file(S("shaders/textured_unprojected.vert.txt"), &state->persistent_memory.allocator);
        
        assert(vertex_shader_source.count);
        
        defer { free(&state->persistent_memory.allocator, vertex_shader_source.data); };
        
        string fragment_shader_source = platform_api->read_file(S("shaders/textured.frag.txt"), &state->persistent_memory.allocator);
        
        assert(fragment_shader_source.count);
        
        defer { free(&state->persistent_memory.allocator, fragment_shader_source.data); };
        
        Shader_Attribute_Info attributes[] = {
            { Vertex_Position_Index, "a_position" },
            { Vertex_UV_Index,       "a_uv" },
            { Vertex_Color_Index,    "a_color" },
        };
        
        string uniform_names = S("u_texture, u_alpha_threshold");
        
        GLuint shader_objects[2];
        shader_objects[0] = make_shader_object(GL_VERTEX_SHADER, &vertex_shader_source, 1, &state->persistent_memory.allocator);
        
        string grayscale_sources[] = {
            S(
                "#version 150\n"
                //"#define GRAYSCALE_COLOR\n"
                ),
            fragment_shader_source
        };
        
        shader_objects[1] = make_shader_object(GL_FRAGMENT_SHADER, ARRAY_WITH_COUNT(grayscale_sources), &state->persistent_memory.allocator);
        
        GLuint program_object = make_shader_program(ARRAY_WITH_COUNT(shader_objects), false, ARRAY_WITH_COUNT(attributes), uniform_names, ARRAY_WITH_COUNT(state->ui_font_material.shader.uniforms), &state->persistent_memory.allocator);
        
        assert(program_object);
        
        if (state->ui_font_material.shader.program_object)
            glDeleteProgram(state->ui_font_material.shader.program_object);
        
        state->ui_font_material.shader.program_object = program_object;
    }
    
    glGenBuffers(2, state->uniform_buffer_objects);
    glBindBuffer(GL_UNIFORM_BUFFER, state->projection_uniform_buffer_object);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Camera_Uniform_Block), null, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, Camera_Uniform_Block_Index, state->projection_uniform_buffer_object);
    
    glBindBuffer(GL_UNIFORM_BUFFER, state->lighting_uniform_buffer_object);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(Lighting_Uniform_Block), null, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, Lighting_Uniform_Block_Index, state->lighting_uniform_buffer_object);
    
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    
    load_phong_shader(state, platform_api);
    load_water_shader(state, platform_api);
    
    state->font_lib = make_font_library();
    state->font = make_font(state->font_lib, S("C:/Windows/Fonts/arial.ttf"), 10, ' ', 128, platform_api->read_file, &state->persistent_memory.allocator);
    set_texture_filter_level(state->font.texture.object, Texture_Filter_Level_Linear);
    
    state->ui_font_material.base.bind_material = bind_ui_font_material;
    state->ui_font_material.texture = &state->font.texture;
    
    state->entities = ALLOCATE_ARRAY_INFO(&state->persistent_memory.allocator, Entity, 512);
    
    // make ship
    
    {
        state->ship.entity = push(&state->entities, {});
        
        string source = platform_api->read_file(S("meshs/astroids_ship.glm"), &state->transient_memory.allocator);
        state->ship_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->ship.entity->ship = &state->ship;
        state->ship.entity->diffuse_color = make_vec4_scale(1.0f);
        state->ship.entity->mesh = &state->ship_mesh;
        state->ship.entity->scale  = 1.0f;
        state->ship.entity->radius = state->ship.entity->scale * 1.0f;
        state->ship.entity->angular_rotation_axis = VEC3_Z_AXIS;
        state->ship.entity->angular_velocity = 0;
        state->ship.entity->kind = Ship_Kind;
        // will be updated anyway
        state->ship.entity->to_world_transform = MAT4X3_IDENTITY;
        
        // thrusters
        state->ship_thrusters = push(&state->entities, {});
        state->ship_thrusters->is_light = true;
        state->ship_thrusters->parent = state->ship.entity;
        // make the radius big to have nicer lighting at border switch
        state->ship_thrusters->radius = 10.0f;
        state->ship_thrusters->scale = 1.0f;
        state->ship_thrusters->to_world_transform = MAT4X3_IDENTITY;
        state->ship_thrusters->to_world_transform.translation = vec3f{ 0.0f, -2.5f, 0.0f };
    }
    
    bool debug_ok = tga_load_texture(&state->asteroid_normal_map, S("meshs/asteroid_normal_map.tga"), platform_api->read_file, &state->transient_memory.allocator);
    debug_ok &= tga_load_texture(&state->asteroid_ambient_occlusion_map, S("meshs/asteroid_ambient_occlusion.tga"), platform_api->read_file, &state->transient_memory.allocator);
    assert(debug_ok);
    
    {
        string source = platform_api->read_file(S("meshs/asteroid_baked.glm"), &state->transient_memory.allocator);
        state->asteroid_mesh = make_mesh(source, &state->persistent_memory.allocator, &state->debug_mesh_vertex_buffers, &state->debug_mesh_vertex_count);
        free(&state->transient_memory.allocator, source.data);
    }
    
    {
        string source = platform_api->read_file(S("meshs/uv_sphere.glm"), &state->transient_memory.allocator);
        state->planet_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
    }
    
    {
        string source = platform_api->read_file(S("meshs/asteroids_beam.glm"), &state->transient_memory.allocator);
        state->beam_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
    }
    
    state->camera.to_world_transform = make_transform(QUAT_IDENTITY, vec3f{ 0.0f, 0.0f, 80.0f });
    state->main_window_area = { -1, -1, cast_v(s16, 400 * width_over_height(Reference_Resolution)), 400 };
    
    FILETIME system_time;
    GetSystemTimeAsFileTime(&system_time);
    srand(system_time.dwLowDateTime);
    
    for (u32 i = 0; i < 16; ++i)
        spawn_asteroid(state, random_unit_vector(true, true, false) * random_f32(10.0f, 30.0f), vec3f{ 1.0f, 1.0f, 0.0f }, 3.0f, make_vec4(random_unit_vector() * 0.5f + vec3f{1.0f, 1.0f, 1.0f}));
    
    return state;
}

void output_sound(Sound_Buffer *sound_buffer, Immediate_Render_Context *imc) {
    return;
    
    // tone a?
    u32 hz = 440;
    
    s16 volume = 3000;
    
    u32 square_wave_period = sound_buffer->samples_per_second / hz;
    
    s16 *sample_it = cast_p(s16, sound_buffer->output.data);
    u32 frame = sound_buffer->frame;
    
    u32 sample_count = sound_buffer->output.count / sound_buffer->bytes_per_sample;
    assert(sound_buffer->channel_count == 2);
    
    f32 scale = 80;
    f32 x = -40.0f;
    
    f32 thickness = 4;
    
    f32 pos   = scale * frame / sound_buffer->frame_count;
    f32 width = scale * sound_buffer->output.count / (sound_buffer->bytes_per_sample * sound_buffer->frame_count);
    
    draw_line(imc, vec3f{x + pos, 0, -thickness}, vec3f{x + pos, 0, thickness}, rgba32{255, 0, 255, 255});
    
    f32 play_pos = scale * sound_buffer->debug_play_frame / sound_buffer->frame_count;
    draw_line(imc, vec3f{x + play_pos, 0, -thickness}, vec3f{x + play_pos, 0, thickness}, rgba32{255, 0, 0, 255});
    
    play_pos = scale * sound_buffer->debug_write_frame / sound_buffer->frame_count;
    draw_line(imc, vec3f{x + play_pos, 0, -thickness}, vec3f{x + play_pos, 0, thickness}, rgba32{0, 255, 0, 255});
    
    if (pos + width > scale) {
        draw_rect(imc, vec3f{x + pos, 0, -thickness/2}, vec3f{scale - pos, 0, 0}, vec3f{0, 0, thickness}, rgba32{255, 255, 0, 255});
        width = pos + width - scale;
        pos = 0;
    }
    
    draw_rect(imc, vec3f{x + pos, 0, -thickness/2}, vec3f{width, 0, 0}, vec3f{0, 0, thickness}, rgba32{255, 255, 0, 255});
    
    draw_rect(imc, vec3f{x, 0, -thickness/2}, vec3f{scale, 0, 0}, vec3f{0, 0, thickness}, rgba32{0, 0, 255, 255});
    
    for (u32 i = 0; i < sample_count; ++i) {
        s16 value = ((frame / (square_wave_period / 2)) % 2) ? volume : -volume;
        
        value = sin(2.0f * PIf * hz * frame / sound_buffer->samples_per_second) * volume * 3;
        
        *(sample_it++) = value;
        *(sample_it++) = value;
        
        ++frame;
    }
}

vec3f mirror_offset(Plane3f plane) {
    return plane.orthogonal * (-2 * plane.distance_to_origin / squared_length(plane.orthogonal));
}

APP_MAIN_LOOP_DEC(application_main_loop) {
    Application_State *state = CAST_P(Application_State, app_data_ptr);
    auto imc = &state->immediate_render_context;
    auto ui = &state->ui_render_context;
    
    output_sound(output_sound_buffer, imc);
    
    {
#if 0
        global_debug_draw_info.immediate_render_context = &state->imc;
        global_debug_draw_info.max_iteration_count = 0;
        
        mat4x3f idenity;
        set_identity(&idenity);
        DEBUG_DRAW_SET_TO_WORLD_MATRIX(idenity);
#endif
        
        // check if .dll was reloaded
        // TODO: application should notify this maybe a plain bool from the platform_api
        if (!glUseProgram) {
            init_memory_stack_allocators();
            init_memory_growing_stack_allocators();
            platform_api->sync_allocators(global_allocate_functions, global_reallocate_functions, global_free_functions);
            
            init_gl();
            load_phong_shader(state, platform_api);
            load_water_shader(state, platform_api);
            
            // make shure pointers for dynamic dispatch are valid
            state->ui_font_material.base.bind_material = bind_ui_font_material;
        }
    }
    
    clear(&state->transient_memory.memory_growing_stack);
    
    // change game speed
    static f32 game_speed = 1.0f;
    static f32 backup_game_speed = 1.0f;
    
    if (was_pressed(input->keys[VK_F5])) {
        game_speed = MAX(1.0f / 16.0f, game_speed * 0.5f);
        backup_game_speed = 1.0f;
    }
    
    if (was_pressed(input->keys[VK_F6])) {
        game_speed = MIN(128, game_speed * 2.0f);
        backup_game_speed = 1.0f;
    }
    
    if (was_pressed(input->keys[VK_F7])) {
        f32 temp = backup_game_speed;
        backup_game_speed = game_speed;
        game_speed = temp;
    }
    
    delta_seconds *= game_speed;
    
    // alt + F4 close application
    if (input->left_alt.is_active && was_pressed(input->keys[VK_F4]))
        PostQuitMessage(0);
    
    // toggle game pause
    if (was_pressed(input->keys[VK_F2]))
        state->pause_game = !state->pause_game;
    
    // toggle fullscreen, this may freez the app for about 5 seconds
    if (input->left_alt.is_active && was_pressed(input->keys[VK_RETURN]))
        state->main_window_is_fullscreen = !state->main_window_is_fullscreen;
    
    if (!platform_api->window(platform_api, 0, S("Astroids"), &state->main_window_area, true, state->main_window_is_fullscreen, width_over_height(Reference_Resolution)))
        PostQuitMessage(0);
    
    Pixel_Dimensions render_resolution = get_auto_render_resolution(state->main_window_area.size, Reference_Resolution);
    
    if (render_resolution.width == 0 || render_resolution.height == 0)
        return;
    
    vec4f background_color = vec4f{ 0.1f, 0.1f, 0.2f, 1.0f };
    set_auto_viewport(state->main_window_area.size, render_resolution, background_color);
    
    // enable to scale text relative to Reference_Resolution
#if 0
    ui_set_transform(ui, Reference_Resolution, 1.0f);
#else
    ui_set_transform(ui, render_resolution, 1.0f);
#endif
    
    ui_set_font(ui, &state->font, CAST_P(Render_Material, &state->ui_font_material));
    ui->font_rendering.color = rgba32{ 255, 255, 255, 255 };
    
    if (was_pressed(input->keys[VK_F1]))
        state->in_debug_mode = !state->in_debug_mode;
    
    vec3f camera_world_position;
    vec3f imc_view_direction = {};
    
    // update debug camera
    if (state->in_debug_mode) {
        if (was_pressed(input->keys[VK_F3]))
            state->debug_use_game_controls = !state->debug_use_game_controls;
        
        f32 debug_delta_seconds = delta_seconds / game_speed;
        
        if (!state->debug_use_game_controls) {
            if (was_pressed(input->mouse.right))
                state->last_mouse_window_position = input->mouse.window_position;
            else if (input->mouse.right.is_active) {
                vec2f mouse_delta =  input->mouse.window_position - state->last_mouse_window_position;
                state->last_mouse_window_position = input->mouse.window_position;
                
                state->debug_camera_alpha -= mouse_delta.x * Debug_Camera_Mouse_Sensitivity;
                
                state->debug_camera_beta -= mouse_delta.y * Debug_Camera_Mouse_Sensitivity;
                //state->debug_camera_beta = CLAMP(state->debug_camera_beta, PIf * -0.5f, PIf * 0.5f);
                state->debug_camera_beta = CLAMP(state->debug_camera_beta, 0, PIf );
                
                debug_update_camera(state);
            }
            
            vec3f direction = {};
            
            if (input->keys['W'].is_active)
                direction.z -= 1.0f;
            
            if (input->keys['S'].is_active)
                direction.z += 1.0f;
            
            if (input->keys['A'].is_active)
                direction.x -= 1.0f;
            
            if (input->keys['D'].is_active)
                direction.x += 1.0f;
            
            if (input->keys['Q'].is_active)
                direction.y -= 1.0f;
            
            if (input->keys['E'].is_active)
                direction.y += 1.0f;
            
            direction = normalize_or_zero(direction);
            
            state->debug_camera.to_world_transform.translation +=  transform_direction(state->debug_camera.to_world_transform, direction) * debug_delta_seconds * Debug_Camera_Move_Speed;
        }
        
        state->world_to_camera_transform = make_inverse_unscaled_transform(state->debug_camera.to_world_transform);
        camera_world_position = state->debug_camera.to_world_transform.translation;
        
        imc_view_direction = -state->debug_camera.to_world_transform.forward;
        
        imc->camera_to_clip_projection = state->camera_to_clip_projection;
        imc->world_to_camera_transform = state->world_to_camera_transform;
        
        // draw game camera
        // note that the matrix.forward vector points to the opposite position of the camera view direction (the blue line)
        draw_circle(imc, state->camera.to_world_transform.translation, 1.0f, make_rgba32(0.0f, 1.0f, 1.0f));
        draw_line(imc, state->camera.to_world_transform.translation, state->camera.to_world_transform.translation + state->camera.to_world_transform.right, make_rgba32(1.0f, 0.0f, 0.0f));
        draw_line(imc, state->camera.to_world_transform.translation, state->camera.to_world_transform.translation + state->camera.to_world_transform.up, make_rgba32(0.0f, 1.0f, 0.0f));
        draw_line(imc, state->camera.to_world_transform.translation, state->camera.to_world_transform.translation + state->camera.to_world_transform.forward, make_rgba32(0.0f, 0.0f, 1.0f));
        
        // enable to scale with window size
#if 0
        f32 depth = get_clip_plane_z(state->camera_to_clip_projection, state->world_to_camera_transform, vec3f{});
        f32 ui_scale = get_clip_to_world_up_scale(state->debug_camera.to_world_transform, state->clip_to_camera_projection, depth);
        ui_scale *= 0.25f;
#else
        f32 ui_scale = 1.0f;
#endif
        
        // draw origin
        draw_circle(imc, vec3f{}, ui_scale, VEC3_Z_AXIS, make_rgba32(1.0f, 0.0f, 1.0f));
        draw_circle(imc, vec3f{}, ui_scale, make_rgba32(1.0f, 1.0f, 1.0f));
        draw_line(imc, vec3f{}, VEC3_X_AXIS * ui_scale * 2, make_rgba32(1.0f, 0.0f, 0.0f));
        draw_line(imc, vec3f{}, VEC3_Y_AXIS * ui_scale * 2, make_rgba32(0.0f, 1.0f, 0.0f));
        draw_line(imc, vec3f{}, VEC3_Z_AXIS * ui_scale * 2, make_rgba32(0.0f, 0.0f, 1.0f));
        
    }
    else {
        state->world_to_camera_transform =  make_inverse_unscaled_transform(state->camera.to_world_transform);
        camera_world_position = state->camera.to_world_transform.translation;
        imc_view_direction = -state->camera.to_world_transform.forward;
        imc->camera_to_clip_projection = state->camera_to_clip_projection;
        imc->world_to_camera_transform = state->world_to_camera_transform;
    }
    
    //
    // calculate game area from camera projection
    //
    
    mat4x3f world_to_camera_transform = make_inverse_unscaled_transform(state->camera.to_world_transform);
    f32 depth = get_clip_plane_z(state->camera_to_clip_projection, world_to_camera_transform, vec3f{});
    
    vec3f bottem_left_corner = get_clip_to_world_point(state->camera.to_world_transform, state->clip_to_camera_projection, vec3f{ -1.0f, -1.0f, depth});
    vec3f top_right_corner   = get_clip_to_world_point(state->camera.to_world_transform, state->clip_to_camera_projection, vec3f{  1.0f,  1.0f, depth});
    
    vec3f area_size = top_right_corner - bottem_left_corner;
    
    Plane3f area_planes[4];
    
    draw_circle(imc, bottem_left_corner, 2, VEC3_Z_AXIS, rgba32{255, 0, 0, 255});
    draw_circle(imc, top_right_corner, 2, VEC3_Z_AXIS, rgba32{0, 255, 0, 255});
    
    vec3f a = bottem_left_corner;
    vec3f b = a;
    b.y += area_size.y;
    area_planes[0] = make_plane(cross(b - a, VEC3_Z_AXIS), a);
    draw_line(imc, (a + b) * 0.5f, (a + b) * 0.5f + normalize(area_planes[0].orthogonal) * 5, rgba32{0, 0, 255, 255});
    
    a = b;
    a.x += area_size.x;
    area_planes[1] = make_plane(cross(a - b, VEC3_Z_AXIS), a);
    draw_line(imc, (a + b) * 0.5f, (a + b) * 0.5f + normalize(area_planes[1].orthogonal) * 5, rgba32{0, 0, 255, 255});
    
    b = a;
    b.y -= area_size.y;
    area_planes[2] = make_plane(cross(b - a, VEC3_Z_AXIS), a);
    draw_line(imc, (a + b) * 0.5f, (a + b) * 0.5f + normalize(area_planes[2].orthogonal) * 5, rgba32{0, 0, 255, 255});
    
    a = b;
    a.x -= area_size.x;
    area_planes[3] = make_plane(cross(a - b, VEC3_Z_AXIS), a);
    draw_line(imc, (a + b) * 0.5f, (a + b) * 0.5f + normalize(area_planes[3].orthogonal) * 5, rgba32{0, 0, 255, 255});
    
    // draw game area
    draw_rect(imc, bottem_left_corner, vec3{ area_size.x, 0.0f, 0.0f }, vec3{ 0.0f, area_size.y, 0.0f }, make_rgba32(1.0f, 1.0f, 0.0f));
    
    
    // handle ship controls
    
    Ship_Entity *ship = &state->ship;
    
    if (!state->pause_game) {
        ship->thruster_intensity = MAX(0.0f, ship->thruster_intensity - delta_seconds);
        
        vec3f direction = {};
        
        f32 rotation = 0.0f;
        f32 accelaration = 0.0f;
        
        const f32 Max_Velocity = 20.0f;
        const f32 Acceleration = 25.0f;
        const f32 Min_Acceleraton = Acceleration * 0.1f;
        
        if (!state->in_debug_mode || state->debug_use_game_controls) {
            if (input->keys['W'].is_active) {
                accelaration += Acceleration * delta_seconds;
                ship->thruster_intensity = MAX(ship->thruster_intensity, 0.5f);
            }
            
            if (was_pressed(input->keys['W'])) {
                ship->thruster_intensity = 1.0f;
                accelaration = MAX(Min_Acceleraton, accelaration);
            }
            
            // rotate counter clockwise; mathematically positive
            if (input->keys['A'].is_active)
                rotation += 1.0f;
            
            // rotate clockwise; mathematically negative
            if (input->keys['D'].is_active)
                rotation -= 1.0f;
            
            const f32 Rotation_Speed = 2*PIf;
            ship->entity->orientation += rotation * Rotation_Speed * delta_seconds;
            
            vec3f accelaration_vector = ship->entity->to_world_transform.up * accelaration;
            
            ship->entity->velocity += accelaration_vector;
            f32 v2 = MIN(squared_length(ship->entity->velocity), Max_Velocity * Max_Velocity);
            
            ship->entity->velocity = normalize_or_zero(ship->entity->velocity) * sqrt(v2);
            
            if (was_pressed(input->keys['J'])) {
                spawn_bullet(state);
            }
        }
        
        // turn thrusters on or off
        state->ship_thrusters->is_light = (ship->thruster_intensity > 0);
        if (state->ship_thrusters->is_light) {
            state->ship_thrusters->diffuse_color  = vec4f{ 1, 1, 1, 1 } * ship->thruster_intensity;
            state->ship_thrusters->specular_color = vec4f{ 1, 1, 0, 1 } * ship->thruster_intensity;
        }
    }
    
#if 0
    u32 position_stride;
    u32 position_buffer_index;
    u32 position_offset;
    
    u32 normal_buffer_index;
    u32 normal_offset;
    u32 normal_stride;
    
    u32 tangent_buffer_index;
    u32 tangent_offset;
    u32 tangent_stride;
    
    u32 found_count = 3;
    for (u32 buffer_index = 0;
         buffer_index < state->asteroid_mesh.vertex_buffer_count;
         ++buffer_index)
    {
        u32 offset = 0;
        
        for (u32 attribute_index = 0;
             attribute_index < state->asteroid_mesh.vertex_buffers[buffer_index].vertex_attribute_info_count;
             ++attribute_index)
        {
            if (state->asteroid_mesh.vertex_buffers[buffer_index].vertex_attribute_infos[attribute_index].index == Vertex_Position_Index)
            {
                position_buffer_index = buffer_index;
                position_offset = offset;
                position_stride = state->asteroid_mesh.vertex_buffers[buffer_index].vertex_stride;
                --found_count;
            }
            else if (state->asteroid_mesh.vertex_buffers[buffer_index].vertex_attribute_infos[attribute_index].index == Vertex_Normal_Index)
            {
                normal_buffer_index = buffer_index;
                normal_offset = offset;
                normal_stride = state->asteroid_mesh.vertex_buffers[buffer_index].vertex_stride;
                --found_count;
            }
            else if (state->asteroid_mesh.vertex_buffers[buffer_index].vertex_attribute_infos[attribute_index].index == Vertex_Tangent_Index)
            {
                tangent_buffer_index = buffer_index;
                tangent_offset = offset;
                tangent_stride = state->asteroid_mesh.vertex_buffers[buffer_index].vertex_stride;
                --found_count;
            }
            
            if (!found_count)
                break;
            
            offset += get_vertex_attribute_size(state->asteroid_mesh.vertex_buffers[buffer_index].vertex_attribute_infos + attribute_index);
        }
        
        if (!found_count)
            break;
    }
    
    assert(!found_count);
    
    for (u32 i = 0; i < state->debug_mesh_vertex_count; ++i) {
        vec3f normal = *CAST_P(vec3f, state->debug_mesh_vertex_buffers[normal_buffer_index] + normal_offset + i * normal_stride);
        vec3f tangent = *CAST_P(vec3f, state->debug_mesh_vertex_buffers[tangent_buffer_index] + tangent_offset + i * tangent_stride);
        vec3f position = *CAST_P(vec3f, state->debug_mesh_vertex_buffers[position_buffer_index] + position_offset + i * position_stride);
        
        position = transform_point(state->asteroid->transform, position);
        
        draw_line(imc, position, position + normal,  rgba32{ 0, 0, 255, 255 });
        draw_line(imc, position, position + tangent, rgba32{ 255, 0, 255, 255 });
    }
#endif
    
    // update physics and add draw, light entities
    
#if 1
    f32 max_timestep = delta_seconds;
    
    // Increase timestep in pause mode,
    // to better visualize movement and collisions.
    if (state->pause_game)
        max_timestep = 2.0f;
    
    static u32 max_physics_step_count = 100;
    
    if (was_pressed(input->keys[VK_F9]))
        max_physics_step_count = 0;
    
    if (max_physics_step_count && was_pressed(input->keys[VK_F10]))
        --max_physics_step_count;
    
    if (was_pressed(input->keys[VK_F11]))
        ++max_physics_step_count;
    
    f32 margin = 0.2f;
    
    Body_Array bodies = {};
    defer { if (bodies.count) free(&state->transient_memory.allocator, bodies.data); };
    
    for (auto entity = first(state->entities); entity != one_past_last(state->entities); ++entity) {
        if (entity->parent)
            continue;
        
        auto body = push(&bodies, null, 1, &state->transient_memory.allocator);
        body->entity_index = index(state->entities, entity);
        body->sphere = { entity->to_world_transform.translation, entity->radius };
        body->velocity = entity->velocity;
        
        if (entity->kind == Bullet_Kind)
            body->destroy_on_collision = true;
        else
            body->destroy_on_collision = false;
        
        body->was_destroyed = false;
        
        draw_circle(imc, entity->to_world_transform.translation, entity->radius, rgba32{ 255, 255, 0, 255 });
    }
    
    f32 debug_step_with   = 70.0f;
    f32 debug_step_height = 2.0f;
    f32 debug_step_x      = -debug_step_with * 0.5f;
    f32 debug_step_y      = -10.0f;
    vec3f debug_step_last_mark = vec3f{ debug_step_x, debug_step_y };
    
    draw_rect(imc, vec3f{ debug_step_x, debug_step_y - debug_step_height * 0.5f }, vec3f{ debug_step_with }, vec3f{ 0, debug_step_height }, rgba32{ 255, 255, 255, 255 });
    
    bool collision_table[RANGE_SUM(Entity_Kind_Count)] = {};
    
    collision_table[get_collision_kind(Asteroid_Kind, Asteroid_Kind)] = true;
    collision_table[get_collision_kind(Ship_Kind    , Asteroid_Kind)] = true;
    collision_table[get_collision_kind(Bullet_Kind  , Asteroid_Kind)] = true;
    
    bool reflection_table[RANGE_SUM(Entity_Kind_Count)] = {};
    
    reflection_table[get_collision_kind(Asteroid_Kind, Asteroid_Kind)] = true;
    reflection_table[get_collision_kind(Ship_Kind    , Asteroid_Kind)] = true;
    reflection_table[get_collision_kind(Ship_Kind    , Ship_Kind)]     = true;
    
    
    f32 timestep = max_timestep;
    u32 physics_step_count = 0;
    while ((!max_physics_step_count || (physics_step_count < max_physics_step_count)) && (timestep > 0.00001f)) 
    {
        f32 min_allowed_timestep = timestep;
        
        Clone_Body_Array clones = {};
        defer { if (clones.count) free(&state->transient_memory.allocator, clones.data); };
        
        for (auto body = first(bodies); body != one_past_last(bodies); ++body)
        {
            if (body->was_destroyed)
                continue;
            
            u32 first_clone_index = clones.count;
            body->first_clone_index = first_clone_index;
            
            auto first_clone = push(&clones, null, 1, &state->transient_memory.allocator);
            first_clone->body_index          = index(bodies, body);
            first_clone->sphere              = body->sphere;
            first_clone->offset_to_next_body = 1;
            
            for (u32 plane_index = 0; plane_index < ARRAY_COUNT(area_planes); ++plane_index)
            {
                if (intersect(area_planes[plane_index], body->sphere))
                {
                    u32 clone_count = first_clone->offset_to_next_body;
                    
                    vec3f offset = mirror_offset(area_planes[plane_index]);
                    
                    for (u32 clone_index = first_clone_index; clone_index < first_clone_index + clone_count; ++clone_index)
                    {
                        auto clone = push(&clones, clones + clone_index, 1, &state->transient_memory.allocator);
                        clone->sphere.center += offset;
                        clone->offset_to_next_body = first_clone_index + clone_count - clone_index;
                        
                        draw_circle(imc, clone->sphere.center, clone->sphere.radius, make_rgba32(vec3f{ 1, 0, 1 } * ((timestep) / max_timestep)));
                        
                        clones[clone_index].offset_to_next_body += clone_count;
                    }
                }
            }
            
#if 0            
            body->next_velocity = body->velocity;
            body->max_timestep  = timestep;
            body->next_center   = body->sphere.center + body->velocity * body->max_timestep;
#endif
            
        }
        
        Collision_Pair_Array collisions = {};
        defer { if (collisions.count) free(&state->transient_memory.allocator, collisions.data); };
        
        Body *body_pair[2];
        for (body_pair[0] = first(bodies); body_pair[0] != one_past_last(bodies); ++body_pair[0]) 
        {
            if (body_pair[0]->was_destroyed)
                continue;
            
            Sphere3f moving_sphere = body_pair[0]->sphere;
            
            for (body_pair[1] = body_pair[0] + 1; body_pair[1] != one_past_last(bodies); ++body_pair[1]) {
                if (body_pair[1]->was_destroyed)
                    continue;
                
                u32 collision_kind = get_collision_kind(state->entities[body_pair[0]->entity_index].kind, state->entities[body_pair[1]->entity_index].kind);
                
                if (!collision_table[collision_kind])
                    continue;
                
                vec3f movement = body_pair[0]->velocity - body_pair[1]->velocity;
                
                if (squared_length(movement) == 0.0f)
                    continue;
                
                f32 movement_length = length(movement);
                
                f32 whole_timestep = 0.0f;
                f32 remaining_timestep = timestep;
                
                while (true) {
                    assert(remaining_timestep > 0.0f);
                    
                    vec3f next_moving_sphere_center;
                    f32 moving_timestep = remaining_timestep;
                    bool movement_was_split = false;
                    
                    for (u32 plane_index = 0; plane_index < ARRAY_COUNT(area_planes); ++plane_index) {
                        f32 time_until_split = (area_planes[plane_index].distance_to_origin - dot(area_planes[plane_index].orthogonal, moving_sphere.center)) /
                            dot(area_planes[plane_index].orthogonal, movement);
                        
                        if ((time_until_split == 0.0f) && (dot(area_planes[plane_index].orthogonal, movement) >= 0.0f))
                            continue;
                        
                        if ((time_until_split >= 0.0f) && (time_until_split < moving_timestep)) {
                            moving_timestep = time_until_split;
                            next_moving_sphere_center = moving_sphere.center + movement * time_until_split + mirror_offset(area_planes[plane_index]);
                            
                            movement_was_split = true;
                        }
                    }
                    
                    if (moving_timestep == 0.0f) {
                        moving_sphere.center = next_moving_sphere_center;
                        continue;
                    }
                    
                    f32 relative_margin = margin / (moving_timestep * movement_length);
                    
                    Clone_Body *first_static_clone = clones + body_pair[1]->first_clone_index;
                    Clone_Body *one_past_last_static_clone = first_static_clone + first_static_clone->offset_to_next_body;
                    for (auto static_clone = first_static_clone; static_clone != one_past_last_static_clone; ++static_clone)
                    {
                        Sphere3f static_sphere = static_clone->sphere;
                        
                        f32 t[2];
                        u32 collision_count = movement_distance_until_collision(moving_sphere.center - static_sphere.center, movement * moving_timestep, moving_sphere.radius + static_sphere.radius, t);
                        f32 d;
                        switch (collision_count) {
                            case 0:
                            case 1: {
                                d = 1.0f;
                            } break;
                            
                            case 2: {
                                if (t[0] >= 1.0f) {
                                    d = MIN(1.0f, t[0] - relative_margin);
                                    // d = 1.0f;
                                    break;
                                }
                                
                                if (t[0] > 0.0f) {
                                    d = MAX(0.0f, t[0] - relative_margin);
                                    break;
                                }
                                
                                if (t[1] < 0.0f) {
                                    d = 1.0f;
                                    break;
                                }
                                
                                // spheres are overlapping
                                // find minimal time to pull them appart
                                // either in past or in future
                                if (-t[0] < t[1]) {
                                    // rewind time until collision
                                    //d = t[0] - relative_margin;
                                    
                                    // dont rewind, just ignore small overlaps and reflect
                                    d = 0.0f;
                                }
                                else {
                                    // sphere passed through, so we can also continue with full movement
                                    //d = t[1] + relative_margin;
                                    d = 1.0f;
                                }
                            } break;
                            
                            default:
                            UNREACHABLE_CODE;
                        }
                        
                        if (d < 1.0f) {
                            f32 current_timestep = whole_timestep + moving_timestep * d;
                            
#if 0                                
                            min_allowed_timestep = MIN(min_allowed_timestep, current_timestep);
                            vec3f mirror_normal = normalize_or_zero(moving_sphere.center + body_pair[0]->velocity * (moving_timestep * d) - (static_sphere.center + body_pair[1]->velocity * current_timestep));
#endif
                            
                            if (current_timestep < min_allowed_timestep) {
                                min_allowed_timestep = current_timestep;
                                if (collisions.count) {
                                    free(&state->transient_memory.allocator, collisions.data);
                                    collisions = {};
                                }
                            }
                            
                            if (current_timestep == min_allowed_timestep) {
                                Collision_Pair pair;
                                COPY(pair.body_pair, body_pair, sizeof(body_pair));
                                pair.collision_kind = collision_kind;
                                pair.spheres[0].center = moving_sphere.center + body_pair[0]->velocity * (moving_timestep * d);
                                pair.spheres[0].radius = moving_sphere.radius;
                                
                                pair.spheres[1].center = static_sphere.center + body_pair[1]->velocity * current_timestep;
                                pair.spheres[1].radius = static_sphere.radius;
                                
                                push(&collisions, pair, &state->transient_memory.allocator);
                            }
                            
#if 0                                
                            for (s32 pair_index = 0; pair_index < 2; ++pair_index) {
                                auto body = body_pair[pair_index];
                                
                                if (body->max_timestep > current_timestep) {
                                    body->max_timestep = current_timestep;
                                    body->next_center  = body->sphere.center + body->velocity * current_timestep;
                                    
                                    if (body->destroy_on_collision)
                                        body->was_destroyed = true;
                                    
                                    f32 damage_intensity =-dot(normalize_or_zero(body_pair[1 - pair_index]->sphere.center - body->sphere.center), normalize_or_zero(body_pair[1 - pair_index]->velocity));
                                    
                                    body->next_hp = body->hp - damage * MIN(1, cast_v(u32, damage_intensity * 4 + 0.5f));
                                    
                                    // reflect velocity
                                    if (reflection_table[collision_kind] && (dot(mirror_normal * (pair_index * -2 + 1), body->velocity) <= 0))
                                        body->next_velocity = body->velocity - mirror_normal * (2 * dot(mirror_normal, body->velocity));
                                    else
                                        body->next_velocity = body->velocity;
                                }
                            }
#endif
                            
                            movement_was_split = false;
                        }
                    }
                    
                    if (!movement_was_split)
                        break;
                    
                    whole_timestep     += moving_timestep;
                    remaining_timestep -= moving_timestep;
                    
                    moving_sphere.center = next_moving_sphere_center;
                }
            }
        }
        
        
        rgba32 old_timestep_color = make_rgba32(vec3f{ 0, 0, 1 } * ((max_timestep - timestep) / max_timestep));
        rgba32 new_timestep_color = make_rgba32(vec3f{ 0, 0, 1 } * ((max_timestep - timestep + min_allowed_timestep) / max_timestep));
        
        // draw relative timestep in scale
        {
            vec3f next_mark = debug_step_last_mark + vec3f{ min_allowed_timestep * debug_step_with / max_timestep };
            
            draw_line(imc, debug_step_last_mark, next_mark, old_timestep_color, true, new_timestep_color);
            draw_line(imc, next_mark + vec3f{ 0, debug_step_height * 0.5f}, next_mark - vec3f{ 0, debug_step_height * 0.5f}, new_timestep_color);
            
            debug_step_last_mark = next_mark;
        }
        
        for (auto body = first(bodies); body != one_past_last(bodies); ++body) {
            
            vec3f old_center = body->sphere.center;
            
            body->sphere.center = body->sphere.center + body->velocity * min_allowed_timestep;
            
            if (body->was_destroyed)
                continue;
            
#if 0            
            
            u32 damage = (body->hp - body->next_hp) / 2;
            body->hp = body->next_hp;
            
            if (damage) {
                auto entity = state->entities + body->entity_index;
                auto new_entity = push(&state->entities, state->entities[body->entity_index]);
                
                auto new_body = push(&bodies, *body, &state->transient_memory.allocator);
                body->max_hp = damage;
                body->hp = body->max_hp;
                body->entity_index = index(state->entities, new_entity);
                new_entity->scale = entity->scale * 0.5;
                new_entity->radius = entity->radius * 0.5;
                
            }
            
            while (body->hp <= body->max_hp/2) {
                body->max_hp /= 2;
            }
#endif
            
            f32 min_distance_to_border = min_allowed_timestep;
            
            bool was_outside_game_area;
            do {
                was_outside_game_area = false;
                
                f32 min_distance_to_border;
                vec3f offset = vec3f{};
                
                // force body inside game area
                for (u32 plane_index = 0; plane_index < ARRAY_COUNT(area_planes); ++plane_index)
                {
                    if (contains(area_planes[plane_index], body->sphere.center)) {
                        f32 d = (area_planes[plane_index].distance_to_origin - dot(area_planes[plane_index].orthogonal, old_center)) /
                            dot(area_planes[plane_index].orthogonal, body->velocity);
                        
                        if (!was_outside_game_area || (min_distance_to_border > d)) {
                            min_distance_to_border = d;
                            offset = mirror_offset(area_planes[plane_index]);
                            was_outside_game_area = true;
                        }
                    }
                }
                
                if (was_outside_game_area) {
                    draw_line(imc, old_center, old_center + body->velocity * min_distance_to_border, old_timestep_color, true, new_timestep_color);
                    
                    body->sphere.center += offset;
                    old_center = old_center + body->velocity * min_distance_to_border + offset;
                }
            } while (was_outside_game_area);
            
            draw_line(imc, old_center, body->sphere.center, old_timestep_color, true, new_timestep_color);
            draw_circle(imc, body->sphere.center, body->sphere.radius, new_timestep_color);
            
            //body->velocity = body->next_velocity;
            //body->next_velocity = body->velocity;
            body->velocity_accumulated_orientation = 0.0f;
            body->velocity_change_count = 0;
        }
        
        for (auto collision = first(collisions); collision != one_past_last(collisions); ++collision) {
            vec3f mirror_normal = normalize_or_zero(collision->spheres[0].center - collision->spheres[1].center);
            
            draw_line(imc, collision->spheres[0].center, collision->spheres[1].center, rgba32{ 255, 255, 0, 255 });
            
            u32 reflection_count = 0;
            
            for (s32 pair_index = 0; pair_index < 2; ++pair_index) {
                auto body = collision->body_pair[pair_index];
                
                if (body->destroy_on_collision)
                    body->was_destroyed = true;
                
#if 0                
                f32 damage_intensity = -dot(normalize_or_zero(collision->body_pair[1 - pair_index]->sphere.center - body->sphere.center), normalize_or_zero(collision->body_pair[1 - pair_index]->velocity));
                
                body->next_hp = body->hp - damage * MIN(1, cast_v(u32, damage_intensity * 4 + 0.5f));
#endif
                
                // reflect velocity
                if (reflection_table[collision->collision_kind] && (dot(mirror_normal * (pair_index * -2 + 1), body->velocity) < 0)) {
                    reflection_count++;
                    //body->next_velocity = reflect(mirror_normal, body->velocity);
                    
                    vec3f normalized_velocity = normalize_or_zero(reflect(mirror_normal, body->velocity));
                    
                    f32 alpha = acos(dot(VEC3_Y_AXIS, normalized_velocity));
                    f32 cos_beta = dot(VEC3_X_AXIS, normalized_velocity);
                    
                    if (cos_beta > 0)
                        alpha *= -1;
                    
                    body->velocity_accumulated_orientation += alpha;
                    body->velocity_change_count++;
                }
            }
            
            //assert(!reflection_table[collision->collision_kind] || reflection_count);
            
            if (reflection_table[collision->collision_kind] && !reflection_count) {
                draw_circle(imc, collision->body_pair[0]->sphere.center, collision->body_pair[0]->sphere.radius, rgba32{ 255, 0, 0, 255 });
                draw_circle(imc, collision->body_pair[1]->sphere.center, collision->body_pair[1]->sphere.radius, rgba32{ 255, 0, 0, 255 });
                
                timestep = 0.0f;
            }
        }
        
        for (auto body = first(bodies); body != one_past_last(bodies); ++body) {
            if (body->velocity_change_count) {
                mat4x3f rotation = make_transform(make_quat(VEC3_Z_AXIS, body->velocity_accumulated_orientation / body->velocity_change_count));
                
                body->velocity = transform_direction(rotation, VEC3_Y_AXIS * length(body->velocity));
            }
        }
        
        ++physics_step_count;
        timestep -= min_allowed_timestep;
        
        draw_and_flush(imc);
    }
    
    static u32 frame_count = 0;
    frame_count++;
    
    static u32 physics_interation_count_accumalated = 0;
    static f32 physics_interation_count_average = 0.0f;
    
    physics_interation_count_accumalated += physics_step_count;
    static u32 physics_interation_max_count = 0;
    physics_interation_max_count = MAX(physics_interation_max_count, physics_step_count);
    
    static f32 fps_accumalated = 0.0f;
    static f32 fps_average = 0.0f;
    fps_accumalated += game_speed / delta_seconds;
    
    const f32 trottle_timeout = 1.0f;
    static f32 trottle_countdown = trottle_timeout;
    trottle_countdown -= delta_seconds / game_speed;
    
    if (trottle_countdown <= 0.0f) {
        trottle_countdown += trottle_timeout;
        
        fps_average = fps_accumalated / frame_count;
        physics_interation_count_average = physics_interation_count_accumalated / cast_v(f32, frame_count);
        
        frame_count = 0;
        fps_accumalated = 0.0f;
        physics_interation_count_accumalated = 0;
    }
    
    ui_printf(ui, 5, 120, S("physics iteration count: % (%)"), f(physics_interation_count_average), f(physics_interation_max_count));
    ui_printf(ui, 5, 90, S("fps: %"), f(fps_average));
    
    if (!state->pause_game && (physics_step_count != max_physics_step_count)) {
        
        for (auto body = first(bodies); body != one_past_last(bodies); ++body) {
            state->entities[body->entity_index].to_world_transform.translation = body->sphere.center;
            state->entities[body->entity_index].velocity                       = body->velocity;
            
            if (body->was_destroyed) {
                state->entities[body->entity_index].mark_for_destruction = true;
            }
        }
        
        for (auto entity = first(state->entities); entity != one_past_last(state->entities); ++entity) {
            if (entity->mark_for_destruction) {
                unordered_remove(&state->entities, index(state->entities, entity));
                --entity; // repeat current entity index
            }
        }
    }
#endif
    
    Draw_Entity_Buffer  draw_entities  = ALLOCATE_ARRAY_INFO(&state->transient_memory.allocator, Draw_Entity, state->entities.count * 4);
    defer { free(&state->transient_memory.allocator, draw_entities.data); };
    
    Light_Entity_Buffer light_entities = ALLOCATE_ARRAY_INFO(&state->transient_memory.allocator, Light_Entity, 10);
    defer { free(&state->transient_memory.allocator, light_entities.data); };
    
    for (auto entity = first(state->entities); entity != one_past_last(state->entities); ++entity)
    {
        mat4x3f entity_to_world_transform;
        
        if (!entity->parent) {
            if (!state->pause_game) {
                //entity->to_world_transform.translation += entity->velocity * delta_seconds;
                entity->orientation += entity->angular_velocity * delta_seconds;
            }
            
            entity_to_world_transform = entity->to_world_transform;
        }
        else {
            // make shure parents are processed befor children
            // child address in buffer is higher then parent address
            assert(entity > entity->parent);
            entity_to_world_transform = entity->parent->to_world_transform * entity->to_world_transform;
        }
        
        entity_to_world_transform = make_transform(make_quat(entity->angular_rotation_axis, entity->orientation), entity_to_world_transform.translation, make_vec3_scale(entity->scale));
        
        // only update top entities
        if (!entity->parent)
            entity->to_world_transform = entity_to_world_transform;
        
        s32 min_x;
        s32 max_x;
        if (entity_to_world_transform.translation.x - entity->radius < area_size.x * -0.5f) {
            min_x = 0;
            max_x = min_x + 2;
        }
        else if (entity_to_world_transform.translation.x + entity->radius > area_size.x * 0.5f) {
            min_x = -1;
            max_x = min_x + 2;
        }
        else {
            min_x = 0;
            max_x = 1;
        }
        
        s32 min_y;
        s32 max_y;
        if ((entity_to_world_transform.translation.y - entity->radius < area_size.y * -0.5f)) {
            min_y = 0;
            max_y = min_y + 2;
        }
        else if ((entity_to_world_transform.translation.y + entity->radius > area_size.y * 0.5f)) {
            min_y = -1;
            max_y = min_y + 2;
        }
        else {
            min_y = 0;
            max_y = 1;
        }
        
        for (s32 y = min_y; y < max_y; ++y) {
            for (s32 x = min_x; x < max_x; ++x) {
                mat4x3f transform = entity_to_world_transform;
                
                transform.translation.x += area_size.x * x;
                transform.translation.y += area_size.y * y;
                
                if (entity->mesh) {
                    Draw_Entity draw_entity;
                    draw_entity.to_world_transform = transform;
                    draw_entity.mesh = entity->mesh;
                    draw_entity.color = entity->diffuse_color;
                    draw_entity.shininess = 128.0f;
                    push(&draw_entities, draw_entity);
                }
                
                if (entity->is_light) {
                    Light_Entity light_entity;
                    light_entity.world_position = transform.translation;
                    
                    f32 intensity = 1.0f;
                    
                    if (light_entity.world_position.x < area_size.x * -0.5f)
                        intensity *= 1.0f - (area_size.x * -0.5f - light_entity.world_position.x) / entity->radius;
                    else if (light_entity.world_position.x > area_size.x * 0.5f)
                        intensity *= 1.0f - (light_entity.world_position.x - area_size.x * 0.5f) / entity->radius;
                    
                    if (light_entity.world_position.y < area_size.y * -0.5f)
                        intensity *= 1.0f - (area_size.y * -0.5f - light_entity.world_position.y) / entity->radius;
                    else if (light_entity.world_position.y > area_size.y * 0.5f)
                        intensity *= 1.0f - (light_entity.world_position.y - area_size.y * 0.5f) / entity->radius;
                    
                    if (state->in_debug_mode)
                        draw_circle(imc, light_entity.world_position, entity->radius * intensity,  make_rgba32(x * 0.5f + 0.5f, 0, y * 0.5f + 0.5f)); // make_rgba32(entity->diffuse_color * intensity));
                    
                    light_entity.diffuse_color  = entity->diffuse_color  * intensity;
                    light_entity.specular_color = entity->specular_color * intensity;
                    light_entity.attenuation    = 0.005f;
                    push(&light_entities, light_entity);
                }
            }
        }
    }
    
    //ui_printf(ui, ui->anchors.left + 5, ui->anchors.top - 30, S("max physics iteration: %"), f(max_physics_step_count));
    ui_printf(ui, ui->anchors.left + 5, ui->anchors.top - 60, S("game_speed: %"), f(game_speed));
    
    ui_printf(ui, 5, 30, S("light count: %"), f(light_entities.count));
    
    // rendering
    
    {
        glBindBuffer(GL_UNIFORM_BUFFER, state->projection_uniform_buffer_object);
        auto camera_block = cast_p(Camera_Uniform_Block, glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY));
        camera_block->camera_to_clip_projection = state->camera_to_clip_projection;
        
        for (u32 i = 0; i < 4; ++i)
            camera_block->world_to_camera_transform.columns[i] = make_vec4(state->world_to_camera_transform.columns[i], 0.0f);
        
        camera_block->camera_world_position = camera_world_position;
        
        glUnmapBuffer(GL_UNIFORM_BUFFER);
    }
    
    glUseProgram(state->phong_shader.program_object);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    
    u32 texture_index = 0;
    
    if (state->phong_shader.u_normal_map != -1) {
        glUniform1i(state->phong_shader.u_normal_map, texture_index);
        glActiveTexture(GL_TEXTURE0 + texture_index);
        glBindTexture(GL_TEXTURE_2D, state->asteroid_normal_map.object);
        
        ++texture_index;
    }
    
    if (state->phong_shader.u_diffuse_texture != -1) {
        glUniform1i(state->phong_shader.u_diffuse_texture, texture_index);
        glActiveTexture(GL_TEXTURE0 + texture_index);
        glBindTexture(GL_TEXTURE_2D, state->asteroid_ambient_occlusion_map.object);
        
        ++texture_index;
    }
    
    // lights
    {
        Light_Entity main_light;
        main_light.world_position = VEC3_Z_AXIS * 5; //  camera_world_position;
        //main_light.world_position = camera_world_position;
        main_light.diffuse_color  = vec4f{1, 1, 1, 1};
        main_light.specular_color = vec4f{1, 1, 1, 1};
        main_light.attenuation    = 0.005f;
        push(&light_entities, main_light);
        
        glBindBuffer(GL_UNIFORM_BUFFER, state->lighting_uniform_buffer_object);
        auto lighting_block = cast_p(Lighting_Uniform_Block, glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY));
        
        u32 light_index = 0;
        for (auto light_entity = first(light_entities); light_entity != one_past_last(light_entities); ++light_entity)
        {
            lighting_block->world_positions_and_attenuations[light_index] = make_vec4(light_entity->world_position, light_entity->attenuation);
            
            lighting_block->diffuse_colors[light_index] = light_entity->diffuse_color;
            lighting_block->specular_colors[light_index] = light_entity->specular_color;
            
            if (state->in_debug_mode) {
                draw_circle(imc, light_entity->world_position, squared_length(light_entity->diffuse_color), make_rgba32(light_entity->diffuse_color));
                draw_circle(imc, light_entity->world_position, squared_length(light_entity->specular_color), make_rgba32(light_entity->specular_color));
            }
            
            ++light_index;
        }
        
        ui_printf(ui, 5, 60, S("uniform light count: %"), f(light_index));
        
        lighting_block->count = light_index;
        
        glUnmapBuffer(GL_UNIFORM_BUFFER);
    }
    
    // render queue draw entities
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    
    for (auto draw_entity = first(draw_entities); draw_entity != one_past_last(draw_entities); ++draw_entity)
    {
        glUniform4fv(state->phong_shader.u_ambient_color, 1, draw_entity->color * 0.1f);
        glUniform4fv(state->phong_shader.u_diffuse_color, 1, draw_entity->color);
        glUniformMatrix4x3fv(state->phong_shader.u_object_to_world_transform, 1, GL_FALSE, draw_entity->to_world_transform);
        glUniform1f(state->phong_shader.u_shininess, draw_entity->shininess);
        
        draw(&draw_entity->mesh->batch, 0);
    }
    
    {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        //glDisable(GL_CULL_FACE);
        //defer { glEnable(GL_CULL_FACE); };
        //glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        
        glUseProgram(state->water_shader.program_object);
        
        glUniform4fv(state->water_shader.u_ambient_color, 1, vec4f{});
        glUniform4fv(state->water_shader.u_diffuse_color, 1, vec4f{ 0.0f, 0.935f, 1.0f, 0.25f });
        
        static float phase = 0.0f;
        phase += delta_seconds;
        if (phase >= 1.0f)
            phase -= 1.0f;
        
        glUniform1f(state->water_shader.u_phase, phase);
        
        mat4x3f transform = make_transform(QUAT_IDENTITY, vec3f{ 5.0, 3.0f, 0.0f });
        
        glUniformMatrix4x3fv(state->water_shader.u_object_to_world_transform, 1, GL_FALSE, transform);
        glUniform1f(state->water_shader.u_shininess, 16.0f);
        
        draw(&state->planet_mesh.batch, 0);
    }
    
    if (input->keys[VK_TAB].is_active) 
    {
        //SCOPE_PUSH(imc->world_to_camera_transform, ui->transform);
        
        ui_set_transform(ui, render_resolution, 1.0f);
        imc->world_to_camera_transform = ui->transform;
        imc->camera_to_clip_projection = MAT4_IDENTITY;
        
        f32 button_width  = 68;
        f32 button_height = 42;
        f32 space         = 10;
        
        bool f_button_available[12] = {};
        bool f_button_active[12] = {};
        
        f_button_available[0] = true;
        if (state->in_debug_mode) {
            f_button_active[0] = true;
            
            f_button_available[2] = true;
            if (state->debug_use_game_controls)
                f_button_active[2] = true;
        }
        
        f_button_available[1] = true;
        if (state->pause_game)
            f_button_active[1] = true;
        
        f_button_available[4] = true;
        f_button_available[5] = true;
        f_button_available[6] = true;
        
        if (game_speed < 1.0f)
            f_button_active[4] = true;
        else if (game_speed > 1.0f)
            f_button_active[5] = true;
        else
            f_button_active[6] = true;
        
        SCOPE_PUSH(ui->font_rendering.alignment, vec2f{});
        SCOPE_PUSH(ui->font_rendering.color, {});
        
        for (u32 i = 0; i < ARRAY_COUNT(f_button_available); ++i)
        {
            f32 x = 20 + (button_width + space) * i + i/4 * space * 2;
            f32 y = ui->anchors.top - 20;
            
            rgba32 color;
            if (f_button_available[i]) {
                if (f_button_active[i])
                    color = make_rgba32(0, 1, 0);
                else
                    color = make_rgba32(1, 0, 0);
            }
            else
                color = make_rgba32(0.2f, 0.2f, 0.2f);
            
            draw_rect(imc, vec3f{ x, y, 0 }, vec3f{ button_width, 0, 0 }, vec3f{ 0, -button_height, 0 }, color);
            
            ui->font_rendering.color = color;
            
            ui_printf(ui, x + button_width/2, y - button_height/2, S("F%"), f(i + 1));
        }
    }
    
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    draw_and_flush(imc);
    
    glEnable(GL_BLEND);
    //
    draw(ui);
}
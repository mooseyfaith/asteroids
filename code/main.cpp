#include <platform.h>
#include <memory_growing_stack.h>
#include <immediate_render.h>
#include <ui_render.h>
#include <mesh.h>
#include <tga.h>

#include <stdlib.h>

struct Ship_Entity;

struct Entity {
    mat4x3f to_world_transform;
    f32 orientation;
    f32 scale;
    
    vec3f velocity;
    vec3f angular_rotation_axis;
    f32   angular_velocity;
    f32 radius;
    
    vec4 diffuse_color;
    vec4 specular_color;
    bool is_asteroid;
    bool is_light;
    Mesh *mesh;
    Ship_Entity *ship;
    Entity *parent;
};

struct Ship_Entity {
    Entity *entity;
    f32 thruster_intensity;
};

struct Draw_Entity {
    mat4x3f to_world_transform;
    Mesh *mesh;
    vec4f color;
};

struct Light_Entity {
    vec3f world_position;
    vec4f diffuse_color;
    vec4f specular_color;
};

#define BUFFER_TEMPLATE_NAME Entity_Buffer
#define BUFFER_TEMPLATE_DATA_TYPE Entity
#define BUFFER_TEMPLATE_IS_BUFFER
#include "buffer_template.h"

#define BUFFER_TEMPLATE_NAME      Draw_Entity_Buffer
#define BUFFER_TEMPLATE_DATA_TYPE Draw_Entity
#define BUFFER_TEMPLATE_IS_BUFFER
#include "buffer_template.h"

#define BUFFER_TEMPLATE_NAME      Light_Entity_Buffer
#define BUFFER_TEMPLATE_DATA_TYPE Light_Entity
#define BUFFER_TEMPLATE_IS_BUFFER
#include "buffer_template.h"

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
    
    Mesh ship_mesh, asteroid_mesh, beam_mesh;
    
    struct {
        GLuint program_object;
        
#define PHONG_UNIFORMS \
        u_camera_to_clip_projection, \
        u_world_to_camera_transform, \
        u_object_to_world_transform, \
        u_camera_world_position, \
        u_bone_transforms, \
        u_light_world_positions, \
        u_light_diffuse_colors, \
        u_light_specular_colors, \
        u_light_count, \
        u_ambient_color, \
        u_diffuse_texture, \
        u_diffuse_color, \
        u_normal_map
        
        union {
            struct { GLint PHONG_UNIFORMS; };
            
            // sadly we cannot automate this,
            // but make_shader_program will catch a missmatch
            // while parsing the uniform_names string
            GLint uniforms[13];
        };
    } phong_shader;
    
    Texture asteroid_normal_map;
    Texture asteroid_ambient_occlusion_map;
    Entity_Buffer entities;
    
    Ship_Entity ship;
    Entity *ship_thrusters;
    Entity *beam;
    
    u8_array *debug_mesh_vertex_buffers;
    u32 debug_mesh_vertex_count;
    
    bool in_debug_mode;
    bool pause_game;
};

Pixel_Dimensions const Reference_Resolution = { 1280, 720 };

f32 const Debug_Camera_Move_Speed = 20.0f;
f32 const Debug_Camera_Mouse_Sensitivity = 2.0f * PIf / 2048.0f;
vec3f const Debug_Camera_Axis_Alpha = VEC3_Y_AXIS;
vec3f const Debug_Camera_Axis_Beta = VEC3_X_AXIS;

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
    string shader_source = platform_api->read_text_file(S("shaders/phong.shader.txt"), &state->transient_memory.allocator);
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
    
    GLint uniforms[ARRAY_COUNT(state->phong_shader.uniforms)];
    
    GLuint program_object = make_shader_program(ARRAY_WITH_COUNT(shader_objects), true, ARRAY_WITH_COUNT(attributes), uniform_names, ARRAY_WITH_COUNT(uniforms), &state->transient_memory.allocator
                                                );
    
    if (program_object) {
        if (state->phong_shader.program_object) {
            glUseProgram(0);
            glDeleteProgram(state->phong_shader.program_object);
        }
        
        state->phong_shader.program_object = program_object;
        COPY(state->phong_shader.uniforms, uniforms, sizeof(uniforms));
        
        for (u32 i = 0; i < ARRAY_COUNT(uniforms); ++i)
            printf("uniform %d: %i\n", i, uniforms[i]);
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

void spawn_asteroid(Application_State *state, vec3f position, vec3f velocity, f32 scale, vec4f color) {
    Entity *asteroid = push(&state->entities, {});
    asteroid->velocity = random_unit_vector(true, false, true) * random_f32(0.5f, 10.0f);
    asteroid->diffuse_color = color;
    asteroid->is_asteroid = true;
    asteroid->mesh = &state->asteroid_mesh;
    asteroid->scale = scale;
    asteroid->radius = scale * 1.0f;
    
    asteroid->to_world_transform = MAT4X3_IDENTITY;
    asteroid->to_world_transform.translation = position;
    asteroid->angular_rotation_axis = random_unit_vector();
    asteroid->angular_velocity = random_f32(0.0f, 2 * PIf);
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
    state->debug_camera_beta = PIf * -0.25f;
    state->debug_camera.to_world_transform.translation = vec3f{ 0.0f, 10.0f, 10.0f };
    debug_update_camera(state);
    
    state->camera_to_clip_projection = make_perspective_fov_projection(60.0f, width_over_height(Reference_Resolution));
    state->clip_to_camera_projection = make_inverse_perspective_projection(state->camera_to_clip_projection);
    
    init_immediate_render_context(&state->immediate_render_context, &state->world_to_camera_transform, &state->camera_to_clip_projection, KILO(64), &state->persistent_memory.allocator);
    
    {
        init_ui_render_context(&state->ui_render_context, KILO(10), 512, KILO(4), &state->persistent_memory.allocator);
        
        string vertex_shader_source = platform_api->read_text_file(S("shaders/textured_unprojected.vert.txt"), &state->persistent_memory.allocator);
        
        assert(vertex_shader_source.count);
        
        defer { free(&state->persistent_memory.allocator, vertex_shader_source.data); };
        
        string fragment_shader_source = platform_api->read_text_file(S("shaders/textured.frag.txt"), &state->persistent_memory.allocator);
        
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
    
    load_phong_shader(state, platform_api);
    
    state->font_lib = make_font_library();
    state->font = make_font(state->font_lib, S("C:/Windows/Fonts/arial.ttf"), 32, ' ', 128, platform_api->read_file, &state->persistent_memory.allocator);
    set_texture_filter_level(state->font.texture.object, Texture_Filter_Level_Linear);
    
    state->ui_font_material.base.bind_material = bind_ui_font_material;
    state->ui_font_material.texture = &state->font.texture;
    
    state->entities = ALLOCATE_ARRAY_INFO(&state->persistent_memory.allocator, Entity, 20);
    
    // make ship
    
    {
        state->ship.entity = push(&state->entities, {});
        
        string source = platform_api->read_text_file(S("meshs/astroids_ship.glm"), &state->transient_memory.allocator);
        state->ship_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->ship.entity->ship = &state->ship;
        state->ship.entity->diffuse_color = make_vec4_scale(1.0f);
        state->ship.entity->mesh = &state->ship_mesh;
        state->ship.entity->scale  = 1.0f;
        state->ship.entity->radius = state->ship.entity->scale * 1.0f;
        state->ship.entity->angular_rotation_axis = VEC3_Y_AXIS;
        state->ship.entity->angular_velocity = 0;
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
        state->ship_thrusters->to_world_transform.translation = vec3f{0.0f, 0.0f, 1.5f};
    }
    
    bool debug_ok = tga_load_texture(&state->asteroid_normal_map, S("meshs/asteroid_normal_map.tga"), platform_api->read_file, &state->transient_memory.allocator);
    debug_ok &= tga_load_texture(&state->asteroid_ambient_occlusion_map, S("meshs/asteroid_ambient_occlusion.tga"), platform_api->read_file, &state->transient_memory.allocator);
    assert(debug_ok);
    
    {
        string source = platform_api->read_text_file(S("meshs/asteroid_baked.glm"), &state->transient_memory.allocator);
        state->asteroid_mesh = make_mesh(source, &state->persistent_memory.allocator, &state->debug_mesh_vertex_buffers, &state->debug_mesh_vertex_count);
        free(&state->transient_memory.allocator, source.data);
    }
    
    {
        state->beam = push(&state->entities, {});
        string source = platform_api->read_text_file(S("meshs/astroids_beam.glm"), &state->transient_memory.allocator);
        state->beam_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->beam->to_world_transform = make_transform(QUAT_IDENTITY, vec3f{ -4.0f, 0.0f, 0.0f });
        state->beam->diffuse_color = make_vec4_scale(1.0f);
        state->beam->mesh = &state->beam_mesh;
    }
    
    state->camera.to_world_transform = make_transform(make_quat(VEC3_X_AXIS, PIf * -0.5f), vec3f{ 0.0f, 80.0f, 0.0f });
    state->main_window_area = { -1, -1, 800, 450 };
    
    FILETIME system_time;
    GetSystemTimeAsFileTime(&system_time);
    srand(system_time.dwLowDateTime);
    
    for (u32 i = 0; i < 6; ++i)
        spawn_asteroid(state, random_unit_vector(true, false, true) * random_f32(0.0f, 50.0f), vec3f{ 1.0f, 0.0f, 1.0f }, 3.0f, make_vec4(random_unit_vector() * 0.5f + vec3f{1.0f, 1.0f, 1.0f}));
    
    return state;
}

APP_MAIN_LOOP_DEC(application_main_loop) {
    Application_State *state = CAST_P(Application_State, app_data_ptr);
    auto imc = &state->immediate_render_context;
    auto ui = &state->ui_render_context;
    {
#if 0
        global_debug_draw_info.immediate_render_context = &state->imc;
        global_debug_draw_info.max_iteration_count = 0;
        
        mat4x3f idenity;
        set_identity(&idenity);
        DEBUG_DRAW_SET_TO_WORLD_MATRIX(idenity);
#endif
        
        // check if .dll was reloaded
        if (!glUseProgram) {
            init_memory_stack_allocators();
            init_memory_growing_stack_allocators();
            platform_api->sync_allocators(global_allocate_functions, global_reallocate_functions, global_free_functions);
            
            init_gl();
            
            //state->transient_memory = make_allocator(state->transient_memory.memory_growing_stack);
            
            load_phong_shader(state, platform_api);
            
            // make shure pointers for dynamic dispatch are valid
            state->ui_font_material.base.bind_material = bind_ui_font_material;
        }
    }
    
    // alt + F4 close application
    if (input->left_alt.is_active && was_pressed(input->keys[VK_F4]))
        PostQuitMessage(0);
    
    // toggle fullscreen, this may freez the app for about 5 seconds
    if (input->left_alt.is_active && was_pressed(input->keys[VK_RETURN]))
        state->main_window_is_fullscreen = !state->main_window_is_fullscreen;
    
    // enable to keep Reference_Resolution aspect ratio and add black borders
#if 0
    if (!platform_api->window(platform_api, 0, S("Astroids"), &state->main_window_area, true, state->main_window_is_fullscreen, 0.0f))
        PostQuitMessage(0);
    
    Pixel_Dimensions render_resolution = get_auto_render_resolution(state->main_window_area.size, Reference_Resolution);
#else // forece window aspect ratio like Reference_Resolution
    if (!platform_api->window(platform_api, 0, S("Astroids"), &state->main_window_area, true, state->main_window_is_fullscreen, width_over_height(Reference_Resolution)))
        PostQuitMessage(0);
    
    Pixel_Dimensions render_resolution = state->main_window_area.size;
#endif
    
    if (render_resolution.width == 0 || render_resolution.height == 0)
        return;
    
    //clear(&state->transient_memory.memory_growing_stack);
    
    vec4f background_color = vec4f{ 0.1f, 0.1f, 0.3f, 1.0f };
    set_auto_viewport(state->main_window_area.size, render_resolution, vec4f{ 0.05f, 0.05f, 0.05f, 1.0f }, background_color);
    
    // enable to scale text relative to Reference_Resolution
#if 0
    ui_set_transform(ui, Reference_Resolution, 1.0f);
#else
    ui_set_transform(ui, render_resolution, 1.0f);
#endif
    
    ui_set_font(ui, &state->font, CAST_P(Render_Material, &state->ui_font_material));
    
    ui_printf(ui, 0, 0, S("some text"));
    
    if (was_pressed(input->keys[VK_F1]))
        state->in_debug_mode = !state->in_debug_mode;
    
    //
    // calculate game area from camera projection
    //
    
    mat4x3f world_to_camera_transform = make_inverse_unscaled_transform(state->camera.to_world_transform);
    f32 depth = get_clip_plane_z(state->camera_to_clip_projection, world_to_camera_transform, vec3f{});
    
    vec3f bottem_left_corner = get_clip_to_world_point(state->camera.to_world_transform, state->clip_to_camera_projection, vec3f{ -1.0f, 1.0f, depth });
    vec3f top_right_corner = get_clip_to_world_point(state->camera.to_world_transform, state->clip_to_camera_projection, vec3f{ 1.0f, -1.0f, depth });
    
    vec3f area_size = top_right_corner - bottem_left_corner;
    
    if (was_pressed(input->keys[VK_F2]))
        state->pause_game = !state->pause_game;
    
    vec3f camera_world_position;
    vec3f imc_view_direction = {};
    
    // update debug camera
    if (state->in_debug_mode) {
        if (was_pressed(input->mouse.right))
            state->last_mouse_window_position = input->mouse.window_position;
        else if (input->mouse.right.is_active) {
            vec2f mouse_delta =  input->mouse.window_position - state->last_mouse_window_position;
            state->last_mouse_window_position = input->mouse.window_position;
            
            state->debug_camera_alpha -= mouse_delta.x * Debug_Camera_Mouse_Sensitivity;
            
            state->debug_camera_beta -= mouse_delta.y * Debug_Camera_Mouse_Sensitivity;
            state->debug_camera_beta = CLAMP(state->debug_camera_beta, PIf * -0.5f, PIf * 0.5f);
            
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
        
        state->debug_camera.to_world_transform.translation +=  transform_direction(state->debug_camera.to_world_transform, direction) * delta_seconds * Debug_Camera_Move_Speed;
        state->world_to_camera_transform =  make_inverse_unscaled_transform(state->debug_camera.to_world_transform);
        camera_world_position = state->debug_camera.to_world_transform.translation;
        
        imc_view_direction = -state->debug_camera.to_world_transform.forward;
        
        // draw game camera
        // note that the matrix.forward vector points to the opposite position of the camera view direction (the blue line)
        draw_circle(imc, state->camera.to_world_transform.translation, imc_view_direction, 1.0f, make_rgba32(0.0f, 1.0f, 1.0f));
        draw_line(imc, state->camera.to_world_transform.translation, state->camera.to_world_transform.translation + state->camera.to_world_transform.right, make_rgba32(1.0f, 0.0f, 0.0f));
        draw_line(imc, state->camera.to_world_transform.translation, state->camera.to_world_transform.translation + state->camera.to_world_transform.up, make_rgba32(0.0f, 1.0f, 0.0f));
        draw_line(imc, state->camera.to_world_transform.translation, state->camera.to_world_transform.translation + state->camera.to_world_transform.forward, make_rgba32(0.0f, 0.0f, 1.0f));
        
        // draw origin
        draw_circle(imc, vec3f{}, VEC3_Y_AXIS, 1.0f, make_rgba32(1.0f, 0.0f, 1.0f));
        draw_circle(imc, vec3f{}, imc_view_direction, 1.0f, make_rgba32(1.0f, 1.0f, 1.0f));
        draw_line(imc, vec3f{}, VEC3_X_AXIS * 5, make_rgba32(1.0f, 0.0f, 0.0f));
        draw_line(imc, vec3f{}, VEC3_Y_AXIS * 5, make_rgba32(0.0f, 1.0f, 0.0f));
        draw_line(imc, vec3f{}, VEC3_Z_AXIS * 5, make_rgba32(0.0f, 0.0f, 1.0f));
        
        // draw game area
        draw_rect(imc, bottem_left_corner, vec3{ area_size.x, 0.0f, 0.0f }, vec3{ 0.0f, 0.0f, area_size.z }, make_rgba32(1.0f, 1.0f, 0.0f));
        draw_rect(imc, vec3f{}, vec3{ 1.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, 1.0f}, make_rgba32(1.0f, 1.0f, 0.0f));
    }
    else {
        state->world_to_camera_transform =  make_inverse_unscaled_transform(state->camera.to_world_transform);
        camera_world_position = state->camera.to_world_transform.translation;
        imc_view_direction = -state->camera.to_world_transform.forward;
    }
    
    Ship_Entity *ship = &state->ship;
    
    if (!state->pause_game) {
        ship->thruster_intensity = MAX(0.0f, ship->thruster_intensity - delta_seconds);
        
        vec3f direction = {};
        
        f32 rotation = 0.0f;
        f32 accelaration = 0.0f;
        
        f32 Max_Velocity = 58.0f;
        f32 Acceleration = 25.0f;
        f32 Min_Acceleraton = Acceleration * 0.1f;
        
        if (!state->in_debug_mode) {
            if (input->keys['W'].is_active) {
                accelaration += Acceleration * delta_seconds;
                ship->thruster_intensity = MAX(ship->thruster_intensity, 0.5f);
            }
            
            if (was_pressed(input->keys['W'])) {
                ship->thruster_intensity = 1.0f;
                accelaration = MAX(Min_Acceleraton, accelaration);
            }
            
            // turn thrusters on or off
            state->ship_thrusters->is_light = (ship->thruster_intensity > 0);
            if (state->ship_thrusters->is_light) {
                state->ship_thrusters->diffuse_color = vec4f{1, 1, 0, 1} * ship->thruster_intensity;
                state->ship_thrusters->specular_color = vec4f{1, 0, 0, 1} * ship->thruster_intensity;
            }
            
            // rotate counter clockwise; mathematically positive
            if (input->keys['A'].is_active)
                rotation += 1.0f;
            
            // rotate clockwise; mathematically negative
            if (input->keys['D'].is_active)
                rotation -= 1.0f;
            
            ship->entity->orientation += rotation * PIf * delta_seconds;
            
            vec3f accelaration_vector = ship->entity->to_world_transform.forward * accelaration;
            
            ship->entity->velocity += accelaration_vector;
            f32 v2 = MIN(squared_length(ship->entity->velocity), Max_Velocity * Max_Velocity);
            
            ship->entity->velocity = normalize_or_zero(ship->entity->velocity) * sqrt(v2);
        }
        
#if 0        
        draw_line(imc, state->ship.transform.translation, state->ship.transform.translation + accelaration_vector, make_rgba32(1.0f, 0.0f, 0.0f));
        
        draw_line(imc, state->ship.transform.translation,  state->ship.transform.translation + state->ship.velocity, make_rgba32(1.0f, 1.0f, 0.0f));
        
        
        draw_line(imc, state->ship.transform.translation,  state->ship.transform.translation + state->ship.transform.forward, make_rgba32(0.0f, 0.0f, 1.0f));
#endif
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
    
    
    //bind_phong_shader(&state->phong_shader);
    //set_light_position(&state->phong_shader, vec3f{ 0.0f, 2.0f, 0.0f });
    
    
    
    Draw_Entity_Buffer draw_entities = ALLOCATE_ARRAY_INFO(&state->transient_memory.allocator, Draw_Entity, state->entities.count * 4);
    Light_Entity_Buffer light_entities = ALLOCATE_ARRAY_INFO(&state->transient_memory.allocator, Light_Entity, 10);
    
    // update physics and add draw, light entities
    
    for (auto entity = first(&state->entities); entity != one_past_last(&state->entities); ++entity)
    {
        mat4x3f entity_to_world_transform;
        
        if (!entity->parent) {
            if (!state->pause_game) {
                entity->to_world_transform.translation += entity->velocity * delta_seconds;
                entity->orientation += entity->angular_velocity * delta_seconds;
            }
            
            entity_to_world_transform = entity->to_world_transform;
        }
        else {
            // make shure parents are processed befor children
            // child address in buffer is higher then parent address
            assert(entity > entity->parent);
            entity_to_world_transform = entity->to_world_transform * entity->parent->to_world_transform;
        }
        
        if (entity_to_world_transform.translation.x - bottem_left_corner.x < 0.0f) {
            entity_to_world_transform.translation.x = area_size.x - dirty_mod(entity_to_world_transform.translation.x - bottem_left_corner.x, area_size.x) + bottem_left_corner.x;
        }
        else {
            entity_to_world_transform.translation.x = dirty_mod(entity_to_world_transform.translation.x - bottem_left_corner.x, area_size.x) + bottem_left_corner.x;
        }
        
        if (entity_to_world_transform.translation.z - bottem_left_corner.z < 0.0f) {
            entity_to_world_transform.translation.z = area_size.z - dirty_mod(entity_to_world_transform.translation.z - bottem_left_corner.z, area_size.z) + bottem_left_corner.z;
        }
        else {
            entity_to_world_transform.translation.z = dirty_mod(entity_to_world_transform.translation.z - bottem_left_corner.z, area_size.z) + bottem_left_corner.z;
        }
        
        entity_to_world_transform = make_transform(make_quat(entity->angular_rotation_axis, entity->orientation), entity_to_world_transform.translation, make_vec3_scale(entity->scale));
        
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
        
        s32 min_z;
        s32 max_z;
        if ((entity_to_world_transform.translation.z - entity->radius < area_size.z * -0.5f)) {
            min_z = 0;
            max_z = min_z + 2;
        }
        else if ((entity_to_world_transform.translation.z + entity->radius > area_size.z * 0.5f)) {
            min_z = -1;
            max_z = min_z + 2;
        }
        else {
            min_z = 0;
            max_z = 1;
        }
        
        // only update top entities
        if (!entity->parent)
            entity->to_world_transform = entity_to_world_transform;
        
        for (s32 z = min_z; z < max_z; ++z) {
            for (s32 x = min_x; x < max_x; ++x) {
                mat4x3f transform = entity_to_world_transform;
                
                transform.translation.x += area_size.x * x;
                transform.translation.z += area_size.z * z;
                
                if (entity->mesh) {
                    Draw_Entity draw_entity;
                    draw_entity.to_world_transform = transform;
                    draw_entity.mesh = entity->mesh;
                    draw_entity.color = entity->diffuse_color;
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
                    
                    if (light_entity.world_position.z < area_size.z * -0.5f)
                        intensity *= 1.0f - (area_size.z * -0.5f - light_entity.world_position.z) / entity->radius;
                    else if (light_entity.world_position.z > area_size.z * 0.5f)
                        intensity *= 1.0f - (light_entity.world_position.z - area_size.z * 0.5f) / entity->radius;
                    
                    draw_circle(imc, light_entity.world_position, imc_view_direction, entity->radius, make_rgba32(entity->diffuse_color * intensity));
                    
                    light_entity.diffuse_color  = entity->diffuse_color  * intensity;
                    light_entity.specular_color = entity->specular_color * intensity;
                    push(&light_entities, light_entity);
                }
            }
        }
    }
    
    ui_printf(ui, 5, 30, S("light count: %"), f(light_entities.count));
    
    // rendering
    
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
    
    // upload model view matrix
    glUniformMatrix4fv(state->phong_shader.u_camera_to_clip_projection, 1, GL_FALSE, state->camera_to_clip_projection);
    glUniformMatrix4x3fv(state->phong_shader.u_world_to_camera_transform, 1, GL_FALSE, state->world_to_camera_transform);
    glUniform3fv(state->phong_shader.u_camera_world_position, 1, camera_world_position);
    
    // lights
    
    Light_Entity main_light;
    main_light.world_position = camera_world_position;
    main_light.diffuse_color  = vec4f{1, 1, 1, 1};
    main_light.specular_color = vec4f{1, 1, 1, 1};
    push(&light_entities, main_light);
    
    if (state->phong_shader.u_light_world_positions != -1)
    {
        u32 light_index = 0;
        
        for (auto light_entity = first(&light_entities); light_entity != one_past_last(&light_entities); ++light_entity)
        {
            glUniform3fv(state->phong_shader.u_light_world_positions + light_index, 1, light_entity->world_position);
            glUniform4fv(state->phong_shader.u_light_diffuse_colors  + light_index, 1, light_entity->diffuse_color);
            glUniform4fv(state->phong_shader.u_light_specular_colors + light_index, 1, light_entity->specular_color);
            
            ++light_index;
        }
        
        ui_printf(ui, 5, 60, S("uniform light count: %"), f(light_index));
        
        glUniform1ui(state->phong_shader.u_light_count, light_index);
    }
    
    // render queue draw entities
    
    for (auto draw_entity = first(&draw_entities); draw_entity != one_past_last(&draw_entities); ++draw_entity)
    {
        glUniform4fv(state->phong_shader.u_ambient_color, 1, draw_entity->color * 0.1f);
        glUniform4fv(state->phong_shader.u_diffuse_color, 1, draw_entity->color);
        glUniformMatrix4x3fv(state->phong_shader.u_object_to_world_transform, 1, GL_FALSE, draw_entity->to_world_transform);
        
        draw(&draw_entity->mesh->batch, 0);
    }
    
    {
        glUniform4fv(state->phong_shader.u_diffuse_color, 1, vec4f{ 1.0f, 0.5f, 1.0f, 1.0f });
        glUniformMatrix4x3fv(state->phong_shader.u_object_to_world_transform, 1, GL_FALSE, state->beam->to_world_transform);
        
        //set_diffuse_texture(&state->phong_shader, state->beam_material.texture_object);
        //set_object_to_world_transform(&state->phong_shader, state->beam_transform);
        
        draw(&state->beam_mesh.batch, 0);
    }
    
    draw(imc);
    draw(ui);
}
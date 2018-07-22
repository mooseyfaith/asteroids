#include <platform.h>
#include <memory_growing_stack.h>
#include <immediate_render.h>
#include <ui_render.h>
#include <mesh.h>
#include <tga.h>

struct Entity {
    mat4x3f transform;
    f32 orientation;
    vec3f velocity;
    vec4 color;
};

#define BUFFER_TEMPLATE_NAME Entity_Buffer
#define BUFFER_TEMPLATE_DATA_TYPE Entity
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
    
    struct Ship_Entity {
        Entity *entity;
        f32 thruster_intensity;
    } ship;
    
    Entity *asteroid, *beam;
    
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
        //"#define WITH_DIFFUSE_COLOR\n"
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
    
    {
        state->ship.entity = push_reserve(&state->entities);
        
        string source = platform_api->read_text_file(S("meshs/astroids_ship.glm"), &state->transient_memory.allocator);
        state->ship_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->ship.entity->transform = MAT4X3_IDENTITY;
        state->ship.entity->color = make_vec4_scale(1.0f);
    }
    
    
    bool debug_ok = tga_load_texture(&state->asteroid_normal_map, S("meshs/asteroid_normal_map.tga"), platform_api->read_file, &state->transient_memory.allocator);
    debug_ok &= tga_load_texture(&state->asteroid_ambient_occlusion_map, S("meshs/asteroid_ambient_occlusion.tga"), platform_api->read_file, &state->transient_memory.allocator);
    
    assert(debug_ok);
    
    {
        state->asteroid = push_reserve(&state->entities);
        
        string source = platform_api->read_text_file(S("meshs/asteroid_baked.glm"), &state->transient_memory.allocator);
        state->asteroid_mesh = make_mesh(source, &state->persistent_memory.allocator, &state->debug_mesh_vertex_buffers, &state->debug_mesh_vertex_count);
        free(&state->transient_memory.allocator, source.data);
        
        state->asteroid->transform = make_transform(QUAT_IDENTITY, vec3f{ 4.0f, 0.0f, 0.0f }, make_vec3_scale(3.0f));
        state->asteroid->color = make_vec4_scale(1.0f);
    }
    
    {
        state->beam = push_reserve(&state->entities);
        string source = platform_api->read_text_file(S("meshs/astroids_beam.glm"), &state->transient_memory.allocator);
        state->beam_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->beam->transform = make_transform(QUAT_IDENTITY, vec3f{ -4.0f, 0.0f, 0.0f });
        state->beam->color = make_vec4_scale(1.0f);
    }
    
    state->camera.to_world_transform = make_transform(make_quat(VEC3_X_AXIS, PIf * -0.5f), vec3f{ 0.0f, 80.0f, 0.0f });
    state->main_window_area = { -1, -1, 800, 450 };
    
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
        
        vec3f imc_view_direction = -state->debug_camera.to_world_transform.forward;
        
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
    }
    
    Application_State::Ship_Entity *ship = &state->ship;
    f32 ship_scale = 1.0f;
    
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
            
            // rotate counter clockwise; mathematically positive
            if (input->keys['A'].is_active)
                rotation += 1.0f;
            
            // rotate clockwise; mathematically negative
            if (input->keys['D'].is_active)
                rotation -= 1.0f;
            
            ship->entity->orientation += rotation * PIf * delta_seconds;
            
            vec3f accelaration_vector = ship->entity->transform.forward * accelaration;
            
            ship->entity->velocity += accelaration_vector;
            f32 v2 = MIN(squared_length(ship->entity->velocity), Max_Velocity * Max_Velocity);
            
            ship->entity->velocity = normalize_or_zero(ship->entity->velocity) * sqrt(v2);
        }
        
        ship->entity->transform.translation += ship->entity->velocity * delta_seconds;
        
        if (ship->entity->transform.translation.x - bottem_left_corner.x < 0.0f) {
            ship->entity->transform.translation.x = area_size.x - dirty_mod(ship->entity->transform.translation.x - bottem_left_corner.x, area_size.x) + bottem_left_corner.x;
        }
        else {
            ship->entity->transform.translation.x = dirty_mod(ship->entity->transform.translation.x - bottem_left_corner.x, area_size.x) + bottem_left_corner.x;
        }
        
        if (ship->entity->transform.translation.z - bottem_left_corner.z < 0.0f) {
            ship->entity->transform.translation.z = area_size.z - dirty_mod(ship->entity->transform.translation.z - bottem_left_corner.z, area_size.z) + bottem_left_corner.z;
        }
        else {
            ship->entity->transform.translation.z = dirty_mod(ship->entity->transform.translation.z - bottem_left_corner.z, area_size.z) + bottem_left_corner.z;
        }
        
        ship->entity->transform = make_transform(make_quat(VEC3_Y_AXIS, ship->entity->orientation), ship->entity->transform.translation, vec3f{ ship_scale, ship_scale, ship_scale });
        
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
    
    glUseProgram(state->phong_shader.program_object);
    glDisable(GL_BLEND);
    
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
    
    
    //bind_phong_shader(&state->phong_shader);
    //set_light_position(&state->phong_shader, vec3f{ 0.0f, 2.0f, 0.0f });
    
    u32 ligth_index = 0;
    
    if (state->phong_shader.u_light_world_positions != -1) {
        if (ship->thruster_intensity > 0.0f) {
            glUniform3fv(state->phong_shader.u_light_world_positions + ligth_index, 1, transform_point(ship->entity->transform, vec3f{ 0.0f, 0.0f, -5.0f }));
            glUniform4fv(state->phong_shader.u_light_diffuse_colors  + ligth_index, 1, make_vec4(vec3f{ 5.0f, 5.0f, 0.0f } * ship->thruster_intensity));
            glUniform4fv(state->phong_shader.u_light_specular_colors + ligth_index, 1, make_vec4(vec3f{ 5.0f, 0.0f, 0.0f } * ship->thruster_intensity));
            
            ++ligth_index;
        }
        
        // main light
        glUniform3fv(state->phong_shader.u_light_world_positions + ligth_index, 1, camera_world_position);
        glUniform4fv(state->phong_shader.u_light_diffuse_colors  + ligth_index, 1, vec4f{ 1.0f, 1.0f, 1.0f, 1.0f });
        glUniform4fv(state->phong_shader.u_light_specular_colors + ligth_index, 1, vec4f{ 1.0f, 1.0f, 1.0f, 1.0f });
        ++ligth_index;
    }
    
    glUniform1ui(state->phong_shader.u_light_count, ligth_index);
    
    glUniformMatrix4fv(state->phong_shader.u_camera_to_clip_projection, 1, GL_FALSE, state->camera_to_clip_projection);
    glUniformMatrix4x3fv(state->phong_shader.u_world_to_camera_transform, 1, GL_FALSE, state->world_to_camera_transform);
    glUniform3fv(state->phong_shader.u_camera_world_position, 1, camera_world_position);
    
    
    {
        f32 ship_radius = ship_scale * 2.2f;
        
        s32 min_x;
        s32 max_x;
        if (ship->entity->transform.translation.x - ship_radius < area_size.x * -0.5f) {
            min_x = 0;
            max_x = min_x + 2;
        }
        else if (ship->entity->transform.translation.x + ship_radius > area_size.x * 0.5f) {
            min_x = -1;
            max_x = min_x + 2;
        }
        else {
            min_x = 0;
            max_x = 1;
        }
        
        s32 min_z;
        s32 max_z;
        if ((ship->entity->transform.translation.z - ship_radius < area_size.z * -0.5f)) {
            min_z = 0;
            max_z = min_z + 2;
        }
        else if ((ship->entity->transform.translation.z + ship_radius > area_size.z * 0.5f)) {
            min_z = -1;
            max_z = min_z + 2;
        }
        else {
            min_z = 0;
            max_z = 1;
        }
        
        vec4f ship_color = vec4f{ 1.0f, 1.0f, 1.0f, 1.0f };
        glUniform4fv(state->phong_shader.u_ambient_color, 1, ship_color * 0.1f);
        glUniform4fv(state->phong_shader.u_diffuse_color, 1, ship_color);
        //set_diffuse_texture(&state->phong_shader, state->ship_material.texture_object);
        
        for (s32 z = min_z; z < max_z; ++z) {
            for (s32 x = min_x; x < max_x; ++x) {
                mat4x3f transform = ship->entity->transform;
                
                transform.translation.x += area_size.x * x;
                transform.translation.z += area_size.z * z;
                glUniformMatrix4x3fv(state->phong_shader.u_object_to_world_transform, 1, GL_FALSE, transform);
                //set_object_to_world_transform(&state->phong_shader, transform);
                
                draw(&state->ship_mesh.batch, 0);
            }
        }
    }
    
    {
        //set_diffuse_texture(&state->phong_shader, state->astroid_material.texture_object);
        //set_object_to_world_transform(&state->phong_shader, state->astroid_transform);
        
        glUniform4fv(state->phong_shader.u_diffuse_color, 1, vec4f{ 1.0f, 1.0f, 1.0f, 1.0f });
        glUniformMatrix4x3fv(state->phong_shader.u_object_to_world_transform, 1, GL_FALSE, state->asteroid->transform);
        
        draw(&state->asteroid_mesh.batch, 0);
    }
    
    {
        glUniform4fv(state->phong_shader.u_diffuse_color, 1, vec4f{ 1.0f, 0.5f, 1.0f, 1.0f });
        glUniformMatrix4x3fv(state->phong_shader.u_object_to_world_transform, 1, GL_FALSE, state->beam->transform);
        
        //set_diffuse_texture(&state->phong_shader, state->beam_material.texture_object);
        //set_object_to_world_transform(&state->phong_shader, state->beam_transform);
        
        draw(&state->beam_mesh.batch, 0);
    }
    
    draw(imc);
    draw(ui);
}
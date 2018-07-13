#include <platform.h>
#include <memory_growing_stack.h>
#include <immediate_render.h>
#include <ui_render.h>
#include <mesh.h>
#include <phong_material.h>

Pixel_Dimensions const Reference_Resolution = { 1280, 720 };

f32 const Camera_Move_Speed = 20.0f;
f32 const Camera_Mouse_Sensitivity = 2.0f * PIf / 2048.0f;
vec3f const Debug_Camera_Axis_Alpha = VEC3_Y_AXIS;
vec3f const Debug_Camera_Axis_Beta = VEC3_X_AXIS;

f32 const Player_Rotation_Speed = PIf * 4.0f;
f32 const Player_Movement_Speed = 12.0f;

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
    
    struct Camera {
        union { mat4x3f to_world_transform, inverse_view_matrix; };
    } debug_camera, camera;
    
    union { mat4x3f world_to_camera_transform, view_matrix; };
    
    f32 debug_camera_alpha;
    f32 debug_camera_beta;
    
    vec2f last_mouse_window_position;
    
    Phong_Shader phong_shader;
    Phong_Material ship_material, astroid_material, beam_material;
    
    Mesh ship_mesh, astroid_mesh, beam_mesh;
    mat4x3f ship_transform, astroid_transform, beam_transform;
    
    bool in_debug_mode;
};

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

APP_INIT_DEC(application_init) {
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
        
        const char *uniform_names[] = {
            "u_texture",
            "u_alpha_threshold",
        };
        assert(ARRAY_COUNT(uniform_names) == ARRAY_COUNT(state->ui_font_material.shader.uniforms));
        
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
    
    state->font_lib = make_font_library();
    state->font = make_font(state->font_lib, S("C:/Windows/Fonts/arial.ttf"), 32, ' ', 128, platform_api->read_file, &state->persistent_memory.allocator);
    set_texture_filter_level(state->font.texture.object, Texture_Filter_Level_Linear);
    
    state->ui_font_material.base.bind_material = bind_ui_font_material;
    state->ui_font_material.texture = &state->font.texture;
    
    string phong_vertex_shader_source = platform_api->read_text_file(S("shaders/phong_pixel.vert.txt"), &state->transient_memory.allocator);
    string phong_fragment_shader_source = platform_api->read_text_file(S("shaders/phong_pixel.frag.txt"), &state->transient_memory.allocator);
    state->phong_shader = make_phong_shader(phong_vertex_shader_source, phong_fragment_shader_source, &state->transient_memory.allocator);
    free(&state->transient_memory.allocator, phong_fragment_shader_source.data);
    free(&state->transient_memory.allocator, phong_vertex_shader_source.data);
    
    {
        string source = platform_api->read_text_file(S("meshs/astroids_ship.glm"), &state->transient_memory.allocator);
        state->ship_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->ship_transform = MAT4X3_IDENTITY;
        
        state->ship_material.bind_material = bind_phong_material;
        state->ship_material.shader = &state->phong_shader;
        state->ship_material.camera_to_clip_projection = &state->camera_to_clip_projection;
        state->ship_material.world_to_camera_transform = &state->world_to_camera_transform;
        state->ship_material.object_to_world_transform = &state->ship_transform;
        
        glGenTextures(1, &state->ship_material.texture_object);
        glBindTexture(GL_TEXTURE_2D, state->ship_material.texture_object);
        rgb32 color = make_rgb32(0.1f, 0.1f, 0.8f);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, &color);
    }
    
    {
        string source = platform_api->read_text_file(S("meshs/astroids_astroid.glm"), &state->transient_memory.allocator);
        state->astroid_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->astroid_transform = make_transform(QUAT_IDENTITY, vec3f{ 4.0f, 0.0f, 0.0f });
        
        state->astroid_material = state->ship_material;
        state->astroid_material.object_to_world_transform = &state->astroid_transform;
        
        glGenTextures(1, &state->astroid_material.texture_object);
        glBindTexture(GL_TEXTURE_2D, state->astroid_material.texture_object);
        rgb32 color = make_rgb32(0.7f, 0.8f, 0.2f);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, &color);
    }
    
    {
        string source = platform_api->read_text_file(S("meshs/astroids_beam.glm"), &state->transient_memory.allocator);
        state->beam_mesh = make_mesh(source, &state->persistent_memory.allocator);
        free(&state->transient_memory.allocator, source.data);
        
        state->beam_transform = make_transform(QUAT_IDENTITY, vec3f{ -4.0f, 0.0f, 0.0f });
        
        state->beam_material = state->ship_material;
        state->beam_material.object_to_world_transform = &state->beam_transform;
        
        glGenTextures(1, &state->beam_material.texture_object);
        glBindTexture(GL_TEXTURE_2D, state->beam_material.texture_object);
        rgb32 color = make_rgb32(1.0f, 0.0f, 1.0f);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, &color);
    }
    
    state->camera.to_world_transform = make_transform(make_quat(VEC3_X_AXIS, PIf * -0.5f), vec3f{ 0.0f, 80.0f, 0.0f });
    state->main_window_area = { -1, -1, 800, 450 };
    
    return state;
}

APP_MAIN_LOOP_DEC(application_main_loop) {
    Application_State *state = CAST_P(Application_State, app_data_ptr);
    auto imc = &state->immediate_render_context;
    auto ui = &state->ui_render_context;
    // check if .dll was reloaded
    {
#if 0
        global_debug_draw_info.immediate_render_context = &state->imc;
        global_debug_draw_info.max_iteration_count = 0;
        
        mat4x3f idenity;
        set_identity(&idenity);
        DEBUG_DRAW_SET_TO_WORLD_MATRIX(idenity);
#endif
        
        if (!glUseProgram)
            init_gl();
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
    
    clear(&state->transient_memory.memory_growing_stack);
    
    set_auto_viewport(state->main_window_area.size, render_resolution, vec4f{ 0.05f, 0.05f, 0.05f, 1.0f }, vec4f{ 0.05f, 0.05f, 0.05f, 1.0f });
    
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
    
    // update debug camera
    if (state->in_debug_mode) {
        if (was_pressed(input->mouse.right))
            state->last_mouse_window_position = input->mouse.window_position;
        else if (input->mouse.right.is_active) {
            vec2f mouse_delta =  input->mouse.window_position - state->last_mouse_window_position;
            state->last_mouse_window_position = input->mouse.window_position;
            
            state->debug_camera_alpha -= mouse_delta.x * Camera_Mouse_Sensitivity;
            
            state->debug_camera_beta -= mouse_delta.y * Camera_Mouse_Sensitivity;
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
        
        state->debug_camera.to_world_transform.translation +=  transform_direction(state->debug_camera.to_world_transform, direction) * delta_seconds * Camera_Move_Speed;
        state->world_to_camera_transform =  make_inverse_unscaled_transform(state->debug_camera.to_world_transform);
        
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
    }
    else {
        state->world_to_camera_transform =  make_inverse_unscaled_transform(state->camera.to_world_transform);
        
        
    }
    
    glUseProgram(state->phong_shader.program_object);
    glUniform3fv(state->phong_shader.u_light_world_position, 1, vec3f{ 0.0f, 2.0f, 0.0f });
    
    {
        Render_Material *materials[] = { CAST_P(Render_Material, &state->ship_material) };
        draw(&state->ship_mesh.batch, ARRAY_WITH_COUNT(materials));
    }
    
    {
        Render_Material *materials[] = { CAST_P(Render_Material, &state->astroid_material) };
        draw(&state->astroid_mesh.batch, ARRAY_WITH_COUNT(materials));
    }
    
    {
        Render_Material *materials[] = { CAST_P(Render_Material, &state->beam_material) };
        draw(&state->beam_mesh.batch, ARRAY_WITH_COUNT(materials));
    }
    
    draw(imc);
    draw(ui);
}
#include <SDL3/SDL.h>
#include <cmath>
#include <glad/glad.h>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <chrono>
#include <iostream>
#include <optional>
#include <print>
#include <numbers>
#include <random>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

auto constexpr work_groups = std::size_t{256};
auto constexpr work_group_size = std::size_t{256};
auto constexpr num_particles = work_group_size * work_groups;

template <SDL_InitFlags flag>
class sdl_subsystem
{
    public:
    sdl_subsystem()
    {
        if(!SDL_InitSubSystem(flag))
        {
            throw std::runtime_error{"InitSubsystem failed"};
        }
    }

    sdl_subsystem(sdl_subsystem const &)
    {
        if(!SDL_InitSubSystem(flag))
        {
            throw std::runtime_error{"InitSubsystem failed"};
        }
    }

    ~sdl_subsystem()
    {
        SDL_QuitSubSystem(flag);
    }

    private:
    sdl_subsystem(sdl_subsystem &&) = delete;
    sdl_subsystem & operator=(sdl_subsystem const &) = delete;
    sdl_subsystem & operator=(sdl_subsystem &&) = delete;
}; /* class sdl_subsystem */

class gl_window
{
    public:
    gl_window(sdl_subsystem<SDL_INIT_VIDEO> const &):
        m_window{SDL_CreateWindow("compute-test", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE)},
        m_context{SDL_GL_CreateContext(m_window)}
    {
        if(!m_context)
        {
            throw std::runtime_error{"GL context creation failed"};
        }

        SDL_GL_MakeCurrent(m_window, m_context);
        gladLoadGLLoader(reinterpret_cast<GLADloadproc>(&SDL_GL_GetProcAddress));
    }

    gl_window(gl_window && other):
        m_window{std::exchange(other.m_window, nullptr)},
        m_context{std::exchange(other.m_context, nullptr)}
    {}

    ~gl_window()
    {
        SDL_GL_DestroyContext(m_context);
        SDL_DestroyWindow(m_window);
    }

    void swap() const
    {
        SDL_GL_SwapWindow(m_window);
    }

    private:
    gl_window() = delete;
    gl_window(gl_window const &) = delete;

    SDL_Window * m_window = nullptr;
    SDL_GLContext m_context = nullptr;
}; /* class gl_window */

class vertex_array_object
{
    public:
    vertex_array_object()
    {
        glCreateVertexArrays(1, &m_object);
    }

    vertex_array_object(vertex_array_object && other)
    {
        if(m_object)
        {
            if(g_bound == this)
            {
                glBindVertexArray(0u);
                g_bound = nullptr;
            }
        }

        glDeleteVertexArrays(1, &m_object);
        m_object = std::exchange(other.m_object, 0u);

        if(g_bound == &other)
        {
            g_bound = this;
        }
    }

    ~vertex_array_object()
    {
        if(m_object == 0u)
        {
            return;
        }

        if(g_bound == this)
        {
            glBindVertexArray(0u);
            g_bound = nullptr;
        }

        glDeleteVertexArrays(1, &m_object);
    }

    vertex_array_object & operator=(vertex_array_object && other)
    {
        if(m_object != 0u)
        {
            if(g_bound == this)
            {
                glBindVertexArray(0u);
                g_bound = nullptr;
            }

            glDeleteVertexArrays(1, &m_object);
        }

        m_object = std::exchange(other.m_object, 0u);

        if(g_bound == &other)
        {
            g_bound = this;
        }

        return *this;
    }

    void bind()
    {
        glBindVertexArray(m_object);
        g_bound = this;
    }

    private:
    vertex_array_object(vertex_array_object const &) = delete;
    vertex_array_object & operator=(vertex_array_object const &) = delete;

    static inline vertex_array_object * g_bound = nullptr;
    GLuint m_object = 0u;
}; /* class vertex_array_object */

auto poll_event() -> std::optional<SDL_Event>
{
    SDL_Event e;
    return SDL_PollEvent(&e) ? std::make_optional(e) : std::nullopt;
}

void print_gl_info()
{
    auto const get_string = [](auto e)
    {
        return reinterpret_cast<char const *>(glGetString(e));
    };

    std::println("GL vendor: {}", get_string(GL_VENDOR));
    std::println("GL renderer: {}", get_string(GL_RENDERER));
    std::println("GL version: {}", get_string(GL_VERSION));
    std::println("GL shading language version: {}", get_string(GL_SHADING_LANGUAGE_VERSION));
}

GLuint create_shader(GLenum type, std::string_view source)
{
    auto shader_object = glCreateShader(type);

    auto const * const source_data = source.data();
    auto const source_len = static_cast<GLint>(source.size());

    glShaderSource(shader_object, 1, &source_data, &source_len);
    glCompileShader(shader_object);

    auto status = GLint{0};
    glGetShaderiv(shader_object, GL_COMPILE_STATUS, &status);

    auto len = GLsizei{0};

    glGetShaderiv(shader_object, GL_INFO_LOG_LENGTH, &len);

    if(len)
    {
        auto cv = GLsizei{0};
        auto chars = std::vector<char>(static_cast<std::size_t>(len));
        glGetShaderInfoLog(shader_object, len, &cv, chars.data());
        std::println(std::cerr, "Shader error: {}", chars.data());
    }

    if(status == GL_FALSE)
    {
        std::println(std::cerr, "Shader compile error.");
        glDeleteShader(shader_object);
        return 0u;
    }

    return shader_object;
}

void dump_program_info_log(GLuint program)
{
    auto len = GLsizei{0};
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);

    if(len == 0)
    {
        return;
    }

    auto cv = GLsizei{0};
    auto chars = std::vector<char>(static_cast<std::size_t>(len));
    glGetProgramInfoLog(program, len, &cv, chars.data());
    std::println("Program info log: {}", chars.data());
}

class gl_shader
{
    public:
    gl_shader() = default;

    gl_shader(GLenum t, std::string_view source):
        m_type{t},
        m_object{create_shader(t, source)}
    {}

    gl_shader(gl_shader && other):
        m_type{std::exchange(other.m_type, GL_FALSE)},
        m_object{std::exchange(other.m_object, 0u)}
    {}

    ~gl_shader()
    {
        if(m_object)
        {
            glDeleteShader(m_object);
        }
    }

    gl_shader & operator=(gl_shader && other)
    {
        if(m_object)
        {
            glDeleteShader(m_object);
        }

        m_type = std::exchange(other.m_type, GL_FALSE);
        m_object = std::exchange(other.m_object, 0u);

        return *this;
    }

    void use() const
    {
        glUseProgram(m_object);
    }

    private:
    friend class gl_shader_program;
    gl_shader(gl_shader const &) = delete;
    gl_shader & operator=(gl_shader const &) = delete;

    GLenum m_type = GL_FALSE;
    GLuint m_object = 0u;
}; /* class gl_shader */

class gl_shader_program
{
    public:
    gl_shader_program() = default;

    template <typename ... S>
        requires (std::is_same_v<std::decay_t<S>, gl_shader> && ...)
    gl_shader_program(S const & ... shader):
        m_object{glCreateProgram()}
    {
        (glAttachShader(m_object, shader.m_object), ...);
        glLinkProgram(m_object);
        dump_program_info_log(m_object);
    }

    gl_shader_program(gl_shader_program && other)
    {
        *this = std::move(other);
    }

    ~gl_shader_program()
    {
        if(g_used == this)
        {
            glUseProgram(0u);
            g_used = nullptr;
        }

        if(m_object)
        {
            glDeleteProgram(m_object);
        }
    }

    gl_shader_program & operator=(gl_shader_program && other)
    {
        if(g_used == this)
        {
            glUseProgram(0u);
            g_used = nullptr;
        }

        if(m_object)
        {
            glDeleteProgram(m_object);
        }

        m_object = std::exchange(other.m_object, 0u);

        if(g_used == &other)
        {
            use();
        }

        return *this;
    }

    void use() const
    {
        if(m_object)
        {
            glUseProgram(m_object);
            g_used = this;
        }
    }

    private:
    static inline gl_shader_program const * g_used = nullptr;
    gl_shader_program(gl_shader_program const &) = delete;
    gl_shader_program & operator=(gl_shader_program const &) = delete;

    GLuint m_object = 0u;
}; /* class gl_shader_program */

auto const * const compute_shader_src = R"glsl(#version 460 core

struct object_t
{
    vec4 position;
    vec4 velocity;
};

layout(std430, binding = 0) buffer objs_b
{
    object_t objects[];
};

layout (location = 0) uniform vec4 screen_size;
layout (location = 1) uniform float time_delta;

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

void main()
{
    uint id = gl_GlobalInvocationID.x;
    objects[id].position += objects[id].velocity * time_delta;

    if(abs(objects[id].position.x) >= screen_size.x)
    {
        objects[id].position.x = sign(objects[id].position.x) * screen_size.x;
        objects[id].velocity.x *= -1.0;
    }

    if(abs(objects[id].position.y) >= screen_size.y)
    {
        objects[id].position.y = sign(objects[id].position.y) * screen_size.y;
        objects[id].velocity.y *= -1.0;
    }
}

)glsl";

auto const * const vertex_shader_src = R"glsl(#version 460 core

struct object_t
{
    vec4 position;
    vec4 velocity;
};

layout(std430, binding = 0) buffer objs_b
{
    object_t objects[];
};

layout (location = 0) uniform vec4 screen_size;

const vec2 offset_table[6] = vec2[]
(
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),

    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main()
{
    const float sprite_size = 4.0;
    vec2 pos = objects[gl_InstanceID].position.xy / screen_size.xy;
    vec2 off = offset_table[gl_VertexID] * sprite_size / screen_size.xy;
    gl_Position = vec4(pos + off, 0.0, 1.0);
}

)glsl";

auto const * const fragment_shader_src = R"glsl(#version 460 core

out vec4 out_color;

void main()
{
    out_color = vec4(1.0, 1.0, 1.0, 1.0);
}

)glsl";

struct object_t
{
    glm::vec4 position;
    glm::vec4 velocity;
};

std::vector<object_t> create_objects()
{
    auto rd = std::random_device{};
    auto rng = std::mt19937{rd()};
    auto random_angle = std::uniform_real_distribution<float>{0.0f, 2 * std::numbers::pi_v<float>};
    auto random_vel = std::uniform_real_distribution<float>{60.0, 320.0f};

    auto r = std::vector<object_t>{};
    r.reserve(num_particles);

    for(auto i: std::views::iota(0u, num_particles))
    {
        auto const a = random_angle(rng);
        auto const v = random_vel(rng);
        r.emplace_back(
            glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
            glm::vec4{v * std::sinf(a), v * cosf(a), 0.0f, 0.0f});
    }

    return r;
}

} /* namespace */

int main(int argc, char * argv[])
{
    std::ignore = argc;
    std::ignore = argv;

    auto const video = sdl_subsystem<SDL_INIT_VIDEO>{};
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    auto window = gl_window{video};
    auto vao = vertex_array_object{};
    vao.bind();

    print_gl_info();

    auto objects_data = create_objects();
    auto objects_buffer = GLuint{0};
    glCreateBuffers(1, &objects_buffer);
    glNamedBufferData(
        objects_buffer,
        objects_data.size() * sizeof(object_t),
        objects_data.data(),
        GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, objects_buffer);

    auto compute_shader = gl_shader{GL_COMPUTE_SHADER, compute_shader_src};
    auto compute_program = gl_shader_program{compute_shader};

    auto vertex_shader = gl_shader{GL_VERTEX_SHADER, vertex_shader_src};
    auto fragment_shader = gl_shader{GL_FRAGMENT_SHADER, fragment_shader_src};
    auto render_program = gl_shader_program{vertex_shader, fragment_shader};

    auto viewport = glm::ivec2{1280, 720};
    auto screen_size = glm::vec4{viewport.x, viewport.y, 0.0f, 0.0f};

    std::println("{} x {} = {} particles", work_group_size, work_groups, num_particles);
    auto done = false;
    auto const start_time = std::chrono::high_resolution_clock::now();
    auto prev_frame = start_time;
    auto frame_count = std::size_t{0};
    while(!done)
    {
        ++frame_count;
        auto const frame_time = std::chrono::high_resolution_clock::now();
        auto const time_delta = std::chrono::duration<float>{frame_time - prev_frame};
        prev_frame = frame_time;

        while(auto const event = poll_event())
        {
            switch(event->type)
            {
                case SDL_EVENT_QUIT:
                    done = true;
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    viewport = glm::ivec2{event->window.data1, event->window.data2};
                    screen_size = glm::vec4{viewport.x, viewport.y, 0.0f, 0.0f};
                    break;

                default:
                    break;
            }
        }

        glClearColor(0.2, 0.0, 0.4, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, viewport.x, viewport.y);

        compute_program.use();
        glUniform4fv(0, 1, &screen_size.x);
        glUniform1f(1, time_delta.count());
        glDispatchCompute(work_groups, 1, 1);

        render_program.use();
        glUniform4fv(0, 1, &screen_size.x);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_particles);

        window.swap();

        auto err = glGetError();
        if(err != GL_NO_ERROR)
        {
            std::println(std::cerr, "GL error: {}", err);
            return -1;
        }
    }
    auto const end_time = std::chrono::high_resolution_clock::now();
    auto const duration = std::chrono::duration<double>(end_time - start_time);

    std::println("{} frames in {}", frame_count, duration);
    std::println("{} frames per second", frame_count/duration.count());

    glDeleteBuffers(1, &objects_buffer);
}

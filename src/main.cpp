#include <glad/glad.h>

#include <SDL3/SDL.h>

#include <optional>
#include <print>
#include <utility>
#include <stdexcept>

namespace
{

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
        m_window{SDL_CreateWindow("compute-test", 1280, 720, SDL_WINDOW_OPENGL)},
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

auto poll_event() -> std::optional<SDL_Event>
{
    SDL_Event e;
    return SDL_PollEvent(&e) ? std::make_optional(e) : std::nullopt;
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    auto window = gl_window{video};

    auto done = false;
    while(!done)
    {
        while(auto const event = poll_event())
        {
            switch(event->type)
            {
                case SDL_EVENT_QUIT:
                    done = true;
                    break;

                default:
                    break;
            }
        }

        glClearColor(0.5, 0.0, 1.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        window.swap();
    }

    std::println("Hello, world!");
}

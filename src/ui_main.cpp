//
// Includes
//

// SDL
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

// Uni.GUI
#include "ui_app.h"
#include "ui_callbacks.h"



//
// Typedefs
//

struct AppState
{
    Uni::GUI::Ui ui{};
    std::vector<std::shared_ptr<Uni::GUI::UiElement>> elements;
};



//
// Implementation
//

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    // initialize UI
    auto* state = new AppState();
    if (!state)
    {
        return SDL_APP_FAILURE;
    }

    *appstate = state;

    if (!state->ui.Init(uni_gui_app_name_get() + " v" + uni_gui_app_version_get())) {
        return SDL_APP_FAILURE;
    }
    state->ui.SetVsync(1);

    // register windows
    state->elements = uni_gui_app_initialize(argc, argv);
    if (state->elements.empty())
    {
        return SDL_APP_FAILURE;
    }
    for (auto& element : state->elements)
    {
        state->ui.RegisterWindow(element.get());
    }

    return SDL_APP_CONTINUE;
}


SDL_AppResult SDL_AppIterate(void *appstate)
{
    if (appstate)
    {
        static_cast<AppState*>(appstate)->ui.Process();
        return SDL_APP_CONTINUE;
    }
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (!appstate)
    {
        return SDL_APP_FAILURE;
    }

    if ( !static_cast<AppState*>(appstate)->ui.ProcessEvent(event))
    {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (appstate)
    {
        delete static_cast<AppState*>(appstate);
    }
}
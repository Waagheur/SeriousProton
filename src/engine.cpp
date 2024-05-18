#include "engine.h"
#include "random.h"
#include "Updatable.h"
#include "collisionable.h"
#include "audio/source.h"
#include "io/keybinding.h"
#include "soundManager.h"
#include "windowManager.h"
#include "scriptInterface.h"
#include "multiplayer_server.h"

#include <thread>
#include <SDL.h>

#include <iostream>
#include <chrono>

#ifdef STEAMSDK
#include "steam/steam_api.h"
#include "steam/steam_api_flat.h"
#endif

#ifdef DEBUG
#include <typeinfo>
int DEBUG_PobjCount;
PObject* DEBUG_PobjListStart;
#endif

Engine* engine;

Engine::Engine()
{
    engine = this;

#ifdef STEAMSDK
    if (SteamAPI_RestartAppIfNecessary(1907040))
        exit(1);
    if (!SteamAPI_Init())
    {
        LOG(Error, "Failed to initialize steam API.");
        exit(1);
    }
    SteamNetworkingUtils()->InitRelayNetworkAccess();
    LOG(Debug, "SteamID:", SteamAPI_ISteamUser_GetSteamID(SteamAPI_SteamUser()));
#endif

#ifdef WIN32
    // Setup crash reporter (Dr. MinGW) if available.
    exchndl = DynamicLibrary::open("exchndl.dll");

    if (exchndl)
    {
        auto pfnExcHndlInit = exchndl->getFunction<void(*)(void)>("ExcHndlInit");

        if (pfnExcHndlInit)
        {
            pfnExcHndlInit();
            LOG(INFO) << "Crash Reporter ON";
        }
        else
        {
            exchndl.reset();
        }
    } 
#endif // WIN32

    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"); // Have clicking on a window to get focus generate mouse events. For multimonitor support.
#ifdef SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH
    SDL_SetHint(SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH, "1");
#elif defined(SDL_HINT_MOUSE_TOUCH_EVENTS)
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_ShowCursor(false);
    SDL_StopTextInput();

    atexit(SDL_Quit);

    initRandom();
    CollisionManager::initialize();
    gameSpeed = 1.0f;
    running = true;
    elapsedTime = 0.0f;
    soundManager = new SoundManager();
}

Engine::~Engine()
{
    Window::all_windows.clear();
    updatableList.clear();
    safeUpdatableList.clear();
    delete soundManager;
    soundManager = nullptr;
}

void Engine::registerObject(string name, P<PObject> obj)
{
    objectMap[name] = obj;
}

P<PObject> Engine::getObject(string name)
{
    if (!objectMap[name])
        return NULL;
    return objectMap[name];
}

/**
#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
void handler(int sig) {
  void *array[10];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}

signal(SIGSEGV, handler);   // install our handler
/**/
// OMP functions and stuff
// #include <omp.h>
#include <future>
#include <vector>

void Engine::runMainLoop()
{	
    if (Window::all_windows.size() == 0)
    {
        sp::SystemStopwatch frame_timer;
#ifdef DEBUG
        sp::SystemTimer debug_output_timer;
        debug_output_timer.repeat(5);
#endif

        while(running)
        {
            // Handle SDL_QUIT event
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT)
                {
                    running = false;
                }
            }
#ifdef DEBUG
            if (debug_output_timer.isExpired())
                LOG(DEBUG) << "Object count: " << DEBUG_PobjCount << " " << updatableList.size();
#endif

            float delta = frame_timer.restart();
            if (delta > 0.5f)
                delta = 0.5f;
            if (delta < 0.001f)
                delta = 0.001f;
            delta *= gameSpeed;

            foreach(Updatable, u, updatableList)
                u->update(delta);
            elapsedTime += delta;
            CollisionManager::handleCollisions(delta);
            ScriptObject::clearDestroyedObjects();
            soundManager->updateTick();
#ifdef STEAMSDK
            SteamAPI_RunCallbacks();
#endif
            std::this_thread::sleep_for(std::chrono::duration<float>(1.f/60.f - delta));
        }
    }else{
        sp::audio::Source::startAudioSystem();
        sp::SystemStopwatch frame_timer;
#ifdef DEBUG
        sp::SystemTimer debug_output_timer;
        debug_output_timer.repeat(5);
#endif
		// omp_set_num_threads(omp_get_num_procs());
// #pragma omp parallel
// {
	// #pragma omp master
	// {
		std::vector<std::future<void>> tasks;
		// std::future<void> task2;
        while(running)
        {
			auto preEvents = std::chrono::high_resolution_clock::now();
            // Handle events
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                handleEvent(event);
            }

#ifdef DEBUG
            if (debug_output_timer.isExpired())
                LOG(DEBUG) << "Object count: " << DEBUG_PobjCount << " " << updatableList.size();
#endif
			auto postEvents = std::chrono::high_resolution_clock::now();
			std::cout << "(postEvents - preEvents).count()" << (postEvents - preEvents).count() << std::endl;
            float delta = frame_timer.restart();
            if (delta > 0.5f)
                delta = 0.5f;
            if (delta < 0.001f)
                delta = 0.001f;
            delta *= gameSpeed;
            EngineTiming engine_timing;
            
            sp::SystemStopwatch engine_timing_stopwatch;
			auto preUpdates = std::chrono::high_resolution_clock::now();
			
			// foreach(Updatable, u, safeUpdatableList) {
				// u->update(delta);
				// #pragma omp task
				// u->safeUpdate(delta);
				// std::thread t([&u, &delta] () {u->safeUpdate(delta);});
				// t.detach();
				// Must be a *u because otherwise we're not passing an Updatable to the function
				// task = std::async(std::launch::async, &Updatable::safeUpdate, *u, delta);
			// }
			
			// Piterator<Updatable> u(updatableList);
			// Marche pas avec plusieurs threads
			// #pragma omp parallel for num_threads(4)
			//#pragma omp parallel for
				// for(P<Updatable> u : updatableList) {
					// if (*u) {
						// u->update(delta);
					// }
				// }
				// foreach(type, var, list) 
				// for(Piterator<type> var(list); var; var.next())
				// for (Piterator<Updatable> u(updatableList); u; u.next()) {
				// for (u; u < std::end(); u.next()) {
				// for (auto u : Piterator<Updatable>(updatableList)) {
					// u->update(delta);
				// }
				// Source
				foreach(Updatable, u, updatableList) {
					u->update(delta);
				}
				// foreach(Updatable, u, updatableList) {
					// std::thread td(&Updatable::update, (*u), delta);
					// td.join();
					// td.detach();
				// }
			// Marche pas
			// #pragma omp parallel num_threads(4)
			// {
				// #pragma omp single
				// foreach(Updatable, u, updatableList) {
					// #pragma omp task firstprivate(u)
					// u->update(delta);
				// }
			// }
					// #pragma omp task
					// {
						// foreach(Updatable, u, updatableList) {
							// u->update(delta);
						// }
					// }
					
			// auto task = std::async(std::launch::async, 
				// [&delta] (PVector<Updatable>& safeUpdatableList) {
					// foreach(Updatable, u, safeUpdatableList) {
						// u->safeUpdate(delta);
					// }
				// }, safeUpdatableList
			// );
			foreach(Updatable, u, safeUpdatableList) {
				u->update(delta);
				// #pragma omp task
				// u->safeUpdate(delta);
				// std::thread t([&u, &delta] () {u->safeUpdate(delta);});
				// t.detach();
				// Must be a *u because otherwise we're not passing an Updatable to the function
				tasks.push_back(std::async(std::launch::async, &Updatable::safeUpdate, *u, delta));
			}
					
			auto postUpdates = std::chrono::high_resolution_clock::now();
			std::cout << "(postUpdates - preUpdates).count()" << (postUpdates - preUpdates).count() << std::endl;
				
            elapsedTime += delta;
            engine_timing.update = engine_timing_stopwatch.restart();
			auto preCollisions = std::chrono::high_resolution_clock::now();
            
			// source
			CollisionManager::handleCollisions(delta);
			// task2 = std::async(std::launch::async, &CollisionManager::handleCollisions, delta);
			// std::thread t([&delta] () {
				// CollisionManager::handleCollisions(delta);
				// ScriptObject::clearDestroyedObjects();
			// });
			// t.detach();
				
            engine_timing.collision = engine_timing_stopwatch.restart();
			auto postCollisions = std::chrono::high_resolution_clock::now();
			std::cout << "(postCollisions - preCollisions).count()" << (postCollisions - preCollisions).count() << std::endl;
			
            ScriptObject::clearDestroyedObjects();
            soundManager->updateTick();
#ifdef STEAMSDK
            SteamAPI_RunCallbacks();
#endif

            // Clear the window
			auto preRender = std::chrono::high_resolution_clock::now();
			// Source
            for(auto window : Window::all_windows)
                window->render();
			// for(auto window : Window::all_windows) {
				// std::thread td(&Window::render, *window);
                // td.join();
				// td.detach();
				// task = std::async(std::launch::async, &Window::render, *window);
			// }
			auto postRender = std::chrono::high_resolution_clock::now();
			std::cout << "(postRender - preRender).count()" << (postRender - preRender).count() << std::endl;
			
            engine_timing.render = engine_timing_stopwatch.restart();
            engine_timing.server_update = 0.0f;
            if (game_server)
                engine_timing.server_update = game_server->getUpdateTime();
            
			tasks.clear();
			
            last_engine_timing = engine_timing;

            sp::io::Keybinding::allPostUpdate();
			
			auto postLoop = std::chrono::high_resolution_clock::now();
			std::cout << "(postLoop - postCollisions).count()" << (postLoop - postCollisions).count() << std::endl;
        }
        soundManager->stopMusic();
        sp::audio::Source::stopAudioSystem();
	// }
// }
    }
}

void Engine::handleEvent(SDL_Event& event)
{
    if (event.type == SDL_QUIT)
        running = false;
#ifdef DEBUG
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
        running = false;
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_l && !SDL_IsTextInputActive())
    {
        int n = 0;
        printf("------------------------\n");
        std::unordered_map<string,int> totals;
        for(PObject* obj = DEBUG_PobjListStart; obj; obj = obj->DEBUG_PobjListNext)
        {
            printf("%c%4d: %4d: %s\n", obj->isDestroyed() ? '>' : ' ', n++, obj->getRefCount(), typeid(*obj).name());
            if (!obj->isDestroyed())
            {
                totals[typeid(*obj).name()]=totals[typeid(*obj).name()]+1;
            }
        }
        printf("--non-destroyed totals--\n");
        int grand_total=0;
        for (auto entry : totals)
        {
            printf("%4d %s\n", entry.second, entry.first.c_str());
            grand_total+=entry.second;
        }
        printf("%4d %s\n",grand_total,"All PObjects");
        printf("------------------------\n");
    }
#endif

    unsigned int window_id = 0;
    switch(event.type)
    {
    case SDL_KEYDOWN:
#ifdef __EMSCRIPTEN__
        if (!audio_started)
        {
            audio::AudioSource::startAudioSystem();
            audio_started = true;
        }
#endif
    case SDL_KEYUP:
        window_id = event.key.windowID;
        break;
    case SDL_MOUSEMOTION:
        window_id = event.motion.windowID;
        break;
    case SDL_MOUSEBUTTONDOWN:
#ifdef __EMSCRIPTEN__
        if (!audio_started)
        {
            audio::AudioSource::startAudioSystem();
            audio_started = true;
        }
#endif
    case SDL_MOUSEBUTTONUP:
        window_id = event.button.windowID;
        break;
    case SDL_MOUSEWHEEL:
        window_id = event.wheel.windowID;
        break;
    case SDL_WINDOWEVENT:
        window_id = event.window.windowID;
        break;
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION:
#if SDL_VERSION_ATLEAST(2, 0, 12)
        window_id = event.tfinger.windowID;
#else
        window_id = SDL_GetWindowID(SDL_GetMouseFocus());
#endif
        break;
    case SDL_TEXTEDITING:
        window_id = event.edit.windowID;
        break;
    case SDL_TEXTINPUT:
        window_id = event.text.windowID;
        break;
    }
    if (window_id != 0)
    {
        foreach(Window, window, Window::all_windows)
            if (window->window && SDL_GetWindowID(static_cast<SDL_Window*>(window->window)) == window_id)
                window->handleEvent(event);
    }
    sp::io::Keybinding::handleEvent(event);
}

void Engine::setGameSpeed(float speed)
{
    gameSpeed = speed;
}

float Engine::getGameSpeed()
{
    return gameSpeed;
}

float Engine::getElapsedTime()
{
    return elapsedTime;
}

Engine::EngineTiming Engine::getEngineTiming()
{
    return last_engine_timing;
}

void Engine::shutdown()
{
    running = false;
}

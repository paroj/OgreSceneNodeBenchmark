/*
 * OgreApplicationContext.cpp
 *
 *  Created on: 18.05.2016
 *      Author: pavel
 */

#include "OgreApplicationContext.h"

#include "OgreRoot.h"
#include "OgreGpuProgramManager.h"
#include "OgreConfigFile.h"
#include "OgreRenderWindow.h"
#include "OgreViewport.h"
#include "OgreOverlaySystem.h"
#include "OgreDataStream.h"

#if OGRE_BITES_HAVE_SDL
#include <SDL_video.h>
#include <SDL_syswm.h>
#endif

#if OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
#include "OgreArchiveManager.h"
#include "Android/OgreAPKFileSystemArchive.h"
#include "Android/OgreAPKZipArchive.h"
#endif

namespace Bites {

static const char* SHADER_CACHE_FILENAME = "cache.bin";

ApplicationContext::ApplicationContext(const Ogre::String& appName, bool grabInput)
#if (OGRE_THREAD_PROVIDER == 3) && (OGRE_NO_TBB_SCHEDULER == 1)
    : mTaskScheduler(tbb::task_scheduler_init::deferred)
    #endif
{
    mAppName = appName;
    mGrabInput = grabInput;
    mFSLayer = new Ogre::FileSystemLayer(mAppName);
    mRoot = NULL;
    mWindow = NULL;
    mOverlaySystem = NULL;
    mSDLWindow = NULL;
    mFirstRun = true;

#if OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
    mAAssetMgr = NULL;
    mAConfig = NULL;
    mAWindow = NULL;
#endif

#ifdef OGRE_BUILD_COMPONENT_RTSHADERSYSTEM
    mMaterialMgrListener = NULL;
    mShaderGenerator = NULL;
#endif
}

ApplicationContext::~ApplicationContext()
{
    delete mFSLayer;
}

void ApplicationContext::initApp()
{
    createRoot();
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
    if (!oneTimeConfig()) return;

    if (!mFirstRun) mRoot->setRenderSystem(mRoot->getRenderSystemByName(mNextRenderer));

    setup();

    mRoot->saveConfig();

    Ogre::Root::getSingleton().getRenderSystem()->_initRenderTargets();

    // Clear event times
    Ogre::Root::getSingleton().clearEventTimes();
#else

#if OGRE_PLATFORM == OGRE_PLATFORM_NACL
    mNextRenderer = mRoot->getAvailableRenderers()[0]->getName();
#else
    if (!oneTimeConfig()) return;
#endif

#if OGRE_PLATFORM != OGRE_PLATFORM_ANDROID
    // if the context was reconfigured, set requested renderer
    if (!mFirstRun) mRoot->setRenderSystem(mRoot->getRenderSystemByName(mNextRenderer));
#endif

    setup();
#endif
}

void ApplicationContext::closeApp()
{
#if OGRE_PLATFORM != OGRE_PLATFORM_ANDROID
    mRoot->saveConfig();
#endif

    shutdown();
    if (mRoot)
    {
        OGRE_DELETE mRoot;
        mRoot = NULL;
    }

#ifdef OGRE_STATIC_LIB
    mStaticPluginLoader.unload();
#endif

#if (OGRE_THREAD_PROVIDER == 3) && (OGRE_NO_TBB_SCHEDULER == 1)
    if (mTaskScheduler.is_active())
        mTaskScheduler.terminate();
#endif
}

bool ApplicationContext::initialiseRTShaderSystem()
{
#ifdef OGRE_BUILD_COMPONENT_RTSHADERSYSTEM
    if (Ogre::RTShader::ShaderGenerator::initialize())
    {
        mShaderGenerator = Ogre::RTShader::ShaderGenerator::getSingletonPtr();

        // Create and register the material manager listener if it doesn't exist yet.
        if (!mMaterialMgrListener) {
            mMaterialMgrListener = new SGTechniqueResolverListener(mShaderGenerator);
            Ogre::MaterialManager::getSingleton().addListener(mMaterialMgrListener);
        }
    }

    return true;
#else
    return false;
#endif
}

void ApplicationContext::setRTSSWriteShadersToDisk(bool write)
{
#ifdef OGRE_BUILD_COMPONENT_RTSHADERSYSTEM
    if(!write) {
        mShaderGenerator->setShaderCachePath("");
        return;
    }

    // Set shader cache path.
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
    mShaderGenerator->setShaderCachePath(Ogre::macCachePath());
#elif OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    mShaderGenerator->setShaderCachePath(Ogre::macCachePath() + "/org.ogre3d.RTShaderCache");
#else
    mShaderGenerator->setShaderCachePath(mRTShaderLibPath+"/cache/");
#endif
#endif
}

void ApplicationContext::destroyRTShaderSystem()
{
#ifdef OGRE_BUILD_COMPONENT_RTSHADERSYSTEM
    //mShaderGenerator->removeAllShaderBasedTechniques();
    //mShaderGenerator->flushShaderCache();

    // Restore default scheme.
    Ogre::MaterialManager::getSingleton().setActiveScheme(Ogre::MaterialManager::DEFAULT_SCHEME_NAME);

    // Unregister the material manager listener.
    if (mMaterialMgrListener != NULL)
    {
        Ogre::MaterialManager::getSingleton().removeListener(mMaterialMgrListener);
        delete mMaterialMgrListener;
        mMaterialMgrListener = NULL;
    }

    // Destroy RTShader system.
    if (mShaderGenerator != NULL)
    {
        Ogre::RTShader::ShaderGenerator::destroy();
        mShaderGenerator = NULL;
    }
#endif
}

void ApplicationContext::setup()
{
    mWindow = createWindow();
    setupInput(mGrabInput);
    locateResources();
#ifdef OGRE_BUILD_COMPONENT_RTSHADERSYSTEM
    initialiseRTShaderSystem();
#endif
    loadResources();

    // adds context as listener to process context-level (above the sample level) events
    mRoot->addFrameListener(this);
#if OGRE_PLATFORM != OGRE_PLATFORM_ANDROID
    WindowEventUtilities::addWindowEventListener(mWindow, this);
#endif
}

void ApplicationContext::createRoot()
{
#if (OGRE_THREAD_PROVIDER == 3) && (OGRE_NO_TBB_SCHEDULER == 1)
    mTaskScheduler.initialize(OGRE_THREAD_HARDWARE_CONCURRENCY);
#endif

#if OGRE_PLATFORM == OGRE_PLATFORM_ANDROID || OGRE_PLATFORM == OGRE_PLATFORM_EMSCRIPTEN
    mRoot = OGRE_NEW Ogre::Root("");
#else
    Ogre::String pluginsPath = Ogre::BLANKSTRING;
#   ifndef OGRE_STATIC_LIB
    pluginsPath = mFSLayer->getConfigFilePath("plugins.cfg");
#   endif
    mRoot = OGRE_NEW Ogre::Root(pluginsPath, "ogre.cfg", "ogre.log");
#endif

#ifdef OGRE_STATIC_LIB
    mStaticPluginLoader.load();
#endif
    mOverlaySystem = OGRE_NEW Ogre::OverlaySystem();
}

bool ApplicationContext::oneTimeConfig()
{
    if (!mRoot->restoreConfig()) {
        mRoot->setRenderSystem(mRoot->getAvailableRenderers().at(0));
    }
    return true;
}

void ApplicationContext::enableShaderCache() const
{
    Ogre::GpuProgramManager::getSingleton().setSaveMicrocodesToCache(true);

    // Load for a package version of the shaders.
    Ogre::String path = mFSLayer->getWritablePath(SHADER_CACHE_FILENAME);
    std::fstream inFile(path.c_str(), std::ios::binary);
    if (inFile.is_open())
    {
        Ogre::LogManager::getSingleton().logMessage("Loading shader cache from "+path);
        Ogre::DataStreamPtr istream(new Ogre::FileStreamDataStream(path, &inFile, false));
        Ogre::GpuProgramManager::getSingleton().loadMicrocodeCache(istream);
    }
}

bool ApplicationContext::frameRenderingQueued(const Ogre::FrameEvent& evt)
{
    for(std::set<InputListener*>::iterator it = mInputListeners.begin();
            it != mInputListeners.end(); ++it) {
        (*it)->frameRendered(evt);
    }

    return true;
}

Ogre::RenderWindow *ApplicationContext::createWindow()
{
    mRoot->initialise(false, mAppName);
    Ogre::NameValuePairList miscParams;
#if OGRE_PLATFORM == OGRE_PLATFORM_NACL
    miscParams["pp::Instance"] = Ogre::StringConverter::toString((unsigned long)mNaClInstance);
    miscParams["SwapCallback"] = Ogre::StringConverter::toString((unsigned long)mNaClSwapCallback);
    // create 1x1 window - we will resize later
    return mRoot->createRenderWindow(mAppName, mInitWidth, mInitHeight, false, &miscParams);

#elif OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
    miscParams["externalWindowHandle"] = Ogre::StringConverter::toString(reinterpret_cast<size_t>(mAWindow));
    miscParams["androidConfig"] = Ogre::StringConverter::toString(reinterpret_cast<size_t>(mAConfig));
    miscParams["preserveContext"] = "true"; //Optionally preserve the gl context, prevents reloading all resources, this is false by default

    return Ogre::Root::getSingleton().createRenderWindow(mAppName, 0, 0, false, &miscParams);
#else
    Ogre::ConfigOptionMap ropts = mRoot->getRenderSystem()->getConfigOptions();

    Ogre::uint32 w = 640, h = 480;



    miscParams["FSAA"] = ropts["FSAA"].currentValue;
    miscParams["vsync"] = ropts["VSync"].currentValue;

#if OGRE_BITES_HAVE_SDL
    if(!SDL_WasInit(SDL_INIT_VIDEO)) {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
    }

    mSDLWindow = SDL_CreateWindow(mAppName.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_RESIZABLE);

#if OGRE_PLATFORM == OGRE_PLATFORM_EMSCRIPTEN
    SDL_GL_CreateContext(mSDLWindow);
    miscParams["currentGLContext"] = "true";
#else
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(mSDLWindow, &wmInfo);
#endif

#if OGRE_PLATFORM == OGRE_PLATFORM_LINUX
    miscParams["parentWindowHandle"] = Ogre::StringConverter::toString(size_t(wmInfo.info.x11.window));
#elif OGRE_PLATFORM == OGRE_PLATFORM_WIN32
    miscParams["externalWindowHandle"] = Ogre::StringConverter::toString(size_t(wmInfo.info.win.window));
#elif OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    assert(wmInfo.subsystem == SDL_SYSWM_COCOA);
    miscParams["externalWindowHandle"] = Ogre::StringConverter::toString(size_t(wmInfo.info.cocoa.window));
#endif
#endif
    return mRoot->createRenderWindow(mAppName, w, h, false, &miscParams);
#endif
}

#if OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
void ApplicationContext::initAppForAndroid(AAssetManager* assetMgr, ANativeWindow* window)
{
    mAConfig = AConfiguration_new();
    AConfiguration_fromAssetManager(mAConfig, assetMgr);
    mAAssetMgr = assetMgr;
    mAWindow = window;

    initApp();
}

Ogre::DataStreamPtr ApplicationContext::openAPKFile(const Ogre::String& fileName)
{
    Ogre::MemoryDataStreamPtr stream;
    AAsset* asset = AAssetManager_open(mAAssetMgr, fileName.c_str(), AASSET_MODE_BUFFER);
    if(asset)
    {
        off_t length = AAsset_getLength(asset);
        stream.reset(new Ogre::MemoryDataStream(length, true, true));
        memcpy(stream->getPtr(), AAsset_getBuffer(asset), length);
        AAsset_close(asset);
    }
    return stream;
}

void ApplicationContext::_fireInputEventAndroid(AInputEvent* event, int wheel) {
    Event evt = {0};

    static TouchFingerEvent lastTouch = {0};

    if(wheel) {
        evt.type = SDL_MOUSEWHEEL;
        evt.wheel.y = wheel;
        _fireInputEvent(evt);
        lastTouch.fingerId = -1; // prevent move-jump after pinch is over
        return;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMOTION_EVENT_ACTION_MASK & AMotionEvent_getAction(event);

        switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            evt.type = SDL_FINGERDOWN;
            break;
        case AMOTION_EVENT_ACTION_UP:
            evt.type = SDL_FINGERUP;
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            evt.type = SDL_FINGERMOTION;
            break;
        default:
            return;
        }

        evt.tfinger.fingerId = AMotionEvent_getPointerId(event, 0);
        evt.tfinger.x = AMotionEvent_getRawX(event, 0) / mWindow->getWidth();
        evt.tfinger.y = AMotionEvent_getRawY(event, 0) / mWindow->getHeight();

        if(evt.type == SDL_FINGERMOTION) {
            if(evt.tfinger.fingerId != lastTouch.fingerId)
                return; // wrong finger

            evt.tfinger.dx = evt.tfinger.x - lastTouch.x;
            evt.tfinger.dy = evt.tfinger.y - lastTouch.y;
        }

        lastTouch = evt.tfinger;
    } else {
        if(AKeyEvent_getKeyCode(event) != AKEYCODE_BACK)
            return;

        evt.type = AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN ? SDL_KEYDOWN : SDL_KEYUP;
        evt.key.keysym.sym = SDLK_ESCAPE;
    }

    _fireInputEvent(evt);
}
#endif

void ApplicationContext::_fireInputEvent(const Event& event) const
{
    for(std::set<InputListener*>::iterator it = mInputListeners.begin();
            it != mInputListeners.end(); ++it) {
        InputListener& l = **it;

        switch (event.type)
        {
        case SDL_KEYDOWN:
            // Ignore repeated signals from key being held down.
            if (event.key.repeat) break;
            l.keyPressed(event.key);
            break;
        case SDL_KEYUP:
            l.keyReleased(event.key);
            break;
        case SDL_MOUSEBUTTONDOWN:
            l.mousePressed(event.button);
            break;
        case SDL_MOUSEBUTTONUP:
            l.mouseReleased(event.button);
            break;
        case SDL_MOUSEWHEEL:
            l.mouseWheelRolled(event.wheel);
            break;
        case SDL_MOUSEMOTION:
            l.mouseMoved(event.motion);
            break;
        case SDL_FINGERDOWN:
            // for finger down we have to move the pointer first
            l.touchMoved(event.tfinger);
            l.touchPressed(event.tfinger);
            break;
        case SDL_FINGERUP:
            l.touchReleased(event.tfinger);
            break;
        case SDL_FINGERMOTION:
            l.touchMoved(event.tfinger);
            break;
        }
    }
}

void ApplicationContext::setupInput(bool _grab)
{
#if OGRE_BITES_HAVE_SDL
    if (!mSDLWindow)
    {
        OGRE_EXCEPT(Ogre::Exception::ERR_INVALID_STATE,
                    "you must create a SDL window first",
                    "SampleContext::setupInput");
    }

    SDL_ShowCursor(SDL_FALSE);

    SDL_bool grab = SDL_bool(_grab);

    SDL_SetWindowGrab(mSDLWindow, grab);
    SDL_SetRelativeMouseMode(grab);
#endif
}

void ApplicationContext::locateResources()
{
    // load resource paths from config file
    Ogre::ConfigFile cf;
    Ogre::LogManager::getSingleton().logMessage("Loading "+mFSLayer->getConfigFilePath("resources.cfg"));
    cf.load(mFSLayer->getConfigFilePath("resources.cfg"));

    Ogre::String sec, type, arch;
    // go through all specified resource groups
    Ogre::ConfigFile::SectionIterator seci = cf.getSectionIterator();
    while(seci.hasMoreElements()) {
        sec = seci.peekNextKey();
        const Ogre::ConfigFile::SettingsMultiMap& settings = *seci.getNext();
        Ogre::ConfigFile::SettingsMultiMap::const_iterator i;

        // go through all resource paths
        for (i = settings.begin(); i != settings.end(); i++)
        {
            type = i->first;
            arch = i->second;

            Ogre::ResourceGroupManager::getSingleton().addResourceLocation(arch, type, sec);
        }
    }
}

void ApplicationContext::loadResources()
{
    Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
}

void ApplicationContext::reconfigure(const Ogre::String &renderer, Ogre::NameValuePairList &options)
{
    mNextRenderer = renderer;
    Ogre::RenderSystem* rs = mRoot->getRenderSystemByName(renderer);

    // set all given render system options
    for (Ogre::NameValuePairList::iterator it = options.begin(); it != options.end(); it++)
    {
        rs->setConfigOption(it->first, it->second);

#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
        // Change the viewport orientation on the fly if requested
        if(it->first == "Orientation")
        {
            if (it->second == "Landscape Left")
                mWindow->getViewport(0)->setOrientationMode(Ogre::OR_LANDSCAPELEFT, true);
            else if (it->second == "Landscape Right")
                mWindow->getViewport(0)->setOrientationMode(Ogre::OR_LANDSCAPERIGHT, true);
            else if (it->second == "Portrait")
                mWindow->getViewport(0)->setOrientationMode(Ogre::OR_PORTRAIT, true);
        }
#endif
    }

#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
    // Need to save the config on iOS to make sure that changes are kept on disk
    mRoot->saveConfig();
#endif
    mRoot->queueEndRendering();   // break from render loop
}

void ApplicationContext::shutdown()
{
    if (Ogre::GpuProgramManager::getSingleton().isCacheDirty())
    {
        Ogre::String path = mFSLayer->getWritablePath(SHADER_CACHE_FILENAME);
        std::fstream outFile(path.c_str(), std::ios::out | std::ios::binary);

        if (outFile.is_open())
        {
            Ogre::LogManager::getSingleton().logMessage("Writing shader cache to "+path);
            Ogre::DataStreamPtr ostream(new Ogre::FileStreamDataStream(path, &outFile, false));
            Ogre::GpuProgramManager::getSingleton().saveMicrocodeCache(ostream);
        }
    }

#ifdef OGRE_BUILD_COMPONENT_RTSHADERSYSTEM
    // Destroy the RT Shader System.
    destroyRTShaderSystem();
#endif

    // remove window event listener before destroying it
    if(mWindow) {
        WindowEventUtilities::removeWindowEventListener(mWindow, this);
        mRoot->destroyRenderTarget(mWindow);
        mWindow = NULL;
    }

    if (mOverlaySystem)
    {
        OGRE_DELETE mOverlaySystem;
    }

#if OGRE_BITES_HAVE_SDL
    if(mSDLWindow) {
        SDL_DestroyWindow(mSDLWindow);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        mSDLWindow = NULL;
    }
#endif

#if OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
    AConfiguration_delete(mAConfig);
#endif
}

void ApplicationContext::pollEvents()
{
#if OGRE_BITES_HAVE_SDL
    if(!mSDLWindow)
    {
        // SDL events not initialized
        return;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            mRoot->queueEndRendering();
            break;
        case SDL_WINDOWEVENT:
            if(event.window.event == SDL_WINDOWEVENT_RESIZED) {
                mWindow->resize(event.window.data1, event.window.data2);
                windowResized(mWindow);
            }
            break;
        default:
            _fireInputEvent(event);
            break;
        }
    }
#endif
}

}

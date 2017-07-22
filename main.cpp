#include <Ogre.h>
#include "OgreApplicationContext.h"

#if OGRE_VERSION_MAJOR == 2
#include <OgreFrameStats.h>
#include <Compositor/OgreCompositorManager2.h>
#endif

class MyTestApp : public Bites::ApplicationContext, public Bites::InputListener
{
public:
    MyTestApp();
    void setup();
    bool keyPressed(const Bites::KeyboardEvent& evt);

    void setupInput(bool grab) {}

    bool frameRenderingQueued(const Ogre::FrameEvent& evt) {
        Bites::ApplicationContext::frameRenderingQueued(evt);

        if(!rotate_cubes)
            return true;

        for(auto& n : nodes) {
            n->roll(Ogre::Radian(0.08));
        }

        return true;
    }

    bool frameEnded(const Ogre::FrameEvent& evt) {
#if OGRE_VERSION_MAJOR == 2
        auto stats = Ogre::Root::getSingleton().getFrameStats();
        printf("frametime %f ms (mean %f ms)\t\t\r", 1000./stats->getFps(), 1000./stats->getAvgFps());
#else
        auto stats = getRenderWindow()->getStatistics();
        printf("frametime %f ms (mean %f ms)\t\t\r", 1000./stats.lastFPS, 1000./stats.avgFPS);
#endif
        return true;
    }

    bool rotate_cubes = false;

    std::vector<Ogre::SceneNode*> nodes;
    Ogre::SceneNode* camNode;
    int pos = 2;
    std::vector<Ogre::Vector3> campos = {Ogre::Vector3(0, 1, -1), Ogre::Vector3(0, 10, -10), Ogre::Vector3(0, 70, -70)};
};

//! [constructor]
MyTestApp::MyTestApp() : Bites::ApplicationContext("SceneNodeBenchmark")
{
    addInputListener(this);
}
//! [constructor]

//! [key_handler]
bool MyTestApp::keyPressed(const Bites::KeyboardEvent& evt)
{
    if (evt.keysym.sym == SDLK_ESCAPE)
    {
        printf("\n\n");
        exit(0);
        getRoot()->queueEndRendering();
    }

    if (evt.keysym.sym == 'c')
    {
        camNode->setPosition( campos[++pos % campos.size()] );
        camNode->lookAt( Ogre::Vector3(0,0,0) , Ogre::SceneNode::TS_PARENT);
    }

    return true;
}
//! [key_handler]

//! [setup]
void MyTestApp::setup(void)
{
    using namespace Ogre;

    // do not forget to call the base first
    Bites::ApplicationContext::setup();

    // get a pointer to the already created root
    Ogre::Root* root = getRoot();

#if OGRE_VERSION_MAJOR == 2
    size_t numThreads = std::max<size_t>( 1, PlatformInformation::getNumLogicalCores() );
    Ogre::SceneManager* scnMgr = root->createSceneManager(
            Ogre::ST_GENERIC, 1,
            INSTANCING_CULLING_SINGLETHREAD,
            "ExampleSMInstance");
#else
    Ogre::SceneManager* scnMgr = root->createSceneManager(Ogre::ST_GENERIC);
#endif

    // register our scene with the RTSS
    Ogre::RTShader::ShaderGenerator* shadergen = Ogre::RTShader::ShaderGenerator::getSingletonPtr();
    shadergen->addSceneManager(scnMgr);

    // without light we would just get a black screen
    scnMgr->setAmbientLight( ColourValue( 0.1f, 0.1f, 0.1f ) );

    Ogre::Light* light = scnMgr->createLight();

#if OGRE_VERSION_MAJOR == 2
    auto lightNode = scnMgr->getRootSceneNode()->createChildSceneNode();
    lightNode->attachObject(light);
#endif

    light->setType( Light::LT_DIRECTIONAL );
    light->setDirection( Vector3( 1.0f, -1.0f, -1.0f ).normalisedCopy() );
    light->setDiffuseColour( ColourValue::White );
    light->setSpecularColour( ColourValue::White );
    light->setVisibilityFlags( -1 );

    // also need to tell where we are
    camNode = scnMgr->getRootSceneNode()->createChildSceneNode();

    // create the camera
    Ogre::Camera* cam = scnMgr->createCamera("myCam");
    cam->setAutoAspectRatio(true);
    cam->setNearClipDistance( 0.1f );
    cam->setFarClipDistance( 300.0f );

#if OGRE_VERSION_MAJOR == 2
    scnMgr->getRootSceneNode()->detachObject(cam);
#endif
    camNode->attachObject(cam);
    camNode->setFixedYawAxis(true);
    camNode->setPosition( campos[pos] );
    camNode->lookAt( Vector3(0,0,0) , SceneNode::TS_PARENT);

    // and tell it to render into the main window
#if OGRE_VERSION_MAJOR == 2
    root->getCompositorManager2()->createBasicWorkspaceDef( "TestWorkspace", ColourValue::Black );
    root->getCompositorManager2()->addWorkspace(scnMgr, getRenderWindow(), cam, "TestWorkspace", true );
#else
    getRenderWindow()->addViewport(cam);
#endif

    // finally something to render
    const int numW = 140;
    const int numH = 140;

    nodes.reserve(numW*numH);

    //AnimationState *animState;
    for( int i=0; i<numH; ++i )
    {
        for( int j=0; j<numW; ++j )
        {
            SceneNode *sceneNode = scnMgr->getRootSceneNode()->createChildSceneNode();
#ifdef HW_BASIC
#if OGRE_VERSION_MAJOR == 2
            InstancedEntity *ent = instanceManager->createInstancedEntity(
                                                "Examples/Instancing/HWBasic/Cube",
                                                SCENE_TYPE_PARAM );
#else
            InstancedEntity *ent = instanceManager->createInstancedEntity(
                                                "Examples/Instancing/HWBasic/Cube" );
#endif
#else
            Entity *ent = scnMgr->createEntity( "Cube_d.mesh" );
#endif
            sceneNode->attachObject( ent );
            sceneNode->setPosition( Vector3( 0.5f * (i - numH/2), 0.0f, 0.5f * (j -numW/2)) );
            sceneNode->scale( 0.2f, 0.2f, 0.2f );
            nodes.push_back(sceneNode);
        }
    }
}
//! [setup]

//! [main]
int main(int argc, char *argv[])
{
    MyTestApp app;

    app.rotate_cubes = argc > 1 && atoi(argv[1]);

    app.initApp();
    app.getRoot()->startRendering();
    app.closeApp();
    return 0;
}
//! [main]

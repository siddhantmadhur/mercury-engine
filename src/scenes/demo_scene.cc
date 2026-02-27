#include <iostream>
#include <gameobjects/cube.hh> 
#include "demo_scene.hh"
#include "engine/scene.hh"
using namespace std;

#include <glm/gtc/matrix_transform.hpp>

#define ZOOM 50.0f

void DemoScene::OnStart() {

    { // configure 2d camera
        Camera playerCam = Camera(16.0/9.0);
        glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f);
        playerCam.setPosition(pos);
        
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-pos.x, pos.y, 0.0f));
        float zoom = ZOOM;
        view = glm::scale(view, glm::vec3(zoom, zoom, 1.0f));


        float width = 1280.0f;
        float height = 720.0f;
        glm::mat4 proj = glm::ortho<float>(-(width / 2.0f), width / 2.0f , -(height / 2.0f), (height / 2.0f), -1.0f, 1.0f);

        playerCam.setProj(proj);
        playerCam.setView(view);

        SetPlayerCamera(playerCam);
    }


    Shapes::Cube obj = Shapes::Cube();
    AddObject(obj);

    Shapes::Cube secondObj = Shapes::Cube();
    for (int i = 0; i < secondObj.vertices.size(); i++) {
        secondObj.vertices[i].x += 2; 
    }
    AddObject(secondObj);
    
    Shapes::Cube thirdObj = Shapes::Cube();
    for (int i = 0; i < thirdObj.vertices.size(); i++) {
        thirdObj.vertices[i].x -= 2; 
    }
    AddObject(thirdObj);
}

void DemoScene::OnUpdate() {
    Camera& cam = GetPlayerCamera();
    glm::vec3 pos = cam.getPosition();
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-pos.x, pos.y, 0.0f));
    float zoom = ZOOM;
    view = glm::scale(view, glm::vec3(zoom, zoom, 1.0f));
    cam.setView(view); 
}

void DemoScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        } else if (event->key_code == SAPP_KEYCODE_W) {
            Camera& cam = GetPlayerCamera();
            glm::vec3 pos = cam.getPosition();
            pos.y -= 5 * delta_t;
            cam.setPosition(pos);
        }
    } 
}


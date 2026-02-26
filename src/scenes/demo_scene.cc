#include <iostream>
#include <gameobjects/cube.hh> 
#include "demo_scene.hh"
using namespace std;

void DemoScene::OnStart() {

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
        
}



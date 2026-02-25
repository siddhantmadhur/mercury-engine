#include "demo_scene.hh"
#include "gameobjects/gameobject.hh"
#include <iostream>
using namespace std;

void DemoScene::OnStart() {
    cout << "DEMO SCENE START: " << endl;

    GameObject obj = GameObject();
    AddObject(obj);


    GameObject secondObj = GameObject();
    for (int i = 0; i < secondObj.vertices.size(); i++) {
        secondObj.vertices[i].x += 2; 
    }
    AddObject(secondObj);
    
    GameObject thirdObj = GameObject();
    for (int i = 0; i < thirdObj.vertices.size(); i++) {
        thirdObj.vertices[i].x -= 2; 
    }
    AddObject(thirdObj);
}

void DemoScene::OnUpdate() {
    
}

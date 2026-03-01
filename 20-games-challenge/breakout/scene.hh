#include <engine/scene.hh>
#include <gameobjects/quad.hh>

class BreakoutScene : public Scene {
public:
    void OnStart() override;
    void OnEnd() override;
    void OnUpdate() override;
    void OnTick() override;
    void OnEvent(const sapp_event* event) override;
private:
    Shapes::Quad* player = NULL;
    Shapes::Quad* ball = NULL;
    //std::vector<Shapes::Quad*> bricks;
    bool game_started = false;
    glm::vec2 ball_dx;
};

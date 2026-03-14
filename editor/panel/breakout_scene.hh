#include <engine/scene.hh>
#include <gameobjects/quad.hh>

class BreakoutScene : public Scene {
public:
    void OnStart() override;
    void OnEnd() override;
    void OnUpdate() override;
    void OnTick(float) override;
    void OnEvent(const sapp_event* event) override;

    void ChangeDirection(float x, float y);
private:
    entity_id player;
    entity_id ball;
    std::vector<entity_id> bricks;

    bool game_started = false;
    glm::vec2 ball_dx;
    float ball_speed = 10.0f;
    float player_speed = 48.0f;
};

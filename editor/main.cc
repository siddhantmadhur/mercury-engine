

#include "application/application.h"
#include "application/d3d11_application.h"

using namespace Pequod;

int main(void)
{
    D3D11Application editor{"Pequod"};

    return editor.Run();
}

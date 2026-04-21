# Pequod

Pequod is a game-engine built with C++ for performant games

## Table of Contents

- [Goal](#goal)
- [Support](#support)
- [Contributing](#contributing)
- [Design](#design)
- [LICENSE](#license)

## Goal

A completely open-source program that is designed to be develop performant video-games. The primary goal
is to develop simulator video-games like Railroad Tycoon, SimCity, Cities Skylines etc. This means that it
is developed specifically for top-down games with lots of systems being simulated, and features like ray-tracing,
or advanced graphics might never be supported.

## Support

A goal of this project was to be cross-platform. Since everything is being implemented
myself it is difficult to and thus I'm only aiming for desktop platforms with a limited number
of graphics backends.

The order of the platform is the order of importance for compatibility.
For example, Windows has the most support and hence is the first on the list.
If a platform is not on the list then that means supporting is not a goal of this project.

| Platform      | Renderer | Support |
|---------------|----------|---------|
| Windows 10/11 | D3D11    | Yes     |
| SteamOS       | Vulkan   | None    |
| MacOS         | Metal    | None    |
| Linux         | Vulkan   | None    |

## Contributing

Pequod is open-source but not open-contribution. Features part of this engine
are specifically for games **we** want to make and not to be generalized.
In fact there are much better engines already out there that do most things
better so I'd recommend using that if you cannot find a feature here.

That being said, there are many instances where you might want a change (bugs, vulnerabilities)
in which case please email [report@pequodengine.com](mailto:report@pequodengine.com) with
more information or
a [git patch](https://git-scm.com/book/en/v2/Distributed-Git-Contributing-to-a-Project#_project_over_email).

Official repositories for the code:

- https://oss.reocelot.com/pequod-engine
- https://github.com/siddhantmadhur/pequod-engine [mirror]

## Design

Pequod is designed as C++ library that can be used to produce games. This involves
tasks such as windowing, shaders, input, memory and more. There are two parts of
its design. First is its implementation which is how you can get started with adding
Pequod to your project, or building using it. And the other is it's internal design
which is how I've decided to go about designing the program which you hopefully never
see.

Some important design features to better understand the engine:

### Scenes

Pequod::GameScene are a state of the game that should be extended to create the world.
They only implement higher level functions like managing object properties, or the physics system
and don't implement platform specific features like drawing or windowing.

### Primitives

Primitive is a basic struct that is the minimum requirement to render an object to the screen.
Scenes can modify or create primitives and implement them however they want. For example if they
want a single texture atlas and contain a bunch of objects in their vertices so less draw calls
are called, that's upto the scene to implement.

When the Application finally receives the Primitives, it creates buffers out of it and draws
them to the screen.

### Application

Pequod::Application is responsible for handling cross-platform application level features such as
creating a window, taking in user-input like mouse, keyboard or controller.
It also implements how

## License

[GNU GPLv3](/LICENSE)

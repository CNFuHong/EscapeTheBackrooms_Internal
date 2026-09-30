#pragma once

namespace etb::render
{
class Renderer;
}

namespace etb::ui
{
bool DrawMenu(const render::Renderer& renderer);
void DrawSpawnerWindow();
void DrawModelBrowserWindow();
}

#pragma once

// Call once after lvglPortBegin succeeds. Both pages and their timers persist and are not recreated.
void lvglDemoBuild();

// Called from loop after creation; shares the LVGL handler's task. Switching pages only hides/shows containers.
void lvglDemoNextPage();

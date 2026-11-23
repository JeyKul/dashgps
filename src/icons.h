// Simple vector icons painted in the current text colour, so they work in
// light and dark mode and look the same on every platform.
#pragma once

#include <QColor>
#include <QIcon>

enum class Icon {
    Play, Pause, Prev, Next, Back, Forward, Volume, Muted, Folder, File, Export, Follow, Fit, Settings, Speed
};

QIcon makeIcon(Icon which, const QColor &color);

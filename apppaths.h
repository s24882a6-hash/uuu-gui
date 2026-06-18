#pragma once
#include <QString>

namespace AppPaths {

// Absolute path to the bundled `uuu-helper` executable, located next to the
// application binary (on macOS this is inside uuuapp.app/Contents/MacOS).
QString helper();

}

#pragma once

#include <iosfwd>

class World;

bool WriteDebugStateReport(std::ostream& output, const World& world);

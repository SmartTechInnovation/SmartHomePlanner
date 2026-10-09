#ifndef ITEMTYPES_H
#define ITEMTYPES_H

#include <QGraphicsItem>

namespace ItemType {
enum : int {
    Background = QGraphicsItem::UserType + 1,
    Wall,
    Room,
    Device,
    Strip,
    Effect,
    Note,
    Handle,
    Night,
    Group,          // curved dashed line linking elements (circuit / group)
    Rail,           // DIN rail (electrical panel)
    Equipment,      // DIN rail module with terminals (electrical panel)
    Wire,           // connection between two terminals (electrical panel)
    WireHandle      // grip on a wire corner / segment
};
}

// Stacking order (z values)
namespace ZOrder {
constexpr double Background = 0;
constexpr double Room       = 2;
constexpr double Night      = 4;
constexpr double Effect     = 5;
constexpr double Wall       = 10;
constexpr double Rail       = 3;
constexpr double Strip      = 15;
constexpr double Group      = 17;
constexpr double Device     = 20;
constexpr double Equipment  = 20;
constexpr double Wire       = 25;
constexpr double Preview    = 50;
}

// Text heights on the plan, in cm (scale with the drawing, so the PDF looks like the screen)
namespace TextSize {
constexpr int Room = 26;
constexpr int Note = 14;
constexpr int Id   = 11;
constexpr int Dim  = 12;
}

#endif // ITEMTYPES_H

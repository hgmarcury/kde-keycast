#pragma once

// UI extraction point.
//
// The existing KeyCast QWidget remains in main.cpp for now.
// This header intentionally contains no duplicate implementation.
//
// Once the input layer is integrated and verified, the existing
// QWidget implementation can be moved here without changing
// the input backends.

namespace KDEKeyCast {
class KeyCastWidget;
}

#include <brtoy/platform.h>

namespace brtoy {

Handle Handle::fromIndex(size_t index) { return {.m_value = index + 1}; }

bool Handle::valid() const { return m_value != 0; }

Handle::operator bool() const { return valid(); }

size_t Handle::index() const { return m_value - 1; }

} // namespace brtoy

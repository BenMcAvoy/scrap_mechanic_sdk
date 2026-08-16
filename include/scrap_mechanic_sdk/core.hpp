#pragma once

#include "scrap_mechanic_sdk/interceptors.hpp"

namespace scrap::sdk::core {
template <typename Event> using Interceptor = ::scrap::sdk::Interceptor<Event>;
}

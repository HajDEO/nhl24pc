// SPDX-FileCopyrightText: Copyright 2026 nhlport contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::Json {

// HLE of libSceJson2 (sce::Json): Initializer, MemAllocator, Parser, Value and String, limited to
// the methods NHL 24 imports. Backed by nlohmann::json.
void RegisterLib(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries::Json

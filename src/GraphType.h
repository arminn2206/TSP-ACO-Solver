//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include <cstdint>
// Kept deliberately: downstream headers pick up cnt/* declarations through this
// include chain.
#include <cnt/PushBackVector.h>

// 1-based town IDs, used project-wide.
using GraphType = std::uint32_t;
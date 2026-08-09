/*
 * Copyright 2025 ClapDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "dense_map.hpp"

namespace stdb::container {

// dense_set is just dense_map with T=void
template <typename Key,
          typename Hash = dense_hash<Key>,
          typename KeyEqual = std::equal_to<>,
          typename Allocator = std::allocator<Key>,
          typename MemoryPolicy = default_memory_policy<Key, void>>
using dense_set = dense_map<Key, void, Hash, KeyEqual, Allocator, MemoryPolicy>;

}  // namespace stdb::container

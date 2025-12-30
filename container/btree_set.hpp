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

// btree_set is now implemented as a template alias of btree_map in "set mode".
// This header is kept for backward compatibility.
//
// btree_set<Key, Compare, Allocator, TargetNodeSize> is equivalent to:
//   btree_map<Key, btree_set_empty_value, Compare, Allocator, TargetNodeSize>
//
// When btree_map's Value type is btree_set_empty_value, it operates in "set mode":
//   - value_type is Key (not pair<const Key, Value>)
//   - Iterators dereference to const Key&
//   - insert() takes just a key, not a key-value pair
//   - operator[] and at() are disabled
//   - Memory is optimized (empty value uses [[no_unique_address]])

#include "btree_map.hpp"

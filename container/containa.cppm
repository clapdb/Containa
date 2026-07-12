module;
#include "boolean_vector.hpp"
#include "btree_map.hpp"
#include "btree_set.hpp"
#include "container_base.hpp"
#include "dense_map.hpp"
#include "dense_set.hpp"
#include "bitmap.hpp"
#include "concurrent_skiplist.hpp"
#include "devectra.hpp"
#include "flat_map.hpp"
#include "immutable_ordered_multimap.hpp"
#include "ring_buffer.hpp"
#include "skiplist_map.hpp"
#include "skiplist_set.hpp"
#include "small_vectra.hpp"
#include "static_vectra.hpp"

export module containa;

export namespace stdb::container {
using ::stdb::container::Safety;
using ::stdb::container::IsRelocatable;
using ::stdb::container::NeedsCleanUp;
using ::stdb::container::kFastVectorMaxSize;
using ::stdb::container::construct_range;
using ::stdb::container::construct_range_with_cref;
using ::stdb::container::copy_from_iterator;
using ::stdb::container::destroy_range;
// The other half of container_base.hpp's namespace-scope helpers. They sit in `stdb::container`, not in
// a `detail` namespace, so a textual includer can call them; four of the eight were exported and these
// six were not, for no reason other than that nobody enumerated the free functions.
using ::stdb::container::copy_cref;
using ::stdb::container::copy_range;
using ::stdb::container::copy_value;
using ::stdb::container::destroy_ptr;
using ::stdb::container::move_range_forward;
using ::stdb::container::move_range_without_overlap;

// The C++20 `erase_if` free functions. One using-declaration names the whole overload set, so this
// single line covers btree_map.hpp, skiplist_map.hpp and skiplist_set.hpp -- every overload is declared
// in the global module fragment above, ahead of this point.
using ::stdb::container::erase_if;

using ::stdb::container::boolean_vector;

// The rest of the public containers. `import containa;` must not expose a smaller API than a textual
// includer does, or the module is not a replacement for the headers.
using ::stdb::container::bitmap;
using ::stdb::container::static_bitmap;
using ::stdb::container::concurrent_skiplist;
using ::stdb::container::concurrent_skiplist_map;
using ::stdb::container::devectra;
using ::stdb::container::flat_hash;
using ::stdb::container::flat_map;
using ::stdb::container::immutable_ordered_multimap;
using ::stdb::container::ring_buffer;
using ::stdb::container::skiplist_map;
using ::stdb::container::skiplist_map_default;
using ::stdb::container::skiplist_set;
using ::stdb::container::skiplist_set_default;
using ::stdb::container::skiplist_set_empty_value;

using ::stdb::container::btree_map;
using ::stdb::container::btree_map_auto;
using ::stdb::container::btree_map_compact;
using ::stdb::container::btree_multimap;
using ::stdb::container::btree_multimap_compact;
using ::stdb::container::btree_multiset;
using ::stdb::container::btree_multiset_compact;
using ::stdb::container::btree_set;
using ::stdb::container::btree_set_auto;
using ::stdb::container::btree_set_compact;

// btree_map.hpp has no `detail` namespace: these spellings sit directly in `stdb::container` and a
// textual includer can name them. Several are unavoidable in user code -- `btree_set_empty_value` is
// the mapped type of every `btree_set`, and the policy tags appear in `btree_map`'s own signature.
using ::stdb::container::btree_multi_policy;
using ::stdb::container::btree_set_empty_value;
using ::stdb::container::btree_unique_policy;
using ::stdb::container::compressed_pair;
using ::stdb::container::is_transparent_comparator;
using ::stdb::container::is_transparent_comparator_v;
using ::stdb::container::optimal_node_size;
using ::stdb::container::string_like;

using ::stdb::container::dense_hash;
using ::stdb::container::dense_map;
using ::stdb::container::dense_set;
using ::stdb::container::fast_map;
// `default_memory_policy` is the default template argument of dense_map/dense_set, so anyone who
// spells those templates' later parameters explicitly needs it; `flat_storage_tag` completes the
// tag trio whose other two members were already exported.
using ::stdb::container::default_memory_policy;
using ::stdb::container::flat_storage_policy;
using ::stdb::container::flat_storage_tag;
using ::stdb::container::force_flat_policy;
using ::stdb::container::force_indirect_policy;
using ::stdb::container::force_inline_policy;
using ::stdb::container::indirect_storage_tag;
using ::stdb::container::inline_storage_policy;
using ::stdb::container::inline_storage_tag;
using ::stdb::container::robin_hood_policy;
using ::stdb::container::string_hash;
using ::stdb::container::swiss_table_policy;

using ::stdb::container::small_vectra;
using ::stdb::container::static_vectra;

using ::stdb::container::operator==;
using ::stdb::container::operator!=;
using ::stdb::container::operator<=>;
using ::stdb::container::swap;
}  // namespace stdb::container

export namespace stdb::container::detail {
using ::stdb::container::detail::wyhash;
}

// container_base.hpp puts these in `stdb::container::simd`, not in a `detail` namespace, so a textual
// includer can call them and an importer must be able to as well.
export namespace stdb::container::simd {
using ::stdb::container::simd::is_simd_comparable_v;
using ::stdb::container::simd::simd_find;
}  // namespace stdb::container::simd

export namespace stdb::pmr {
using ::stdb::pmr::btree_map;
using ::stdb::pmr::btree_map_compact;
using ::stdb::pmr::btree_multimap;
using ::stdb::pmr::btree_multimap_compact;
using ::stdb::pmr::btree_multiset;
using ::stdb::pmr::btree_multiset_compact;
using ::stdb::pmr::btree_set;
using ::stdb::pmr::btree_set_compact;
using ::stdb::pmr::small_vectra;

// The PMR aliases the newly-included headers define. Declarations in the global module fragment are
// not visible to importers unless exported, so leaving these out kept `import containa;` narrower than
// a textual include for exactly these containers.
using ::stdb::pmr::concurrent_skiplist;
using ::stdb::pmr::concurrent_skiplist_map;
using ::stdb::pmr::devectra;
using ::stdb::pmr::skiplist_map;
using ::stdb::pmr::skiplist_set;
}  // namespace stdb::pmr

export namespace stdb::container::pmr {
using ::stdb::container::pmr::dense_map;
using ::stdb::container::pmr::fast_map;
}  // namespace stdb::container::pmr

// devectra.hpp, small_vectra.hpp, static_vectra.hpp and ring_buffer.hpp each open `namespace std` and
// add std::erase / std::erase_if / std::swap overloads for their container. Those are public API -- a
// textual includer writes `std::erase(dv, value)` today -- but they sit *outside* stdb, so none of the
// export blocks above reach them, and the global module fragment they were declared in is not visible
// through an import.
//
// The two failure modes differ, and the quiet one is the worse of the two. `std::erase` and
// `std::erase_if` have no generic fallback, so an importer gets a hard error ("no matching function for
// call to 'erase'"). `std::swap` does have one: the importer silently binds to the generic
// std::swap(T&, T&) from <utility> -- three moves -- instead of the overload that forwards to the
// container's own swap(). It compiles, it is correct, and it is slower, which is exactly the kind of
// difference an import is not allowed to introduce.
//
// A using-declaration names the whole overload set, so these re-export the containers' overloads along
// with the std ones already visible here. They are the same global-module entities the consumer's own
// <utility> declares, so nothing is duplicated or shadowed.
export namespace std {
using ::std::erase;
using ::std::erase_if;
using ::std::swap;
}  // namespace std

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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "small_vectra.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define ART_HAS_SSE2 1
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ART_PREFETCH(p) __builtin_prefetch((p))
#else
#define ART_PREFETCH(p) ((void)0)
#endif

namespace stdb::container {

// ============================================================================
// Key encoding: produce an order-preserving byte string so that lexicographic
// comparison of the encoded bytes matches the desired ordering of keys.
//   - unsigned integral : big-endian
//   - signed integral   : flip sign bit, big-endian
//   - floating point    : IEEE total order, big-endian
//   - string-like       : identity (already byte-ordered)
// Users may specialize art_key_encoder<Key> for custom key types.
// ============================================================================

struct art_key_view
{
    const uint8_t* ptr;
    uint32_t len;
};

// Maximum scratch buffer (bytes) required to encode a fixed-length key.
inline constexpr std::size_t kArtKeyScratch = 32;

template <typename Key, typename Enable = void>
struct art_key_encoder;  // primary template intentionally undefined

// --- unsigned integral -------------------------------------------------------
template <typename Key>
struct art_key_encoder<Key, std::enable_if_t<std::is_integral_v<Key> && std::is_unsigned_v<Key>>>
{
    static constexpr bool fixed = true;
    static constexpr std::size_t bound = sizeof(Key);
    static art_key_view encode(const Key& k, uint8_t* scratch) {
        for (std::size_t i = 0; i < sizeof(Key); ++i) {
            scratch[i] = static_cast<uint8_t>(k >> (8 * (sizeof(Key) - 1 - i)));
        }
        return {scratch, static_cast<uint32_t>(sizeof(Key))};
    }
};

// --- signed integral (incl. char/bool handled by unsigned path when unsigned) -
template <typename Key>
struct art_key_encoder<Key, std::enable_if_t<std::is_integral_v<Key> && std::is_signed_v<Key>>>
{
    using U = std::make_unsigned_t<Key>;
    static constexpr bool fixed = true;
    static constexpr std::size_t bound = sizeof(Key);
    static art_key_view encode(const Key& k, uint8_t* scratch) {
        U u = static_cast<U>(k) ^ (U(1) << (sizeof(Key) * 8 - 1));
        for (std::size_t i = 0; i < sizeof(Key); ++i) {
            scratch[i] = static_cast<uint8_t>(u >> (8 * (sizeof(Key) - 1 - i)));
        }
        return {scratch, static_cast<uint32_t>(sizeof(Key))};
    }
};

// --- floating point (storage-sized: float/double; excludes 80-bit long double) -
template <typename Key>
struct art_key_encoder<Key, std::enable_if_t<std::is_floating_point_v<Key> && sizeof(Key) <= 8>>
{
    using U = std::conditional_t<sizeof(Key) == 4, uint32_t, uint64_t>;
    static constexpr bool fixed = true;
    static constexpr std::size_t bound = sizeof(Key);
    static art_key_view encode(const Key& k, uint8_t* scratch) {
        U bits;
        std::memcpy(&bits, &k, sizeof(Key));
        // IEEE -0.0 and +0.0 compare equal but have distinct bit patterns; collapse -0.0
        // to the +0.0 representation so both encode to the same key and the map treats
        // them as one entry (otherwise insert({-0.0, ...}) and insert({0.0, ...}) coexist).
        if (k == Key(0)) {
            bits = 0;
        }
        U mask = (bits >> (sizeof(Key) * 8 - 1)) ? ~U(0) : (U(1) << (sizeof(Key) * 8 - 1));
        bits ^= mask;
        for (std::size_t i = 0; i < sizeof(Key); ++i) {
            scratch[i] = static_cast<uint8_t>(bits >> (8 * (sizeof(Key) - 1 - i)));
        }
        return {scratch, static_cast<uint32_t>(sizeof(Key))};
    }
};

// --- string-like -------------------------------------------------------------
template <>
struct art_key_encoder<std::string>
{
    static constexpr bool fixed = false;
    static constexpr std::size_t bound = 0;
    static art_key_view encode(const std::string& k, uint8_t*) {
        return {reinterpret_cast<const uint8_t*>(k.data()), static_cast<uint32_t>(k.size())};
    }
};

template <>
struct art_key_encoder<std::string_view>
{
    static constexpr bool fixed = false;
    static constexpr std::size_t bound = 0;
    static art_key_view encode(std::string_view k, uint8_t*) {
        return {reinterpret_cast<const uint8_t*>(k.data()), static_cast<uint32_t>(k.size())};
    }
};

// ============================================================================
// art_map
// ============================================================================

template <typename Key, typename T, typename Allocator = std::allocator<std::pair<const Key, T>>>
class art_map
{
  public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using allocator_type = Allocator;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

  private:
    using encoder = art_key_encoder<Key>;
    static_assert(encoder::bound <= kArtKeyScratch, "fixed key too large for scratch buffer");

    static constexpr uint8_t kCap = 8;  // bounded path-compression prefix

    enum class nkind : uint8_t { leaf = 0, n4, n16, n48, n256 };

    struct node
    {
        nkind kind;
    };

    struct inner : node
    {
        uint16_t nchild;  // up to 256 (node256 holds all byte values) — must not wrap
        uint8_t plen;
        uint8_t prefix[kCap];
        node* end_leaf;  // leaf whose key ends exactly at this node (prefix-of case)
    };

    struct node4 : inner
    {
        uint8_t keys[4];
        node* child[4];
    };
    struct node16 : inner
    {
        uint8_t keys[16];
        node* child[16];
    };
    struct node48 : inner
    {
        uint8_t cindex[256];  // 0 = empty, else slot+1
        node* child[48];
    };
    struct node256 : inner
    {
        node* child[256];
    };

    struct leaf_node : node
    {
        value_type kv;
        // Build kv with uses-allocator construction so an allocator-aware Key/T (e.g. a PMR
        // mapped_type) receives the map allocator. Constructing kv inside the leaf_node
        // constructor starts the enclosing object's lifetime properly; guaranteed copy
        // elision builds kv in place (no extra move).
        template <typename Alloc, typename... Args>
        leaf_node(std::allocator_arg_t, const Alloc& a, Args&&... args)
            : kv(std::make_obj_using_allocator<value_type>(a, std::forward<Args>(args)...)) {}
    };

    template <typename U>
    using rebind_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;

    // --- node pool: slab allocation + per-kind free list ---------------------
    // Cuts the malloc count dramatically (one chunk per kPer nodes) and recycles
    // freed shells; this is the main lever for ART insert throughput.
    template <typename NT>
    struct node_pool
    {
        union slot
        {
            slot* next;
            alignas(NT) unsigned char buf[sizeof(NT)];
        };
        static constexpr std::size_t kPer = 512;
        slot* freelist = nullptr;
        slot* cur = nullptr;
        std::size_t left = 0;
        small_vectra<slot*, 8> chunks;

        node_pool() = default;
        // Moving must leave the source fully reset: otherwise its freelist/cur
        // still point into slabs now owned by the destination (use-after-free if
        // the moved-from map is reused).
        node_pool(node_pool&& o) noexcept
            : freelist(o.freelist), cur(o.cur), left(o.left), chunks(std::move(o.chunks)) {
            o.freelist = nullptr;
            o.cur = nullptr;
            o.left = 0;
        }
        node_pool& operator=(node_pool&& o) noexcept {
            freelist = o.freelist;
            cur = o.cur;
            left = o.left;
            chunks = std::move(o.chunks);
            o.freelist = nullptr;
            o.cur = nullptr;
            o.left = 0;
            return *this;
        }

        template <typename A>
        NT* allocate(A& a) {
            if (freelist) {
                slot* s = freelist;
                freelist = s->next;
                return reinterpret_cast<NT*>(s);
            }
            if (left == 0) {
                slot* c = a.allocate(kPer);
                // Record the slab before using it: if push_back grows `chunks` and throws,
                // `c` is untracked and release_all() would never free it. Hand it back.
                try {
                    chunks.push_back(c);
                } catch (...) {
                    a.deallocate(c, kPer);
                    throw;
                }
                cur = c;
                left = kPer;
            }
            slot* s = cur++;
            --left;
            return reinterpret_cast<NT*>(s);
        }
        void deallocate(NT* p) {
            slot* s = reinterpret_cast<slot*>(p);
            s->next = freelist;
            freelist = s;
        }
        template <typename A>
        void release_all(A& a) {
            for (std::size_t i = 0; i < chunks.size(); ++i) a.deallocate(chunks[i], kPer);
            chunks.clear();
            freelist = nullptr;
            cur = nullptr;
            left = 0;
        }
    };

    template <typename NT>
    NT* pool_new(node_pool<NT>& pool) {
        rebind_alloc<typename node_pool<NT>::slot> a(_alloc);
        return pool.allocate(a);
    }
    template <typename NT>
    void pool_release(node_pool<NT>& pool) {
        rebind_alloc<typename node_pool<NT>::slot> a(_alloc);
        pool.release_all(a);
    }
    void release_pools() {
        pool_release(_pool4);
        pool_release(_pool16);
        pool_release(_pool48);
        pool_release(_pool256);
        pool_release(_poolL);
    }

    node4* make4() {
        auto* n = pool_new(_pool4);
        n->kind = nkind::n4;
        n->nchild = 0;
        n->plen = 0;
        n->end_leaf = nullptr;
        return n;
    }
    node16* make16() {
        auto* n = pool_new(_pool16);
        n->kind = nkind::n16;
        n->nchild = 0;
        n->plen = 0;
        n->end_leaf = nullptr;
        return n;
    }
    node48* make48() {
        auto* n = pool_new(_pool48);
        n->kind = nkind::n48;
        n->nchild = 0;
        n->plen = 0;
        n->end_leaf = nullptr;
        std::memset(n->cindex, 0, sizeof(n->cindex));
        return n;
    }
    node256* make256() {
        auto* n = pool_new(_pool256);
        n->kind = nkind::n256;
        n->nchild = 0;
        n->plen = 0;
        n->end_leaf = nullptr;
        std::memset(n->child, 0, sizeof(n->child));
        return n;
    }
    template <typename... Args>
    leaf_node* make_leaf(Args&&... args) {
        auto* p = pool_new(_poolL);
        // Start the leaf_node lifetime via its constructor (it builds kv through the map
        // allocator, so uses-allocator construction reaches an allocator-aware Key/T — e.g.
        // a PMR mapped_type gets the map's resource rather than the default one). On a
        // throwing key/value constructor, return the raw slot to the free list (nothing was
        // constructed) so repeated failed inserts don't keep growing the slab list.
        try {
            ::new (static_cast<void*>(p)) leaf_node(std::allocator_arg, _alloc, std::forward<Args>(args)...);
        } catch (...) {
            _poolL.deallocate(p);
            throw;
        }
        p->kind = nkind::leaf;
        return p;
    }
    void free_leaf(leaf_node* l) {
        l->~leaf_node();
        _poolL.deallocate(l);
    }
    void free_shell(node4* p) { _pool4.deallocate(p); }
    void free_shell(node16* p) { _pool16.deallocate(p); }
    void free_shell(node48* p) { _pool48.deallocate(p); }
    void free_shell(node256* p) { _pool256.deallocate(p); }

    static bool is_leaf(const node* n) { return n->kind == nkind::leaf; }
    static leaf_node* as_leaf(node* n) { return static_cast<leaf_node*>(n); }
    static inner* as_inner(node* n) { return static_cast<inner*>(n); }

    // --- encoded-key helpers -------------------------------------------------
    struct ekey
    {
        uint8_t scratch[kArtKeyScratch];
        art_key_view view;
        explicit ekey(const Key& k) { view = encoder::encode(k, scratch); }
    };
    static art_key_view leaf_key(leaf_node* l, uint8_t* scratch) {
        return encoder::encode(l->kv.first, scratch);
    }
    static bool view_eq(art_key_view a, art_key_view b) {
        return a.len == b.len && (a.len == 0 || std::memcmp(a.ptr, b.ptr, a.len) == 0);
    }

    // --- child lookup --------------------------------------------------------
    static node** find_child_ref(inner* n, uint8_t b) {
        switch (n->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(n);
                for (uint8_t i = 0; i < p->nchild; ++i)
                    if (p->keys[i] == b) return &p->child[i];
                return nullptr;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(n);
#ifdef ART_HAS_SSE2
                __m128i target = _mm_set1_epi8(static_cast<char>(b));
                __m128i keys = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p->keys));
                unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(target, keys)));
                mask &= (p->nchild >= 16) ? 0xFFFFu : ((1u << p->nchild) - 1u);
                return mask ? &p->child[__builtin_ctz(mask)] : nullptr;
#else
                for (uint8_t i = 0; i < p->nchild; ++i)
                    if (p->keys[i] == b) return &p->child[i];
                return nullptr;
#endif
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(n);
                uint8_t idx = p->cindex[b];
                return idx ? &p->child[idx - 1] : nullptr;
            }
            case nkind::n256: {
                auto* p = static_cast<node256*>(n);
                return p->child[b] ? &p->child[b] : nullptr;
            }
            default:
                return nullptr;
        }
    }
    static node* find_child(inner* n, uint8_t b) {
        node** r = find_child_ref(n, b);
        return r ? *r : nullptr;
    }
    static bool is_full(inner* n) {
        switch (n->kind) {
            case nkind::n4:
                return n->nchild == 4;
            case nkind::n16:
                return n->nchild == 16;
            case nkind::n48:
                return n->nchild == 48;
            default:
                return false;  // n256 never full
        }
    }

    // --- add child (assumes space available) ---------------------------------
    static void add_child(inner* n, uint8_t b, node* c) {
        switch (n->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(n);
                uint8_t pos = 0;
                while (pos < p->nchild && p->keys[pos] < b) ++pos;
                for (uint8_t i = static_cast<uint8_t>(p->nchild); i > pos; --i) {
                    p->keys[i] = p->keys[i - 1];
                    p->child[i] = p->child[i - 1];
                }
                p->keys[pos] = b;
                p->child[pos] = c;
                ++p->nchild;
                return;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(n);
                uint8_t pos = 0;
                while (pos < p->nchild && p->keys[pos] < b) ++pos;
                for (uint8_t i = static_cast<uint8_t>(p->nchild); i > pos; --i) {
                    p->keys[i] = p->keys[i - 1];
                    p->child[i] = p->child[i - 1];
                }
                p->keys[pos] = b;
                p->child[pos] = c;
                ++p->nchild;
                return;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(n);
                uint8_t slot = static_cast<uint8_t>(p->nchild);
                p->child[slot] = c;
                p->cindex[b] = static_cast<uint8_t>(slot + 1);
                ++p->nchild;
                return;
            }
            case nkind::n256: {
                auto* p = static_cast<node256*>(n);
                p->child[b] = c;
                ++p->nchild;
                return;
            }
            default:
                return;
        }
    }

    // Add a child to a node4 and return a stable pointer to its child slot.
    static node** add_child4_slot(node4* p, uint8_t b) {
        uint8_t pos = 0;
        while (pos < p->nchild && p->keys[pos] < b) ++pos;
        for (uint8_t i = static_cast<uint8_t>(p->nchild); i > pos; --i) {
            p->keys[i] = p->keys[i - 1];
            p->child[i] = p->child[i - 1];
        }
        p->keys[pos] = b;
        p->child[pos] = nullptr;
        ++p->nchild;
        return &p->child[pos];
    }

    // --- grow node to next larger kind; replaces *ref, frees old shell -------
    inner* grow(node** ref, inner* n) {
        switch (n->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(n);
                node16* g = make16();
                copy_header(g, p);
                for (uint8_t i = 0; i < p->nchild; ++i) {
                    g->keys[i] = p->keys[i];
                    g->child[i] = p->child[i];
                }
                g->nchild = p->nchild;
                *ref = g;
                free_shell(p);
                return g;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(n);
                node48* g = make48();
                copy_header(g, p);
                for (uint8_t i = 0; i < p->nchild; ++i) {
                    g->child[i] = p->child[i];
                    g->cindex[p->keys[i]] = static_cast<uint8_t>(i + 1);
                }
                g->nchild = p->nchild;
                *ref = g;
                free_shell(p);
                return g;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(n);
                node256* g = make256();
                copy_header(g, p);
                for (int b = 0; b < 256; ++b) {
                    uint8_t idx = p->cindex[b];
                    if (idx) g->child[b] = p->child[idx - 1];
                }
                g->nchild = p->nchild;
                *ref = g;
                free_shell(p);
                return g;
            }
            default:
                return n;
        }
    }
    static void copy_header(inner* dst, inner* src) {
        dst->plen = src->plen;
        std::memcpy(dst->prefix, src->prefix, kCap);
        dst->end_leaf = src->end_leaf;
    }

    // --- prefix match: how many prefix bytes match key from depth ------------
    static uint8_t prefix_match(inner* n, art_key_view key, uint32_t depth) {
        uint8_t maxlen = n->plen;
        if (key.len - depth < maxlen) maxlen = static_cast<uint8_t>(key.len - depth);
        uint8_t i = 0;
        for (; i < maxlen; ++i)
            if (n->prefix[i] != key.ptr[depth + i]) break;
        return i;
    }

    // --- iterator (defined before modifiers that return iterator) ------------
    struct frame
    {
        inner* node;
        int cursor;  // -1 == end_leaf, else byte value 0..255
    };

    template <bool Const>
    class iter_impl
    {
        friend class art_map;
        template <bool>
        friend class iter_impl;
        using map_ptr = std::conditional_t<Const, const art_map*, art_map*>;

      public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = art_map::value_type;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<Const, const value_type&, value_type&>;
        using pointer = std::conditional_t<Const, const value_type*, value_type*>;

        iter_impl() = default;
        // allow iterator -> const_iterator conversion
        template <bool C = Const, typename = std::enable_if_t<C>>
        iter_impl(const iter_impl<false>& o) : _owner(o._owner), _leaf(o._leaf), _stack() {
            for (std::size_t i = 0; i < o._stack.size(); ++i) _stack.push_back(o._stack[i]);
        }

        reference operator*() const { return _leaf->kv; }
        pointer operator->() const { return &_leaf->kv; }

        iter_impl& operator++() {
            advance();
            return *this;
        }
        iter_impl operator++(int) {
            iter_impl t = *this;
            advance();
            return t;
        }
        iter_impl& operator--() {
            retreat();
            return *this;
        }
        iter_impl operator--(int) {
            iter_impl t = *this;
            retreat();
            return t;
        }
        bool operator==(const iter_impl& o) const { return _leaf == o._leaf; }
        bool operator!=(const iter_impl& o) const { return _leaf != o._leaf; }

      private:
        void push(inner* n, int c) { _stack.push_back(frame{n, c}); }

        void descend_leftmost_into(node* c) {
            while (true) {
                if (is_leaf(c)) {
                    _leaf = as_leaf(c);
                    return;
                }
                inner* in = as_inner(c);
                if (in->end_leaf) {
                    push(in, -1);
                    _leaf = as_leaf(in->end_leaf);
                    return;
                }
                step s = first_step(in);
                push(in, s.byte);
                c = s.child;
            }
        }
        void descend_rightmost_into(node* c) {
            while (true) {
                if (is_leaf(c)) {
                    _leaf = as_leaf(c);
                    return;
                }
                inner* in = as_inner(c);
                step s = last_step(in);
                if (s.byte >= 0) {
                    push(in, s.byte);
                    c = s.child;
                } else {
                    push(in, -1);
                    _leaf = as_leaf(in->end_leaf);
                    return;
                }
            }
        }
        void advance() {
            while (!_stack.empty()) {
                frame& f = _stack.back();
                step s = (f.cursor == -1) ? first_step(f.node) : next_step(f.node, f.cursor);
                if (s.byte >= 0) {
                    f.cursor = s.byte;
                    descend_leftmost_into(s.child);
                    return;
                }
                _stack.pop_back();
            }
            _leaf = nullptr;
        }
        void retreat() {
            if (_leaf == nullptr) {  // --end()
                if (_owner && _owner->_root) descend_rightmost_into(_owner->_root);
                return;
            }
            while (!_stack.empty()) {
                frame& f = _stack.back();
                if (f.cursor == -1) {
                    _stack.pop_back();
                    continue;
                }
                step s = prev_step(f.node, f.cursor);
                if (s.byte >= 0) {
                    f.cursor = s.byte;
                    descend_rightmost_into(s.child);
                    return;
                }
                if (f.node->end_leaf) {
                    f.cursor = -1;
                    _leaf = as_leaf(f.node->end_leaf);
                    return;
                }
                _stack.pop_back();
            }
        }

        map_ptr _owner = nullptr;
        leaf_node* _leaf = nullptr;
        small_vectra<frame, 16> _stack;
    };

  public:
    using iterator = iter_impl<false>;
    using const_iterator = iter_impl<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    art_map() = default;
    explicit art_map(const Allocator& alloc) : _alloc(alloc) {}
    art_map(std::initializer_list<value_type> init) {
        // If an insert throws partway, this constructor fails and ~art_map() will not run;
        // clear() tears down the (consistent) partial tree and frees the slabs so nothing
        // leaks.
        try {
            for (const auto& v : init) insert(v);
        } catch (...) {
            clear();
            throw;
        }
    }

    art_map(const art_map& other)
        : _alloc(std::allocator_traits<Allocator>::select_on_container_copy_construction(
              other._alloc)) {
        // If clone() throws, this constructor fails and ~art_map() will NOT run, so the
        // bump-allocator slabs (node_pool has no destructor of its own) would leak. Release
        // them explicitly. clone() is exception-safe, so no constructed T outlives this.
        try {
            if (other._root) _root = clone(other._root);
            _size = other._size;
        } catch (...) {
            release_pools();
            throw;
        }
    }
    art_map(art_map&& other) noexcept
        : _root(other._root),
          _size(other._size),
          _alloc(std::move(other._alloc)),
          _pool4(std::move(other._pool4)),
          _pool16(std::move(other._pool16)),
          _pool48(std::move(other._pool48)),
          _pool256(std::move(other._pool256)),
          _poolL(std::move(other._poolL)) {
        other._root = nullptr;
        other._size = 0;
    }
    art_map& operator=(const art_map& other) {
        if (this != &other) {
            clear();  // free existing nodes with the current allocator
            if constexpr (std::allocator_traits<
                              Allocator>::propagate_on_container_copy_assignment::value) {
                _alloc = other._alloc;  // adopt before cloning so slabs use the right allocator
            }
            if (other._root) _root = clone(other._root);
            _size = other._size;
        }
        return *this;
    }
    art_map& operator=(art_map&& other) noexcept(
        std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value ||
        std::allocator_traits<Allocator>::is_always_equal::value) {
        if (this == &other) return *this;
        constexpr bool pocma =
            std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value;
        if (pocma || _alloc == other._alloc) {
            // Safe to steal the source's slabs: either we adopt its allocator, or
            // the two allocators are interchangeable.
            clear();
            if constexpr (pocma) _alloc = std::move(other._alloc);
            _root = other._root;
            _size = other._size;
            _pool4 = std::move(other._pool4);
            _pool16 = std::move(other._pool16);
            _pool48 = std::move(other._pool48);
            _pool256 = std::move(other._pool256);
            _poolL = std::move(other._poolL);
            other._root = nullptr;
            other._size = 0;
        } else {
            // Unequal, non-propagating allocators (e.g. distinct PMR resources):
            // the source's slabs must not be freed by our allocator. Move elements
            // individually using this map's allocator.
            clear();
            for (auto it = other.begin(); it != other.end(); ++it) {
                bool inserted = false;
                do_insert(it->first, inserted, [&] {
                    return make_leaf(std::piecewise_construct, std::forward_as_tuple(it->first),
                                     std::forward_as_tuple(std::move(it->second)));
                });
            }
            other.clear();
        }
        return *this;
    }
    ~art_map() { clear(); }

    [[nodiscard]] bool empty() const noexcept { return _size == 0; }
    [[nodiscard]] size_type size() const noexcept { return _size; }
    allocator_type get_allocator() const noexcept { return _alloc; }

    void clear() noexcept {
        if (_root) {
            destroy(_root);  // runs leaf destructors, returns shells to pools
            _root = nullptr;
        }
        _size = 0;
        release_pools();  // free the underlying slabs
    }

    [[nodiscard]] bool contains(const Key& key) const { return find_leaf(key) != nullptr; }
    [[nodiscard]] size_type count(const Key& key) const { return find_leaf(key) ? 1 : 0; }

    T& at(const Key& key) {
        leaf_node* l = find_leaf(key);
        if (!l) throw std::out_of_range("art_map::at");
        return l->kv.second;
    }
    const T& at(const Key& key) const {
        leaf_node* l = find_leaf(key);
        if (!l) throw std::out_of_range("art_map::at");
        return l->kv.second;
    }

    T& operator[](const Key& key) {
        bool inserted = false;
        leaf_node* l = do_insert(key, inserted, [&] { return make_leaf(std::piecewise_construct,
                                                                      std::forward_as_tuple(key),
                                                                      std::forward_as_tuple()); });
        return l->kv.second;
    }

    std::pair<iterator, bool> insert(const value_type& v) {
        iterator it;
        it._owner = this;
        bool inserted = false;
        do_insert(v.first, inserted, [&] { return make_leaf(v); }, &it);
        return {it, inserted};
    }
    std::pair<iterator, bool> insert(value_type&& v) {
        iterator it;
        it._owner = this;
        bool inserted = false;
        do_insert(v.first, inserted, [&] { return make_leaf(std::move(v)); }, &it);
        return {it, inserted};
    }
    template <typename M>
    std::pair<iterator, bool> insert_or_assign(const Key& key, M&& value) {
        iterator it;
        it._owner = this;
        bool inserted = false;
        leaf_node* l = do_insert(key, inserted, [&] {
            return make_leaf(std::piecewise_construct, std::forward_as_tuple(key),
                             std::forward_as_tuple(std::forward<M>(value)));
        }, &it);
        if (!inserted) l->kv.second = std::forward<M>(value);
        return {it, inserted};
    }
    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        return insert(value_type(std::forward<Args>(args)...));
    }
    template <typename... Args>
    std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
        iterator it;
        it._owner = this;
        bool inserted = false;
        do_insert(key, inserted, [&] {
            return make_leaf(std::piecewise_construct, std::forward_as_tuple(key),
                             std::forward_as_tuple(std::forward<Args>(args)...));
        }, &it);
        return {it, inserted};
    }

    size_type erase(const Key& key) {
        if (!_root) return 0;
        ekey kc(key);
        art_key_view kv = kc.view;
        uint8_t s[kArtKeyScratch];

        if (is_leaf(_root)) {
            if (!view_eq(leaf_key(static_cast<leaf_node*>(_root), s), kv)) return 0;
            free_leaf(static_cast<leaf_node*>(_root));
            _root = nullptr;
            --_size;
            return 1;
        }

        // Descend over inner nodes, recording the path so we can cascade the
        // structural simplification back up to the root.
        small_vectra<erase_frame, 24> path;
        inner* in = as_inner(_root);
        uint32_t depth = 0;
        inner* hit = nullptr;  // node we removed a child / end_leaf from
        while (true) {
            if (in->plen) {
                if (kv.len - depth < in->plen) return 0;
                if (std::memcmp(in->prefix, kv.ptr + depth, in->plen) != 0) return 0;
                depth += in->plen;
            }
            if (depth == kv.len) {
                if (!in->end_leaf) return 0;
                free_leaf(static_cast<leaf_node*>(in->end_leaf));
                in->end_leaf = nullptr;
                hit = in;
                break;
            }
            uint8_t b = kv.ptr[depth];
            node** cref = find_child_ref(in, b);
            if (!cref) return 0;
            node* child = *cref;
            if (is_leaf(child)) {
                if (!view_eq(leaf_key(static_cast<leaf_node*>(child), s), kv)) return 0;
                free_leaf(static_cast<leaf_node*>(child));
                remove_child(in, b);
                hit = in;
                break;
            }
            path.push_back(erase_frame{in, b});
            in = as_inner(child);
            ++depth;
        }
        --_size;

        // Cascade: replace `prev` by `repl` in its parent; if a node emptied to
        // null, drop it from the parent and re-simplify the parent, and so on.
        node* prev = hit;
        node* repl = collapse_result(hit);
        while (repl != prev && !path.empty()) {
            erase_frame f = path.back();
            path.pop_back();
            if (repl == nullptr) {
                remove_child(f.node, f.byte);
                prev = f.node;
                repl = collapse_result(f.node);
            } else {
                *find_child_ref(f.node, f.byte) = repl;
                return 1;
            }
        }
        if (repl != prev) _root = repl;  // collapsed up to the root
        return 1;
    }

  private:
    struct erase_frame
    {
        inner* node;
        uint8_t byte;
    };

    static node* single_child(inner* in, uint8_t* out_byte) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                *out_byte = p->keys[0];
                return p->child[0];
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                *out_byte = p->keys[0];
                return p->child[0];
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->cindex[b]) {
                        *out_byte = static_cast<uint8_t>(b);
                        return p->child[p->cindex[b] - 1];
                    }
                return nullptr;
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->child[b]) {
                        *out_byte = static_cast<uint8_t>(b);
                        return p->child[b];
                    }
                return nullptr;
            }
        }
    }

    void remove_child(inner* in, uint8_t b) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                uint8_t i = 0;
                while (i < p->nchild && p->keys[i] != b) ++i;
                for (uint8_t j = i; j + 1 < p->nchild; ++j) {
                    p->keys[j] = p->keys[j + 1];
                    p->child[j] = p->child[j + 1];
                }
                --p->nchild;
                return;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                uint8_t i = 0;
                while (i < p->nchild && p->keys[i] != b) ++i;
                for (uint8_t j = i; j + 1 < p->nchild; ++j) {
                    p->keys[j] = p->keys[j + 1];
                    p->child[j] = p->child[j + 1];
                }
                --p->nchild;
                return;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                uint8_t slot = static_cast<uint8_t>(p->cindex[b] - 1);
                uint8_t last = static_cast<uint8_t>(p->nchild - 1);
                p->cindex[b] = 0;
                if (slot != last) {
                    p->child[slot] = p->child[last];
                    for (int bb = 0; bb < 256; ++bb)
                        if (p->cindex[bb] == last + 1) {
                            p->cindex[bb] = static_cast<uint8_t>(slot + 1);
                            break;
                        }
                }
                --p->nchild;
                return;
            }
            case nkind::n256: {
                auto* p = static_cast<node256*>(in);
                p->child[b] = nullptr;
                --p->nchild;
                return;
            }
            default:
                return;
        }
    }

    // After a removal, simplify `in` and return what should replace it in its
    // parent: nullptr (emptied), a hoisted/merged child, a shrunk node, or `in`
    // itself when unchanged. The caller splices the result into the parent slot,
    // cascading upward when the result is null so the parent's child count stays
    // consistent (single-child path-compression chains depend on this).
    node* collapse_result(inner* in) {
        if (in->nchild == 0) {
            node* r = in->end_leaf;  // may be null
            free_shell_dispatch(in);
            return r;
        }
        if (in->nchild == 1 && in->end_leaf == nullptr) {
            uint8_t cb;
            node* child = single_child(in, &cb);
            if (is_leaf(child)) {
                free_shell_dispatch(in);
                return child;  // hoist leaf (stores full key)
            }
            inner* ci = as_inner(child);
            uint32_t comb = static_cast<uint32_t>(in->plen) + 1 + ci->plen;
            if (comb <= kCap) {
                uint8_t buf[kCap];
                std::memcpy(buf, in->prefix, in->plen);
                buf[in->plen] = cb;
                std::memcpy(buf + in->plen + 1, ci->prefix, ci->plen);
                std::memcpy(ci->prefix, buf, comb);
                ci->plen = static_cast<uint8_t>(comb);
                free_shell_dispatch(in);
                return child;
            }
            return in;  // cannot compress further; keep as a single-child node
        }
        return shrink_result(in);
    }

    void free_shell_dispatch(inner* in) {
        switch (in->kind) {
            case nkind::n4:
                free_shell(static_cast<node4*>(in));
                break;
            case nkind::n16:
                free_shell(static_cast<node16*>(in));
                break;
            case nkind::n48:
                free_shell(static_cast<node48*>(in));
                break;
            default:
                free_shell(static_cast<node256*>(in));
                break;
        }
    }

    // Downsize a node to a smaller kind when sparse; returns the replacement
    // (or `in` unchanged).
    inner* shrink_result(inner* in) {
        switch (in->kind) {
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                if (p->nchild > 4) return in;
                // Shrinking is a best-effort compaction that runs after the element is
                // already removed; if the replacement can't be allocated, keep the larger
                // node (still valid) rather than failing an erase that already succeeded.
                node4* g;
                try {
                    g = make4();
                } catch (...) {
                    return in;
                }
                copy_header(g, p);
                for (uint8_t i = 0; i < p->nchild; ++i) {
                    g->keys[i] = p->keys[i];
                    g->child[i] = p->child[i];
                }
                g->nchild = p->nchild;
                free_shell(p);
                return g;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                if (p->nchild > 12) return in;
                node16* g;  // best-effort shrink (see node16 case)
                try {
                    g = make16();
                } catch (...) {
                    return in;
                }
                copy_header(g, p);
                uint8_t i = 0;
                for (int b = 0; b < 256; ++b)
                    if (p->cindex[b]) {
                        g->keys[i] = static_cast<uint8_t>(b);
                        g->child[i] = p->child[p->cindex[b] - 1];
                        ++i;
                    }
                g->nchild = i;
                free_shell(p);
                return g;
            }
            case nkind::n256: {
                auto* p = static_cast<node256*>(in);
                if (p->nchild > 37) return in;
                node48* g;  // best-effort shrink (see node16 case)
                try {
                    g = make48();
                } catch (...) {
                    return in;
                }
                copy_header(g, p);
                uint8_t i = 0;
                for (int b = 0; b < 256; ++b)
                    if (p->child[b]) {
                        g->child[i] = p->child[b];
                        g->cindex[b] = static_cast<uint8_t>(i + 1);
                        ++i;
                    }
                g->nchild = i;
                free_shell(p);
                return g;
            }
            default:
                return in;
        }
    }

    // Recursively destroy a subtree (frees all nodes + leaves).
    void destroy(node* n) {
        if (is_leaf(n)) {
            free_leaf(as_leaf(n));
            return;
        }
        inner* in = as_inner(n);
        if (in->end_leaf) free_leaf(as_leaf(in->end_leaf));
        switch (n->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i) destroy(p->child[i]);
                free_shell(p);
                break;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i) destroy(p->child[i]);
                free_shell(p);
                break;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->cindex[b]) destroy(p->child[p->cindex[b] - 1]);
                free_shell(p);
                break;
            }
            case nkind::n256: {
                auto* p = static_cast<node256*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->child[b]) destroy(p->child[b]);
                free_shell(p);
                break;
            }
            default:
                break;
        }
    }

    // Recursively clone a subtree. Strongly exception-safe: if any leaf copy (T's copy
    // constructor) or node allocation throws partway, every node and leaf already cloned
    // for this subtree is destroyed before the exception propagates, so no half-built node
    // is leaked and no constructed T is left without a matching destructor.
    node* clone(node* n) {
        if (is_leaf(n)) return make_leaf(as_leaf(n)->kv);
        inner* in = as_inner(n);
        inner* out = nullptr;
        switch (n->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                auto* g = make4();
                try {
                    for (uint8_t i = 0; i < p->nchild; ++i) {
                        g->keys[i] = p->keys[i];
                        g->child[i] = clone(p->child[i]);
                        g->nchild = static_cast<uint8_t>(i + 1);  // keep destroyable on throw
                    }
                } catch (...) {
                    for (uint8_t i = 0; i < g->nchild; ++i) destroy(g->child[i]);
                    free_shell(g);
                    throw;
                }
                out = g;
                break;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                auto* g = make16();
                try {
                    for (uint8_t i = 0; i < p->nchild; ++i) {
                        g->keys[i] = p->keys[i];
                        g->child[i] = clone(p->child[i]);
                        g->nchild = static_cast<uint8_t>(i + 1);
                    }
                } catch (...) {
                    for (uint8_t i = 0; i < g->nchild; ++i) destroy(g->child[i]);
                    free_shell(g);
                    throw;
                }
                out = g;
                break;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                auto* g = make48();
                std::memcpy(g->cindex, p->cindex, sizeof(p->cindex));
                try {
                    // child[] is dense; clean up by index (not cindex, which still maps to
                    // not-yet-cloned source slots until the loop completes).
                    for (uint8_t i = 0; i < p->nchild; ++i) {
                        g->child[i] = clone(p->child[i]);
                        g->nchild = static_cast<uint8_t>(i + 1);
                    }
                } catch (...) {
                    for (uint8_t i = 0; i < g->nchild; ++i) destroy(g->child[i]);
                    free_shell(g);
                    throw;
                }
                out = g;
                break;
            }
            case nkind::n256: {
                auto* p = static_cast<node256*>(in);
                auto* g = make256();  // child[] is zero-initialized
                try {
                    for (int b = 0; b < 256; ++b)
                        if (p->child[b]) g->child[b] = clone(p->child[b]);
                } catch (...) {
                    for (int b = 0; b < 256; ++b)
                        if (g->child[b]) destroy(g->child[b]);
                    free_shell(g);
                    throw;
                }
                g->nchild = p->nchild;
                out = g;
                break;
            }
            default:
                break;
        }
        out->plen = in->plen;
        std::memcpy(out->prefix, in->prefix, kCap);
        // out is now a fully consistent node; if the end-leaf copy throws, destroy() can
        // unwind the whole subtree.
        try {
            out->end_leaf = in->end_leaf ? make_leaf(as_leaf(in->end_leaf)->kv) : nullptr;
        } catch (...) {
            out->end_leaf = nullptr;
            destroy(out);
            throw;
        }
        return out;
    }

    // --- bulk load (sorted input, bottom-up) ---------------------------------
    inner* bulk_make_by_count(std::size_t runs) {
        if (runs <= 4) return make4();
        if (runs <= 16) return make16();
        if (runs <= 48) return make48();
        return make256();
    }
    static void bulk_append(inner* n, uint8_t b, node* c) {
        switch (n->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(n);
                p->keys[p->nchild] = b;
                p->child[p->nchild] = c;
                ++p->nchild;
                return;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(n);
                p->keys[p->nchild] = b;
                p->child[p->nchild] = c;
                ++p->nchild;
                return;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(n);
                p->child[p->nchild] = c;
                p->cindex[b] = static_cast<uint8_t>(p->nchild + 1);
                ++p->nchild;
                return;
            }
            default: {
                auto* p = static_cast<node256*>(n);
                p->child[b] = c;
                ++p->nchild;
                return;
            }
        }
    }
    // Build the subtree for the sorted, unique leaves e[lo,hi) branching at `depth`.
    // v[] holds each leaf's precomputed encoded key (parallel to e[]).
    node* bulk_build(leaf_node** e, art_key_view* v, std::size_t lo, std::size_t hi, uint32_t depth) {
        if (hi - lo == 1) return e[lo];
        art_key_view a = v[lo];
        art_key_view b = v[hi - 1];
        uint32_t maxp = a.len < b.len ? a.len : b.len;
        uint32_t p = depth;
        while (p < maxp && a.ptr[p] == b.ptr[p]) ++p;  // LCP of range (sorted -> first vs last)
        uint8_t chunk = static_cast<uint8_t>(std::min<uint32_t>(p - depth, kCap));
        uint32_t pp = depth + chunk;
        // shortest key in range sorts first; if it ends exactly here it is the end_leaf
        std::size_t start = lo;
        node* el = nullptr;
        if (a.len == pp) {
            el = e[lo];
            start = lo + 1;
        }
        std::size_t runs = 0;
        for (std::size_t i = start; i < hi;) {
            uint8_t bb = v[i].ptr[pp];
            std::size_t j = i + 1;
            while (j < hi && v[j].ptr[pp] == bb) ++j;
            ++runs;
            i = j;
        }
        inner* nd = bulk_make_by_count(runs);
        nd->plen = chunk;
        if (chunk) std::memcpy(nd->prefix, a.ptr + depth, chunk);
        nd->end_leaf = el;
        for (std::size_t i = start; i < hi;) {
            uint8_t bb = v[i].ptr[pp];
            std::size_t j = i + 1;
            while (j < hi && v[j].ptr[pp] == bb) ++j;
            bulk_append(nd, bb, bulk_build(e, v, i, j, pp + 1));
            i = j;
        }
        return nd;
    }

    // --- ordered DFS with sibling-leaf prefetch ------------------------------
    template <typename F>
    void for_each_rec(node* n, F& fn) const {
        if (is_leaf(n)) {
            fn(const_cast<const value_type&>(static_cast<leaf_node*>(n)->kv));
            return;
        }
        inner* in = as_inner(n);
        if (in->end_leaf)
            fn(const_cast<const value_type&>(static_cast<leaf_node*>(in->end_leaf)->kv));
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i) {
                    if (i + 1 < p->nchild) ART_PREFETCH(p->child[i + 1]);
                    for_each_rec(p->child[i], fn);
                }
                break;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i) {
                    if (i + 1 < p->nchild) ART_PREFETCH(p->child[i + 1]);
                    for_each_rec(p->child[i], fn);
                }
                break;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                node* pending = nullptr;
                for (int b = 0; b < 256; ++b) {
                    if (!p->cindex[b]) continue;
                    node* c = p->child[p->cindex[b] - 1];
                    if (pending) {
                        ART_PREFETCH(c);
                        for_each_rec(pending, fn);
                    }
                    pending = c;
                }
                if (pending) for_each_rec(pending, fn);
                break;
            }
            default: {
                auto* p = static_cast<node256*>(in);
                node* pending = nullptr;
                for (int b = 0; b < 256; ++b) {
                    if (!p->child[b]) continue;
                    if (pending) {
                        ART_PREFETCH(p->child[b]);
                        for_each_rec(pending, fn);
                    }
                    pending = p->child[b];
                }
                if (pending) for_each_rec(pending, fn);
                break;
            }
        }
    }

    // --- lookup --------------------------------------------------------------
    leaf_node* find_leaf(const Key& key) const {
        ekey kc(key);
        art_key_view kv = kc.view;
        node* n = _root;
        uint32_t depth = 0;
        while (n) {
            if (is_leaf(n)) {
                uint8_t s[kArtKeyScratch];
                art_key_view lv = leaf_key(static_cast<leaf_node*>(n), s);
                return view_eq(lv, kv) ? static_cast<leaf_node*>(n) : nullptr;
            }
            inner* in = as_inner(n);
            if (in->plen) {
                if (kv.len - depth < in->plen) return nullptr;
                if (std::memcmp(in->prefix, kv.ptr + depth, in->plen) != 0) return nullptr;
                depth += in->plen;
            }
            if (depth == kv.len) {
                return in->end_leaf ? static_cast<leaf_node*>(in->end_leaf) : nullptr;
            }
            n = find_child(in, kv.ptr[depth]);
            if (n) ART_PREFETCH(n);
            ++depth;
        }
        return nullptr;
    }

    // --- insert --------------------------------------------------------------
    // When `path` is non-null, the iterator's stack is built in this single pass
    // (no second descent) so insert()/emplace() can return a positioned iterator.
    template <typename MakeLeaf>
    leaf_node* do_insert(const Key& key, bool& inserted, MakeLeaf&& mk, iterator* path = nullptr) {
        ekey kc(key);
        art_key_view kv = kc.view;
        // The path frames are pushed as the tree is mutated below — some after end_leaf/
        // _size have already been committed. Reserve the stack up front (one frame per
        // inner node on the route, each consuming >= 1 key byte, so kv.len + 1 bounds it)
        // so those push() calls cannot allocate and throw. Reserving before any mutation
        // keeps insert() strongly exception-safe: a throw here leaves the tree untouched.
        if (path) path->_stack.reserve(static_cast<std::size_t>(kv.len) + 1);
        node** ref = &_root;
        uint32_t depth = 0;
        while (true) {
            node* n = *ref;
            if (!n) {
                leaf_node* nl = mk();
                *ref = nl;
                ++_size;
                inserted = true;
                if (path) path->_leaf = nl;
                return nl;
            }
            if (is_leaf(n)) {
                leaf_node* old = static_cast<leaf_node*>(n);
                uint8_t s[kArtKeyScratch];
                art_key_view lv = leaf_key(old, s);
                if (view_eq(lv, kv)) {
                    inserted = false;
                    if (path) path->_leaf = old;
                    return old;
                }
                leaf_node* nl = mk();
                // If the structural step throws (node allocation), free the freshly made
                // leaf so it does not leak; `old` is left reachable at *ref by split_leaf.
                try {
                    split_leaf(ref, old, lv, kv, depth, nl, path);
                } catch (...) {
                    free_leaf(nl);
                    throw;
                }
                ++_size;
                inserted = true;
                return nl;
            }
            inner* in = as_inner(n);
            if (in->plen) {
                uint8_t common = prefix_match(in, kv, depth);
                if (common < in->plen) {
                    leaf_node* nl = mk();
                    try {
                        split_prefix(ref, in, common, kv, depth, nl, path);
                    } catch (...) {
                        free_leaf(nl);  // node alloc failed before *ref changed; drop new leaf
                        throw;
                    }
                    ++_size;
                    inserted = true;
                    return nl;
                }
                depth += in->plen;
            }
            if (depth == kv.len) {
                if (in->end_leaf) {
                    inserted = false;
                    if (path) {
                        path->push(in, -1);
                        path->_leaf = as_leaf(in->end_leaf);
                    }
                    return static_cast<leaf_node*>(in->end_leaf);
                }
                leaf_node* nl = mk();
                in->end_leaf = nl;
                ++_size;
                inserted = true;
                if (path) {
                    path->push(in, -1);
                    path->_leaf = nl;
                }
                return nl;
            }
            uint8_t b = kv.ptr[depth];
            node** cref = find_child_ref(in, b);
            if (!cref) {
                leaf_node* nl = mk();
                // grow() is exception-safe (it allocates the larger node before touching
                // the tree), so on a throw *ref still holds the original node; just free
                // the new leaf.
                try {
                    if (is_full(in)) in = grow(ref, in);
                    add_child(in, b, nl);
                } catch (...) {
                    free_leaf(nl);
                    throw;
                }
                ++_size;
                inserted = true;
                if (path) {
                    path->push(in, b);
                    path->_leaf = nl;
                }
                return nl;
            }
            if (path) path->push(in, b);
            ref = cref;
            ++depth;
        }
    }

    // Split two leaves that share bytes [depth, S); build a prefix chain.
    void split_leaf(node** ref, leaf_node* old, art_key_view lv, art_key_view kv, uint32_t depth,
                    leaf_node* nl, iterator* path = nullptr) {
        uint32_t s = depth;
        while (s < lv.len && s < kv.len && lv.ptr[s] == kv.ptr[s]) ++s;  // divergence depth
        // Build the prefix chain off to the side and only publish it to *ref once it is
        // fully built. make4() is the one allocation that can throw; if it does mid-chain,
        // *ref still points at `old`, so the existing entry stays reachable and there is no
        // partially built node (with a null child slot) left in the tree for destroy() to
        // walk into.
        node* head = nullptr;
        node** cur = &head;
        uint32_t d = depth;
        try {
            while (true) {
                node4* nd = make4();
                uint8_t chunk = static_cast<uint8_t>(std::min<uint32_t>(s - d, kCap));
                nd->plen = chunk;
                if (chunk) std::memcpy(nd->prefix, kv.ptr + d, chunk);
                *cur = nd;
                d += chunk;
                if (d == s) {
                    if (d == lv.len) {
                        nd->end_leaf = old;
                    } else {
                        add_child(nd, lv.ptr[d], old);
                    }
                    if (d == kv.len) {
                        nd->end_leaf = nl;
                    } else {
                        add_child(nd, kv.ptr[d], nl);
                    }
                    if (path) {
                        path->push(nd, d == kv.len ? -1 : static_cast<int>(kv.ptr[d]));
                        path->_leaf = nl;
                    }
                    *ref = head;  // publish the completed chain atomically
                    return;
                }
                // bytes still shared at d: single child, continue chain
                uint8_t b = kv.ptr[d];
                if (path) path->push(nd, b);
                cur = add_child4_slot(nd, b);
                ++d;
            }
        } catch (...) {
            // make4() threw mid-chain. The staged nodes are reachable only through `head`
            // (the divergence point — where old/nl/end_leaf are attached — was not reached,
            // so the chain holds no leaves); free those node4 shells so they don't leak pool
            // slots. *ref is untouched, and do_insert frees the new leaf.
            for (node* p = head; p != nullptr;) {
                auto* n4 = static_cast<node4*>(p);
                node* nxt = n4->nchild > 0 ? n4->child[0] : nullptr;  // single-child links
                free_shell(n4);
                p = nxt;
            }
            throw;
        }
    }

    // Split an inner node whose prefix diverges from the key at `common`.
    void split_prefix(node** ref, inner* n, uint8_t common, art_key_view kv, uint32_t depth,
                      leaf_node* nl, iterator* path = nullptr) {
        node4* top = make4();
        top->plen = common;
        if (common) std::memcpy(top->prefix, n->prefix, common);
        *ref = top;
        uint8_t old_byte = n->prefix[common];
        uint8_t rem = static_cast<uint8_t>(n->plen - common - 1);
        std::memmove(n->prefix, n->prefix + common + 1, rem);
        n->plen = rem;
        add_child(top, old_byte, n);
        uint32_t d = depth + common;
        if (d == kv.len) {
            top->end_leaf = nl;
        } else {
            add_child(top, kv.ptr[d], nl);
        }
        if (path) {
            path->push(top, d == kv.len ? -1 : static_cast<int>(kv.ptr[d]));
            path->_leaf = nl;
        }
    }

    // --- ordered child navigation (used by iterators / bounds) ---------------
    // A single step returns both the child byte and pointer in one node scan,
    // avoiding a second find_child lookup during iteration.
    struct step
    {
        int byte;     // -1 when no such child
        node* child;  // valid iff byte >= 0
    };
    static step first_step(inner* in) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                return in->nchild ? step{p->keys[0], p->child[0]} : step{-1, nullptr};
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                return in->nchild ? step{p->keys[0], p->child[0]} : step{-1, nullptr};
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->cindex[b]) return {b, p->child[p->cindex[b] - 1]};
                return {-1, nullptr};
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->child[b]) return {b, p->child[b]};
                return {-1, nullptr};
            }
        }
    }
    static step last_step(inner* in) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                return in->nchild ? step{p->keys[in->nchild - 1], p->child[in->nchild - 1]}
                                  : step{-1, nullptr};
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                return in->nchild ? step{p->keys[in->nchild - 1], p->child[in->nchild - 1]}
                                  : step{-1, nullptr};
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int b = 255; b >= 0; --b)
                    if (p->cindex[b]) return {b, p->child[p->cindex[b] - 1]};
                return {-1, nullptr};
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int b = 255; b >= 0; --b)
                    if (p->child[b]) return {b, p->child[b]};
                return {-1, nullptr};
            }
        }
    }
    static step next_step(inner* in, int b) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i)
                    if (p->keys[i] > b) return {p->keys[i], p->child[i]};
                return {-1, nullptr};
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i)
                    if (p->keys[i] > b) return {p->keys[i], p->child[i]};
                return {-1, nullptr};
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int x = b + 1; x < 256; ++x)
                    if (p->cindex[x]) return {x, p->child[p->cindex[x] - 1]};
                return {-1, nullptr};
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int x = b + 1; x < 256; ++x)
                    if (p->child[x]) return {x, p->child[x]};
                return {-1, nullptr};
            }
        }
    }
    static step prev_step(inner* in, int b) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                for (int i = p->nchild - 1; i >= 0; --i)
                    if (p->keys[i] < b) return {p->keys[i], p->child[i]};
                return {-1, nullptr};
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                for (int i = p->nchild - 1; i >= 0; --i)
                    if (p->keys[i] < b) return {p->keys[i], p->child[i]};
                return {-1, nullptr};
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int x = b - 1; x >= 0; --x)
                    if (p->cindex[x]) return {x, p->child[p->cindex[x] - 1]};
                return {-1, nullptr};
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int x = b - 1; x >= 0; --x)
                    if (p->child[x]) return {x, p->child[x]};
                return {-1, nullptr};
            }
        }
    }
    static int first_child_byte(inner* in) {
        switch (in->kind) {
            case nkind::n4:
                return in->nchild ? static_cast<node4*>(in)->keys[0] : -1;
            case nkind::n16:
                return in->nchild ? static_cast<node16*>(in)->keys[0] : -1;
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->cindex[b]) return b;
                return -1;
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int b = 0; b < 256; ++b)
                    if (p->child[b]) return b;
                return -1;
            }
        }
    }
    static int last_child_byte(inner* in) {
        switch (in->kind) {
            case nkind::n4:
                return in->nchild ? static_cast<node4*>(in)->keys[in->nchild - 1] : -1;
            case nkind::n16:
                return in->nchild ? static_cast<node16*>(in)->keys[in->nchild - 1] : -1;
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int b = 255; b >= 0; --b)
                    if (p->cindex[b]) return b;
                return -1;
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int b = 255; b >= 0; --b)
                    if (p->child[b]) return b;
                return -1;
            }
        }
    }
    static int next_child_byte_gt(inner* in, int b) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i)
                    if (p->keys[i] > b) return p->keys[i];
                return -1;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                for (uint8_t i = 0; i < p->nchild; ++i)
                    if (p->keys[i] > b) return p->keys[i];
                return -1;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int x = b + 1; x < 256; ++x)
                    if (p->cindex[x]) return x;
                return -1;
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int x = b + 1; x < 256; ++x)
                    if (p->child[x]) return x;
                return -1;
            }
        }
    }
    static int prev_child_byte_lt(inner* in, int b) {
        switch (in->kind) {
            case nkind::n4: {
                auto* p = static_cast<node4*>(in);
                for (int i = p->nchild - 1; i >= 0; --i)
                    if (p->keys[i] < b) return p->keys[i];
                return -1;
            }
            case nkind::n16: {
                auto* p = static_cast<node16*>(in);
                for (int i = p->nchild - 1; i >= 0; --i)
                    if (p->keys[i] < b) return p->keys[i];
                return -1;
            }
            case nkind::n48: {
                auto* p = static_cast<node48*>(in);
                for (int x = b - 1; x >= 0; --x)
                    if (p->cindex[x]) return x;
                return -1;
            }
            default: {
                auto* p = static_cast<node256*>(in);
                for (int x = b - 1; x >= 0; --x)
                    if (p->child[x]) return x;
                return -1;
            }
        }
    }
    static int view_cmp(art_key_view a, art_key_view b) {
        uint32_t m = a.len < b.len ? a.len : b.len;
        int c = m ? std::memcmp(a.ptr, b.ptr, m) : 0;
        if (c) return c;
        return a.len < b.len ? -1 : (a.len > b.len ? 1 : 0);
    }

  public:
    iterator begin() {
        iterator it;
        it._owner = this;
        if (_root) it.descend_leftmost_into(_root);
        return it;
    }
    const_iterator begin() const { return cbegin(); }
    const_iterator cbegin() const {
        const_iterator it;
        it._owner = this;
        if (_root) it.descend_leftmost_into(_root);
        return it;
    }
    iterator end() {
        iterator it;
        it._owner = this;
        return it;
    }
    const_iterator end() const { return cend(); }
    const_iterator cend() const {
        const_iterator it;
        it._owner = this;
        return it;
    }
    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(cend()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(cbegin()); }
    const_reverse_iterator crbegin() const { return const_reverse_iterator(cend()); }
    const_reverse_iterator crend() const { return const_reverse_iterator(cbegin()); }

    iterator find(const Key& key) {
        iterator it = lower_bound_impl<false>(key);
        if (it._leaf) {
            ekey kc(key);
            uint8_t s[kArtKeyScratch];
            if (view_eq(leaf_key(it._leaf, s), kc.view)) return it;
        }
        return end();
    }
    const_iterator find(const Key& key) const {
        const_iterator it = lower_bound_impl<true>(key);
        if (it._leaf) {
            ekey kc(key);
            uint8_t s[kArtKeyScratch];
            if (view_eq(leaf_key(it._leaf, s), kc.view)) return it;
        }
        return cend();
    }
    iterator lower_bound(const Key& key) { return lower_bound_impl<false>(key); }
    const_iterator lower_bound(const Key& key) const { return lower_bound_impl<true>(key); }
    iterator upper_bound(const Key& key) {
        iterator it = lower_bound_impl<false>(key);
        if (it._leaf) {
            ekey kc(key);
            uint8_t s[kArtKeyScratch];
            if (view_eq(leaf_key(it._leaf, s), kc.view)) ++it;
        }
        return it;
    }
    const_iterator upper_bound(const Key& key) const {
        const_iterator it = lower_bound_impl<true>(key);
        if (it._leaf) {
            ekey kc(key);
            uint8_t s[kArtKeyScratch];
            if (view_eq(leaf_key(it._leaf, s), kc.view)) ++it;
        }
        return it;
    }
    std::pair<iterator, iterator> equal_range(const Key& key) {
        return {lower_bound(key), upper_bound(key)};
    }
    std::pair<const_iterator, const_iterator> equal_range(const Key& key) const {
        return {lower_bound(key), upper_bound(key)};
    }

    // Ordered traversal that calls fn(const value_type&) for every element.
    // Faster than iterating begin()..end() for full scans: a recursive DFS avoids
    // the per-step stack navigation, and it prefetches sibling leaves to overlap
    // the (insertion-order) leaf cache misses.
    template <typename F>
    void for_each(F&& fn) const {
        if (_root) for_each_rec(_root, fn);
    }

    // Bulk-load from a range of value_type that is sorted ascending by key.
    // Builds the tree bottom-up (one correctly-sized node per branch, no per-key
    // descent or node growth) — much faster than repeated insert for build-once
    // workloads. Replaces any existing contents. Duplicate keys: last wins.
    template <typename InputIt>
    void bulk_load(InputIt first, InputIt last) {
        clear();
        // Leaves are staged in `e` (and the in-flight `pending`) before the tree exists;
        // none are reachable through _root yet, so if any allocation below throws we must
        // run their destructors here — ~art_map()/clear() would only see _root == null and
        // release the raw slabs without destroying the staged values. Inner-node shells a
        // partial bulk_build may have allocated own no T and are reclaimed by the next
        // clear()/destructor via release_pools().
        std::vector<leaf_node*> e;
        leaf_node* pending = nullptr;
        try {
            uint8_t s1[kArtKeyScratch];
            uint8_t s2[kArtKeyScratch];
            for (InputIt it = first; it != last; ++it) {
                pending = make_leaf(*it);
                // dedup adjacent equal keys (sorted input): last wins
                if (!e.empty() && view_eq(leaf_key(e.back(), s1), leaf_key(pending, s2))) {
                    free_leaf(e.back());
                    e.back() = pending;
                } else {
                    e.push_back(pending);  // may throw before `pending` is owned by `e`
                }
                pending = nullptr;
            }
            if (e.empty()) return;
            // Precompute each leaf's encoded key once into a stable buffer (avoids
            // re-encoding fixed keys on every comparison during the build).
            std::size_t n = e.size();
            std::vector<art_key_view> views(n);
            std::vector<uint8_t> blob;
            if constexpr (encoder::fixed) {
                blob.resize(n * (encoder::bound ? encoder::bound : 1));
                for (std::size_t i = 0; i < n; ++i)
                    views[i] = leaf_key(e[i], blob.data() + i * encoder::bound);
            } else {
                uint8_t dummy[kArtKeyScratch];
                for (std::size_t i = 0; i < n; ++i) views[i] = leaf_key(e[i], dummy);
            }
            _root = bulk_build(e.data(), views.data(), 0, n, 0);
            _size = n;
        } catch (...) {
            if (pending) free_leaf(pending);
            for (leaf_node* l : e) free_leaf(l);
            throw;
        }
    }

    iterator erase(const_iterator pos) {
        const_iterator nxt = pos;
        ++nxt;
        Key k = pos->first;
        if (nxt._leaf == nullptr) {
            erase(k);
            return end();
        }
        Key nk = nxt->first;
        erase(k);
        return find(nk);
    }

  private:
    static node* child_at(inner* in, uint8_t b) { return *find_child_ref(in, b); }

    template <bool Const>
    iter_impl<Const> lower_bound_impl(const Key& key) const {
        iter_impl<Const> it;
        it._owner = const_cast<std::conditional_t<Const, const art_map*, art_map*>>(this);
        ekey kc(key);
        art_key_view kv = kc.view;
        node* n = _root;
        uint32_t depth = 0;
        while (n) {
            if (is_leaf(n)) {
                uint8_t s[kArtKeyScratch];
                art_key_view lv = leaf_key(static_cast<leaf_node*>(n), s);
                if (view_cmp(lv, kv) >= 0) {
                    it._leaf = static_cast<leaf_node*>(n);
                } else {
                    it._leaf = static_cast<leaf_node*>(n);
                    it.advance();
                }
                return it;
            }
            inner* in = as_inner(n);
            if (in->plen) {
                uint32_t avail = kv.len - depth;
                uint8_t cmplen = in->plen < avail ? in->plen : static_cast<uint8_t>(avail);
                uint8_t m = 0;
                for (; m < cmplen; ++m)
                    if (in->prefix[m] != kv.ptr[depth + m]) break;
                if (m < in->plen) {
                    // prefix diverges (or key ran out inside prefix)
                    if (m == avail || kv.ptr[depth + m] < in->prefix[m]) {
                        it.descend_leftmost_into(n);  // whole subtree > key
                    } else {
                        it.advance();  // whole subtree < key -> successor up the stack
                    }
                    return it;
                }
                depth += in->plen;
            }
            if (depth == kv.len) {
                if (in->end_leaf) {
                    it.push(in, -1);
                    it._leaf = static_cast<leaf_node*>(in->end_leaf);
                } else {
                    it.descend_leftmost_into(n);  // all children > key
                }
                return it;
            }
            uint8_t b = kv.ptr[depth];
            node* c = find_child(in, b);
            if (c) {
                it.push(in, b);
                n = c;
                ++depth;
                continue;
            }
            int nb = next_child_byte_gt(in, b);
            if (nb >= 0) {
                it.push(in, nb);
                it.descend_leftmost_into(child_at(in, static_cast<uint8_t>(nb)));
            } else {
                it.advance();  // exhausted this node -> successor up the stack
            }
            return it;
        }
        it._leaf = nullptr;  // empty tree
        return it;
    }

    node* _root = nullptr;
    size_type _size = 0;
    [[no_unique_address]] Allocator _alloc;
    node_pool<node4> _pool4;
    node_pool<node16> _pool16;
    node_pool<node48> _pool48;
    node_pool<node256> _pool256;
    node_pool<leaf_node> _poolL;
};

}  // namespace stdb::container

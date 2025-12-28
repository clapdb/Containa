# Lock-Free Concurrent Skiplist

本文档记录 `concurrent_skiplist` 的 lock-free 实现原理和性能分析。

## 核心技术

### 1. Marked Pointers（标记指针）

利用指针的最低位标记节点是否被"逻辑删除"：

```cpp
template <typename T>
struct MarkedPtr {
    uintptr_t _bits;

    T* ptr() const { return reinterpret_cast<T*>(_bits & ~uintptr_t(1)); }
    bool marked() const { return _bits & 1; }
    MarkedPtr with_mark() const { return MarkedPtr(ptr(), true); }
};
```

由于内存对齐，指针末位总是 0，可以用来存储标记位。

### 2. CAS（Compare-And-Swap）

所有修改都通过原子 CAS 完成，不需要锁：

```cpp
// 插入：原子地将新节点链接到前驱
pred_next->compare_exchange_strong(expected, new_node,
    std::memory_order_release,
    std::memory_order_relaxed);
```

### 3. 两阶段删除

**阶段1 - 逻辑删除**：标记所有层的 next 指针

```cpp
for (int i = victim->height; i >= 0; --i) {
    MarkedPtr<Node> next = victim->next[i].load();
    while (!next.marked()) {
        victim->next[i].compare_exchange_weak(next, next.with_mark(),
            std::memory_order_release, std::memory_order_relaxed);
    }
}
```

**阶段2 - 物理删除**：将前驱指向后继

```cpp
for (int i = victim->height; i >= 0; --i) {
    pred->next[i].compare_exchange_strong(victim, victim->next[i].ptr());
}
```

### 4. 帮助机制（Helping）

遍历时遇到标记节点会帮助完成物理删除：

```cpp
if (succ.marked() || curr.marked()) {
    // 找到下一个未标记节点并解除链接
    pred_next->compare_exchange_strong(curr, next_unmarked);
}
```

## Memory Ordering 分析

### 配对规则

```
Writer (release)  ──同步──►  Reader (acquire)
     ↓                            ↓
  之前的写                     之后的读
  都可见                       都能看到
```

### 同步关系图

```
Insert:                              Find:
─────────────────                    ─────────────────
node->key = k;
node->value = v;
node->next[i] = succ;
        │                                    │
        ▼                                    ▼
   CAS(release) ════════════════════► load(acquire)
                 同步点                      │
                                            ▼
                                     read node->key  ✓ 可见
                                     read node->value ✓ 可见
```

```
Delete:                              Find:
─────────────────                    ─────────────────
   CAS(release) ════════════════════► load(acquire)
   设置 mark 位     同步点              检查 mark 位
                                            │
                                            ▼
                                     if (marked) skip ✓
```

### 各操作的 Memory Order

| 操作 | Order | 原因 |
|------|-------|------|
| 读取 next 指针 | `acquire` | 与 insert 的 release 配对，看到节点数据 |
| 读取 height | `relaxed` | 近似值即可，不影响正确性 |
| 读取 size | `relaxed` | 近似值即可 |
| Insert CAS 链接 | `release/relaxed` | 发布节点数据 / 失败只是重试 |
| Delete CAS 标记 | `release/relaxed` | 发布标记 / 失败只是重试 |
| Delete 物理解链 | `relaxed` | 尽力而为，后续遍历会补救 |
| 初始化节点 next | `relaxed` | 节点还未发布，无需同步 |

### 架构差异

```asm
; x86-64: acquire 和 relaxed 编译结果相同（TSO 内存模型）
mov rax, [rcx]    ; 两者都是普通 mov

; ARM64: 有区别
ldr x0, [x1]      ; relaxed
ldar x0, [x1]     ; acquire (带 load-acquire 屏障)
```

x86 的 TSO（Total Store Order）内存模型自动提供 acquire-release 语义，但代码仍需正确标注以保证在 ARM 等弱内存模型架构上的正确性。

## 性能测试

### 测试环境

- CPU: 16 核心
- 操作: 1,000,000 次
- 初始数据: 100,000 条
- 负载: 80% 读, 10% 插入, 10% 删除

### 结果对比

| 实现 | 1T | 2T | 4T | 8T | 12T | 16T |
|------|-----|-----|-----|-----|------|------|
| B-tree (单线程基准) | 13.3M | - | - | - | - | - |
| Coarse-grained Lock | 3.5M | 1.5M | 1.3M | 1.2M | 1.2M | 1.1M |
| Sharded (16分片) | 3.8M | 5.7M | 8.3M | 10.4M | 10.4M | 10.1M |
| **Lock-free** | 3.8M | 7.1M | **12.8M** | **19.5M** | **25.8M** | **32.0M** |

### 相对性能 (vs 单线程 B-tree)

| 线程数 | Lock-free Skiplist | 状态 |
|--------|-------------------|------|
| 1 | 0.28x | |
| 2 | 0.54x | |
| 4 | 0.97x | 接近持平 |
| 8 | 1.47x | ✓ 胜出 |
| 12 | 1.94x | ✓ 胜出 |
| 16 | 2.41x | ✓ 胜出 |

## 优化技术

### 1. 快速随机数生成

使用 xorshift64 替代 mt19937：

```cpp
static uint64_t xorshift64() {
    static thread_local uint64_t state = 0x853c49e6748fea9bULL ^
        (uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id());
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}
```

### 2. 高度生成优化

使用 `__builtin_ctzll` 实现 O(1) 几何分布：

```cpp
uint8_t random_height() {
    uint64_t r = xorshift64();
    uint8_t h = __builtin_ctzll(r | (1ULL << MaxLevel));
    return h;
}
```

### 3. 避免 False Sharing

```cpp
alignas(64) std::atomic<MarkedPtr<Node>> _head[MaxLevel + 1];
alignas(64) std::atomic<size_type> _size{0};
```

### 4. 简化内存管理

节点在删除时只做逻辑标记，不立即释放，由析构函数统一清理：

```cpp
~concurrent_skiplist() {
    Node* curr = _head[0].load().ptr();
    while (curr) {
        Node* next = curr->next[0].load().ptr();
        curr->destroy();
        curr = next;
    }
}
```

## 使用示例

```cpp
#include "container/concurrent_skiplist.hpp"

stdb::container::concurrent_skiplist<int, std::string> map;

// 多线程安全操作
map.insert(1, "one");
map.insert(2, "two");

auto val = map.find(1);  // std::optional<std::string>
if (val) {
    std::cout << *val << std::endl;
}

map.erase(1);
bool exists = map.contains(2);
```

## 适用场景

Lock-free skiplist 适合：
- 高并发读写场景（8+ 核心优势明显）
- 需要迭代器稳定性的场景
- 需要指针稳定性的场景

B-tree 更适合：
- 单线程或低并发场景
- 需要最佳缓存利用率的场景
- 内存紧凑性要求高的场景

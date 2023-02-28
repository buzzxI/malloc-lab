# 格式

segregated list, 每个 list 保存的大小为 $2^i ~ 2^{i + 1}$, 比如 list[3] 中保存 free block 大小均在 8 Byte 到 15 Byte 之间

> 因为整个堆大小限制为 20MB, list 大小为 25(list[24] 中保存的 block 大小为 $2^{24}$ Byte(16MB))

所有的 block 都是 8 Byte 对齐的, 所有的 block 都具有 header, 但只有 free block 才具有 footer

header 大小为 4 字节, 因为 block 都是 8 字节对齐的, 因此 block size 仅占用了 header/footer 的高 29 bit

header 中低 3 bit 作为控制信息, 其中最低位 bit 表示当前 block 是否被分配了, 次低位 bit 表示前一个 block 是否被分配了, 从而一个 header 即可实现 coalescing

> 正因为 allocated block 中只有一个 header, 在进行 block 分配的时候只需要考虑当前需要分配的 size 和 block size - 4 的大小即可

因为使用 segregate list, 所以需要 free block 可以记住前后 free block 的 block, 因为物理机是 64 bit 的, 因此一个地址需要占用 8 Byte, predecessor(pre) 和 successor(succ) 一共需要额外占用 16 Byte, 再加上 header 和 footer, 一个最小的 free block 也需要 24 Byte, 但这些都不重要, 因为分配 block 的时候 pre, succ, footer 都会被去掉

综上 segregate list 中只有从 list[4] 之后才是真的有效的, 但这也导致了如果分配了大量大小小于 20 Byte 的 block, 将出现大量的内部碎片

> 尽管 64 bit 中只有低 48 bit(6 Byte) 才会用作地址, 但没有什么类型的大小正好为 6 Byte, 因此这里还是使用了 8 字节保存地址, **这里可以作为优化的地方**

为了避免多次向对申请内存, 每次最小会申请大小为 4KB 的 block 放入对应 segregated list

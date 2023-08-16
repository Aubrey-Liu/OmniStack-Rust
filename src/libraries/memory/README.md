# Memory 库使用说明

所有通过本库获得的内存都形如

|---RegionMeta---(memory::kMetaHeadroomSize bytes)---|(Return Value)-----|

> 包括从内存池中获取的内存

其中RegionMeta的信息如下：

``` c++
struct RegionMeta {
    RegionType type;
    uint64_t iova;
    uint64_t process_id;
    #if defined (OMNIMEM_BACKEND_DPDK)
    void* addr;
    #else
    uint64_t offset;
    #endif
    size_t size;
    uint64_t ref_cnt;

    union {
#if defined (OMNIMEM_BACKEND_DPDK)
        MemoryPool* mempool;
#else
        uint64_t mempool_offset;
#endif
    };
};
```

为了支持在是否开启DPDK的情况下都能统一编程，我们实现了`memory::Pointer`类。从本库中获取的所有的内存都可以通过`memory::Pointer(ptr)`来进程封装使用。

`memory::Pointer`提供了`->,*`的重载，同时提供了`Get/Set`方法用于直接获取真实指针和设置真实指针。
`memory::Pointer`可以直接通过内存拷贝的方法实现在不同进程之间的重用。（将一个`memory::Pointer`的内存拷贝到另一个进程中，强制类型转换回`memory::Pointer`类型后即可正常使用）

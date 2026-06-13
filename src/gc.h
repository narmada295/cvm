#ifndef CVM_GC_H
#define CVM_GC_H

#include <functional>
#include <vector>

#include "value.h"

// A precise mark-and-sweep garbage collector.
//
// The Heap owns every GCObject (linked in an intrusive list). Collection runs
// only at VM safepoints (between bytecode instructions), where the entire live
// set is reachable from a small, well-defined root set. That invariant is what
// makes mid-instruction allocations safe without extra bookkeeping.
class Heap {
public:
    Heap() = default;
    ~Heap();
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    // Allocate a T (derived from GCObject), tracked by the heap.
    template <typename T, typename... Args>
    T* allocate(Args&&... args) {
        T* obj = new T(std::forward<Args>(args)...);
        obj->size = sizeof(T);
        obj->next = objects_;
        objects_ = obj;
        bytesAllocated_ += obj->size;
        objectCount_++;
        return obj;
    }

    // The VM registers a callback that marks its roots (stack, frames, globals…).
    void setRootMarker(std::function<void()> marker) { markRoots_ = std::move(marker); }
    void setStress(bool on) { stress_ = on; }
    void setVerbose(bool on) { verbose_ = on; }

    // Should a collection run at the next safepoint?
    bool shouldCollect() const { return stress_ || bytesAllocated_ > nextGC_; }

    // Mark helpers used by the root marker and the per-object tracer.
    void markValue(const Value& v);
    void markObject(GCObject* obj);

    void collect();  // run one full mark-sweep cycle

    // Stats (shown by --gc-stats).
    int collections() const { return collections_; }
    size_t objectCount() const { return objectCount_; }
    size_t bytesAllocated() const { return bytesAllocated_; }

private:
    void blacken(GCObject* obj);  // mark everything an object references
    void sweep();                 // free all unmarked objects

    GCObject* objects_ = nullptr;
    std::vector<GCObject*> greyStack_;
    std::function<void()> markRoots_;

    size_t bytesAllocated_ = 0;
    size_t nextGC_ = 1024 * 1024;  // first collection threshold (1 MiB)
    size_t objectCount_ = 0;
    int collections_ = 0;
    bool stress_ = false;
    bool verbose_ = false;
};

#endif // CVM_GC_H

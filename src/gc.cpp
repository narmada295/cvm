#include "gc.h"

#include <iostream>

Heap::~Heap() {
    // Free every remaining object on shutdown.
    GCObject* obj = objects_;
    while (obj) {
        GCObject* next = obj->next;
        delete obj;
        obj = next;
    }
}

void Heap::markObject(GCObject* obj) {
    if (obj == nullptr || obj->marked) return;
    obj->marked = true;
    greyStack_.push_back(obj);  // remember to trace its references later
}

void Heap::markValue(const Value& v) {
    // Only the pointer-typed alternatives reference heap objects.
    if (std::holds_alternative<FunctionObj*>(v)) markObject(std::get<FunctionObj*>(v));
    else if (std::holds_alternative<ClosureObj*>(v)) markObject(std::get<ClosureObj*>(v));
    else if (std::holds_alternative<NativeObj*>(v)) markObject(std::get<NativeObj*>(v));
    else if (std::holds_alternative<ArrayObj*>(v)) markObject(std::get<ArrayObj*>(v));
    else if (std::holds_alternative<MapObj*>(v)) markObject(std::get<MapObj*>(v));
}

// Mark everything reachable from `obj` (the "blacken" step of tri-color marking).
void Heap::blacken(GCObject* obj) {
    switch (obj->type) {
        case ObjType::Function: {
            auto* fn = static_cast<FunctionObj*>(obj);
            for (const Value& c : fn->chunk.constants) markValue(c);  // nested functions, etc.
            break;
        }
        case ObjType::Closure: {
            auto* cl = static_cast<ClosureObj*>(obj);
            markObject(cl->function);
            for (Upvalue* uv : cl->upvalues) markObject(uv);
            break;
        }
        case ObjType::Upvalue:
            markValue(static_cast<Upvalue*>(obj)->closedValue);
            break;
        case ObjType::Array:
            for (const Value& e : static_cast<ArrayObj*>(obj)->elements) markValue(e);
            break;
        case ObjType::Map:
            for (const auto& [k, v] : static_cast<MapObj*>(obj)->entries) markValue(v);
            break;
        case ObjType::Native:
            break;  // holds no references to other heap objects
    }
}

void Heap::sweep() {
    GCObject** link = &objects_;
    while (*link) {
        GCObject* obj = *link;
        if (obj->marked) {
            obj->marked = false;  // reset for the next cycle
            link = &obj->next;
        } else {
            *link = obj->next;          // unlink
            bytesAllocated_ -= obj->size;
            objectCount_--;
            delete obj;
        }
    }
}

void Heap::collect() {
    size_t before = bytesAllocated_;

    greyStack_.clear();
    if (markRoots_) markRoots_();              // mark the root set
    while (!greyStack_.empty()) {              // trace transitively
        GCObject* obj = greyStack_.back();
        greyStack_.pop_back();
        blacken(obj);
    }
    sweep();                                   // reclaim the unmarked

    collections_++;
    nextGC_ = bytesAllocated_ * 2;             // grow the threshold (heuristic)
    if (nextGC_ < 1024 * 1024) nextGC_ = 1024 * 1024;

    if (verbose_) {
        std::cerr << "[gc] cycle #" << collections_ << ": "
                  << before << " -> " << bytesAllocated_ << " bytes ("
                  << objectCount_ << " objects live)\n";
    }
}

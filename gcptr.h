/*
    A ref counted + mark & sweep garbage collector.

    Mostly objects will be recycled by the ref-counter,
    the circular referenced objects will be handled by the mark & sweep gc.

    Based on http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.
    Improvements:
    - add ref count
    - c++11 support
    - reduce unnecessary obj info searching.
    - replace std::map with hash table.
    - replace the operator new & delete with free factory function.
    TODO:
    - make it exception safe.
    - add compability for old compiler

    by crazybie at soniced@sina.com, shawn.li.song@gmail.com.
*/

#pragma once
#include <vector>

namespace gc
{
    namespace details
    {
        struct ClassInfo;
        struct ObjInfo;
        extern const int ObjInfoSize;
        ObjInfo* newObjInfo(void* obj, ClassInfo* clsInfo, char* mem);        

        class PointerBase
        {
        public:
            ObjInfo* owner;
            ObjInfo* objInfo;
            PointerBase();
            PointerBase(void* obj);
            void onPointerUpdate();
            static PointerBase* fromOffset(char* obj, int offset);

#ifdef _DEBUG
            virtual ~PointerBase();
#else
            ~PointerBase();
#endif      
        };
                
        struct ClassInfo
        {
            void                (*dctor)(void*);
            int                 size;
            std::vector<int>    memPtrOffsets;
        };

        template<typename T>
        class ObjClassInfo
        {
        public:
            static void destroy(void* obj) { ((T*)obj)->~T(); }
            static ClassInfo* get() { static ClassInfo i{ destroy, sizeof(T) }; return &i; }
        };
    };
    

    template <typename T>
    class gc_ptr : protected details::PointerBase
    {
        typedef details::ObjInfo ObjInfo;
    public:
        // Constructors

        gc_ptr() : ptr(0) { }
        gc_ptr(T* obj, ObjInfo* info_) { reset(obj, info_);}
        explicit gc_ptr(T* obj) : PointerBase(obj), ptr(obj) { }
        template <typename U>
        gc_ptr(const gc_ptr<U>& r) { reset(r.ptr, r.objInfo); }
        gc_ptr(const gc_ptr& r) { reset(r.ptr, r.objInfo);  }
        gc_ptr(gc_ptr&& r) { reset(r.ptr, r.objInfo); r.objInfo = 0; }

        // Operators

        template <typename U>
        gc_ptr& operator=(const gc_ptr<U>& r) { reset(r.ptr, r.objInfo);  return *this; }
        gc_ptr& operator=(const gc_ptr& r) { reset(r.ptr, r.objInfo);  return *this; }
        gc_ptr& operator=(gc_ptr&& r) { reset(r.ptr, r.objInfo); r.objInfo = 0; return *this; }
        T* operator->() const { return ptr; }        
        explicit operator bool() const { return ptr != 0; }
        bool operator==(const gc_ptr& r)const { return objInfo == r.objInfo; }
        bool operator!=(const gc_ptr& r)const { return objInfo != r.objInfo; }
        void operator=(T*) = delete;
        gc_ptr& operator=(decltype(nullptr)) { return *this = gc_ptr(); }

        // Methods

        void reset(T* o) { gc_ptr(o).swap(*this); }
        void reset(T* o, ObjInfo* n) { ptr = o; objInfo = n; onPointerUpdate(); }
        void swap(gc_ptr& r)
        {
            T* temp = ptr;
            ObjInfo* tinfo = objInfo;
            reset(r.ptr, r.objInfo);
            r.reset(temp, tinfo);
        }

    private:
        template <typename U>
        friend class gc_ptr;

        T*  ptr;
    };

    
    template<typename T, typename... Args>
    gc_ptr<T> make_gc(Args&&... args)
    {
        char* buf = new char[sizeof(T) + details::ObjInfoSize];
        T* obj = new (buf + details::ObjInfoSize) T(std::forward<Args>(args)...);
        return gc_ptr<T>(obj, details::newObjInfo(obj, details::ObjClassInfo<T>::get(), buf));
    }

    template<typename T>
    gc_ptr<T> gc_from_this(T* t) { return gc_ptr<T>(t); }

    int GcCollect(int step);
}


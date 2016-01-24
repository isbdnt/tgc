#include "gcptr.h"
#include <set>
#include <stdint.h>
#include <unordered_set>


namespace gc
{
    namespace details
    {
        enum class MarkColor : char { White, Gray, Black };

        struct ObjInfo
        {
            char*                       obj;
            ClassInfo*                  clsInfo;
            MarkColor                   color;

            struct Less
            {
                bool operator()(ObjInfo* x, ObjInfo* y) { return x->obj + x->clsInfo->size <= y->obj; }
            };

            ObjInfo(void* o, ClassInfo* c) : obj((char*)o), clsInfo(c), color(MarkColor::White) {}
            ~ObjInfo() { if (clsInfo->dctor) clsInfo->dctor(obj); }
            bool containsPointer(void* ptr) { return obj <= ptr && ptr < obj + clsInfo->size; }
        };

        const int ObjInfoSize = sizeof(ObjInfo);
        ObjInfo* kInvalidObjInfo = (ObjInfo*)-1;

        struct GC
        {
            std::unordered_set<PointerBase*>    pointers;
            std::set<ObjInfo*, ObjInfo::Less>   objInfoSet;
            std::vector<ObjInfo*>               grayObjs;

            enum class State { Idle, Marking, Sweeping } state;

            GC()
            {
                state = State::Idle;
                grayObjs.reserve(1024);
                pointers.reserve(1024);
            }
            
            ~GC() 
            { 
                while (objInfoSet.size()) 
                    gc::GcCollect(INT32_MAX); 
            }
            void onPointerUpdate(PointerBase* p) 
            { 
                if (state == State::Marking) 
                    markAsRoot(p);
            }
            ObjInfo* findOwnerObjInfo(void* obj)
            {
                ClassInfo clsInfo{ 0, 0 };
                ObjInfo temp(obj, &clsInfo);
                auto i = objInfoSet.lower_bound(&temp);
                if (i == objInfoSet.end() || !(*i)->containsPointer(obj))
                    return 0;
                return *i;
            }
            ObjInfo* newObjInfo(void* o, ClassInfo* clsInfo, char* mem)
            {
                ObjInfo* objInfo = new (mem)ObjInfo(o, clsInfo);
                objInfoSet.insert(objInfo);
                return objInfo;
            }
            void markAsRoot(PointerBase* p)
            {
                if (!p->objInfo) return;
                if (isRoot(p)) {
                    if (p->objInfo->color == MarkColor::White) {
                        p->objInfo->color = MarkColor::Gray;
                        grayObjs.push_back(p->objInfo);
                    }
                }
            }
            bool isRoot(PointerBase* p)
            {
                // 不能放到指针构造并注册的地方：
                // 因为指针构造的时候owner还没有完成构造并注册到gc中。
                if (p->owner == kInvalidObjInfo) {
                    auto owner = findOwnerObjInfo(p);
                    p->owner = owner;
                    if (owner) {
                        auto offset = (char*)p - (char*)owner->obj;
                        auto& offsets = owner->clsInfo->memPtrOffsets;                        
                        if (std::find(offsets.begin(), offsets.end(), offset) == offsets.end())
                            offsets.push_back(offset);
                    }
                }
                return !p->owner;
            }
            int collect(int stepCnt)
            {
                int sweptCnt = 0;
                switch (state) {
                case State::Idle:
                    state = State::Marking;
                    for (auto i : pointers) {
                        markAsRoot(i);
                    }
                    break;

                case State::Marking:
                    while (grayObjs.size() && stepCnt--) {
                        ObjInfo* objInfo = grayObjs.back();
                        grayObjs.pop_back();
                        objInfo->color = MarkColor::Black;
                        for (auto memPtrOffset : objInfo->clsInfo->memPtrOffsets) {
                            auto* mp = PointerBase::fromOffset(objInfo->obj, memPtrOffset);
                            if (mp->objInfo->color == MarkColor::White) {
                                grayObjs.push_back(mp->objInfo);
                            }
                        }
                    }
                    if (!grayObjs.size()) state = State::Sweeping;
                    break;

                case State::Sweeping:
                    state = State::Idle;
                    for (auto i = objInfoSet.begin(); i != objInfoSet.end();) {
                        ObjInfo* obj = *i;
                        if (obj->color == MarkColor::White) {
                            sweptCnt++;
                            delete obj;
                            i = objInfoSet.erase(i);
                            continue;
                        }
                        obj->color = MarkColor::White;
                        ++i;
                    }
                    break;
                }
                return sweptCnt;
            }
        };

        GC* getGC() { static GC i; return &i; }
        ObjInfo* newObjInfo(void* o, ClassInfo* clsInfo, char* mem) { return getGC()->newObjInfo(o, clsInfo, mem); }

        PointerBase::PointerBase() : objInfo(0), owner(kInvalidObjInfo) { getGC()->pointers.insert(this); }
        PointerBase::PointerBase(void* obj) : owner(kInvalidObjInfo), objInfo(getGC()->findOwnerObjInfo(obj)) { getGC()->pointers.insert(this); }
        PointerBase::~PointerBase() { getGC()->pointers.erase(this); }
        void PointerBase::onPointerUpdate() { getGC()->onPointerUpdate(this); }
        PointerBase* PointerBase::fromOffset(char* obj, int offset) { return (PointerBase*)(obj + offset); }

    }

    int GcCollect(int step) { return details::getGC()->collect(step); }
}

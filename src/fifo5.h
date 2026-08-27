/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/cppFiles/file.h to edit this template
 */

/* 
 * File:   fifo5.h
 * Author: rishitsharma
 *
 * Created on 15 April, 2026, 9:53 PM
 */
//Version 1 of Fifo 5 (not for non trivial types and some restrictions)
//#ifndef FIFO5_H
//#define FIFO5_H
//#include<iostream>
//#include<atomic>
//#include<memory>
//#include<thread>
//#include<exception>
//#include<new>
//
//template<typename T, typename Alloc = std::allocator<T>>
//class Fifo5 : private Alloc
//{
//    T* ring_;
//    std::size_t capacity_;
//    //Now we add masking for bitwise modulus which reduces latency
//    std::size_t mask_;
//    struct alignas(std::hardware_destructive_interference_size) PushState{
//        std::atomic<std::size_t> cursor{0};
//        std::size_t cachedPopCursor{0};
//    };
//    PushState pushState_;
//    struct alignas(std::hardware_destructive_interference_size) PopState{
//        std::atomic<std::size_t> cursor{0};
//        std::size_t cachedPushCursor{0};
//    };
//    PopState popState_;
//    
//    T* element(std::size_t cursor)
//    {
//        return &ring_[cursor & mask_];
//    }
//    
//    public:
//        explicit Fifo5(std::size_t capacity, Alloc const& alloc = Alloc{})
//        : Alloc{alloc},
//                capacity_{capacity},
//                mask_{capacity-1},
//                        ring_{std::allocator_traits<Alloc>::allocate(*this,capacity)}
//        {};
//        
//        ~Fifo5() // Simple destructor cause object lifetime is managed by the producer-consumer itself. Consumer destroys objects
//        {
//            std::allocator_traits<Alloc>::deallocate(*this,ring_, capacity_);
//        };
//        class pusher_t
//        {
//            Fifo5* fifo_{};
//            std::size_t cursor_{0};
//        public:
//            pusher_t() = default;
//            explicit pusher_t(Fifo5* fifo, std::size_t cursor) : fifo_{fifo}, cursor_{cursor} {}
//            ~pusher_t()
//            {
//                if(fifo_)
//                {
//                    fifo_->pushState_.cursor.store(cursor_+1,std::memory_order_release);
//                }
//            }
//            //No-Copy Semantics
//            pusher_t(const pusher_t&) = delete;
//            pusher_t& operator= (const pusher_t&) = delete;
//            //Allow moving so there isn't copying
//            pusher_t(pusher_t&& other) noexcept:fifo_(other.fifo_),cursor_(other.cursor_){
//                other.fifo_ = nullptr;
//            } //other is rvalue type
//            explicit operator bool() const { return fifo_ != nullptr; } // if Fifo is full then returns validity of ticket and allows if(pusher_t) usage
//            void release() { fifo_ = nullptr; } // Stop Publish if there's error.
//            T* get() { return fifo_->element(cursor_); } // TO get the actual memory slot in the queue
//            T* operator->() { return get(); }
//            T& operator*() { return *get(); }
//        };
//        
//        class popper_t
//        {
//            Fifo5* fifo_{};
//            std::size_t cursor_{0};
//        public:
//            popper_t() = default;
//            explicit popper_t(Fifo5* fifo, std::size_t cursor) : fifo_{fifo}, cursor_{cursor} {}
//            ~popper_t()
//            {
//                if(fifo_)
//                {
//                    fifo_->popState_.cursor.store(cursor_ +1, std::memory_order_release);
//                }
//            }
//            popper_t(const popper_t&) = delete;
//            popper_t& operator= (const popper_t&) = delete;
//            popper_t(popper_t&& other) noexcept: fifo_(other.fifo_),cursor_(other.cursor_){
//                other.fifo_ = nullptr;
//            }
//            explicit operator bool() const { return fifo_ != nullptr; }
//            void release() { fifo_ = nullptr; }
//            T* get() { return fifo_->element(cursor_); }
//            T* operator->() { return get(); }
//            T& operator*() { return *get(); }
//        };
//        
//        pusher_t push() {
//        auto pushCursor = pushState_.cursor.load(std::memory_order_relaxed);
//
//        if (pushCursor - pushState_.cachedPopCursor == capacity_) {
//            pushState_.cachedPopCursor = popState_.cursor.load(std::memory_order_acquire);
//            if (pushCursor - pushState_.cachedPopCursor == capacity_) {
//                return pusher_t{}; // Returns an empty proxy evaluating to false
//            }
//        }
//        return pusher_t{this, pushCursor}; // Returns a valid proxy mapping to the array slot
//    }
//
//    popper_t pop() {
//        auto popCursor = popState_.cursor.load(std::memory_order_relaxed);
//
//        if (popState_.cachedPushCursor == popCursor) {
//            popState_.cachedPushCursor = pushState_.cursor.load(std::memory_order_acquire);
//            if (popState_.cachedPushCursor == popCursor) {
//                return popper_t{};
//            }
//        }
//        return popper_t{this, popCursor};
//    }
//};
//
//
//#endif /* FIFO5_H */

//Version 2 of Fifo 5 ( allows non trivial types and relaxes some restrictions)
#ifndef FIFO5_H
#define FIFO5_H

#include <atomic>
#include <memory>
#include <new>
#include <cassert>

template<typename T, typename Alloc = std::allocator<T>>
class Fifo5 : private Alloc
{
    static constexpr std::size_t cache_line_size = 64;

    T* ring_;
    std::size_t capacity_;
    std::size_t mask_;

    struct alignas(cache_line_size) PushState {
        std::atomic<std::size_t> cursor{0};
        std::size_t cachedPopCursor{0};
    };

    struct alignas(cache_line_size) PopState {
        std::atomic<std::size_t> cursor{0};
        std::size_t cachedPushCursor{0};
    };

    PushState pushState_;
    PopState  popState_;

    T* element(std::size_t cursor) { return &ring_[cursor & mask_]; }

public:
    explicit Fifo5(std::size_t capacity, Alloc const& alloc = Alloc{})
    : Alloc{alloc},
      ring_{std::allocator_traits<Alloc>::allocate(*this, capacity)},
      capacity_{capacity},
      mask_{capacity - 1}
    {
        assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
    }

    ~Fifo5()
    {
        auto popCursor  = popState_.cursor.load(std::memory_order_relaxed);
        auto pushCursor = pushState_.cursor.load(std::memory_order_relaxed);
        while (popCursor != pushCursor)
        {
            element(popCursor)->~T();
            ++popCursor;
        }
        std::allocator_traits<Alloc>::deallocate(*this, ring_, capacity_);
    }


    class pusher_t
    {
        Fifo5* fifo_{};
        std::size_t cursor_{0};
    public:
        pusher_t() = default;
        explicit pusher_t(Fifo5* fifo, std::size_t cursor)
            : fifo_{fifo}, cursor_{cursor} {}

        ~pusher_t()
        {
            if (fifo_)
                fifo_->pushState_.cursor.store(cursor_ + 1, std::memory_order_release);
        }

        pusher_t(const pusher_t&)            = delete;
        pusher_t& operator=(const pusher_t&) = delete;

        pusher_t(pusher_t&& other) noexcept
            : fifo_{other.fifo_}, cursor_{other.cursor_}
        { other.fifo_ = nullptr; }

        pusher_t& operator=(pusher_t&& other) noexcept
        {
            if (this != &other)
            {
                // publish current slot before overwriting
                if (fifo_)
                    fifo_->pushState_.cursor.store(cursor_ + 1, std::memory_order_release);
                fifo_       = other.fifo_;
                cursor_     = other.cursor_;
                other.fifo_ = nullptr;
            }
            return *this;
        }

        explicit operator bool() const { return fifo_ != nullptr; }
        void release() { fifo_ = nullptr; }
        T* get()        { return fifo_->element(cursor_); }
        T* operator->() { return get(); }
        T& operator*()  { return *get(); }
    };

    class popper_t
    {
        Fifo5* fifo_{};
        std::size_t cursor_{0};
    public:
        popper_t() = default;
        explicit popper_t(Fifo5* fifo, std::size_t cursor)
            : fifo_{fifo}, cursor_{cursor} {}

        ~popper_t()
        {
            if (fifo_)
                fifo_->popState_.cursor.store(cursor_ + 1, std::memory_order_release);
        }

        popper_t(const popper_t&)            = delete;
        popper_t& operator=(const popper_t&) = delete;

        popper_t(popper_t&& other) noexcept
            : fifo_{other.fifo_}, cursor_{other.cursor_}
        { other.fifo_ = nullptr; }

        popper_t& operator=(popper_t&& other) noexcept
        {
            if (this != &other)
            {
                // advance current slot before overwriting
                if (fifo_)
                    fifo_->popState_.cursor.store(cursor_ + 1, std::memory_order_release);
                fifo_       = other.fifo_;
                cursor_     = other.cursor_;
                other.fifo_ = nullptr;
            }
            return *this;
        }

        explicit operator bool() const { return fifo_ != nullptr; }
        void release() { fifo_ = nullptr; }
        T* get()        { return fifo_->element(cursor_); }
        T* operator->() { return get(); }
        T& operator*()  { return *get(); }
    };

    pusher_t push()
    {
        auto pushCursor = pushState_.cursor.load(std::memory_order_relaxed);
        if (pushCursor - pushState_.cachedPopCursor == capacity_)
        {
            pushState_.cachedPopCursor = popState_.cursor.load(std::memory_order_acquire);
            if (pushCursor - pushState_.cachedPopCursor == capacity_)
                return pusher_t{};
        }
        return pusher_t{this, pushCursor};
    }

    popper_t pop()
    {
        auto popCursor = popState_.cursor.load(std::memory_order_relaxed);
        if (popState_.cachedPushCursor == popCursor)
        {
            popState_.cachedPushCursor = pushState_.cursor.load(std::memory_order_acquire);
            if (popState_.cachedPushCursor == popCursor)
                return popper_t{};
        }
        return popper_t{this, popCursor};
    }
};

#endif
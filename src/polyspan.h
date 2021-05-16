
#pragma once

namespace jaut
{
    /**
     *  The PolySpan class maps a range of indices to a different container and allows
     *
     *  @tparam T
     *  @tparam TContainer
     */
    template<class T, class TContainer> class PolySpan;
    
    template<class T, class TBase>
    class PolySpan<T, std::vector<TBase>>
    {
    public:
        static_assert(std::is_base_of_v<TBase, T>, "Type T must be a child of the container's type");
        
        //==============================================================================================================
        using base_type = TBase;
        using container_type = std::vector<base_type>;
        using element_type = T;
        using value_type = std::remove_cv_t<T>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T *;
        using const_pointer = const T *;
        using reference = T &;
        using const_reference = const T &;
        using iterator = pointer;
        using const_iterator = const_pointer;
    
        //==============================================================================================================
        PolySpan() noexcept = default;
        
        template<class It>
        PolySpan(const container_type &container, It itFirst, size_type count)
            : container(&container),
              first(std::distance(container.begin(), itFirst)),
              last(itFirst + count)
        {}
        
        template<class ItBegin, class ItEnd>
        PolySpan(const container_type &container, ItBegin itFirst, ItEnd itLast)
            : container(&container),
              first(std::distance(container.begin(), itFirst)),
              last(std::distance(container.begin(), itLast))
        {}
        
        explicit PolySpan(const container_type &container)
            : container(&container), first(0),
              last(container.size())
        {}
    
        //==============================================================================================================
        reference       operator[](size_type idx)       { return *static_cast<pointer>(begin() + idx); }
        const_reference operator[](size_type idx) const { return *static_cast<pointer>(begin() + idx); }
    
        //==============================================================================================================
        pointer       data()       noexcept { return static_cast<pointer>(container->data() + first); }
        const_pointer data() const noexcept { return static_cast<pointer>(container->data() + first); }
    
        //==============================================================================================================
        size_type size() { return last - first; }
    
        //==============================================================================================================
        bool empty() const noexcept { return first == last; }
    
        //==============================================================================================================
        reference front() noexcept { return *begin();     }
        reference back()  noexcept { return *(end() - 1); }
        
        const_reference front() const noexcept { return *begin();     }
        const_reference back()  const noexcept { return *(end() - 1); }
    
        //==============================================================================================================
        template<class TRType = T>
        PolySpan<TRType, container_type> getPrevRun() const
        {
            return { *container, container->begin(), container->begin() + first };
        }
    
        template<class TRType = T>
        PolySpan<TRType, container_type> getNextRun() const
        {
            return { *container, container->begin() + last, container->end() };
        }
        
        //==============================================================================================================
        iterator begin() noexcept { return static_cast<pointer>(container->begin() + first); }
        iterator end()   noexcept { return static_cast<pointer>(container->begin() + last);  }
        
        const_iterator begin() const noexcept { return static_cast<pointer>(container->begin() + first); }
        const_iterator end()   const noexcept { return static_cast<pointer>(container->begin() + last);  }
        
        const_iterator cbegin() const noexcept { return static_cast<pointer>(container->begin() + first); }
        const_iterator cend()   const noexcept { return static_cast<pointer>(container->begin() + last);  }
    
    private:
        const container_type *container { nullptr };
        
        size_type first { 0 };
        size_type last  { 0 };
    };
    
    template<class T, class TBase> class PolySpan<T, std::vector<std::reference_wrapper<TBase>>>;
    
    template<class T, class TBase>
    class PolySpan<T&, std::vector<std::reference_wrapper<TBase>>>
    {
    public:
        static_assert(std::is_base_of_v<TBase, T>, "Type T must be a child of the container's type");
        static_assert((std::is_const_v<T> && std::is_const_v<TBase>)
                      || (!std::is_const_v<T> && !std::is_const_v<TBase>),
                      "Base and T must both share the same type qualification");
        static_assert((std::is_volatile_v<T> && std::is_volatile_v<TBase>)
                      || (!std::is_volatile_v<T> && !std::is_volatile_v<TBase>),
                      "Base and T must both share the same type qualification");
        
        //==============================================================================================================
        using base_type = TBase;
        using container_type = std::vector<std::reference_wrapper<base_type>>;
        using element_type = T;
        using value_type = std::remove_cv_t<T>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using iterator = pointer;
        using const_iterator = const_pointer;
        
        //==============================================================================================================
        PolySpan() noexcept = default;
        
        template<class It>
        PolySpan(const container_type &container, It itFirst, size_type count)
            : container(&container),
              first(std::distance(container.begin(), itFirst)),
              last(itFirst + count)
        {}
        
        template<class ItBegin, class ItEnd>
        PolySpan(const container_type &container, ItBegin itFirst, ItEnd itLast)
            : container(&container),
              first(std::distance(container.begin(), itFirst)),
              last(std::distance(container.begin(), itLast))
        {}
        
        explicit PolySpan(const container_type &container)
            : container(&container), first(0),
              last(container.size())
        {}
        
        //==============================================================================================================
        reference       operator[](size_type idx)       { return static_cast<reference>((begin() + idx)->get()); }
        const_reference operator[](size_type idx) const { return static_cast<reference>((begin() + idx)->get()); }
        
        //==============================================================================================================
        pointer       data()       noexcept { return static_cast<pointer>(container->data() + first); }
        const_pointer data() const noexcept { return static_cast<pointer>(container->data() + first); }
        
        //==============================================================================================================
        size_type size() { return last - first; }
        
        //==============================================================================================================
        bool empty() const noexcept { return first == last; }
        
        //==============================================================================================================
        reference front() noexcept { return begin()->get();     }
        reference back()  noexcept { return (end() - 1)->get(); }
        
        const_reference front() const noexcept { return *begin()->get();     }
        const_reference back()  const noexcept { return *(end() - 1)->get(); }
    
        //==============================================================================================================
        template<class TRType = T>
        PolySpan<TRType&, container_type> getPrevRun() const
        {
            return { *container, container->begin(), container->begin() + first };
        }
    
        template<class TRType = T>
        PolySpan<TRType&, container_type> getNextRun() const
        {
            return { *container, container->begin() + last, container->end() };
        }
        
        //==============================================================================================================
        iterator begin() noexcept { return static_cast<pointer>(container->begin() + first); }
        iterator end()   noexcept { return static_cast<pointer>(container->begin() + last);  }
        
        const_iterator begin() const noexcept { return static_cast<pointer>(container->begin() + first); }
        const_iterator end()   const noexcept { return static_cast<pointer>(container->begin() + last);  }
        
        const_iterator cbegin() const noexcept { return static_cast<pointer>(container->begin() + first); }
        const_iterator cend()   const noexcept { return static_cast<pointer>(container->begin() + last);  }
    
    private:
        const container_type *container { nullptr };
        
        size_type first { 0 };
        size_type last  { 0 };
    };
}
